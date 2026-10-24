# 0026 — A line may have an operator

Status: accepted. Amends [0022](0022-lines-form-by-vouching.md): the "no owner" consequence holds only for lines that
do not name an authority.

## Context

[0022](0022-lines-form-by-vouching.md) lets any member admit and evict any other. That is the right trust for a family
or a few friends and the wrong one for an organisation: someone must be able to say who may join, remove a laptop
centrally, and not have every member hold that power. The desktop pane also has to show which kind of line it is on.

## Decision

A line **may name an authority**: one Ed25519 public key, fixed when the line is founded, kept offline by the operator
and not necessarily a member. A line without one works exactly as in 0022.

- **Founding.** `up --authority HEX` on the founder. The founder founds the line as soon as it has a number (it does
  not wait for a call), so the line has an id the operator can sign for. The founder's self-admit stays valid, as in
  0022, so the founder is trusted by construction: it is the node that chose the authority. The authority travels in
  `LINE` (a 32-byte field, all zero when there is none) and is stored with the line. It cannot be changed in v1.
- **Admits.** In a managed line an admit is valid only if the authority signed it (the signer field carries the
  authority's public key, matched by equality) or it is the founder's self-admit. A member-signed admit is rejected.
  A newcomer holds its admit before it is on any line (`up --admit HEX`) and presents it when it dials a member.
  The member verifies the admit the caller *presented* and forwards that one; it never makes one.
- **Evictions.** Valid only if the authority signed them, in the 0022 layout. Any member may relay one: it
  applies it, tells every member, and changes the line key, as in 0022. A member's own `evict` is refused. Every member
  keeps the signed evictions and sends them to a member it links to later, so a node that was offline when its peer was
  revoked still hears of it, and a revoked node cannot come back through someone who missed the news.
- **Signing happens off the nodes.** `lw key admit SEED LINEID KEYID` and `lw key evict SEED LINEID KEYID` print the
  signed bytes; the operator's seed never runs a node.
- **Status** carries `managed`, the authority and the full line id (the operator signs the full id; the short one in
  events is for people).

## Consequences

- The operator decides who joins and can revoke from one place; members cannot add or remove each other.
- The operator's key is the line's single point of trust: lose it and no one can join or be evicted; steal it and an
  attacker can join. Keep it offline and backed up like the seed ([THREAT-MODEL](../THREAT-MODEL.md)).
- Admits and evictions carry the operator's clock: an admit must be newer than any eviction of that key to bring it
  back, and the operator is the only one who signs both, so members' clocks do not matter.
- The founder could cheat by founding the line with a different authority, but then no one it was meant for would
  join, so the failure is visible at once.
- The line's wire changes (`LINE` gains the authority), which is free because nothing public exists.
