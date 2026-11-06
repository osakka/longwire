# 0066 — A member shows its admit before anything it sends counts

Status: accepted; built in 0.5.52. Found by an independent read of 0.5.51, given the code and not the author's conclusions.

## Context

The reviewer found seven things, each confirmed by reading the path end to end or plausible from it:

1. **A member without a good admit kept getting in.** On a line with an operator a member is held to the admit it shows ([ADR 0065](0065-an-admit-has-an-end-and-a-revocation-does-not-outlive-it.md)), but a
   member that simply never sends one was only *disconnected* after 20 seconds, stayed on the member list, and was called again; from the moment a session came up its Ethernet frames and its
   relay and exit messages were accepted. A copied key whose admit had ended got the line in repeating 20-second windows, which is the case 0065 was written for.
2. **A DHCP server on the line could capture the machine's traffic.** The helper's hook passed any dotted-quad mask: `128.0.0.0` puts a route over half of the internet on the line, and an address
   such as `8.8.8.8` a route over a public network, so the server answers for the machine's traffic there.
3. **A full list of revocations cut off members already on the line**, and on a line without an operator one member could fill it for good with evictions of keys that are not on the line.
4. **An old eviction, said again, put a re-admitted key off the line a second time**, and a key that was ever evicted could never come back on a line with an operator, whatever admit it showed.
5. **A clock set forward by a year made the node forget every eviction** (they all look too old to matter) and the loss was saved.
6. **The sandbox's claim that no program can be started is not true for a desktop node**: it could open a new local socket and talk to the user's session bus, which starts programs.
7. **A switchboard could swap the admit a renewed node holds** (the hub echoes the admit it received by way of the switchboard) for an older one of the same key that still holds, and the node would go quiet at the old end.

## Decision

1. **A member shows its admit first.** On a line with an operator, from the moment a session with a member comes up until its admit is shown and holds, nothing from it counts except the line's own
   handshake, the safety phrase, evictions, rolls, key changes, leaving, and its admit: no frame, no relay, no exit. Our own admit is said again every 4 s while we wait for theirs (a datagram is not
   acknowledged). If none holds within 20 s the member is **off the line** (its record is removed), not only disconnected.
2. **The DHCP hook passes only a private address and a mask of 8 to 30 bits.** Private is RFC 1918, the shared range 100.64/10 and link-local; the mask must be contiguous. Anything else is dropped before the
   vendor's script sees it, so the client cannot configure it. (dhcpcd configures the address itself, so with dhcpcd only the routes and names are refused; install isc-dhcp-client or udhcpc to have this too.)
3. **A full list refuses newcomers and no one else**: a member with an admit that holds is admitted whatever the list holds. A member's eviction of a key that is not on the line records nothing.
4. **An eviction counts only while it can matter**: not once it is past the horizon (400 days and a skew), and not when the key it names now shows an admit made after it (ours, for this node). On a
   line with an operator a key is in or out by the admit it shows, not by the fact that it was once evicted.
5. **A jump of the wall clock stops the pruning.** The node remembers what the wall clock minus its own monotonic clock was when it began; a difference of more than the skew since then leaves the
   evictions where they are until the node is restarted.
6. **No new local socket once the node is up.** The sandbox's socket filter allows the internet families and the list of addresses and no more: the control and port sockets are made before it.
7. **The admit a node was given is the admit it holds**: a `LINE` whose admit differs from the node's `--admit` is not taken.

## Not done

- Confining which files the node can reach (Landlock) is still *(planned: M8)*; a compromised node can still write what its user can.
- With dhcpcd the lease's own mask and address are its server's word (decision 2).
- On a line without an operator one member can still evict every other (that line's trust is "all of us trust all of us", [0022](0022-lines-form-by-vouching.md)).
- A RING message to a line member is about 1.45 to 1.5 KB (the caller's whole bundle rides in it) and can cross the 1472 bytes of one datagram on a 1500-byte path; splitting large control messages is *(planned: M8)*.

## Tests

`tests/switch.sh` (a member that shows no admit in time is off the line; an old eviction does not bar a re-admitted key, nor make this node leave; a full list still admits a member; a member's
eviction of a stranger records nothing; a clock jump stops the pruning), `tests/sandbox.sh` (a new local socket ends the process), `tests/jack.sh` (ordinary leases pass; masks 128.0.0.0, 0.0.0.0,
non-contiguous and /32, and a public address, are dropped; the private ranges and their neighbours).
