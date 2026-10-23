# SPDX-License-Identifier: GPL-3.0-or-later
ifeq ($(origin CC),default)
CC = tcc
endif
OPT  ?=
HIBR ?= ../hibr
B     = build

VENDOR = vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c \
	 $(filter-out vendor/mlkem768/compat.h,$(wildcard vendor/mlkem768/*.c))

all: $(B)/longwire.so

$(B)/longwire.so: module/longwire.c engine/*.c engine/*.h $(VENDOR) | $(B)
	$(CC) $(OPT) -shared -fPIC -I$(HIBR)/include -Ivendor/mlkem768 -o $@ module/longwire.c engine/all.c $(VENDOR)

$(B):
	mkdir -p $@

check: all
	sh tests/m0.sh
	sh tests/m1.sh

clean:
	rm -rf $(B)

.PHONY: all check clean
