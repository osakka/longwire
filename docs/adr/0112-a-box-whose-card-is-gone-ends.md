# 0112 — A box whose card is gone ends

Status: accepted; built in 0.5.80. Two test boxes whose LAN card disappeared (the test's veths were removed) each used 85% of a core for 40 minutes: a packet socket on a device that no longer exists reports an error at every poll, and the loop only looked for input.

## Decision

When the port's descriptor reports an error or a hang-up and no input (`POLLERR`, `POLLHUP`, `POLLNVAL`), the node says `port-lost` (with the port's name) and ends. It has given up the privilege to open a packet socket again (ADR 0062), so it does not try: its service starts it again (`Restart=always`; a container's supervisor likewise), and the new start opens the card if it is back, or says plainly that it is not.

## Consequences

- An unplugged USB card, or a container's link removed, no longer burns a core.
- Tested (`tests/portlost.sh`): a splice box whose veth is deleted says `port-lost` and ends.
