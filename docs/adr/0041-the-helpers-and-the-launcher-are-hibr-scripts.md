# 0041 — The helpers and the launcher are hibr scripts

Status: accepted. Amends [0034](0034-a-connection-is-a-profile.md) and [0039](0039-a-site-node-from-the-pane.md), which did not say what the root helpers are written in; they were POSIX `sh`.

## Context

longwire is three hibr components ([0007](0007-three-components-all-hibr.md)). The two root helpers (`/usr/lib/longwire/jack`, `/usr/lib/longwire/site`) and the launcher (`/usr/bin/longwire`) were written as `sh` scripts, and nothing said why. The reasons
that were in the author's mind were a small thing to audit and not trusting an interpreter with root; neither survives a look at hibr itself: as root, hibr consults only its install directory for modules and
reads nothing of the environment's module path or rc files (hibr's ADR 0016, "privileges are dropped, never gained", and its module documentation), and `pkexec` cleans the environment besides. What `sh` cost is a second language in a hibr project, without strict
variables, typed parameters or regular expressions in the language: the address check was a pile of `case` patterns and `IFS` juggling.

## Decision

1. **All three are hibr scripts**, with `#!/usr/bin/hibr` (the interpreter the package depends on, at its installed path, not found through `PATH`).
2. The helpers validate with regular expressions (`=~`), in strict mode: an address is `^octet.octet.octet.octet/prefix$` with no leading zeros, a switchboard is `HOST:PORT/KEY`, a token is a short whitelist of characters, a name is letters, digits and `. _ -`. The
   token whitelist is narrower than before (visible characters minus a list); a token with another character is refused with a plain message.
3. **The interpreter is `$HIBR`**, the hibr the script was started under, whenever a script has to name one (the DHCP hook's shebang, the launcher's `exec`): never `hibr` found on `PATH`, and never `/proc/self/exe` read in a subshell (which names the subshell's program).
4. The `LONGWIRE_*` variables the helpers read (`LONGWIRE_RUN`, `LONGWIRE_DHCP_SCRIPT`, `LONGWIRE_ETC`, `LONGWIRE_SYSNET`, `LONGWIRE_RUN_DIR`) are for the tests. Under `pkexec` the environment is clean, so a caller cannot set them; run as root by hand, they are the caller's own.

## Consequences

The tests run each helper with the hibr under test (`tests/jack.sh`, `tests/site.sh`), including the DHCP hook run through its own shebang. The packages depend on hibr already. One thing is lost: a root helper in `sh` would still run on a machine whose hibr is broken, which is the machine on which nothing else in longwire runs either.
