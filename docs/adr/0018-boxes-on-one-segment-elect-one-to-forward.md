# 0018 — Boxes on one segment share a secret and elect one to forward

Status: accepted

## Context

Split horizon prevents loops between sites, but two splice boxes on the *same* LAN segment would each forward the other's
frames. A second box is also the natural way to get high availability.

## Decision

Boxes of one site share a secret (`--site-key FILE`). Each sends an authenticated probe once a second (EtherType `0x88B5`,
never forwarded over the line). The box with the lowest key id among those heard in the last 3.5 seconds forwards; the
others are standby: they keep their sessions up but forward nothing and deliver nothing to the LAN, and a new box listens
for 3 seconds before forwarding anything. The active box shares its list of remote hosts every 2 seconds, so a standby
that takes over can send one frame from each of those addresses and move every switch on the segment at once.

## Consequences

Two boxes on one segment **must** share a key, or they loop; deriving it automatically needs lines *(planned: M3)*. In
the test suite the active box is killed and traffic resumes in about 4 seconds with nothing done to any host. A takeover
in the first two seconds after the group forms, before a list has been shared, falls back to ordinary learning and is
slower. A box that restarts takes over again if its key id is lowest. See
[ARCHITECTURE §4](../ARCHITECTURE.md#4-two-boxes-on-one-segment-site-groups).

---

[← decisions](README.md)
