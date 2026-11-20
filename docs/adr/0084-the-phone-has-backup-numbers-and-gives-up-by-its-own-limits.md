# 0084 — The phone has backup numbers and gives up by its own limits

Status: accepted; built in 0.5.69. The owner, on adding a second server beside the first: the phone should have more than one number to try, and a limit (a number of failures, or a time) after which the VPN is turned off, both settings.

## Decision

1. **The list.** *Numbers to dial, in order* takes several numbers (or full addresses), separated by commas or spaces: the first is called; after two tries without an answer the next, round the list; the number that answers is first next time. The screen says which
   (`calling 312-884-076 (2 of 3)`). An invitation can carry the list (`token invite --dial A,B`, at most four).
2. **Every number in the list may answer DHCP** for the phone (`asked`, a new control word, said for each at start): the guard ([ADR 0080](0080-only-a-member-we-asked-for-may-speak-for-the-network.md)) trusts a number a person chose as it trusts one they dialled, so when the other box of a pair is the active one
   its lease reaches the phone. A box the dialled one vouches for ([ADR 0083](0083-a-pair-of-boxes-is-one-site-and-a-bridge-carries-no-control-frames.md)) already may; the list covers the other direction and a restart.
3. **Limits** (settings, default never): failed tries in a row (5, 10, 20) and time without a line (5 minutes, 30 minutes, 2 hours), whichever comes first. A try is a call made (spaced by the back-off, [ADR 0081](0081-the-phone-tries-again-when-the-network-comes-back.md)), so the time limit is the one that also
   covers a switchboard that cannot be reached. The count and the clock start again whenever the line is up. At the limit the engine is stopped, the tunnel closed, and a notification says why (`Longwire turned itself off: no answer after 10 tries`): **a VPN that turns itself off says so.**

## Not done

Not run on a phone. The list is one field of text; no per-number names. A phone that gave up stays off until the person turns it on: it does not try again by itself.

## Tests

`tests/guard.sh`: the `asked` word takes a number and says it back, refuses what is not one, and a member with that number may answer DHCP. `tests/tokens.sh`: an invitation carries several numbers, and refuses a list with something that is not a number. The app's own logic is for the phone.
