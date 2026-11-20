# 0097 — A member that refuses the mesh is called every six hours

Status: accepted; built in 0.5.74. (Ticket LW-13.) A phone is a leaf ([ADR 0087](0087-a-leaf-keeps-only-the-sessions-it-asked-for.md)): it refuses the mesh's call from every member it did not ask for, and [ADR 0085](0085-the-mesh-calls-an-absent-member-less-and-less-often.md) still had them ring it every quarter of an hour for ever, each call waking its radio and showing at the switchboard.

## Decision

1. **A caller counts the mesh calls a member refuses** (the switchboard says *declined*, as it does for a person's call). A member that has refused three is a leaf.
2. **The wait toward such a member goes on growing past a quarter of an hour, to six hours,** instead of stopping there. A member that comes up, by being called or by calling, starts again from nothing (the count too).
3. **No new message, no new flag on the wire:** an older node or switchboard sees nothing different; a node that is not asked for is refused as before.
4. **The count is shown** in the line's members in the status (`declined`), so an operator can see a leaf.

## Consequences

- Each member rings a phone four times an hour at first, then once an hour and less, then every six hours: a phone in a line of five is no longer a standing cost to the others.
- A member that is only absent and has refused nothing is called as before (a quarter of an hour at most).
- A phone that wants a session with a member says so by dialling it (an asked number is let in), and the member's own wait does not matter then.
- What this does not do is slow the home node's keep-alives to a leaf, which are the home's own rate (`--keepalive`): that needs the leaf to say what it wants in the session *(planned)*.
