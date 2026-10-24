# 0017 — A splice box makes what a NIC gives it fit the line

Status: accepted

## Context

Splice puts the LAN's frames on the line and the line's frames on the LAN, and a real NIC is not the tidy thing the
protocol sketch assumed: it can hand over frames the box itself injected, frames whose checksums are not finished, and
frames of 64 KB. LAN hosts also send 1500-byte frames that do not fit one datagram.

## Decision

- **No loop.** `PACKET_IGNORE_OUTGOING` plus a filter on the packet's direction, so an injected frame is never read back.
- **Finish unfinished checksums.** Frames from a virtual NIC (veth, virtio) arrive with the TCP/UDP checksum only partly
  done; the packet socket flags it and longwire completes it, or the far LAN drops the frame.
- **Cut coalesced frames.** A TCP frame larger than the line carries (GRO, or a virtual NIC's GSO) is cut into line-sized
  segments with fresh lengths, sequence numbers and checksums; any other oversize frame is dropped and counted.
- **Split what is merely too big.** A frame over 1428 bytes travels as two datagrams and is rejoined; a frame up to 1428
  fits one datagram on a 1492 (PPPoE) path.
- **Clamp the TCP MSS** on SYN and SYN-ACK to 1340 (IPv4) or 1320 (IPv6), so almost all TCP fits one datagram.
- **Quiet the WAN.** A flood budget per peer, and ARP requests for hosts across the line answered locally.

## Consequences

Each of these was found by running real traffic: dropping coalesced frames cost 10% retransmissions and made a 3 MB TCP
transfer crawl at 4 Mbit/s, and with them fixed it ran at about 50 Mbit/s with 3 retransmissions in 2,263 segments.
VLAN-tagged traffic is not handled and not tested *(planned)*, IPv6 neighbour discovery is suppressed like ARP (built),
and the receive path reads one frame per `recvmsg` *(planned: M7)*. See [ARCHITECTURE §2 and §6](../ARCHITECTURE.md).

---

[← decisions](README.md)
