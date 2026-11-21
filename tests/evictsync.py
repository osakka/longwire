# SPDX-License-Identifier: GPL-3.0-or-later
# A member away while another is evicted (ADR 0110): it takes the eviction when it links again.
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
    print(f"-- a member whose admit went wrong is let in and gets a new one  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, policy):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h", "--listen", f"127.0.0.1:{port}",
                                                 "--cand-loopback", "--keepalive", "5", "--answer", policy])

    def up(x):
        return sorted(p["number"] for p in x.status()["peers"] if p["state"] == "up")

    A = node("A", 4001, "known")
    na = until(lambda: A.status()["number"], 20)
    B = node("B", 4002, "known")
    nb = until(lambda: B.status()["number"], 20)
    D = node("D", 4003, "ask")
    nd = until(lambda: D.status()["number"], 20)
    for X, nx in ((A, na), (B, nb)):
        mk = X.w.mark()
        D.cmd("dial", nx)
        X.w.wait("ring", 20, after=mk)
        X.cmd("pickup")
    check(until(lambda: len(up(D)) == 2 and len(up(A)) == 2 and len(up(B)) == 2, 90) is not None, f"three members, all in touch: D {up(D)} A {up(A)} B {up(B)}")

    # the desktop is away while B is evicted
    D.stop()
    until(lambda: D.proc.poll() is not None, 20)
    check(until(lambda: nd not in up(A), 90) is not None, "the desktop is away")
    rc, out = A.cmd("evict", nb)
    check(rc == 0, f"A evicts B: {out}")
    check(until(lambda: nb not in A.cmd("peers")[1], 10) is not None, "B is off A's record")
    check(nb in open(f"{T}/D/lw/lines").read().replace("-", "") or nb.replace("-", "") in open(f"{T}/D/lw/lines").read(), "the desktop, away, still has B on its record")
    D = node("D", 4003, "ask")
    check(until(lambda: na in up(D), 90) is not None, f"the desktop comes back and is in touch with A: {up(D)}")
    check(until(lambda: nb.replace("-", "") not in D.cmd("peers")[1].replace("-", ""), 30) is not None, f"and takes B off its record by itself: {D.cmd('peers')[1][:300]}")
    L.finish("evictsync")


main()
