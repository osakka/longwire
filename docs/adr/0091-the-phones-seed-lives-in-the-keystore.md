# 0091 — The phone's seed lives in the keystore

Status: accepted; built in 0.5.72. The identity of a phone was a 32-byte file in the app's private storage, readable by anything that can read that storage (a backup, a rooted phone, a bug) and with no way to replace a lost phone but to tell the home node a new number. (Ticket LW-10.)

## Decision

1. **The seed is kept encrypted by a key in the Android Keystore** (AES-256-GCM, made in the keystore and not exportable). The file in the app's storage (`seed.enc`) is the nonce and the ciphertext: read alone it is nothing.
2. **The engine takes the seed from memory** (`lw_cfg.seed_mem`): the app decrypts it, hands it to the engine, and wipes its copy; the engine derives its identity and wipes the configured copy. No plaintext seed file is made.
3. **The 33 words** (the desktop's `longwire key export`: one word per byte and a checksum word) are shown by a button in the app, and **Restore from 33 words** replaces the identity from them, with the line off. A slip in a word or its order is refused by the checksum.
4. **An install from before this** has a plaintext `seed` file: its seed is taken into the keystore once, and the file is deleted, so the phone keeps its number. This is a one-time step in `SeedStore.load`, not a mode, and can be removed when no such install is left.

## Consequences

- A copy of the app's storage (a backup, an image) does not carry the identity out of the phone: the key to open it is in the keystore.
- A lost phone is replaced by the same number from the words, if they were written down; and anyone with the words can be that phone, which the app says.
- Wiping the app's data or removing the keystore entry makes a new identity, as before.
- **Not tried:** a phone with a hardware-backed keystore and one without behave the same here, but this has been compiled and the engine side unit tested (`tests/seedmem.sh`); the keystore code has not been run on a phone.
