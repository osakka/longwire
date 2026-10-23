# 0022 — Lines form by vouching; the founder admits itself; an evicted key stays out

Status: accepted

## Context

M1 and M2 connect two nodes. The product is a network of sites, so three or more must share one L2 segment, with no
prompt on any member as the mesh builds, and with a way to remove a node for good. The switchboard must stay ignorant of
all of it, and a stolen or retired node must not be able to talk its way back in. Split horizon
([0002](0002-a-software-switch-with-split-horizon.md)) also assumes a full mesh.

## Decision

A line is a founder, a line key, and a list of members. A node joins by calling any member; the member **vouches** by
signing an **admit** (Ed25519, 145 bytes) for it and handing it the line and the member list. The newcomer then calls
every other member, presenting its admit; a member that holds a valid admit for the caller lets the call through without
a prompt. The founder admits itself and members accept a self-admit only for the recorded founder, so every member holds
an admit. An admit is valid if its signer is in the *persisted* member list, not merely connected, and it is newer than
any eviction recorded for that key; evictions are persisted, so a restart cannot re-admit an evicted node with an old
admit. Any member can evict any other, which also rotates the line key. The switchboard carries the admit as an opaque
blob and never verifies it. Every node keeps dialing members it lacks a session with, with a back-off, and says so when
the mesh is incomplete.

## Consequences

A line is a cable into every member's LAN, and any member can add or remove any other: there is no owner and no
quorum, which is the trust the use case implies and is written down in the threat model. There are no roles beyond
trunk (a leaf role arrives with member mode, M6). A node is on one line, up to 16 members. A pair that cannot reach each other
(two strict NATs) is relayed by another member ([0008](0008-hard-nats-are-relayed-by-line-members.md)); if none will relay it splits
the line silently, so partiality is reported rather than hidden.
Clock skew between members blurs "later than the eviction" by the skew; admits and evictions carry the signer's clock.
