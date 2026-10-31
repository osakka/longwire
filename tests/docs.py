# SPDX-License-Identifier: GPL-3.0-or-later
# Documentation consistency: the docs are compared with the code, and the build
# fails when they drift. Run by `make check` (python3 -I tests/docs.py).
#
#   links        every relative link and #anchor resolves
#   paths        every repository path a doc mentions exists
#   verbs        every `lw` verb and every `longwire` command in CLI.md exists in the
#                code, and every one in the code is documented
#   events       every event the workers emit is documented, and nothing else is
#   ids          the message ids and datagram types in the tables match the headers
#   constants    the sizes and limits the docs quote are the ones in the code
#   convention   unbuilt design carries one marker, *(planned: M3)*, and no other
#                wording ("not yet", "not built") is used outside ROADMAP/CHANGELOG
#
# A line that carries *(planned...)* is design that does not exist yet and is exempt
# from the checks that would otherwise fail on it.
import glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
DOCS = ["README.md"] + sorted(glob.glob("docs/*.md")) + sorted(glob.glob("docs/adr/*.md")) + (["CHANGELOG.md"] if os.path.exists("CHANGELOG.md") else [])
problems = []


def bad(where, what):
    problems.append(f"{where}: {what}")


def read(p):
    return open(p, encoding="utf-8").read()


def planned(line):
    return "(planned" in line


def code(*patterns):
    out = ""
    for pat in patterns:
        for f in sorted(glob.glob(pat)):
            out += read(f) + "\n"
    return out


# ---------------------------------------------------------------- links and anchors
def slugs(path):
    s = set()
    for line in read(path).splitlines():
        m = re.match(r"^#{1,6}\s+(.*)", line)
        if m:
            s.add(re.sub(r"[^\w\- ]", "", m.group(1).strip().lower()).replace(" ", "-"))
        s.update(re.findall(r'<a id="([^"]+)"', line))
    return s


for f in DOCS:
    for n, line in enumerate(read(f).splitlines(), 1):
        for l in re.findall(r"\]\(([^)]*)\)", line):
            if l.startswith("http"):
                continue
            p, _, a = l.partition("#")
            t = os.path.normpath(os.path.join(os.path.dirname(f), p)) if p else f
            if not os.path.exists(t):
                bad(f"{f}:{n}", f"link to missing file {l}")
            elif a and t.endswith(".md") and a not in slugs(t):
                bad(f"{f}:{n}", f"link to missing anchor {l}")

# ---------------------------------------------------------------- repository paths
PATH_RE = re.compile(r"`((?:engine|scripts|tests|module|vendor|brand|docs|packaging|control-panel|apps|desktop)/[\w./-]*[\w-])`")
for f in DOCS:
    if f.endswith("CHANGELOG.md"):
        continue
    for n, line in enumerate(read(f).splitlines(), 1):
        if planned(line):
            continue
        for m in PATH_RE.findall(line):
            if "*" in m or "NAME" in m:
                continue
            if not os.path.exists(m):
                bad(f"{f}:{n}", f"mentions {m}, which does not exist")

# the source layout block in ARCHITECTURE: each file named must exist
if os.path.exists("docs/ARCHITECTURE.md"):
    lines = read("docs/ARCHITECTURE.md").splitlines()
    try:
        i = next(k for k, l in enumerate(lines) if l.startswith("### Source layout"))
        opened = next(k for k in range(i + 1, len(lines)) if lines[k].startswith("```"))
        j = next(k for k in range(opened + 1, len(lines)) if lines[k].startswith("```"))
        base = ""
        for n in range(opened + 1, j):
            l = lines[n]
            m = re.match(r"^(\S+/)\s{2,}", l)                     # a top-level directory: engine/ ...
            if m:
                base = m.group(1)
                if not planned(l) and not os.path.isdir(m.group(1)):
                    bad(f"docs/ARCHITECTURE.md:{n + 1}", f"layout names directory {m.group(1)}, which does not exist")
                continue
            m = re.match(r"^(?:\s+|)([\w./-]+\.(?:c|h|hibr))\b", l)
            if m and not planned(l):
                name = m.group(1)
                cands = [name, base + name, "engine/" + name, "module/" + name, "scripts/" + name]
                if not any(os.path.exists(c) for c in cands):
                    bad(f"docs/ARCHITECTURE.md:{n + 1}", f"layout names {name}, which does not exist")
    except StopIteration:
        bad("docs/ARCHITECTURE.md", "has no '### Source layout' block")

# ...and the other way: every source file we wrote is in that layout
if os.path.exists("docs/ARCHITECTURE.md"):
    arch = read("docs/ARCHITECTURE.md")
    for f in sorted(glob.glob("engine/*.c") + glob.glob("engine/*.h") + glob.glob("scripts/*.hibr") + glob.glob("apps/*.hibr") + glob.glob("control-panel/*.hibr") + ["module/longwire.c"]):
        if not re.search(r"^\s*(?:\S+/)?" + re.escape(os.path.basename(f)) + r"\b", arch, re.M):
            bad("docs/ARCHITECTURE.md", f"the source layout does not list {f}")

# ---------------------------------------------------------------- verbs and commands
mod = read("module/longwire.c")
verbs = set(re.findall(r'strcmp\(av\[1\], "(\w+)"\)', mod))
sbverbs = set(re.findall(r'strcmp\(av\[2\], "(\w+)"\)', mod))
cli = read("docs/CLI.md")
doc_verbs = set()
for n, line in enumerate(cli.splitlines(), 1):
    for m in re.finditer(r"`lw (\w+)(?: (\w+))?", line):
        if planned(line):
            continue
        v = m.group(1)
        if v == "sb":
            if m.group(2) and m.group(2) not in sbverbs:
                bad(f"docs/CLI.md:{n}", f"`lw sb {m.group(2)}` is not in the module")
        elif v not in verbs:
            bad(f"docs/CLI.md:{n}", f"`lw {v}` is not in the module")
        doc_verbs.add(v)
for v in sorted(verbs - doc_verbs):
    bad("docs/CLI.md", f"the module has `lw {v}` but CLI.md never mentions it")
if "sb" not in doc_verbs:
    bad("docs/CLI.md", "`lw sb` is not documented")

script = read("scripts/longwire.hibr")
cmds = set(re.findall(r"^([a-z]+)\)", script, re.M)) - {"help"}
doc_cmds = set()
for n, line in enumerate(cli.splitlines(), 1):
    if planned(line):
        continue
    for m in re.finditer(r"`longwire(?:-switchboard)?\b ?([a-z][a-z-]*)?", line):
        w = m.group(1)
        if w and m.group(0).startswith("`longwire ") and w not in cmds:
            bad(f"docs/CLI.md:{n}", f"`longwire {w}` is not a command of scripts/longwire.hibr")
        if w:
            doc_cmds.add(w)
for c in sorted(cmds - doc_cmds):
    bad("docs/CLI.md", f"scripts/longwire.hibr has the command `{c}` but CLI.md never mentions it")

# ---------------------------------------------------------------- events
engine = code("engine/*.c")
node_events = set(re.findall(r'\\"event\\":\\"([a-z-]+)\\"', code("engine/call.c", "engine/site.c", "engine/line.c", "engine/sw.c", "engine/under.c")))
node_events |= set(re.findall(r'member_json\(n, "([a-z-]+)"', read("engine/line.c")))   # names passed to a helper
sb_events = set(re.findall(r'\\"event\\":\\"([a-z-]+)\\"', code("engine/board.c")))
m = re.search(r"\*\*Events\.\*\*(.*?)\n\n", cli, re.S)
doc_events = set(re.findall(r"`([a-z]+(?:-[a-z]+)*)`", m.group(1))) if m else set()
if not m:
    bad("docs/CLI.md", "has no '**Events.**' paragraph")
for e in sorted(node_events - doc_events):
    bad("docs/CLI.md", f"the node emits the event `{e}`, which the Events paragraph does not list")
named = {e for e in doc_events if e not in ("number", "path", "ep", "phrase", "reason", "from", "known", "by", "us", "them", "we_called", "role", "members", "lw", "active", "standby", "admit", "line", "up", "to", "host", "answers", "silent")}
for e in sorted(named - node_events - {"watch"}):
    bad("docs/CLI.md", f"the Events paragraph lists `{e}`, which no code emits")
sbdoc = read("docs/SWITCHBOARD.md")
for e in sorted(sb_events):
    if f"`{e}`" not in sbdoc:
        bad("docs/SWITCHBOARD.md", f"the switchboard worker emits the event `{e}`, which SWITCHBOARD.md never names")

# ---------------------------------------------------------------- ids and types
hdr = read("engine/proto.h") + read("engine/int.h")
mid = {n: int(v, 16) for n, v in re.findall(r"#define M_(\w+)\s+(0x[0-9a-fA-F]+)", hdr)}
tid = {n: int(v, 16) for n, v in re.findall(r"#define T_(\w+)\s+(0x[0-9a-fA-F]+)", hdr)}
kid = {n: int(v, 16) for n, v in re.findall(r"#define K_(\w+)\s+(0x[0-9a-fA-F]+)", hdr)}
for n, line in enumerate(sbdoc.splitlines(), 1):
    m = re.match(r"^\| `([A-Z]+)` \| `0x([0-9a-f]+)` \|", line)
    if m and not planned(line):
        name, val = m.group(1), int(m.group(2), 16)
        if name not in mid:
            bad(f"docs/SWITCHBOARD.md:{n}", f"message {name} is not in proto.h")
        elif mid[name] != val:
            bad(f"docs/SWITCHBOARD.md:{n}", f"{name} is 0x{val:02x} in the table but 0x{mid[name]:02x} in proto.h")
proto = read("docs/PROTOCOL.md")
for n, line in enumerate(proto.splitlines(), 1):
    m = re.match(r"^\| `0x([0-9a-f]+)` \| `([A-Z_]+)` \|", line)       # datagram types and control messages
    if m and not planned(line):
        val, name = int(m.group(1), 16), m.group(2)
        if name in mid:
            if mid[name] != val:
                bad(f"docs/PROTOCOL.md:{n}", f"{name} is 0x{val:02x} in the table but 0x{mid[name]:02x} in proto.h")
        elif name not in tid:
            bad(f"docs/PROTOCOL.md:{n}", f"{name} is in neither the datagram types nor the control messages of the headers")
        elif tid[name] != val:
            bad(f"docs/PROTOCOL.md:{n}", f"{name} is 0x{val:02x} in the table but 0x{tid[name]:02x} in the headers")
    m = re.match(r"^\| `0x([0-9a-f]+) ([A-Z_]+)` \|", line)             # plaintext kinds
    if m and not planned(line):
        val, name = int(m.group(1), 16), m.group(2)
        if name in kid and kid[name] != val:
            bad(f"docs/PROTOCOL.md:{n}", f"kind {name} is 0x{val:02x} in the table but 0x{kid[name]:02x} in the headers")
        elif name not in kid and name != "CTL":
            bad(f"docs/PROTOCOL.md:{n}", f"kind {name} is not in the headers")
for name in sorted(tid):
    if name in ("DATA",) or name.startswith("HS_") or name in ("COOKIE", "PUNCH", "HELLO"):
        if f"`{name}`" not in proto:
            bad("docs/PROTOCOL.md", f"datagram type {name} is in the headers but not in the table")

# ---------------------------------------------------------------- constants the docs quote
def num(pattern, text, what):
    m = re.search(pattern, text)
    if not m:
        bad("tests/docs.py", f"could not find {what} in the code")
        return None
    return m.group(1)


consts = [
    (r"#define FRAME_SINGLE (\d+)", hdr, "docs/ARCHITECTURE.md", "FRAME_SINGLE"),
    (r"#define LINE_MSS4 (\d+)", hdr, "docs/ARCHITECTURE.md", "LINE_MSS4"),
    (r"#define LINE_MSS6 (\d+)", hdr, "docs/ARCHITECTURE.md", "LINE_MSS6"),
    (r"#define HS_CHUNK (\d+)", hdr, "docs/PROTOCOL.md", "HS_CHUNK"),
    (r"#define HS_INIT_LEN (\d+)", hdr, "docs/PROTOCOL.md", "HS_INIT_LEN"),
    (r"#define HS_RESP_LEN (\d+)", hdr, "docs/PROTOCOL.md", "HS_RESP_LEN"),
    (r"#define FLOOD_RATE ([\d.]+)", read("engine/sw.c"), "docs/ARCHITECTURE.md", "FLOOD_RATE"),
    (r"#define FLOOD_BURST ([\d.]+)", read("engine/sw.c"), "docs/ARCHITECTURE.md", "FLOOD_BURST"),
    (r"#define PRESENT ([\d.]+)", read("engine/board.c"), "docs/SWITCHBOARD.md", "PRESENT"),
    (r"#define SILENCE ([\d.]+)", read("engine/site.c"), "docs/ARCHITECTURE.md", "SILENCE"),
    (r"#define LISTEN ([\d.]+)", read("engine/site.c"), "docs/ARCHITECTURE.md", "LISTEN"),
    (r"return n->role \? ([\d.]+) : ([\d.]+);", read("engine/hs.c"), "docs/PROTOCOL.md", "handshake budget"),
]
for pat, text, doc, what in consts:
    m = re.search(pat, text)
    if not m:
        bad("tests/docs.py", f"could not find {what} in the code")
        continue
    for v in m.groups():
        v = v.rstrip("0").rstrip(".") if "." in v else v
        if not re.search(r"(?<![\d.])" + re.escape(v) + r"(?![\d])", read(doc).replace(",", "")):
            bad(doc, f"does not state {what} = {v} (the code's value)")

# ---------------------------------------------------------------- convention
for f in DOCS:
    if f in ("docs/ROADMAP.md", "CHANGELOG.md"):
        continue
    for n, line in enumerate(read(f).splitlines(), 1):
        if re.search(r"\b(not yet|not built|Built in M\d|TODO)\b", line, re.I) and not planned(line):
            bad(f"{f}:{n}", "unbuilt design must carry *(planned: M..)*, not other wording: " + line.strip()[:80])
        for m in re.finditer(r"\(planned[^)]*\)", line):
            if not re.search(r"\*\(planned(: M\d+)?\)\*", line):
                bad(f"{f}:{n}", "the marker must be written *(planned: M3)* exactly")
                break

# ---------------------------------------------------------------- milestones and decision records
road = read("docs/ROADMAP.md")
milestones = set(re.findall(r"^\| \*\*(M\d+)\*\*", road, re.M))
for f in DOCS + sorted(glob.glob("docs/adr/*.md")):
    for n, line in enumerate(read(f).splitlines(), 1):
        for ms in re.findall(r"\*\(planned: (M\d+)\)\*", line):
            if ms not in milestones:
                bad(f"{f}:{n}", f"*(planned: {ms})* names a milestone that is not in the ROADMAP")
adr_files = sorted(glob.glob("docs/adr/[0-9]*.md"))
index = read("docs/adr/README.md")
for f in adr_files:
    base = os.path.basename(f)
    if f"]({base})" not in index:
        bad("docs/adr/README.md", f"does not list {base}")
    first = read(f).splitlines()[0]
    if not re.match(r"^# " + base[:4] + r" — ", first):
        bad(f, "first line must be '# NNNN — Title' with the file's number")
    if not re.search(r"^Status: ", read(f), re.M):
        bad(f, "has no Status line")
for m in re.findall(r"\]\((\d{4}-[^)]+\.md)\)", index):
    if not os.path.exists("docs/adr/" + m):
        bad("docs/adr/README.md", f"lists {m}, which does not exist")

# TCC is the only compiler (ADR 0021): no other compiler is offered, planned or used anywhere we write
RECORD = ("docs/adr/0021-tcc-is-the-compiler-without-exception.md", "CHANGELOG.md")
for f in DOCS:
    if f in RECORD:
        continue
    for n, line in enumerate(read(f).splitlines(), 1):
        if re.search(r"\b(gcc|g\+\+|clang|llvm|msvc|NDK|CC=)\w*", line, re.I):
            bad(f"{f}:{n}", "names a compiler other than TCC (docs/adr/0021): " + line.strip()[:70])
mk = read("Makefile")
if not re.search(r"^override CC = tcc$", mk, re.M):
    bad("Makefile", "must say `override CC = tcc`")
for n, line in enumerate(mk.splitlines(), 1):
    if re.search(r"\b(gcc|clang|cc)\b", line) and not line.lstrip().startswith("#"):
        bad(f"Makefile:{n}", "names a compiler other than TCC: " + line.strip()[:70])
for f in sorted(glob.glob("tests/*.sh") + glob.glob("tests/*.py") + glob.glob("scripts/*")):
    if f.endswith("docs.py"):
        continue
    for n, line in enumerate(read(f).splitlines(), 1):
        if re.search(r"\b(gcc|clang)\b", line):
            bad(f"{f}:{n}", "uses a compiler other than TCC: " + line.strip()[:70])

if problems:
    print("documentation is out of step with the code:")
    for p in problems:
        print("  " + p)
    print(f"\n{len(problems)} problem(s)")
    sys.exit(1)
print(f"documentation consistent: {len(DOCS)} files checked against the code")
