# Brand

<p><img src="../brand/wordmark.svg" alt="longwire" width="300"></p>

## The name

**longwire**: always lowercase, one word, including at the start of a sentence (as with `git`).

- **One long wire.** The product is a single Ethernet cable stretched between your sites. Everything plugged in at
  either end is on the same wire.
- **The longwire antenna.** A plain strand of wire, the simplest antenna there is, that reaches around the world.
  No complication, and a long reach.
- **The telephone exchange** supplies everything else: you have a number, you dial, it rings, someone picks up. The
  switchboard connects you, then gets off the line.

**Tagline:** *One long wire between your sites.*
**Short form:** *Same LAN. Any distance.*
**For the switchboard:** *It puts the call through, then it hangs up.*

## Vocabulary

Use these words consistently in the CLI, the docs, the hibr desktop and the Android app. They are the brand.

| Say | Meaning | Don't say |
|---|---|---|
| **line** | one bridged L2 network | tunnel, overlay, VLAN |
| **number** | a node's dialable identity (`447-120-933`) | ID, peer ID, endpoint |
| **full address** | the `lw1…` key address | public key, fingerprint |
| **dial** / **hang up** | connect / disconnect | connect, establish, tear down |
| **ring** / **pick up** / **reject** | incoming request / accept / refuse | handshake request, approve |
| **switchboard** | the rendezvous server | coordinator, controller, signalling server |
| **dial tone** | registered with the switchboard, ready | online, connected to server |
| **party line** | a line with 3 or more members | mesh network |
| **splice / jack** | the site and host attach modes | bridge mode, client mode |
| **trunk / leaf** | full-mesh site node / single-session device | server / client |
| **safety phrase** | the 6 words that verify a first call | SAS, fingerprint |

Use **"VPN"** only where people search for it ("a layer-2 VPN"), never in the product UI.

## Voice

- **Plain and short.** The CLI says `connected to 312-884-076`, not `Connection established successfully`.
- **Say what happened and what to do next.** `call failed: no direct path (both sides behind strict NAT?)` is better
  than `Fallback path engaged`. A relayed call says which node is relaying: `connected to 540-283-207 through a relay (312-884-076)`.
- **The metaphor seasons the text; it shouldn't be the whole meal.** Status words come from the telephone, but
  explanations say what's really happening ("hole-punched", "behind CGNAT").
- **A name is a claim and is worded as one: `312-884-076 is calling, and says it is "Osama's laptop"`. Never cute about security.** Key changes, evictions and first calls are said straight, in full.

## Mark

<p><img src="../brand/mark.svg" alt="longwire mark" width="96"></p>

Two poles, one wire sagging between them, and a green signal riding it. It's drawn as a catenary, the shape a real
wire hangs in, rather than a straight line: real distance, real weight, still connected. Files are in
[`brand/`](../brand): `mark.svg` (icon, favicon, app icon) and `wordmark.svg` (mark + name). Both switch to light
poles and text automatically in dark mode.

- The smallest size is 16 px: drop the green pulse below 24 px.
- Clear space around the mark is one node diameter on every side.
- Never put the wire in a straight line, add a padlock or globe, or put it in a shield.

## Palette

| Token | Hex | Use |
|---|---|---|
| **copper** | `#C0632B` | the wire, primary accent, links |
| **line black** | `#121417` | text, poles, dark backgrounds |
| **insulator** | `#F3EEE4` | light backgrounds, text on dark |
| **dial green** | `#2FB67C` | dial tone, connected, direct path |
| **ringing amber** | `#E0A100` | ringing, attention; a relayed path |
| **hang-up red** | `#D9473F` | rejected, key changed, failure |

Status colours mean the same thing in the hibr desktop and on Android *(planned: M4)*; the terminal narration is plain
text today, and will get ANSI fallbacks (green, yellow, red) *(planned: M4)*. Copper is never used for a status.

## Type

- **Monospace first.** longwire lives in terminals. Use the system monospace (`ui-monospace`, JetBrains Mono,
  DejaVu Sans Mono) for the wordmark, for numbers and in the UI.
- **Numbers are always grouped** `447-120-933` and set in monospace, so the digits line up in columns. A full number
  adds the switchboard's code with a plus: `+0731042 447-120-933`; on your own switchboard the code is left off.
- In the hibr desktop, follow the active hibr theme (a theme is data, per hibr ADR 0028), adding only copper and the
  three status colours.

## Glyph

**☎** (U+260E) marks your own number and incoming rings in the CLI, and later the hibr menu bar *(planned: M4)*. It is never used for
anything else.
