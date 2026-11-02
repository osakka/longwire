# 0052 — The strong phrase is the default, the echo port has a budget, tokens are 128 bits

Status: accepted; built in 0.5.35. Follows [0040](0040-the-safety-phrase-is-committed-before-it-is-shown.md), [0043](0043-strikes-and-bans-count-only-what-cannot-be-faked.md) and [0048](0048-a-silent-switchboard-is-said-so.md).

## Context

A security review by the owner asked about the port, the size of tokens and the size of what a client holds. Ports turned out not to be a security matter (a scanner finds a UDP service on any
port; the handshake is what guards it) and the node's key is its credential (X25519 and ML-KEM-768). Three real points remained:

- **The old six-word phrase** was still shown, marked as weaker, whenever the other end did not do the committed exchange: to a node older than 0.5.21, or to anyone a switchboard in the middle
  chose to strip the exchange from. `--require-sas` refused that, but was off by default.
- **The echo port** answers anyone, with no handshake, and had no limit: a reflector (40 bytes in, 15 out), and a sign to a scanner.
- **Tokens** were 80 bits. Only online guessing reaches them, against a full handshake per guess and bans, so 80 bits was never the weak point; but a figure nobody has to argue about is worth 12
  characters of typing.

## Decision

1. **The committed phrase is required by default.** A call whose other end does not do it is ended, with a reason that names the way out: `--allow-legacy-phrase` (or `legacy_phrase = yes` in a
   profile) shows the old phrase instead, for a node older than 0.5.21. A mesh call to a fellow member (admitted by the line's signatures) and a relayed call between members are exempt, as before.
   `--require-sas` is still accepted and does nothing new.
2. **The echo port has a budget**: per source address, 8 at once and then 2 a second (a node asks at most six times when it starts: four for its NAT type, two when its switchboard is silent), and
   1000 a second for everyone. What is refused is counted (`echo_shed` in a switchboard's status). Many nodes behind one address starting together may see `nat unknown` until they measure again.
3. **New tokens are 128 bits** (32 hex digits). Tokens already given out keep working.

## Consequences

A call to a node older than 0.5.21 now ends unless the profile allows it. Tested: the echo budget on a real switchboard (`tests/ban2.sh`), the tokens (`tests/tokens.sh`, `tests/sw.sh`), the profile key
(`tests/jack.sh`); the phrase's required mode as before (`tests/sas.sh`), now the default.
