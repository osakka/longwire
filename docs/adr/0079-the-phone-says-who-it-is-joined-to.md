# 0079 — The phone says who it is joined to

Status: accepted; built in 0.5.64. The owner, on 0.5.63: the logo can be prettier; the card is not "Home", it is the switchboard and the number; and the notification says an address and a gateway, which is no use: it should say who the
phone is connected to, and maybe through which switchboard.

## Decision

1. **The logo is redrawn**: the classic rotary telephone with its handset on the cradle, finger holes round the dial, on a plate, the wire hanging below it with endpoints and the green signal on it. One list of shapes makes
   both the Android drawable and `brand/app-icon.svg`; it is drawn at 84% so a round launcher mask never cuts it.
2. **"Home" goes.** What the phone is joined to is a switchboard and **the number it dials** (a number is the node's identity, [BRAND](../BRAND.md)). The card is **Line**, the field is **Number to dial**, the switch says
   "through the line" (the brand's own words), the status says "calling 312-884-076" and not "calling home". The invitation's option is `--dial NUMBER` and its link parameter `dial=`, not `--home`/`home=` (nothing
   else read them: no compatibility is kept). The app's saved number is stored under a new name, so a phone that had one is asked for it again once.
3. **The notification names the other end**: `Connected to 312-884-076 · sb.example.org:1877`, who the phone is joined to and through which switchboard, and nothing about addresses. The screen keeps the address and the gateway,
   on its own lines under it, for whoever is debugging. The notification changes only when what it says changes.

## Tests

`tests/tokens.sh` (the `--dial` option and the `dial=` parameter, and that what the script prints passes the app's checks of a link). The icon, the wording and the notification are for the phone.
