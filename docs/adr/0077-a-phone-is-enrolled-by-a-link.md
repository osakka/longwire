# 0077 — A phone is enrolled by a link

Status: accepted; built in 0.5.62. The owner: the switchboard should be able to make something that enrols a phone (a QR code, or sent by email), because otherwise it is a manual process; if it is complicated, drop it.

## Context

A phone needs three things before it can join a line: the switchboard's address (`HOST:PORT/` and 55 characters of key), a token if the switchboard wants one, and the number of the home node to call. Typed on a phone, that is
a mistake waiting to happen, and the app is no use to anyone who cannot do it.

## Decision

1. **`longwire-switchboard token invite --host HOST:PORT [--home NUMBER] [--name TEXT]`** makes one token (the same 128 bits, in the same file, spent the same way) and prints one **link**:
   `longwire://enroll?sb=HOST:PORT/lw1…&token=…[&home=…][&name=…]`. The switchboard's own key comes from its seed, so the owner never copies it. If `qrencode` is installed it also draws the link as a QR code in the
   terminal (the packages suggest it); without it the script says so and the link still works, pasted into a message or an email. No mail is sent by the script: sending is the owner's act, by whatever they already use.
2. **The app opens the link** (an intent filter for `longwire://enroll`, so the phone's camera, a mail app or a chat opens it in Longwire). It **fills the form and turns nothing on**. It accepts only exactly what such a
   link carries, and refuses the rest with a message: the switchboard as `[A-Za-z0-9._-]{1,200}:PORT/lw1` and 52 base32 characters, a token of 32 hex digits, home as a number `NNN-NNN-NNN` or a full address, a name of at
   most 40 letters, digits, spaces and `. ' -`. A link anyone can send a phone is untrusted input; none of it is run, and none reaches a command line.
3. **The token is the secret.** The link is good for one phone, once; whoever uses it first spends it. It is not a way to make the phone trust a switchboard it was not told about: the link names the switchboard, and
   the phone pins it as it pins any.

## Not done

- **No scanner inside the app**: that would need a camera permission and a QR decoder, which the system's libraries do not have without Google's services. The phone's own camera does the scan.
- **No mail sent by the script**, and no shortened or hosted link: the token would pass through someone else's server.
- **Not run on a phone.** The link's shape is tested against the app's own checks (`tests/tokens.sh`); that the system routes `longwire://` to the app is for the first device test.

## Tests

`tests/tokens.sh`: one token in the file; the link's shape; the checks the app makes (the same patterns, run on what the script prints); no `--host`, a host that is not a name and a port, and a place no switchboard has run are refused.
