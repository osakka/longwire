# SPDX-License-Identifier: GPL-3.0-or-later
# The control panel pane (control-panel/longwire.hibr). hibr's desktop is not run: the functions the pane calls from it are
# stubbed to record what they were asked, the pane is sourced, and its rows and actions are driven against real nodes.
# What this cannot check is how the rows are drawn.
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node

PANE = L.ROOT + "/control-panel/longwire.hibr"

DRIVER = r'''
declare -gA CP GL
GL[ellipsis]="..."
mod load %(mod)s
LOG=%(log)s
fn cp_absent(int w, what) { CP[$w][0]["kind"]=head; CP[$w][0]["text"]="absent: $what"; return 0; }
fn dt_want(int ms) { return 0; }
fn dt_notep(prio, text, ...rest) { printf 'NOTE %%s %%s\n' "$prio" "$text ${rest[*]}" >> "$LOG"; }
fn dt_confirm(msg, cmd) { printf 'CONFIRM %%s -> %%s\n' "$msg" "$cmd" >> "$LOG"; DRV_CMD=$cmd; }
fn dt_ask(title, label, value, then, hint = "") { printf 'ASK %%s -> %%s\n' "$title" "$then" >> "$LOG"; DRV_THEN=$then; }
fn cp_save() { return 0; }
fn cp_toggle(var) { return 0; }
fn cp_next(cur, int dir, ...rest) { local x p=; for x in "${rest[@]}"; do [ "$p" = "$cur" ] && [ -n "$p" ] && { ret "$x"; return 0; }; p=$x; done; ret "${rest[0]}"; }
fn dt_job(kind, tag, then, ...cmd) { printf 'JOB %%s\n' "${cmd[*]}" >> "$LOG"; ret 1; }
. %(pane)s
fn dump(int w) {
	local n i
	n := longwire_rows "$w"
	for ((i = 0; i < n; i++)); do
		printf 'ROW %%s|%%s|%%s|%%s\n' "${CP[$w][$i]["kind"]}" "${CP[$w][$i]["text"]}" "${CP[$w][$i]["val"]}" "${CP[$w][$i]["key"]}"
	done
}
%(body)s
'''


def drive(env, body):
    log = f"{T}/pane.log"
    open(log, "w").close()
    src = DRIVER % dict(log=log, pane=PANE, mod=MOD, body=body)
    e = dict(os.environ, **env, HIBR_MODPATH=os.path.dirname(MOD))
    r = subprocess.run([HIBR, "-c", src], env=e, capture_output=True, text=True, timeout=30)
    rows = [l[4:].split("|") for l in r.stdout.splitlines() if l.startswith("ROW ")]
    return rows, open(log).read().splitlines(), r


def until(fn, timeout=20, step=0.3):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v: return v
        except Exception:
            pass
        time.sleep(step)
    return None


def main():
    print(f"-- pane: the control panel pane  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, *extra):
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", *extra])

    # no node at all
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), "dump 1")
    check(any(k == "head" and t == "No node is running" for k, t, v, key in rows), f"with no node the pane says so: {rows[:2]} {r.stderr[-200:]}")
    check(any(key == "start" for _, _, _, key in rows), "...and offers to start one")

    A = node("A", 4001, "--answer", "ask", "--name", "Alice", "--listed")
    B = node("B", 4002, "--answer", "ask")
    num = {k: until(lambda n=n: n.status()["number"], 20) for k, n in (("A", A), ("B", B))}
    check(all(num.values()), f"two nodes register: {num}")

    envB = dict(HOME=B.home, LW_DIR=B.home + "/lw", LW_SOCK=B.sock)
    rows, log, r = drive(envB, "dump 1")
    flat = {(k, t): (v, key) for k, t, v, key in rows}
    check(("action", "Number") in flat and flat[("action", "Number")][0].startswith("+"), f"a running node shows its full number: {flat.get(('action', 'Number'))}")
    check(flat.get(("action", "Switchboard"), ("",))[0] == "dial tone", "...and the switchboard state")
    check(any(k == "head" and t == "Calls" for k, t, v, key in rows) and any(k == "set" and key == "policy" and v == "ask" for k, t, v, key in rows),
          "...a Calls section and the answer policy")

    # A rings B
    mk = B.w.mark()
    A.cmd("dial", num["B"])
    ring = B.w.wait("ring", 15, after=mk)
    check(ring is not None, "B is ringing")
    time.sleep(1.2)
    rows, log, r = drive(envB, "dump 1")
    ringrow = [x for x in rows if x[3].startswith("ring:")]
    check(len(ringrow) == 1 and num["A"] in ringrow[0][1] and "Alice" in ringrow[0][1] and "a claim" in ringrow[0][1],
          f"the ringing call is a row naming the caller and saying the name is a claim: {ringrow}")
    check(ring and any(x[1].strip() == "safety phrase" and x[2] == ring["phrase"] for x in rows),
          f"...with the safety phrase the ring event gave: {ring and ring['phrase']!r}")
    key = ringrow[0][3] if ringrow else "ring:x"
    rows, log, r = drive(envB, f'dump 1 > /dev/null; longwire_do 1 "{key}" 1; $DRV_CMD')
    check(any(l.startswith("CONFIRM") and ring["phrase"] in l and "Alice, a claim" in l for l in log), f"clicking it asks, with the phrase in the question: {[l for l in log if l.startswith('CONFIRM')]}")
    check(B.w.wait("connected", 20, after=mk) is not None, "answering yes picks up and the call connects")
    time.sleep(1.5)
    rows, log, r = drive(envB, "dump 1")
    check(any(x[1] == num["A"] and x[3] == "hang:" + num["A"] for x in rows), "a connected peer is a row that offers to hang up")
    check(any(x[0] == "head" and x[1] == "Line" for x in rows) and any(x[1] == num["A"] and x[3].startswith("evict:") for x in rows),
          "...and the line lists its members, each of whom can be evicted (the line is not managed)")

    # policy and dial
    rows, log, r = drive(envB, 'longwire_do 1 policy 1; dump 1')
    pol = open(B.home + "/lw/answer").read().strip()
    check(pol == "known" and any(k == "set" and key == "policy" and v == "known" for k, t, v, key in rows),
          f"changing the answer policy from ask writes the next one to the file the node reads, and the row shows it: {pol}")
    rows, log, r = drive(envB, 'longwire_do 1 dial 1; $DRV_THEN "000-000-001"; $DRV_THEN "  "')
    check(any(l.startswith("ASK Dial") for l in log) and any(l.startswith("NOTE high") for l in log), f"Dial asks for a number, and a bad one is reported rather than swallowed: {log} {r.stderr[-300:]}")

    # starting: refused without what it needs, then with it
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock", PATH="/usr/bin:/bin"), 'longwire_do 1 start 1')
    check(any(l.startswith("NOTE high") and ("not installed" in l or "switchboard" in l) for l in log), f"Start without the command or a switchboard says what is missing: {log}")
    for n in (A, B, S):
        n.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("pane")
