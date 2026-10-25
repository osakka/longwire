# SPDX-License-Identifier: GPL-3.0-or-later
# The data the control panel and the switchboard app are drawn from (ADR 0030): a node's name in its status, the
# registry as pages, the switchboard's gauges, and call progress as events that carry numbers and nothing kept.
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node


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
    print(f"-- panel: what the desktop reads  (workdir {T})", flush=True)
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
    A = node("A", 4001, "--answer", "auto", "--name", "Alice", "--listed")
    B = node("B", 4002, "--answer", "auto")
    num = {k: until(lambda n=n: n.status()["number"], 20) for k, n in (("A", A), ("B", B))}
    check(all(num.values()), f"two nodes register: {num}")

    a, b = A.status(), B.status()
    check(a["name"] == "Alice" and a["listed"] is True, "a node's status carries the name it announces and whether it is listed")
    check(b["name"] == "" and b["listed"] is False, "...and says so when it announces nothing")

    def nodes(frm=None):
        argv = ["lw", "sb", "nodes"] + ([str(frm)] if frm is not None else []) + ["--sock", S.sock]
        r = subprocess.run([HIBR, "-c", f"mod load {MOD}; " + " ".join(argv)], capture_output=True, text=True)
        return json.loads(r.stdout.strip().splitlines()[-1])
    reg = until(lambda: (lambda r: r if r["total"] == 2 else None)(nodes()), 15)
    check(reg is not None, "the switchboard lists its registry: two nodes")
    if reg:
        by = {n["number"]: n for n in reg["nodes"]}
        check(by[num["A"]]["name"] == "Alice" and by[num["A"]]["listed"] and by[num["A"]]["online"], "an entry has the number, the name given, listed and online")
        check(by[num["B"]]["name"] == "" and not by[num["B"]]["listed"], "a node that gave no name has none invented")
        check(all(set(n) == {"number", "name", "listed", "online", "age"} for n in reg["nodes"]), "an entry holds no address and no key")
        check(reg["next"] == 2 and nodes(1)["nodes"][0]["number"] == reg["nodes"][1]["number"], "a page starts where asked and says where the next one starts")

    st = json.loads(subprocess.run([HIBR, "-c", f"mod load {MOD}; lw status --sock {S.sock}"], capture_output=True, text=True).stdout.strip().splitlines()[-1])
    check(st["max_nodes"] == 1024 and "hs_shed" in st and "cookies_sent" in st, "the switchboard's status has the gauges the app draws")

    mk = S.w.mark()
    A.cmd("dial", num["B"])
    ring = S.w.wait("call-ring", 15, after=mk)
    go = S.w.wait("call-go", 20, after=mk)
    check(ring is not None and ring["from"] == num["A"] and ring["to"] == num["B"], f"the switchboard says who is ringing whom: {ring}")
    check(go is not None and go["from"] == num["A"] and go["to"] == num["B"], "...and when it puts the call through (and then forgets it)")
    check(B.w.wait("connected", 20) is not None, "the call itself connects")

    for n in (A, B, S):
        n.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("panel")
