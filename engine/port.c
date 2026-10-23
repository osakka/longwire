/* SPDX-License-Identifier: GPL-3.0-or-later
 * Local ports: where frames enter and leave the line on this machine.
 *   jack  a tap device the user owns (created once: ip tuntap add mode tap user $USER)
 *   sock  frames as datagrams on a Unix socket; used by the tests, needs no privilege
 * splice (AF_PACKET) and shell come in later milestones. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "int.h"

/* linux/if_packet.h, linux/if_tun.h and linux/if.h, spelled out so TCC needn't parse them */
#define LW_SOL_PACKET 263
#define LW_PACKET_ADD_MEMBERSHIP 1
#define LW_PACKET_MR_PROMISC 1
#define LW_PACKET_IGNORE_OUTGOING 23
#define LW_PACKET_OUTGOING 4
#define LW_SIOCGIFHWADDR 0x8927
#define LW_PACKET_AUXDATA 8
#define LW_CSUMNOTREADY (1u << 3)	/* TP_STATUS_CSUMNOTREADY */
struct lw_aux { unsigned status, len, snaplen; unsigned short mac, net, vlan_tci, vlan_tpid; };
struct lw_sll { unsigned short family, proto; int ifindex; unsigned short hatype; unsigned char pkttype, halen, addr[8]; };
struct lw_mreq { int ifindex; unsigned short type, alen; unsigned char addr[8]; };
#define LW_TUNSETIFF 0x400454caU
#define LW_IFF_TAP   0x0002
#define LW_IFF_NO_PI 0x1000

static void seterr(char *err, size_t n, const char *what)
{
	snprintf(err, n, "%s: %s", what, strerror(errno));
}

int port_open(lw_node *n, char *err, size_t errn)
{
	const char *kind = n->cfg.port_kind, *arg = n->cfg.port_arg;

	if (!strcmp(kind, "jack")) {
		struct { char name[16]; short flags; char pad[22]; } ifr;
		int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC | O_NONBLOCK);

		if (fd < 0) {
			seterr(err, errn, "/dev/net/tun");
			return -1;
		}
		memset(&ifr, 0, sizeof ifr);
		snprintf(ifr.name, sizeof ifr.name, "%s", arg);
		ifr.flags = LW_IFF_TAP | LW_IFF_NO_PI;
		if (ioctl(fd, LW_TUNSETIFF, &ifr) < 0) {
			seterr(err, errn, "TUNSETIFF (create the tap first: ip tuntap add dev NAME mode tap user $USER)");
			close(fd);
			return -1;
		}
		n->port_fd = fd;
		return 0;
	}
	if (!strcmp(kind, "splice")) {		/* a NIC on a LAN: no interface is created, frames go straight on the wire */
		struct lw_sll sll;
		struct lw_mreq mr;
		struct { char name[16]; struct { unsigned short fam; unsigned char data[14]; } sa; char pad[8]; } ifr;
		int fd, ifx = (int)if_nametoindex(arg), one = 1;

		if (!ifx) {
			snprintf(err, errn, "no such interface '%s'", arg);
			return -1;
		}
		fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(3));	/* ETH_P_ALL */
		if (fd < 0) {
			snprintf(err, errn, "packet socket: %s (splice needs CAP_NET_RAW, granted once: see docs/BUILD.md)", strerror(errno));
			return -1;
		}
		memset(&sll, 0, sizeof sll);
		sll.family = AF_PACKET;
		sll.proto = htons(3);
		sll.ifindex = ifx;
		if (bind(fd, (struct sockaddr *)&sll, sizeof sll) < 0) {
			seterr(err, errn, "bind packet socket");
			close(fd);
			return -1;
		}
		memset(&mr, 0, sizeof mr);
		mr.ifindex = ifx;
		mr.type = LW_PACKET_MR_PROMISC;		/* released by the kernel when the socket closes: nothing to undo after a crash */
		if (setsockopt(fd, LW_SOL_PACKET, LW_PACKET_ADD_MEMBERSHIP, &mr, sizeof mr) < 0) {
			seterr(err, errn, "promiscuous mode");
			close(fd);
			return -1;
		}
		/* Frames we inject must not come back as local traffic (a loop). The option is Linux 4.20+; port_read
		 * also drops anything marked outgoing, so older kernels are covered too. */
		setsockopt(fd, LW_SOL_PACKET, LW_PACKET_IGNORE_OUTGOING, &one, sizeof one);
		setsockopt(fd, LW_SOL_PACKET, LW_PACKET_AUXDATA, &one, sizeof one);	/* tells us when a checksum is unfinished */
		memset(&ifr, 0, sizeof ifr);
		snprintf(ifr.name, sizeof ifr.name, "%s", arg);
		if (ioctl(fd, LW_SIOCGIFHWADDR, &ifr) == 0)
			memcpy(n->nic_mac, ifr.sa.data, 6);
		n->splice = 1;
		n->port_fd = fd;
		return 0;
	}
	if (!strcmp(kind, "none")) {		/* a switchboard has no local port */
		n->port_fd = -1;
		return 0;
	}
	if (!strcmp(kind, "sock")) {
		char *colon = strchr(arg, ':');
		struct sockaddr_un a;
		int fd;

		if (!colon) {
			snprintf(err, errn, "sock port wants LOCALPATH:PEERPATH");
			return -1;
		}
		fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
		memset(&a, 0, sizeof a);
		a.sun_family = AF_UNIX;
		snprintf(a.sun_path, sizeof a.sun_path, "%.*s", (int)(colon - arg), arg);
		unlink(a.sun_path);
		if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
			seterr(err, errn, "bind sock port");
			if (fd >= 0)
				close(fd);
			return -1;
		}
		snprintf(n->peer_sock, sizeof n->peer_sock, "%s", colon + 1);
		n->port_is_sock = 1;
		n->port_fd = fd;
		return 0;
	}
	snprintf(err, errn, "unknown port '%s' (jack | sock)", kind);
	return -1;
}

int port_read(lw_node *n, uint8_t *buf, size_t cap)
{
	if (n->splice) {			/* skip what this box itself put on the wire */
		for (;;) {
			struct lw_sll sll;
			struct iovec iov = { buf, cap };
			union { struct cmsghdr h; char b[CMSG_SPACE(sizeof(struct lw_aux))]; } cb;
			struct msghdr mh;
			struct cmsghdr *cm;
			ssize_t r;

			memset(&mh, 0, sizeof mh);
			mh.msg_name = &sll;
			mh.msg_namelen = sizeof sll;
			mh.msg_iov = &iov;
			mh.msg_iovlen = 1;
			mh.msg_control = cb.b;
			mh.msg_controllen = sizeof cb.b;
			r = recvmsg(n->port_fd, &mh, 0);
			if (r < 0 || sll.pkttype == LW_PACKET_OUTGOING)
				if (r < 0)
					return (int)r;
			if (sll.pkttype == LW_PACKET_OUTGOING)
				continue;
			for (cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm))
				if (cm->cmsg_level == LW_SOL_PACKET && cm->cmsg_type == LW_PACKET_AUXDATA &&
				    (((struct lw_aux *)CMSG_DATA(cm))->status & LW_CSUMNOTREADY))
					l4_csum_fix(buf, (size_t)r);
			return (int)r;
		}
	}
	return (int)read(n->port_fd, buf, cap);
}

void port_write(lw_node *n, const uint8_t *f, size_t len)
{
	if (n->port_is_sock) {
		struct sockaddr_un a;

		memset(&a, 0, sizeof a);
		a.sun_family = AF_UNIX;
		snprintf(a.sun_path, sizeof a.sun_path, "%s", n->peer_sock);
		sendto(n->port_fd, f, len, 0, (struct sockaddr *)&a, sizeof a);
	} else if (n->splice)
		(void)!send(n->port_fd, f, len, MSG_DONTWAIT);
	else
		(void)!write(n->port_fd, f, len);
}
