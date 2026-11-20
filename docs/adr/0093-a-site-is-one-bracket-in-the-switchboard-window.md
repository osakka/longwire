# 0093 — A site is one bracket in the Switchboard window

Status: accepted; built in the release after 0.5.73. The owner, on the pair vpn-1 and vpn-2: the switchboard should put redundant servers under a tree so it can be seen. (Ticket LW-17; [ADR 0086](0086-the-switchboard-window-shows-a-pair-as-a-pair.md) put a pair's rows together and said each one's role.)

## Decision

1. **The boxes of one site are drawn as one bracket down the left edge of the list:** `┌` on the first, `├` on any between, `└` on the last, a space on a node that is alone. Two rows are of one site when either says (`with`) that the other is its partner.
2. **Nothing else changes:** the rows stay one node each, so selection, search, Copy and the keys act on a node as before; the role and the partner are still said at the end of the row, and the heading still counts the sites.

## Consequences

- A pair is seen at a glance, and a third box of a site would be a `├` between.
- No parent row: a header row would be a row that is not a node, and every key of the window works on the row chosen. A row for the site as a whole is a larger change that this does not exclude.
- What it shows is what the nodes claim (`SITEINFO`), for two minutes since they last said it (BUG-5): a box that has just stopped can still be bracketed for that long.
- A switchboard older than 0.5.70 knows nothing of `SITEINFO`, so it shows no brackets; srv-uk-01 runs 0.5.69.
