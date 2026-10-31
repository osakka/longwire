# 0049 — The Switchboard window shows traffic and names every key

Status: accepted; built in 0.5.29. Follows [0030](0030-three-surfaces-one-engine.md) and [0043](0043-strikes-and-bans-count-only-what-cannot-be-faked.md).

## Context

The Switchboard window showed how full a switchboard is, its nodes and the calls being set up. It could not say how much traffic the switchboard carries, nor what it had done since it started; finding
one node among a thousand meant scrolling; the bans were only on the command line; and its one extra key (`s`, the control socket) was nowhere to be seen. An operator chasing a node that would
not connect had none of this in front of them.

## Decision

1. **The switchboard counts what it carries**: every UDP packet and byte in and out (`rx_pkts`, `rx_bytes`, `tx_pkts`, `tx_bytes`), and since it started the registrations, the rings, the calls put
   through and those that ended, and the keys banned now (`bans_active`), all in its status. Counters only: nothing about who.
2. **The window shows them**: traffic in and out as rates with a sparkline each, then online and the handshake load where there is room, then a line of totals with the bans, warm when there are any.
3. **Search** is the desktop's one search (`/`, or Edit and View > Search): a part of a name in any case, or a part of a number with or without its dashes. **Online Only** (`o`) narrows the list
   to the nodes online. The list's heading says what it is showing (`Nodes (1 of 3)   matching "ali"`), and says so when nothing matches.
4. **The bans** (`b`) replace the list: each banned key's first digits, the time left, and which ban in a row it is. **Lift Ban** (`x`) asks first; only the switchboard's owner may (a member of the group
   `longwire` may look, as before), and a refusal says the command to use. Copy takes a banned key whole, for `bans clear`.
5. **Every key is in a menu**: File (Switchboard Socket…, Close), Edit (Copy, Find), View (Online Only, Bans, Search), Node (Copy Number, Lift Ban, dimmed outside the bans).

## Consequences

The window still keeps nothing and decides nothing, and shows no node's address; a banned key's first digits appear because the operator needs them to lift it. Tested against a real switchboard and
real nodes with the desktop's calls recorded (`tests/sw.sh`): the counters, the rates, the totals, the search by name and by number, Online Only, the bans view, Copy, and that the menus name every key,
and that the app loads under hibr's real desktop code. How it looks is not tested.
