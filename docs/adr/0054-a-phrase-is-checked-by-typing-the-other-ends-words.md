# 0054 — A phrase is checked by typing the other end's words

Status: accepted; built in 0.5.37. Follows [0040](0040-the-safety-phrase-is-committed-before-it-is-shown.md).

## Context

The safety phrase proves a first call, but only if someone compares the two ends' words. On a server (a site node in a container, reached only by a console) nobody sees its words without logging
in, reading them out and comparing eight words by eye; and nothing remembered that a call had been checked, so the next look at the call could not tell.

## Decision

1. **`longwire verify`** lists each call with its phrase and whether it has been verified. **`longwire verify NUMBER WORDS…`** takes the words the *other* end shows, typed here (any case, any
   spacing, commas or dashes between them), and compares them with this end's own:
   - **a match** is recorded in `~/.longwire/verified` (the number and its key id, mode 0600): `show` and the pane mark the phrase *verified*, and the record is tied to the key, so a different
     key under the same number is not verified;
   - **a difference ends the call and forgets the number's key** (as `longwire unpin`), and says not to call back until the person has been reached another way.
2. **The pane** has a **Verify…** row under each call's phrase until it is verified: type the words they see.
3. On a server this is one command run in its console (or by whoever looks after it), with the words read off the other machine: nobody has to read the server's words out.

## Consequences

Checking a server's call no longer needs anyone to see the server's screen, and a checked call says so afterwards. The record is a convenience, not a new proof: the proof is still the words, and
the pin binds the key. Tested on two real nodes (`tests/bar.sh`): the list, a match in another case and with commas, `show` and the pane marking it, a mismatch ending the call and dropping the record.
