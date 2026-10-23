# 0005 — Standard primitives, and a hybrid X25519 + ML-KEM-768 handshake

Status: accepted

## Context

The brief asked for encryption that is "crazy efficient, does not have to be mainstream", and quantum-safe if possible.
Inventing a primitive is the classic way a VPN gets broken. A 256-bit symmetric key is already adequate against a
quantum attacker; what is exposed is *harvest now, decrypt later*, where traffic recorded today is decrypted once the key
exchange can be broken.

## Decision

Use boring primitives (X25519, ML-KEM-768, ChaCha20-Poly1305, BLAKE2b, Ed25519), all from small audited C that compiles
under TCC unchanged ([CRYPTO](../CRYPTO.md)), and compose them in a hybrid Noise-IK handshake that is secure if *either*
X25519 or ML-KEM holds. The efficiency comes from the protocol, not from exotic primitives: no TLS, no certificates, no
negotiation, a 29-byte data overhead, and a handshake every two minutes.

## Consequences

The handshake costs about 5 ms of CPU under TCC once per session, which is irrelevant. The *composition* is new and is
not protected by the primitives' own proofs, so it needs a formal model and an external review before anyone relies on
it *(planned: M8)*. The vendored libraries have not been checked against known-answer vectors *(planned: M8)*.
Signatures stay classical (Ed25519) until lines need them. An optional AEGIS-256 suite for machines with AES-NI is a
speed option *(planned: M7)*.

---

[← decisions](README.md)
