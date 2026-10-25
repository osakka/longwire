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


def drive(sock, body):
    src = DRIVER % dict(mod=MOD, app=APP, body=body)
    e = dict(os.environ, HOME=f"{T}/swhome", LW_SOCK="/nonexistent", SB_DIR=os.path.dirname(sock), DT_SW_SOCK=sock)
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
    check(any(l.strip() == "CLOSED" for l in out), f"closing the window leaves no calls or nodes behind: {[l for l in out if l.startswith('CLOSED')]}")
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
