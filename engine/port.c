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
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "int.h"

/* linux/if_tun.h and linux/if.h, spelled out so TCC needn't parse them */
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
	} else
		(void)!write(n->port_fd, f, len);
}
