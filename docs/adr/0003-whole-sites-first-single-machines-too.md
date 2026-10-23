# 0003 — Whole sites first, and single machines too

Status: accepted

## Context

The primary use is bridging whole sites. But phones and laptops have to be able to join, and macOS and Android offer an
application an IP tunnel, not Ethernet, so a device cannot simply be a switch port.

## Decision

The flagship mode is **splice**: one box per site puts frames straight onto the wired NIC, so every machine at the site
sees the remote site natively, with no interface created. A single Linux host uses **jack**, a tap device it owns (BSD *(planned: M5)*).
A device that only has an L3 tunnel is a **member**: the node emulates an Ethernet host in userspace (a virtual MAC, a
DHCP client, an ARP and NDP responder), so it gets a real lease from the far LAN's own DHCP server *(planned: M6)*.
**shell** gives a user with no privilege at all a shell whose only network is the line *(planned: M5)*.

## Consequences

Four attach modes share one switch and one wire format; they differ only in what the local port is
([ARCHITECTURE §2](../ARCHITECTURE.md#2-attaching-a-node-ports)). Splice works on wired Ethernet only, since most Wi-Fi
client links drop frames with foreign source addresses. Member mode is the only part that needs per-platform code, and
that is the Android app, the one piece outside hibr.

---

[← decisions](README.md)
