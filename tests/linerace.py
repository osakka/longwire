# SPDX-License-Identifier: GPL-3.0-or-later
# Two numbers dialled at once (BUG-13, ADR 0113). The first box is behind a link that takes 1.5 s each way, so its line reaches the desktop well after
# the desktop has dialled the second box: a node that told the second box "no line" at once ended on two lines. It must hold that handshake until the
# first line has come, and all three end on one line.
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, nsh, Node


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
    print(f"-- two numbers dialled at once, the first behind a slow link  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns, slow = L.mkns(), L.mkns()
    for x in (ns, slow):
        nsh(x, "ip link set lo up")
    sh("ip link add va type veth peer name vb")
    sh(f"ip link set va netns {ns.pid}")
    sh(f"ip link set vb netns {slow.pid}")
    nsh(ns, "ip addr add 10.77.0.1/24 dev va")
    nsh(ns, "ip link set va up")
    nsh(slow, "ip addr add 10.77.0.2/24 dev vb")
    nsh(slow, "ip link set vb up")
    nsh(slow, "/usr/sbin/tc qdisc add dev vb root netem delay 1500ms")
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "10.77.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, where, ip, policy):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, where, "longwire.hibr", ["up", "--switchboard", f"10.77.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                                                    "--listen", f"{ip}", "--keepalive", "5", "--answer", policy])

    def up(x):
        return sorted(p["number"] for p in x.status()["peers"] if p["state"] == "up")

    def line(x):
        l = x.status()["line"]
        return l and l["id"]

    A = node("A", slow, "10.77.0.2:4001", "known")
    na = until(lambda: A.status()["number"], 40)
    B = node("B", ns, "10.77.0.1:4002", "known")
    nb = until(lambda: B.status()["number"], 20)
    D = node("D", ns, "10.77.0.1:4003", "ask")
    until(lambda: D.status()["number"], 20)
    check(bool(na and nb), f"a slow box and a quick one register: {na} {nb}")
    mka, mkb, mkd = A.w.mark(), B.w.mark(), D.w.mark()
    D.cmd("dial", na)
    A.w.wait("ring", 30, after=mka)
    A.cmd("pickup")
    D.w.wait("connected", 40, after=mkd)		# the call is up; the slow box's line is three seconds away
    check(not line(D), "connected to the slow box, the desktop has no line yet")
    rc, out = D.cmd("dial", nb)			# at once, as a person may
    check(rc == 0, f"the second number is dialled while the first call comes up: {out}")
    B.w.wait("ring", 30, after=mkb)
    B.cmd("pickup")
    one = until(lambda: line(A) and line(A) == line(B) == line(D), 90)
    check(one is not None, f"all three end on one line: A {line(A)} B {line(B)} D {line(D)}")
    check(until(lambda: len(up(A)) == 2 and len(up(B)) == 2 and len(up(D)) == 2, 180) is not None, f"and all are in touch: A {up(A)} B {up(B)} D {up(D)}")
    L.finish("linerace")


main()
