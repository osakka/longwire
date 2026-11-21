# 0088 — The jack tells the DHCP server the machine's name

Status: accepted; built in 0.5.71. The owner, with a desktop on a line, saw every connection listed at the router under the same name, not the machine's. ([ADR 0078](0078-a-phone-with-a-face-a-logo-and-a-battery.md) did the phone; this is the jack; ticket LW-5.)

## Context

The jack's DHCP client was started with its own configuration and sent whatever that said: `dhclient` the system's file (often no name), `dhcpcd` its default, `udhcpc` nothing. A router that lists leases by name then showed a blank or a default.

## Decision

1. **The helper reads the machine's host name** (`/proc/sys/kernel/hostname`), keeps the part before the first dot, keeps only letters, digits and hyphens, drops hyphens at the front, and cuts it at 63 characters (RFC 1123). If nothing is left, nothing is sent.
2. **Each client is told it explicitly:** `dhclient` with a configuration file of its own for the jack (`-cf`, `send host-name "NAME";` and nothing else, in the run directory, removed with the jack); `dhcpcd -h NAME`; `udhcpc -x hostname:NAME`.
3. **Nothing comes back:** the server's host name is still not believed or applied (`-O host_name` is not asked for, and the hook's allowlist drops `new_host_name`).

## Consequences

- The router lists the jack's lease under the machine's name. A machine whose name is odd is listed under what remains of it.
- `dhclient` no longer reads the system's `dhclient.conf` for the jack. That file's other settings (timeouts, requests) were never what a line wants; the client's built-in defaults apply.
- The configuration also says `timeout 20; retry 10;`: `dhclient` waits five minutes after an attempt that got no answer, and a jack that came up while the line was carrying nothing (a box restarting) stayed on a 169.254 address long after the line was back. It now asks again within about half a minute.
- Tested with stand-ins for the three clients (`tests/jack.sh`); not against a real router.
