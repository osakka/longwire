# 0085 — The mesh calls an absent member less and less often

Status: accepted; built in 0.5.70. The owner saw a call being made, every little while, among the members of a line, and asked whether that is too much exposure.

## Context

Every member of a line calls every member it has no session with: "it is how the mesh heals". After three tries the wait was a minute, for ever. In a line with members that are simply away (a closed laptop, a phone that is off) every box called each of them once a minute, all day. Each call is a
`DIAL` to the switchboard, which sees the two numbers and the time and answers "not online": the switchboard's owner reads who is in whose line from them, and every node (a phone, whose battery pays) makes them. And they heal nothing: a member that comes back registers and calls the
others itself, having no session with them either.

## Decision

The wait before calling a member again is 8, 16 and 32 seconds, then 60, 120, 240 and 480, and a quarter of an hour at most (it was a minute for ever): four calls an hour to a member that is away, not sixty. A change of the node's own network puts every wait back to nothing
(`road`, [ADR 0081](0081-the-phone-tries-again-when-the-network-comes-back.md)), so a node that has moved still tries at once. A fresh pair still finds each other within seconds.

## What the switchboard still learns

That a node is online (its heartbeat), its public address, and who calls whom and when ([SECURITY](../SECURITY.md)): not what is carried, which goes straight between the nodes, encrypted. A phone that calls only the numbers it was given
(the leaf role, ticket LW-3 in the [BACKLOG](../BACKLOG.md)) would add no calls of its own to members it does not need.

## Tests

`tests/guard.sh`: the wait is 8, 16, 32, 60, 120, 240, 480 and then 900 seconds.
