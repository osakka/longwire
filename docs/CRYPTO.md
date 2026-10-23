# Cryptography

**The rule: boring primitives, careful composition, no TLS.** longwire is efficient because of what it leaves out:
no certificate chains, no ASN.1, no cipher negotiation, a 12-byte header. It is *not* efficient because of exotic
primitives. Every primitive here is standardised, audited, constant-time C that compiles under TCC unchanged. The
one new thing is how they're combined, and that combination must be formally analysed before 1.0
([ROADMAP](ROADMAP.md) M8). That analysis has not been done yet *(planned: M8)*.

## 1. Primitives

| Role | Primitive | Source | Why |
|---|---|---|---|
| classical key exchange | **X25519** | Monocypher | fast, small, constant-time, no `__int128` needed |
| post-quantum KEM | **ML-KEM-768** (FIPS 203) | PQClean `clean` | NIST standard; category 3; 1184 B public key / 1088 B ciphertext |
| data AEAD | **ChaCha20-Poly1305** (RFC 8439) | Monocypher (`crypto_aead_init_ietf`) | fast in plain C without AES-NI or SIMD, which matters under TCC |
| cookie AEAD | XChaCha20-Poly1305 | Monocypher | random 24-byte nonces, safe for stateless cookies |
| hash / KDF / MAC | **BLAKE2b** (256-bit and 128-bit outputs, keyed) | Monocypher | fast, and keyed mode is a PRF, so no HMAC is needed |
| signatures (admit and evict records) | **Ed25519** (RFC 8032) | Monocypher `optional/` | standard, so other implementations can verify. Every identity carries an Ed25519 key; it signs admits and evictions ([PROTOCOL §6](PROTOCOL.md#6-control-messages-and-lines)) |
| hash inside ML-KEM | SHA-3 / SHAKE | PQClean `fips202.c` | required by FIPS 203 |
| randomness | `getrandom(2)` | OS | never a userspace PRNG; no entropy means exit, not a weaker source |

**On "quantum-safe".** A 256-bit symmetric key is already adequate against quantum attackers: Grover's algorithm
only halves the effective strength. The threat that matters is *harvest now, decrypt later*: someone records your
traffic today and breaks the key exchange once a large quantum computer exists. The hybrid handshake below closes
that gap. It is secure as long as **either** X25519 **or** ML-KEM holds.

## 2. Identity

A node's entire identity is one **32-byte seed**. Everything else is derived from it, so backing up those 32 bytes
backs up your number.

```
x25519_sk  = BLAKE2b-256(key = seed, "longwire x25519 v1")
sign_seed  = BLAKE2b-256(key = seed, "longwire ed25519 v1")
kem_coins  = BLAKE2b-512(key = seed, "longwire mlkem768 v1")       → ML-KEM.KeyGen_derand(coins)

bundle  = 0x01 ‖ x25519_pub(32) ‖ mlkem_pub(1184) ‖ ed25519_pub(32)        1249 bytes
key_id  = BLAKE2b-256(bundle)                                             32 bytes
address = "lw1" ‖ base32(key_id)                                          the "full number", 55 chars
```

PQClean exposes `crypto_kem_keypair_derand`, so the ML-KEM keypair is deterministic from the seed, just like the
other two keys.

## 3. Notation

```
H(x)          = BLAKE2b-256(x)
KDF1(ck, x)   = T0 = BLAKE2b-256(key=ck, x);  return BLAKE2b-256(key=T0, 0x01)
KDF2(ck, x)   = T0 = BLAKE2b-256(key=ck, x);  T1 = BLAKE2b-256(key=T0, 0x01);
                T2 = BLAKE2b-256(key=T0, T1 ‖ 0x02);  return (T1, T2)
AEAD(k, n, p, ad) = ChaCha20-Poly1305
DH(a, B)      = X25519
Encaps(pk)    → (ct, ss);   Decaps(sk, ct) → ss         (ML-KEM-768)
```

## 4. The handshake: hybrid Noise-IK with KEMs

Each side already knows the other's bundle before the handshake starts: from the switchboard (the callee gets the
caller's in `RING`, the caller the callee's in `GO`) or from a bundle file given with `--peer`. (The pin store keeps
key ids only, to check a bundle against, never the bundle.) The pattern follows Noise IK and adds three KEM
encapsulations: one to R's static KEM key (post-quantum authentication of R), one to I's ephemeral KEM key
(post-quantum forward secrecy) and one to I's static KEM key (post-quantum authentication of I). This is the same
family of designs as PQ-WireGuard, Rosenpass and PQNoise.

```
name = "longwire/1 pqIK X25519+MLKEM768 ChaChaPoly BLAKE2b"
ck = H(name);   h = H(ck ‖ R.key_id)

── HS_INIT (I → R) ─────────────────────────────────────────────────────────────────
  e, E      ← X25519 keypair                              ; ephemeral
  ek, EK    ← ML-KEM keypair                              ; ephemeral KEM
  h = H(h ‖ E);           ck = KDF1(ck, E)
  ck, k = KDF2(ck, DH(e, R.x))                            ; es
  (ct_R, s1) = Encaps(R.kem)
  h = H(h ‖ ct_R);        ck, k = KDF2(ck, s1)            ; skem(R)  → only R can continue
  h = H(h ‖ EK)
  c_id  = AEAD(k, 0, I.key_id, h);    h = H(h ‖ c_id)     ; I's identity hidden, even from a quantum eavesdropper
  ck, k = KDF2(ck, DH(I.x, R.x))                          ; ss
  c_pay = AEAD(k, 0, tai64n(12) ‖ I.index(3) ‖ flags(1), h);   h = H(h ‖ c_pay)
  mac1  = BLAKE2b-128(key = H("mac1" ‖ R.key_id), every byte before it)
  mac2  = BLAKE2b-128(key = cookie, every byte before it)  or zeros

  wire:  E(32) ‖ ct_R(1088) ‖ EK(1184) ‖ c_id(48) ‖ c_pay(32) ‖ mac1(16) ‖ mac2(16)  = 2416 B

── HS_RESP (R → I) ─────────────────────────────────────────────────────────────────
  e', E'    ← X25519 keypair
  h = H(h ‖ E');          ck = KDF1(ck, E')
  ck = KDF1(ck, DH(e', E))                                ; ee   classical forward secrecy
  ck = KDF1(ck, DH(e', I.x))                              ; se
  (ct_E, s2) = Encaps(EK);  h = H(h ‖ ct_E); ck = KDF1(ck, s2)    ; ekem PQ forward secrecy
  (ct_I, s3) = Encaps(I.kem); h = H(h ‖ ct_I); ck = KDF1(ck, s3)  ; skem(I) PQ auth of I
  ck, k = KDF2(ck, ε)
  c_pay = AEAD(k, 0, R.index(3) ‖ flags(1), h)
  mac1/mac2 as above, keyed with I.key_id

  wire:  E'(32) ‖ ct_E(1088) ‖ ct_I(1088) ‖ c_pay(20) ‖ mac1(16) ‖ mac2(16)  = 2260 B

── transport ───────────────────────────────────────────────────────────────────────
  (k_I→R, k_R→I) = KDF2(ck, ε);  erase e, e', ek, ck, h, s1..s3
```

**What it gives:**

| Property | Classical attacker | Quantum attacker |
|---|---|---|
| confidentiality of traffic | X25519 (es, ss, ee, se) | ML-KEM (skem R, ekem, skem I) |
| forward secrecy | `ee` | `ekem` |
| R authenticated to I | `es`, `ss` | only R can decapsulate `ct_R` |
| I authenticated to R | `ss`, `se` | only I can decapsulate `ct_I`; confirmed by I's first `DATA` packet |
| I's identity hidden from observers | yes | yes (protected by `s1`) |
| replay of HS_INIT | rejected: tai64n must increase per initiator key | same |

**DoS.** R checks the per-fragment tag, which is a `mac1` over each fragment ([PROTOCOL §3](PROTOCOL.md#3-sessions-and-timers)),
before doing *any* public-key work. That proves the sender knows R's key ID, which filters scans. Under load, R
answers with a `COOKIE` instead of working: an XChaCha20-Poly1305 encryption of
`BLAKE2b-128(key = rotating_secret, source address)`, which the initiator echoes in `cmac` on every fragment and in
`mac2` at the end of the message. It is WireGuard's mechanism, with the cookie MAC on each fragment so that R can
refuse at the first fragment, before it buffers anything. Details, sizes and limits are in
[PROTOCOL §3](PROTOCOL.md#3-sessions-and-timers).

## 5. Key schedule

| Key | Lifetime |
|---|---|
| seed | forever. Your identity; writing it out as 24 words *(planned: M3)* |
| transport keys | `--rekey` (default 120 s) until the next handshake; hard limit 1.5 × that, or 2⁶⁰ packets |
| line key | until a member is evicted, then rotated; it derives the site secret and never encrypts data |
| call token | one call setup, under 10 s |
| cookie secret | rotated every 120 s |

## 6. Measured

On this design machine (x86_64, one core, Linux 7.0), with vendored Monocypher (`f5f0a19`) and PQClean ML-KEM-768
`clean` (`0586a82`):

| Operation | TCC 0.9.27 |
|---|---|
| ML-KEM-768 keygen + encaps + decaps | **1.1–1.5 ms** |
| X25519 public key + DH | 0.53 ms |
| ChaCha20-Poly1305 on 1400 B packets | **36 MB/s ≈ 290 Mbit/s** (26 kpps) |

A full handshake costs about 5 ms of CPU under TCC, once every two minutes per peer. The cipher alone runs at about
**290 Mbit/s per core** under TCC, which is the agreed v1 target. End to end, the only throughput measured so far is
the test suite's 3 MB TCP transfer across two splice nodes, which ran at about 50 Mbit/s over veth pairs with a TCC
build; a proper benchmark *(planned: M7)*. TCC is the only compiler ([ADR 0021](adr/0021-tcc-is-the-compiler-without-exception.md)),
so more speed comes from the code: hand-written x86-64 assembly for ChaCha20 (TCC's assembler handles
x86; arm64 is still to be verified) *(planned: M7)*, as is an optional AEGIS-256 suite for machines with AES-NI *(planned: M7)*.

Monocypher, Ed25519 included, compiles under TCC with no changes. PQClean needs one 5-line `compat.h` shim, because
its compiler check doesn't recognise TCC. The shim defines `PQCLEAN_VLA` and `PQCLEAN_PREVENT_BRANCH_HACK` (TCC
supports the same `__asm__` barrier). The ML-KEM round trip was checked to agree on the shared secret.

## 7. Honest caveats

- **The composition is ours.** Every primitive is standard, but the handshake above is a new composition and needs a
  Tamarin or ProVerif model, then an external review, before anyone relies on it *(planned: M8)*. The vendored
  libraries have not been checked against known-answer vectors either *(planned: M8)*; their correctness rests on
  upstream's own tests.
- **Signatures are classical.** Admits and evictions use Ed25519. An eviction is accepted only inside a PQ-authenticated
  session from the member that signed it, but an *admit* travels in a ring, through the switchboard, and is checked by its
  signature alone: an attacker able to forge Ed25519 could get a node let in, unprompted, to a line it was never vouched
  into. The node would still have to complete a handshake under its own key. ML-DSA signatures are a later option
  *(planned)*.
- **Constant-time code depends on the compiler.** TCC barely optimises, which tends to *help* here. Timing tests
  (dudect-style) on the TCC build *(planned: M8)*.
- **Your LAN is now their LAN.** Cryptography authenticates the *peers*. It doesn't make a line member trustworthy;
  see [THREAT-MODEL](THREAT-MODEL.md).
