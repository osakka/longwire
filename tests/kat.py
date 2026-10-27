# SPDX-License-Identifier: GPL-3.0-or-later
# The vendored Monocypher, built by TCC, against independent implementations (Python's hashlib and the cryptography package) on
# random inputs: X25519, keyed BLAKE2b, ChaCha20-Poly1305 (RFC 8439) and Ed25519. A compiler that miscompiled one of them would
# disagree here. This is not a test of constant-time behaviour, and ML-KEM has no second implementation on the machine (docs/adr/0033).
import os, subprocess, hashlib, random, sys
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives import serialization as S
exe = sys.argv[1]
r = random.Random(20261027)
rb = lambda n: bytes(r.randrange(256) for _ in range(n))
lines, want = [], []
for i in range(300):
    sk, pk = rb(32), rb(32)
    # X25519: clamp is inside crypto_x25519; compare with the library on a real public key of someone else
    other = X25519PrivateKey.from_private_bytes(rb(32)); opub = other.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw)
    mine = X25519PrivateKey.from_private_bytes(sk)
    lines.append(f"X {sk.hex()} {opub.hex()}"); want.append(mine.exchange(other.public_key()).hex())
    lines.append(f"P {sk.hex()}"); want.append(mine.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw).hex())
    key, msg = rb(r.randrange(0, 65)), rb(r.randrange(0, 700))
    for op, n in (("H", 32), ("h", 16)):
        lines.append(f"{op} {key.hex() or '-'} {msg.hex() or '-'}")
        want.append(hashlib.blake2b(msg, key=key, digest_size=n).hexdigest())
    k, nonce, ad, pt = rb(32), rb(12), rb(r.randrange(0, 40)), rb(r.randrange(0, 1500))
    lines.append(f"A {k.hex()} {nonce.hex()} {ad.hex() or '-'} {pt.hex() or '-'}")
    want.append(ChaCha20Poly1305(k).encrypt(nonce, pt, ad or None).hex())
    seed, m = rb(32), rb(r.randrange(0, 300))
    lines.append(f"E {seed.hex()} {m.hex() or '-'}")
    e = Ed25519PrivateKey.from_private_bytes(seed)
    want.append(e.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw).hex()); want.append(e.sign(m).hex())
inp = "\n".join(lines) + "\n"
out = subprocess.run([exe], input=inp, capture_output=True, text=True).stdout.split()
bad = [i for i, (o, w) in enumerate(zip(out, want)) if o != w]
print(f"{len(want)} outputs compared against hashlib and cryptography; {len(out)} produced; mismatches: {len(bad)}")
sys.exit(1 if bad or len(out) != len(want) else 0)
