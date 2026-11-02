# SPDX-License-Identifier: GPL-3.0-or-later
# The switchboard application (apps/switchboard.hibr). hibr's desktop is not run: the app is sourced with the few desktop
# calls it makes stubbed, and its scan, its event drain and its call board are driven against a real switchboard and real
# nodes. What this cannot check is how anything is drawn.
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node

APP = L.ROOT + "/apps/switchboard.hibr"

DRIVER = r'''
mod load %(mod)s
declare -gA GL
GL[spark1]=_; GL[spark2]=.; GL[spark3]=-; GL[spark4]=:; GL[spark5]==; GL[spark6]=+; GL[spark7]='*'; GL[spark8]='#'
fn dt_ms() { ret $((SECONDS * 1000 + ${DRV_MS:-0})); }
fn dt_want(int ms) { return 0; }
for v in DT_INK DT_FACE DT_DIM DT_GOOD DT_WARN DT_BAD DT_INFO DT_ACTIVE DT_SELINK; do declare -g "$v=#000000"; done
GL[block]='#'; GL[light]='.'; GL[circle]='o'; GL[dot]='.'; GL[star]='*'; GL[dash]='-'; GL[ellipsis]='~'; GL[dashh]='-'; GL[minus]='='; GL[hline]='-'; GL[cross]='x'
DT_SCROLLWHEEL=3
fn dt_scrollbar(...a) { return 0; }
fn dt_searchq(int id) { ret "${DRV_Q:-}"; }
fn dt_searchrows(int id) { ret 0; }
fn dt_searchon(int id) { return 1; }
fn dt_searchdraw(...a) { return 0; }
fn dt_searchclear(...a) { return 0; }
fn dt_searchfree(...a) { return 0; }
fn dt_searchopen(int id) { echo "SEARCHOPEN $id"; }
fn dt_searchkey(int id, key) { return 1; }
fn dt_notep(...a) { echo "NOTE $*"; }
fn dt_confirm(msg, cmd, ...a) { echo "CONFIRM $cmd $msg"; }
fn dt_menu(t) { echo "MENU $t"; }
fn dt_item(...a) { echo "ITEM $1|$2"; }
fn dt_mark(...a) { echo "MARK $1|$2|$3"; }
fn dt_dim(...a) { echo "DIM $1"; }
fn dt_sep() { return 0; }
fn dt_editmenu() { echo "MENU Edit"; }
fn dt_jobdir() { DTJ_DIR=%(t)s/jobs; mkdir -p "$DTJ_DIR"; }
declare -gA DTJ
fn dt_ptyjob(kind, tag, then, prompt, ...cmd) { echo "PTYJOB ${cmd[*]}"; ret 9; }
fn dt_jobfile(int n, which = "out") { ret "%(t)s/jobs/$n.$which"; }
console() { if [ "$1" = put ]; then printf 'PUT %%s %%s %%s %%s\n' "$3" "$4" "$5" "$6"; fi; return 0; }
. %(app)s
fn show(int id) {
	local i n=${SW[$id]["nf"]:-0}
	printf 'UP %%s online=%%s numbers=%%s ringing=%%s max=%%s policy=%%s\n' "${SW[$id]["up"]}" "${SW[$id]["online"]}" "${SW[$id]["numbers"]}" "${SW[$id]["ringing"]}" "${SW[$id]["max"]}" "${SW[$id]["policy"]}"
	for ((i = 0; i < ${SW[$id]["ntotal"]:-0}; i++)); do
		printf 'NODE %%s|%%s|%%s|%%s\n' "${SWN[$id][$i]["number"]}" "${SWN[$id][$i]["name"]}" "${SWN[$id][$i]["listed"]}" "${SWN[$id][$i]["online"]}"
	done
	for ((i = 0; i < n; i++)); do
		printf 'CALL %%s|%%s|%%s|%%s\n' "${SWF[$id][$i]["from"]}" "${SWF[$id][$i]["to"]}" "${SWF[$id][$i]["state"]}" "${SWF[$id][$i]["why"]}"
	done
}
%(body)s
'''


def drive(sock, body, **more):
    src = DRIVER % dict(mod=MOD, app=APP, body=body, t=T)
    e = dict(os.environ, HOME=f"{T}/swhome", LW_SOCK="/nonexistent", SB_DIR=os.path.dirname(sock), DT_SW_SOCK=sock)
    e.update(more)
    r = subprocess.run([HIBR, "-c", src], env=e, capture_output=True, text=True, timeout=60)
    return r.stdout.splitlines(), r


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
    print(f"-- sw: the switchboard application  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    out, r = drive(f"{T}/nowhere/ctl", "switchboard_open 1; sw_scan 1 0; show 1")
    check(out and out[0].startswith("UP 0"), f"with no switchboard the app knows it: {out[:1]} {r.stderr[-200:]}")

    def node(name, port, *extra):
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", *extra])
    A = node("A", 4001, "--answer", "auto", "--name", "Alice", "--listed")
    B = node("B", 4002, "--answer", "auto")
    num = {k: until(lambda n=n: n.status()["number"], 20) for k, n in (("A", A), ("B", B))}
    check(all(num.values()), f"two nodes register: {num}")

    # a service switchboard: no socket of the person's own, and one in /run/longwire-switchboard/ctl (here a link to S's)
    os.makedirs(f"{T}/run/longwire-switchboard")
    os.symlink(S.sock, f"{T}/run/longwire-switchboard/ctl")
    out, r = drive("", "x := sw_sock; echo \"SOCK $x\"; switchboard_open 1; sw_scan 1 0; show 1", SB_DIR=f"{T}/nobody", DT_SW_SOCK="", LW_RUN=f"{T}/run")
    check(any(l == f"SOCK {T}/run/longwire-switchboard/ctl" for l in out) and any(l.startswith("UP 1") for l in out),
          f"with no switchboard of its own the app reads the service's socket: {out[:2]} {r.stderr[-200:]}")

    out, r = drive(S.sock, "switchboard_open 1; sw_scan 1 0; show 1")
    nodes = {l[5:].split("|")[0]: l[5:].split("|") for l in out if l.startswith("NODE ")}
    check(out and out[0].startswith("UP 1") and "numbers=2" in out[0] and "max=1024" in out[0] and "policy=script" in out[0], f"it reads the switchboard's status: {out[:1]} {r.stderr[-200:]}")
    check(set(nodes) == set(num.values()) and nodes[num["A"]][1] == "Alice" and nodes[num["A"]][2] == "true" and nodes[num["B"]][1] == "",
          f"...and its registry, with the name a node gave and none invented: {nodes}")

    out, r = drive(S.sock, "switchboard_open 1; switchboard_draw 1 22 74 1 1")
    puts = [l for l in out if l.startswith("PUT ")]
    text = "\n".join(puts)
    check(len(puts) > 8 and "Nodes (2)" in text and num["A"] in text and '"Alice"' in text and "online" in text and "Calls" in text,
          f"drawn into a recording, the window has its head, both nodes (the name in quotes) and the call board: {len(puts)} lines {r.stderr[-300:]}")
    open(f"{T}/draw.txt", "w").write(text)
    st = S.status()
    check(st.get("rx_bytes", 0) > 0 and st.get("tx_bytes", 0) > 0 and st.get("rx_pkts", 0) > 0 and st.get("tx_pkts", 0) > 0 and st.get("registrations", 0) >= 2,
          f"the switchboard counts what comes in and goes out, and its registrations (docs/adr/0049): rx {st.get('rx_bytes')} tx {st.get('tx_bytes')} reg {st.get('registrations')}")
    check(any(l.endswith(" in") for l in puts) and any(l.endswith(" out") for l in puts) and sum("B/s" in l for l in puts) == 2 and "registered 2" in text and "bans 0" in text,
          f"the window shows traffic in and out as rates, and the totals since it started: {[l for l in puts if 'B/s' in l or 'registered' in l][:3]}")
    two = f"switchboard_open 1; sw_scan 1 0; sleep 1.2; sw_scan 1 1200; echo RATES ${{SW[1][rin]}} ${{SW[1][rout]}}"
    out, r = drive(S.sock, two)
    rl = [l for l in out if l.startswith("RATES ")]
    check(rl and all(x.isdigit() for x in rl[0].split()[1:]), f"two looks a moment apart give a rate in and out: {rl} {r.stderr[-200:]}")

    # the search: a part of a name in any case, or a number with or without its dashes
    out, r = drive(S.sock, "switchboard_open 1; sw_scan 1 0; switchboard_draw 1 22 74 1 1", DRV_Q="aLiC")
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check("Nodes (1 of 2)" in text and '"Alice"' in text and num["B"] not in text, f"searching for part of a name finds the one node: {r.stderr[-200:]}")
    out, r = drive(S.sock, "switchboard_open 1; sw_scan 1 0; switchboard_draw 1 22 74 1 1", DRV_Q=num["B"].replace("-", "")[2:7])
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check("Nodes (1 of 2)" in text and num["B"] in text and '"Alice"' not in text, f"and part of a number, typed without dashes, finds the other")
    out, r = drive(S.sock, "switchboard_open 1; sw_scan 1 0; switchboard_draw 1 22 74 1 1", DRV_Q="nobody-here")
    check(any("No node matches." in l for l in out), "a search that matches nothing says so")
    out, r = drive(S.sock, "switchboard_open 1; sw_scan 1 0; switchboard_key 1 o; switchboard_draw 1 22 74 1 1; x := switchboard_copy 1; echo COPY $x")
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check("online only" in text and "Nodes (2)" in text, f"o shows only the nodes online (both are): {r.stderr[-200:]}")
    check(any(l.startswith("COPY ") and l[5:] in num.values() for l in out), f"Copy takes the chosen node's number: {[l for l in out if l.startswith('COPY')]}")

    # the bans: b turns the list into them, x asks before lifting
    out, r = drive(S.sock, "switchboard_open 1; switchboard_key 1 b; sw_scan 1 0; switchboard_draw 1 22 74 1 1; switchboard_key 1 x")
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check("Bans (0)" in text and "Nobody is banned." in text and any(l.startswith("NOTE ") for l in out), f"b shows the bans (none), and x with none says so: {r.stderr[-200:]}")

    # the menus name every key the window answers
    out, r = drive(S.sock, "switchboard_open 1; DT_FOCUS=1; switchboard_menus")
    keys = {l.split("|")[1] for l in out if (l.startswith("ITEM ") or l.startswith("MARK ")) and "|" in l}
    menus = [l[5:] for l in out if l.startswith("MENU ")]
    check({"s", "o", "b", "f", "w", "c"} <= keys and menus[:4] == ["File", "Edit", "View", "Node"] and any(l.startswith("DIM Lift Ban") for l in out),
          f"File, Edit, View and Node menus name every key: s, o, b, f (search), c; Lift Ban dimmed outside the bans: {menus} {sorted(keys)} {r.stderr[-200:]}")
    out, r = drive(S.sock, "switchboard_open 1; DT_FOCUS=1; switchboard_key 1 b; switchboard_menus; switchboard_find 1; echo VIEW ${SW[1][view]}")
    check(any(l.startswith("MARK Bans|b|1") for l in out) and any(l.startswith("ITEM Lift Ban") for l in out) and "SEARCHOPEN 1" in out and "VIEW nodes" in out,
          f"in the bans view the menu ticks Bans and offers Lift Ban; Find goes back to the nodes and opens the search: {[l for l in out if l.startswith(('MARK','ITEM Lift','SEARCH','VIEW'))]}")


    # the calls, followed through the event stream: one scan opens it, then A dials B and the app drains what it hears
    body = f'''switchboard_open 1
sw_scan 1 0
sw_feedopen 1
echo FEED ${{SW[1][fd]}}
touch {T}/feed.ready
i=0
while [ $i -lt 60 ]; do
	sw_drain 1 $((i * 500))
	[ -e {T}/feed.go ] && [ "${{SW[1][nf]}}" -gt 0 ] && [ "${{SWF[1][0][state]}}" != ringing ] && break
	sleep 0.25; i=$((i + 1))
done
show 1
sw_prune 1 99999999
echo AFTER ${{SW[1][nf]}}
switchboard_close 1
echo CLOSED "${{SW[1][nf]-}}" "${{SW[1][fd]-}}"'''
    import threading
    res = {}
    t = threading.Thread(target=lambda: res.update(zip(("out", "r"), drive(S.sock, body))))
    t.start()
    until(lambda: os.path.exists(f"{T}/feed.ready"), 20)
    time.sleep(0.5)
    A.cmd("dial", num["B"])
    open(f"{T}/feed.go", "w").close()
    t.join(40)
    out = res.get("out", [])
    calls = [l[5:].split("|") for l in out if l.startswith("CALL ")]
    check("FEED 1" in out, "the app opens the switchboard's event stream beside the node's own")
    check(calls and calls[0][0] == num["A"] and calls[0][1] == num["B"] and calls[0][2] == "through",
          f"a call it heard ring is on the board, and then put through: {calls} {res.get('r') and res['r'].stderr[-300:]}")
    check("AFTER 0" in out, "after the linger time it is gone, and nothing remembers it")
    check(any(l.strip() == "CLOSED" for l in out), f"closing the window leaves no calls or nodes behind: {out[-6:]} {res.get('r') and res['r'].stderr[-400:]}")
    # the tokens (docs/adr/0050): the window's own switchboard's file, read and changed at once; a service's, through please
    os.makedirs(f"{T}/bin", exist_ok=True)
    open(f"{T}/bin/longwire-switchboard", "w").write(f"#!/bin/sh\nexec {HIBR} {L.ROOT}/scripts/longwire-switchboard.hibr \"$@\"\n")
    os.chmod(f"{T}/bin/longwire-switchboard", 0o755)
    tenv = dict(PATH=f"{T}/bin:" + os.environ["PATH"], DT_SW_TOKENS=f"{T}/tok/tokens")
    os.makedirs(f"{T}/tok", exist_ok=True)
    out, r = drive(S.sock, "switchboard_open 1; switchboard_key 1 t; switchboard_draw 1 22 74 1 1", **tenv)
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check("Tokens (0 unspent)" in text and "No unspent tokens" in text, f"t shows the tokens: none yet, and says n makes one: {r.stderr[-300:]}")
    out, r = drive(S.sock, "switchboard_open 1; switchboard_key 1 t; switchboard_key 1 n; switchboard_draw 1 22 74 1 1; x := switchboard_copy 1; echo COPY $x", **tenv)
    text = "\n".join(l for l in out if l.startswith("PUT "))
    tok = [l[5:] for l in out if l.startswith("COPY ")]
    filetoks = [l.strip() for l in open(f"{T}/tok/tokens") if l.strip()]
    check(tok and len(tok[0]) == 32 and filetoks == tok and "Tokens (1 unspent)" in text and "(new)" in text and any("A new token" in l and tok[0] in l for l in out),
          f"n makes a token in the file, shows it marked new, a note says it, and Copy takes it: {tok} {filetoks} {r.stderr[-300:]}")
    out, r = drive(S.sock, "switchboard_open 1; switchboard_key 1 t; switchboard_key 1 x; sw_tokdeldo; switchboard_draw 1 22 74 1 1", **tenv)
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check(any(l.startswith("CONFIRM sw_tokdeldo") for l in out) and "Tokens (0 unspent)" in text and not [l for l in open(f"{T}/tok/tokens") if l.strip()],
          f"x asks, then removes it: {[l for l in out if l.startswith(('CONFIRM','NOTE'))]} {r.stderr[-200:]}")
    os.makedirs(f"{T}/svc", exist_ok=True); open(f"{T}/svc/tokens", "w").write("abc\n"); os.chmod(f"{T}/svc/tokens", 0); os.chmod(f"{T}/svc", 0o500)
    out, r = drive(S.sock, "switchboard_open 1; switchboard_key 1 t; switchboard_draw 1 22 74 1 1; switchboard_key 1 r", **dict(tenv, DT_SW_TOKENS=f"{T}/svc/tokens", SW_TOKPRIV_TEST="1"))
    text = "\n".join(l for l in out if l.startswith("PUT "))
    check("belong to the switchboard's service" in text and any(l.startswith("PTYJOB please ") and l.endswith(f"token list --tokens {T}/svc/tokens") for l in out),
          f"a file that is not the person's own is read only when asked (r), through please: {[l for l in out if l.startswith('PTYJOB')]} {r.stderr[-200:]}")
    os.chmod(f"{T}/svc", 0o700)
    out, r = drive(S.sock, "switchboard_open 1; DT_FOCUS=1; switchboard_key 1 t; switchboard_menus", **tenv)
    check("MENU Tokens" in out and any(l.startswith("ITEM New Token|n") for l in out) and any(l.startswith("ITEM Remove Token") for l in out) and any(l.startswith("MARK Tokens|t|1") for l in out)
          and any(l.startswith("ITEM Tokens File") for l in out), f"a Tokens menu (New, Remove, Copy, Read Again), View > Tokens ticked, File > Tokens File: {[l for l in out if 'Token' in l]}")
    st = S.status()
    check(st.get("rings", 0) >= 1 and st.get("put_through", 0) >= 1, f"the switchboard counts the rings and the calls put through: rings {st.get('rings')} through {st.get('put_through')}")
    for n in (A, B, S):
        n.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("sw")
