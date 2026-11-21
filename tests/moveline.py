# SPDX-License-Identifier: GPL-3.0-or-later
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node


def until(fn, timeout=20, step=0.5):
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
    print(f"-- a desktop follows its boxes to a new line  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, policy="auto"):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h", "--listen", f"127.0.0.1:{port}",
                                                 "--cand-loopback", "--keepalive", "5", "--answer", policy])

    def up(x):
        return sorted(p["number"] for p in x.status()["peers"] if p["state"] == "up")

    def line(x):
        l = x.status()["line"]
        return l and l["id"]

    # the old line: the old box O and the desktop D
    O = node("O", 4001)
    onum = until(lambda: O.status()["number"], 20)
    D = node("D", 4002, "ask")
    until(lambda: D.status()["number"], 20)
    D.cmd("dial", onum)
    old = until(lambda: line(D), 30)
    check(bool(old) and line(O) == old, f"the desktop is on the old box's line ({old})")

    # the new boxes, on a line of their own
    N1 = node("N1", 4003)
    n1 = until(lambda: N1.status()["number"], 20)
    N2 = node("N2", 4004)
    until(lambda: N2.status()["number"], 20)
    N2.cmd("dial", n1)
    new = until(lambda: line(N1) == line(N2) and line(N1), 30)
    check(bool(new) and new != old, f"the new boxes make a line of their own ({new})")

    # with the old box still up, a dial of a new box is refused: the desktop stays on the line it has
    mk = D.w.mark()
    D.cmd("dial", n1)
    e = D.w.wait("call-failed", 30, after=mk)
    check(e is not None and "another line" in e.get("reason", "") and line(D) == old, f"with a member of its own line up, the desktop stays on it: {e and e.get('reason')}")

    # the old box goes (the move): the desktop's old line has nobody in touch
    O.stop()
    check(until(lambda: up(D) == [], 60) is not None, "the old box is gone, and the desktop has nobody")
    mk = D.w.mark()
    D.cmd("dial", n1)
    left = D.w.wait("line-left", 30, after=mk)
    check(left is not None and left.get("line", "").startswith(old), f"dialling a new box, the desktop leaves the line nobody of which is left: {left}")
    check(until(lambda: line(D) == new, 30) is not None, f"and is on the new line ({line(D)})")
    check(until(lambda: len(up(D)) == 2, 60) is not None, f"with both new boxes, by itself: {up(D)}")
    rings = [x for x in D.w.events[mk:] if x.get("event") == "ring"]
    check(all(x.get("admit") for x in rings), f"and every ring it got from them was let in by itself: {[(x.get('from'), x.get('admit'), x.get('why')) for x in rings]}")
    L.finish("moveline")


main()
