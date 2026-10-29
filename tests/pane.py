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
fn dt_ms() { ret "${DRV_MS:-0}"; }
fn dt_notep(prio, text, ...rest) { printf 'NOTE %%s %%s\n' "$prio" "$text ${rest[*]}" >> "$LOG"; }
fn dt_confirm(msg, cmd) { printf 'CONFIRM %%s -> %%s\n' "$msg" "$cmd" >> "$LOG"; DRV_CMD=$cmd; }
fn dt_ask(title, label, value, then, hint = "", ...rest) { printf 'ASK %%s -> %%s\n' "$title" "$then" >> "$LOG"; DRV_THEN=$then; }
fn dt_ptyjob(kind, tag, then, prompt, ...cmd) { printf 'PTYJOB %%s\n' "${cmd[*]}" >> "$LOG"; ret 7; }
fn dt_jobfile(int n, which) { ret "/nonexistent/$n.$which"; }
fn dt_joblast(int n) { ret ""; }
declare -gA DTJ
fn dt_droplist(...a) { printf 'DROP %%s\n' "${a[*]}" >> "$LOG"; }
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


def drive(env, body, ns=None):
    log = f"{T}/pane.log"
    open(log, "w").close()
    src = DRIVER % dict(log=log, pane=PANE, mod=MOD, body=body)
    e = dict(os.environ, **env, HIBR_MODPATH=os.path.dirname(MOD))
    cmd = [HIBR, "-c", src]
    if ns is not None:
        cmd = ["nsenter", "-t", str(ns.pid), "-n"] + cmd
    # output goes to files, not pipes: a node that Turn on starts keeps running in the background, and a pipe it inherited would never close
    of, ef = f"{T}/drv.out", f"{T}/drv.err"
    with open(of, "w") as fo, open(ef, "w") as fe:
        proc = subprocess.Popen(cmd, env=e, stdout=fo, stderr=fe, stdin=subprocess.DEVNULL)
        try:
            proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            proc.kill()
            print("driver timed out; it had printed:", open(of).read()[-500:], open(ef).read()[-800:], flush=True)
            raise
    r = subprocess.CompletedProcess(cmd, proc.returncode, open(of).read(), open(ef).read())
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
    check(any(k == "head" and t == "Longwire is off" for k, t, v, key in rows), f"with no node the pane says it is off: {rows[:2]} {r.stderr[-200:]}")
    check(any(key == "on" for _, _, _, key in rows), "...and offers to turn it on")

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

    # throughput: totals from the node's counters, rates from two reads of them
    body = r'''
DRV_MS=1000
json parse LWP_S '{"peers":[{"number":"111-111-111","state":"up","path":"direct","age":5,"rx_bytes":1000,"tx_bytes":2000}]}'
lwp_rates
echo "FIRST [${LWP_RRATE[111-111-111]-}]"
DRV_MS=2000
json parse LWP_S '{"peers":[{"number":"111-111-111","state":"up","path":"direct","age":6,"rx_bytes":6000000,"tx_bytes":2500000}]}'
lwp_rates
echo "RATE ${LWP_RRATE[111-111-111]-} ${LWP_TRATE[111-111-111]-}"
LWP_UP=1; LWP_T=$SECONDS
dump 1
DRV_MS=2500
json parse LWP_S '{"peers":[{"number":"111-111-111","state":"up","path":"direct","age":7,"rx_bytes":10,"tx_bytes":5}]}'
lwp_rates
echo "RESET [${LWP_RRATE[111-111-111]-}]"
x := lwp_rate 999; echo "F $x"; x := lwp_rate 12500; echo "F $x"; x := lwp_rate 940000000; echo "F $x"; x := lwp_rate 2500000000; echo "F $x"
x := lwp_bytes 999; echo "F $x"; x := lwp_bytes 12345; echo "F $x"; x := lwp_bytes 5999000; echo "F $x"; x := lwp_bytes 3100000000; echo "F $x"
'''
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock", DRV_MS="0"), body)
    out = r.stdout
    check("FIRST []" in out, "the first read of a connection gives a total but no rate")
    check("RATE 47992000 19984000" in out, f"the second gives bits a second from the bytes since the first: {[l for l in out.splitlines() if l.startswith('RATE')]} {r.stderr[-200:]}")
    rr = [x for x in rows if x[1].startswith("  rx ")]
    check(len(rr) == 1 and rr[0][1] == "  rx 47.9 Mbit/s   tx 19.9 Mbit/s" and rr[0][2] == "6.0 MB in, 2.5 MB out",
          f"the connection's row shows rx and tx rates and the totals: {rr}")
    check("RESET []" in out, "a counter that went down (the connection started again) has no rate until the next read")
    fm = [l[2:] for l in out.splitlines() if l.startswith("F ")]
    check(fm == ["999 bit/s", "12.5 kbit/s", "940.0 Mbit/s", "2.5 Gbit/s", "999 B", "12.3 kB", "5.9 MB", "3.1 GB"], f"rates and totals are written in decimal units: {fm}")

    # a service node: no node of the person's own, and a socket in /run/longwire/INSTANCE/ctl (here a link to B's)
    os.makedirs(f"{T}/run/longwire/svc")
    os.symlink(B.sock, f"{T}/run/longwire/svc/ctl")
    envS = dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock", LW_RUN=f"{T}/run")
    rows, log, r = drive(envS, 'dump 1 > /dev/null; dump 1')
    check(any(x[0] == "head" and x[1] == "This node" for x in rows) and any(x[1] == "Runs as" for x in rows),
          f"with no node of its own the pane reads a service node's socket and says so: {rows[:3]}")
    check(not any(x[3] == "stop" or x[3] == "policy" for x in rows),
          "...and offers neither to stop it nor to change its answer policy, which belong to the service")
    # out of step: no heading when the pane, the engine and the node agree; one that names the package when the pane is newer than it
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), "dump 1")
    check(not any(x[1] == "Out of step" for x in rows), "a pane, an engine and (no) node that agree show no 'Out of step' rows")
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock", LONGWIRE_SELF_VERSION="9.9.9"), "dump 1")
    check(any(x[0] == "head" and x[1] == "Out of step" for x in rows) and any("is older than this pane (9.9.9)" in x[1] for x in rows), f"a pane newer than the installed package says to update the package: {[x[1] for x in rows[:3]]}")
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=A.sock), "dump 1 > /dev/null; dump 1")
    check(not any(x[1] == "Out of step" for x in rows), "and a running node of the same version is no mismatch")

    # two nodes answer here: the person's own (A) and a service (B): a Node row chooses which the pane shows
    envT = dict(HOME=f"{T}/nobody", LW_SOCK=A.sock, LW_RUN=f"{T}/run")
    rows, log, r = drive(envT, 'dump 1 > /dev/null; dump 1')
    nr = [x for x in rows if x[0] == "set" and x[1] == "Node"]
    check(len(nr) == 1 and nr[0][2] == "this machine" and not any(x[1] == "Runs as" for x in rows), f"with two nodes the pane shows its own and has a Node row: {nr}")
    rows, log, r = drive(envT, 'dump 1 > /dev/null; longwire_do 1 lwnode 1; LWP_T=-100; dump 1 > /dev/null; dump 1')
    nr = [x for x in rows if x[0] == "set" and x[1] == "Node"]
    check(len(nr) == 1 and nr[0][2] == "service svc" and any(x[1] == "Runs as" for x in rows), f"choosing the next node shows the service, read through its socket: {nr}")
    rows, log, r = drive(dict(envT, DT_LW_NODE=f"{T}/run/longwire/gone/ctl"), 'dump 1 > /dev/null; dump 1')
    check(any(x[0] == "set" and x[1] == "Node" and x[2] == "this machine" for x in rows), "a node that is gone gives way to the first that answers")
    rows, log, r = drive(envS, 'dump 1 > /dev/null; dump 1')
    check(not any(x[1] == "Node" for x in rows), "and with only one node there is no Node row")
    rows, log, r = drive(envS, 'dump 1 > /dev/null; lwp_run hangup; echo "RC $?"; lwp_run nonsense; echo "RC $?"')
    check("RC 0" in r.stdout and any(l.startswith("NOTE high") for l in log),
          f"a verb goes to that socket (a hang up works) and a failed one is reported: {r.stdout.strip()[-40:]!r} {[l for l in log if l.startswith('NOTE')][:1]}")

    # Phone Style: a row only where the glyph set draws telephones, and it cycles between modern and classic
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), "dump 1")
    check(not any(x[3] == "lwstyle" for x in rows), "with a glyph set that has no telephones there is no Phone Style row (no choice to make)")
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), "GL[phoneoff]=x; dump 1")
    check(any(x[3] == "lwstyle" and x[0] == "set" and x[1] == "Phone Style" and x[2] == "modern" for x in rows), f"with telephones in the glyph set there is one, at modern: {[x for x in rows if x[3] == 'lwstyle']}")
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), 'GL[phoneoff]=x; longwire_do 1 lwstyle 1; echo "STYLE $DT_LWSTYLE"; longwire_do 1 lwstyle 1; echo "STYLE $DT_LWSTYLE"')
    check("STYLE classic" in r.stdout and r.stdout.strip().endswith("STYLE modern"), f"the row cycles modern, classic, modern: {r.stdout.strip()}")

    # Phone Icon Size: a row only where the glyph set has large telephones; it cycles small, large, small
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), "GL[phoneoff]=x; dump 1")
    check(not any(x[3] == "lwsize" for x in rows), "a glyph set with telephones but no large ones has no Phone Icon Size row")
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), "GL[phoneoff]=x; GL[phoneoff2]=xy; dump 1")
    check(any(x[3] == "lwsize" and x[0] == "set" and x[1] == "Phone Icon Size" and x[2] == "small" for x in rows), f"with large ones there is one, at small: {[x for x in rows if x[3] == 'lwsize']}")
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock"), 'GL[phoneoff]=x; GL[phoneoff2]=xy; longwire_do 1 lwsize 1; echo "SIZE $DT_LWSIZE"; longwire_do 1 lwsize 1; echo "SIZE $DT_LWSIZE"')
    check("SIZE large" in r.stdout and r.stdout.strip().endswith("SIZE small"), f"the row cycles small, large, small: {r.stdout.strip()}")

    # turning on: refused without what it needs
    rows, log, r = drive(dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock", PATH="/usr/bin:/bin"), 'longwire_do 1 on 1')
    check(any(l.startswith("NOTE high") and ("not installed" in l or "switchboard" in l) for l in log), f"Turn on without the command or a switchboard says what is missing: {log}")

    # the connection form, over a real `longwire profile` (a wrapper in PATH stands for the installed launcher)
    binp = f"{T}/bin"
    os.makedirs(binp)
    wrapper = f"{binp}/longwire"
    open(wrapper, "w").write(f'#!/bin/sh\nexec {HIBR} {L.ROOT}/scripts/longwire.hibr "$@"\n')
    os.chmod(wrapper, 0o755)
    ph = f"{T}/formhome"
    os.makedirs(ph)
    envF = dict(HOME=ph, XDG_CONFIG_HOME=f"{ph}/cfg", LW_DIR=f"{ph}/lw", LW_SOCK=f"{ph}/none.sock", LONGWIRE_MOD=MOD,
                PATH=f"{binp}:" + os.environ["PATH"])
    rows, log, r = drive(envF, 'dump 1')
    flat = {(x[0], x[1]): x[2:] for x in rows}
    check(any(x[0] == "head" and x[1] == "Longwire is off" for x in rows) and any(x[3] == "on" for x in rows),
          f"with no node the pane says it is off and has Turn on: {rows[:3]}")
    check(flat.get(("action", "Profile"), [""])[0].startswith("none yet") and flat.get(("action", "Token"), [""])[0] == "none",
          "with no profile it says none yet, and no token")
    rows, log, r = drive(envF, 'lwp_setsb "127.0.0.1:1877/lw1abc"; lwp_settoken "secret-token-1"; lwp_pset attach jack; lwp_pset name "Form Box"; dump 1')
    flat = {(x[0], x[1]): x[2:] for x in rows}
    check(flat.get(("set", "Profile"), [""])[0] == "home" and flat.get(("action", "Switchboard"), [""])[0] == "127.0.0.1:1877/lw1abc",
          f"setting the switchboard makes the profile home and shows it: {flat.get(('set', 'Profile'))} {flat.get(('action', 'Switchboard'))} {r.stderr[-200:]}")
    check(flat.get(("action", "Token"), [""])[0] == "set (14 characters)", f"the token is shown as its length, never itself: {flat.get(('action', 'Token'))}")
    check("secret-token-1" not in r.stdout and "secret-token-1" not in open(f"{T}/pane.log").read(), "the token is in no row and no log")
    pf = f"{ph}/cfg/longwire/profiles"
    check(os.path.exists(pf) and oct(os.stat(pf).st_mode & 0o777) == "0o600" and "token = secret-token-1" in open(pf).read(),
          "it went into the profiles file, which only the person can read")
    rows, log, r = drive(envF, 'lwp_pset interface lo; longwire_do 1 on 1')
    check(any(l == "JOB longwire on home" for l in log), f"Turn on with the jack there starts `longwire on home`: {log}")
    rows, log, r = drive(envF, 'lwp_pset interface lw9; longwire_do 1 on 1')
    check(any(l == "PTYJOB longwire jack lw9" for l in log), f"Turn on with no jack asks for it to be made, with the password dialog: {log}")
    # the address of the jack: with one set that the device does not have, Turn on asks for the jack again, with the address; with it there, it does not
    rows, log, r = drive(envF, 'lwp_pset interface lo; lwp_pset address 10.77.0.2/24; dump 1; longwire_do 1 on 1')
    check(any(x[0] == "action" and x[1] == "Address" and x[2] == "10.77.0.2/24" for x in rows), f"the pane has an Address row with the address: {[x[:3] for x in rows if x[0] == 'action']}")
    check(any(l == "PTYJOB longwire jack --address 10.77.0.2/24 lo" for l in log), f"Turn on with an address the device lacks asks for it to be put there: {log}")
    rows, log, r = drive(envF, 'lwp_pset address 127.0.0.1/8; longwire_do 1 on 1')
    check(any(l.startswith("JOB longwire on") for l in log) and not any("PTYJOB" in l for l in log), f"and with the address already on the device it just turns on: {log}")
    rows, log, r = drive(envF, 'lwp_pset address dhcp; longwire_do 1 on 1')
    check(any(l.startswith("JOB longwire on") for l in log) and not any("PTYJOB" in l for l in log), f"address dhcp on a device that already has an address (lo): it just turns on: {log}")
    rows, log, r = drive(envF, 'lwp_pset interface lw9; lwp_pset address dhcp; longwire_do 1 on 1')
    check(any(l == "PTYJOB longwire jack --address dhcp lw9" for l in log), f"address dhcp on a device that is not there asks for the jack, with a DHCP client: {log}")
    rows, log, r = drive(envF, 'lwp_pset address ""; lwp_pset interface lw9; dump 1')
    check(any(x[0] == "action" and x[1] == "Address" and x[2] == "none" for x in rows), f"an empty address shows as none: {[x[:3] for x in rows if x[0] == 'action']}")
    rows, log, r = drive(envF, 'lwp_pset attach splice; longwire_do 1 on 1')
    check(any(l.startswith("NOTE high") and "setup splice" in l for l in log), f"splice says the one-time step: {log}")
    rows, log, r = drive(envF, 'lwp_pset attach jack; lwp_newprofile "work"; lwp_pset switchboard "other:1877/lw1xyz"; dump 1')
    flat = {(x[0], x[1]): x[2:] for x in rows}
    check(flat.get(("set", "Profile"), [""])[0] == "work", f"a new profile becomes the one shown: {flat.get(('set', 'Profile'))}")
    check(any(x[0] == "action" and x[1] == "Rename profile work" for x in rows), f"there is a Rename profile row for the profile shown: {[x[:2] for x in rows if x[0] == 'action']}")
    rows, log, r = drive(envF, 'longwire_do 1 profile 1; n := lwp_cur; echo "NOW $n"')
    check("NOW work" in r.stdout, f"the profile row cycles from the default (home) to the other profile: {r.stdout.strip()[-30:]}")
    # rename the profile shown, then back (the rest of this file expects it to be called work)
    rows, log, r = drive(envF, 'lwp_renameprofile "work2"; n := lwp_cur; echo "NOW $n"; lwp_renameprofile "work"; n := lwp_cur; echo "THEN $n"')
    check("NOW work2" in r.stdout and "THEN work" in r.stdout and "THEN work2" not in r.stdout, f"renaming the profile shown keeps it the one shown: {r.stdout.strip()[-40:]}")
    # Turn on, end to end: a job runner that runs the command and then calls the callback, as the desktop's does (the stub used elsewhere
    # returns at once and never runs it, which is how a callback that cannot run went unnoticed)
    jobs = r'''
DRV_JN=0; DRV_DIR=%s
mkdir -p "$DRV_DIR"
fn dt_job(kind, tag, then, ...cmd) {
	local n st
	DRV_JN=$((DRV_JN + 1)); n=$DRV_JN
	"${cmd[@]}" > "$DRV_DIR/$n.out" 2> "$DRV_DIR/$n.err"
	st=$?
	"$then" "$n" "$st"
	ret "$n"
}
fn dt_jobfile(int n, which = "out") { ret "$DRV_DIR/$n.$which"; }
''' % f"{T}/jobs"
    eh = f"{T}/e2ehome"
    os.makedirs(eh)
    envE = dict(HOME=eh, XDG_CONFIG_HOME=f"{eh}/cfg", LW_DIR=f"{eh}/lw", LW_SOCK=f"{eh}/lw/ctl", LONGWIRE_MOD=MOD, LONGWIRE_HIBR=HIBR,
                PATH=f"{binp}:" + os.environ["PATH"])
    sub = lambda *a: subprocess.run([HIBR, L.ROOT + "/scripts/longwire.hibr", *a], env=envE, capture_output=True, text=True)
    sub("profile", "set", "good", f"switchboard=127.0.0.1:1877/{addr}", "attach=sock", f"interface={eh}/p:{eh}/h")
    sub("profile", "set", "bad", "switchboard=127.0.0.1:1877/lw1notakey", "attach=sock", f"interface={eh}/p:{eh}/h")
    rows, log, r = drive(envE, jobs + "longwire_do 1 on 1; echo done", ns)
    notes = [l for l in log if l.startswith("NOTE")]
    check(any(l.startswith("NOTE low") and "on, your number is" in l for l in notes) and "done" in r.stdout,
          f"Turn on with the job runner running the callback: the note says the number it got: {notes} {r.stderr[-300:]}")
    st = sub("status")
    check(st.returncode == 0 and '"number"' in st.stdout, "...and the node is up")
    sub("off")
    until(lambda: sub("status").returncode != 0, 10)
    sub("profile", "default", "bad")
    rows, log, r = drive(envE, jobs + "longwire_do 1 on 1; echo done", ns)
    notes = [l for l in log if l.startswith("NOTE")]
    check(any(l.startswith("NOTE high") and "Longwire did not turn on:" in l and len(l.split("on:", 1)[1].strip()) > 5 for l in notes) and "done" in r.stdout,
          f"Turn on that cannot work: the note says it did not, and why: {notes} {r.stderr[-300:]}")
    sub("off")
    until(lambda: sub("status").returncode != 0, 10)

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
