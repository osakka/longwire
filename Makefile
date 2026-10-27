# SPDX-License-Identifier: GPL-3.0-or-later
#
# longwire is built with TCC, the Tiny C Compiler, and with nothing else: no
# other compiler, no optimisation flags, no exceptions (docs/adr/0021). The
# override stops `make CC=...` on the command line from changing that.
override CC = tcc
HIBR_SRC ?= ../hibr
B     = build
VERSION := $(shell cat VERSION)
PREFIX ?= /usr/local
DESTDIR ?=

VENDOR = vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c \
	 $(filter-out vendor/mlkem768/compat.h,$(wildcard vendor/mlkem768/*.c))

all: $(B)/longwire.so

$(B)/longwire.so: module/longwire.c engine/*.c engine/*.h $(VENDOR) VERSION | $(B)
	$(CC) -shared -fPIC -DLW_VERSION='"$(VERSION)"' -I$(HIBR_SRC)/include -Ivendor/mlkem768 -o $@ module/longwire.c engine/all.c $(VENDOR)

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
	sh tests/m3b.sh
	sh tests/crossed.sh
	sh tests/names.sh
	sh tests/registry.sh
	sh tests/key.sh
	sh tests/dhcp.sh
	sh tests/release.sh
	sh tests/pmp.sh
	sh tests/operator.sh
	sh tests/ipv6.sh
	sh tests/panel.sh
	sh tests/pane.sh
	sh tests/sw.sh
	sh tests/group.sh
	sh tests/profile.sh
	sh tests/fuzz.sh
	sh tests/kat.sh
	sh tests/start.sh
	sh tests/pkg.sh
	python3 -I tests/docs.py

# The module goes where hibr looks for modules; the two launchers and the services go where a package puts them. The
# scripts are not installed: they come from the catalogue (docs/adr/0028).
install: all
	install -d $(DESTDIR)$(PREFIX)/lib/hibr $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/systemd/system $(DESTDIR)$(PREFIX)/lib/sysusers.d \
		$(DESTDIR)$(PREFIX)/lib/longwire $(DESTDIR)$(PREFIX)/share/polkit-1/actions $(DESTDIR)$(PREFIX)/share/doc/longwire
	install -m 755 $(B)/longwire.so $(DESTDIR)$(PREFIX)/lib/hibr/longwire.so
	install -m 755 packaging/launcher.sh $(DESTDIR)$(PREFIX)/bin/longwire
	install -m 755 packaging/launcher.sh $(DESTDIR)$(PREFIX)/bin/longwire-switchboard
	for u in longwire-switchboard.service longwire@.service; do \
		sed -e 's|/usr/bin/|$(PREFIX)/bin/|g' -e 's|/usr/lib/hibr/|$(PREFIX)/lib/hibr/|g' packaging/systemd/$$u \
			> $(DESTDIR)$(PREFIX)/lib/systemd/system/$$u && chmod 644 $(DESTDIR)$(PREFIX)/lib/systemd/system/$$u; done
	install -m 644 packaging/longwire.sysusers $(DESTDIR)$(PREFIX)/lib/sysusers.d/longwire.conf
	install -m 755 packaging/longwire-jack $(DESTDIR)$(PREFIX)/lib/longwire/jack
	sed -e 's|/usr/lib/longwire/jack|$(PREFIX)/lib/longwire/jack|' packaging/org.longwire.jack.policy \
		> $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.jack.policy && chmod 644 $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.jack.policy
	install -m 644 README.md COPYING CHANGELOG.md docs/*.md $(DESTDIR)$(PREFIX)/share/doc/longwire/

# Packages, into build/pkg: the deb first, and the rpm and the pacman package from the deb's own files.
packages: all
	sh packaging/deb/build-deb.sh $(B)/pkg
	sh packaging/rpm/build-rpm.sh $(B)/pkg/longwire_$(VERSION)-$(or $(DEB_REVISION),1)_$$(dpkg --print-architecture).deb $(B)/pkg
	sh packaging/arch/build-pkg.sh $(B)/pkg/longwire_$(VERSION)-$(or $(DEB_REVISION),1)_$$(dpkg --print-architecture).deb $(B)/pkg

clean:
	rm -rf $(B)

.PHONY: all lint check clean install packages
