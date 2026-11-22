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

all: $(B)/longwire.so $(B)/lw-dhcp

$(B)/longwire.so: module/longwire.c engine/*.c engine/*.h $(VENDOR) VERSION | $(B)
	$(CC) -shared -fPIC -DLW_VERSION='"$(VERSION)"' -I$(HIBR_SRC)/include -Ivendor/mlkem768 -o $@ module/longwire.c engine/all.c $(VENDOR)

# The jack's DHCP client (docs/adr/0102): the phone's client in a program of its own.
$(B)/lw-dhcp: packaging/lw-dhcp.c engine/*.c engine/*.h $(VENDOR) VERSION | $(B)
	$(CC) -DLW_VERSION='"$(VERSION)"' -Ivendor/mlkem768 -o $@ packaging/lw-dhcp.c $(VENDOR)

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
	sh tests/expiry.sh
	sh tests/bigline.sh
	sh tests/leafline.sh
	sh tests/sbrestart.sh
	sh tests/pairrestart.sh
	sh tests/meshask.sh
	sh tests/moveline.sh
	sh tests/sbrelease.sh
	sh tests/admitheal.sh
	sh tests/evictsync.sh
	sh tests/pairauto.sh
	sh tests/portlost.sh
	sh tests/linerace.sh
	sh tests/presence.sh
	sh tests/ipv6.sh
	sh tests/panel.sh
	sh tests/pane.sh
	sh tests/sw.sh
	sh tests/group.sh
	sh tests/profile.sh
	sh tests/showdns.sh
	sh tests/jack.sh
	sh tests/dhcpnet.sh
	sh tests/sas.sh
	sh tests/ban.sh
	sh tests/ban2.sh
	sh tests/jacknet.sh
	sh tests/tunnel.sh
	sh tests/exit.sh
	sh tests/exitnet.sh
	sh tests/road.sh
	sh tests/status.sh
	sh tests/switch.sh
	sh tests/leaf.sh
	sh tests/leafnet.sh
	sh tests/guard.sh
	sh tests/guardnet.sh
	sh tests/claim.sh
	sh tests/seedmem.sh
	sh tests/trust.sh
	sh tests/admitwhy.sh
	sh tests/failover.sh
	sh tests/book.sh
	sh tests/m2b.sh
	sh tests/m2c.sh
	sh tests/m2d.sh
	sh tests/sandbox.sh
	sh tests/events.sh
	sh tests/doctor.sh
	sh tests/site.sh
	sh tests/tokens.sh
	sh tests/policy.sh
	sh tests/bar.sh
	sh tests/man.sh
	sh tests/fuzz.sh
	sh tests/kat.sh
	sh tests/start.sh
	sh tests/pkg.sh
	python3 -I tests/docs.py

# The module goes where hibr looks for modules; the two launchers and the services go where a package puts them. The
# scripts are not installed: they come from the catalogue (docs/adr/0028).
install: all
	install -d $(DESTDIR)$(PREFIX)/lib/hibr $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/systemd/system $(DESTDIR)$(PREFIX)/lib/sysusers.d \
		$(DESTDIR)$(PREFIX)/lib/longwire $(DESTDIR)$(PREFIX)/share/polkit-1/actions $(DESTDIR)$(PREFIX)/share/doc/longwire \
		$(DESTDIR)$(PREFIX)/share/man/man1 $(DESTDIR)$(PREFIX)/share/man/man5 $(DESTDIR)$(PREFIX)/share/man/man7
	install -m 755 $(B)/longwire.so $(DESTDIR)$(PREFIX)/lib/hibr/longwire.so
	install -m 755 packaging/launcher.hibr $(DESTDIR)$(PREFIX)/bin/longwire
	install -m 755 packaging/launcher.hibr $(DESTDIR)$(PREFIX)/bin/longwire-switchboard
	for u in longwire-switchboard.service longwire@.service; do \
		sed -e 's|/usr/bin/|$(PREFIX)/bin/|g' -e 's|/usr/lib/hibr/|$(PREFIX)/lib/hibr/|g' packaging/systemd/$$u \
			> $(DESTDIR)$(PREFIX)/lib/systemd/system/$$u && chmod 644 $(DESTDIR)$(PREFIX)/lib/systemd/system/$$u; done
	install -m 644 packaging/longwire.sysusers $(DESTDIR)$(PREFIX)/lib/sysusers.d/longwire.conf
	install -m 755 packaging/longwire-jack $(DESTDIR)$(PREFIX)/lib/longwire/jack
	install -m 755 $(B)/lw-dhcp $(DESTDIR)$(PREFIX)/lib/longwire/lw-dhcp
	install -m 755 packaging/longwire-jack $(DESTDIR)$(PREFIX)/lib/longwire/jack-everything
	sed -e 's|/usr/lib/longwire/jack|$(PREFIX)/lib/longwire/jack|' packaging/org.longwire.jack.policy \
		> $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.jack.policy && chmod 644 $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.jack.policy
	sed -e 's|/usr/lib/longwire/jack-everything|$(PREFIX)/lib/longwire/jack-everything|' packaging/org.longwire.everything.policy \
		> $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.everything.policy && chmod 644 $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.everything.policy
	install -m 755 packaging/longwire-site $(DESTDIR)$(PREFIX)/lib/longwire/site
	sed -e 's|/usr/lib/longwire/site|$(PREFIX)/lib/longwire/site|' packaging/org.longwire.site.policy \
		> $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.site.policy && chmod 644 $(DESTDIR)$(PREFIX)/share/polkit-1/actions/org.longwire.site.policy
	install -m 644 README.md COPYING CHANGELOG.md docs/*.md $(DESTDIR)$(PREFIX)/share/doc/longwire/
	for s in 1 5 7; do for p in man/*.$$s; do \
		gzip -9n < $$p > $(DESTDIR)$(PREFIX)/share/man/man$$s/$$(basename $$p).gz && chmod 644 $(DESTDIR)$(PREFIX)/share/man/man$$s/$$(basename $$p).gz; done; done

# Packages, into build/pkg: the deb first, built in Debian 12 so that it needs no newer glibc than that (BUG-14), and the rpm and the pacman
# package from the deb's own files.
packages: all
	sh packaging/deb/build-bookworm.sh $(B)/pkg
	sh packaging/rpm/build-rpm.sh $(B)/pkg/longwire_$(VERSION)-$(or $(DEB_REVISION),1)_$$(dpkg --print-architecture).deb $(B)/pkg
	sh packaging/arch/build-pkg.sh $(B)/pkg/longwire_$(VERSION)-$(or $(DEB_REVISION),1)_$$(dpkg --print-architecture).deb $(B)/pkg

clean:
	rm -rf $(B)

.PHONY: all lint check clean install packages
