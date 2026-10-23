# SPDX-License-Identifier: GPL-3.0-or-later
#
# longwire is built with TCC, the Tiny C Compiler, and with nothing else: no
# other compiler, no optimisation flags, no exceptions (docs/adr/0021). The
# override stops `make CC=...` on the command line from changing that.
override CC = tcc
HIBR_SRC ?= ../hibr
B     = build

VENDOR = vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c \
	 $(filter-out vendor/mlkem768/compat.h,$(wildcard vendor/mlkem768/*.c))

all: $(B)/longwire.so

$(B)/longwire.so: module/longwire.c engine/*.c engine/*.h $(VENDOR) | $(B)
	$(CC) -shared -fPIC -I$(HIBR_SRC)/include -Ivendor/mlkem768 -o $@ module/longwire.c engine/all.c $(VENDOR)

# TCC's own warnings, as errors, on everything we wrote (the vendored code is upstream's).
lint:
	$(CC) -Wall -Werror -Wunsupported -c -Ivendor/mlkem768 -o /dev/null engine/all.c
	$(CC) -Wall -Werror -Wunsupported -c -Ivendor/mlkem768 -I$(HIBR_SRC)/include -o /dev/null module/longwire.c

$(B):
	mkdir -p $@

check: all lint
	sh tests/m0.sh
	sh tests/m1.sh
	sh tests/m2.sh
	sh tests/m3.sh
	python3 -I tests/docs.py

clean:
	rm -rf $(B)

.PHONY: all lint check clean
