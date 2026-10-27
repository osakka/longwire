# SPDX-License-Identifier: GPL-3.0-or-later
# The manual pages name every command and option the code has. Commands come from the node script's dispatch, the profile sub-commands from
# cmd_profile, the options of `up` from the table in docs/CLI.md, and the switchboard's options from its own option parsing.
import re, sys

ROOT = __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__)))
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def read(p):
    return open(f"{ROOT}/{p}").read()


def roff_bold_words(text):
    """Every word set in bold or italic by a macro line, with the roff escapes for a hyphen undone."""
    words = set()
    for line in text.splitlines():
        if re.match(r"\.(B|BI|BR|IB|RB|I|IR|TP|SS|TQ)\b", line):
            words |= set(re.findall(r"[A-Za-z0-9_.@:+/-]+", line.replace("\\-", "-").replace("\\fB", " ").replace("\\fR", " ").replace("\\fP", " ")))
    # plain text lines inside a .TP term are on the line after .TP; take words of bold escapes too
    for m in re.finditer(r"\\fB([^\\]*)\\f[RP]", text.replace("\\-", "-")):
        words |= set(re.findall(r"[A-Za-z0-9_.@:+/-]+", m.group(1)))
    return words


node = read("scripts/longwire.hibr")
man1 = read("man/longwire.1")
words1 = roff_bold_words(man1)

# the dispatch: the lines `name)` or `a|b)` of the final `case $cmd in`
dispatch = node[node.rindex("case $cmd in"):]
cmds = set()
for m in re.finditer(r"^([a-z|\"-]+)\)", dispatch, re.M):
    for c in m.group(1).split("|"):
        c = c.strip('"')
        if c and not c.startswith("-") and c != "help":
            cmds.add(c)
missing = sorted(c for c in cmds if c not in words1)
check(not missing, f"man/longwire.1 names every command of the script ({len(cmds)}): missing {missing}")

# the profile sub-commands
prof = node[node.index("cmd_profile() {"):node.index("# Start the node in the background")]
subs = set()
for m in re.finditer(r"^\t([a-z|\"]+)\)", prof, re.M):
    for c in m.group(1).split("|"):
        c = c.strip('"')
        if c:
            subs.add(c)
missing = sorted(s for s in subs if not re.search(r"profile " + s + r"\b", man1.replace("\\fB", "").replace("\\fR", "")))
check(not missing, f"...and every profile sub-command ({sorted(subs)}): missing {missing}")

# the options of `up`, from the table in CLI.md
cli = read("docs/CLI.md")
tbl = cli[cli.index("### Options of `up`"):cli.index("**Attaching.**")]
opts = set()
for line in tbl.splitlines():
    m = re.match(r"\| ((?:`--[a-z-]+[^`]*`(?:, )?)+) \|", line)
    if m:
        opts |= set(re.findall(r"--[a-z-]+", m.group(1)))
opts.add("--profile")
missing = sorted(o for o in opts if o not in man1.replace("\\-", "-"))
check(not missing, f"...and every option of `up` in docs/CLI.md ({len(opts)}): missing {missing}")

# the switchboard
sb = read("scripts/longwire-switchboard.hibr")
man_sb = read("man/longwire-switchboard.1").replace("\\-", "-")
sbopts = set(re.findall(r"^\t(--[a-z-]+)(?:\|--[a-z-]+)*\)", sb, re.M)) | set(re.findall(r"^\t\t?(--[a-z-]+)\)", sb, re.M))
for m in re.finditer(r"^\t(--[a-z|-]+)\)", sb, re.M):
    sbopts |= set(m.group(1).split("|"))
for m in re.finditer(r"^\t(--[a-z-]+(?:\|--[a-z-]+)+)", sb, re.M):
    sbopts |= set(m.group(1).split("|"))
sbopts |= {"--issue-codes"}
sbopts.discard("--help")
missing = sorted(o for o in sbopts if o not in man_sb)
check(len(sbopts) >= 14 and not missing, f"man/longwire-switchboard.1 names every option of the switchboard script ({len(sbopts)}): missing {missing}")

# the profile keys
keys = set(re.findall(r"switchboard\|token\|attach\|interface\|name\|listed\|answer", prof))
man5 = read("man/longwire-profiles.5")
check(all(k in man5 for k in ("switchboard", "token", "attach", "interface", "name", "listed", "answer")), "man/longwire-profiles.5 describes every profile key")

# every page the others refer to exists
refs = set()
for p in ("longwire.1", "longwire-switchboard.1", "longwire-profiles.5", "longwire-security.7"):
    for m in re.finditer(r"\.BR (longwire[a-z-]*) \((\d)\)", read("man/" + p)):
        refs.add(f"man/{m.group(1)}.{m.group(2)}")
import os
missing = sorted(r for r in refs if not os.path.exists(f"{ROOT}/{r}"))
check(not missing, f"every page the manual refers to exists: missing {missing}")

version = read("VERSION").strip()
stale = [f for f in sorted(os.listdir(f"{ROOT}/man")) if f'"longwire {version}"' not in read("man/" + f)]
check(not stale, f"every page carries the version in VERSION ({version}): stale {stale}")

sys.exit(1 if fails else 0)
