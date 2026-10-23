# 0019 — The tests build their own network from unprivileged namespaces

Status: accepted

## Context

A VPN is only tested by moving real traffic, and the interesting cases need things a development machine does not have:
NAT between sites, a bridged LAN with real hosts, a second box on the same segment. The machine this was built on is an
unprivileged LXC container with no `/dev/net/tun`, no `nft` or `iptables`, and no root.

## Decision

The end-to-end tests build a miniature internet inside `unshare -Urn`: network namespaces joined by veth pairs and Linux
bridges, real kernel hosts running real `ping`, ARP and TCP, and, for NAT, a userspace NAT router written in Python on raw
packet sockets with kernel forwarding turned off (port-restricted cone, or symmetric). The nodes under test are the real
hibr scripts. Tests are mutation-checked where it matters: a deliberately reintroduced bug must make them fail.

## Consequences

The tests need unprivileged user namespaces, which some distributions restrict (Ubuntu 24.04 and later by default). A
Python NAT is a faithful model of the filtering behaviour, not a real consumer router. The **jack** port cannot be
exercised here, so it is written but untested ([ROADMAP](../ROADMAP.md)); everything else is.

---

[← decisions](README.md)
