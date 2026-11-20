# SPDX-License-Identifier: GPL-3.0-or-later
# A line of F, A, B and a leaf L. L dialled F only. F, A and B mesh with each other; they ring L (they know it is a member) and L refuses, so L's only session is F's.
import os, subprocess, sys, time

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


def up(x):
    return sorted(p["number"] for p in x.status()["peers"] if p["state"] == "up")


def main():
    print(f"-- a leaf in a line of four  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    sb = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, *extra):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", "--answer", "auto", *extra])

    F = node("F", 4001)
    fnum = until(lambda: F.status()["number"], 20)
    A = node("A", 4002)
    B = node("B", 4003)
    Lf = node("L", 4004, "--leaf", "on")
    nums = {}
    for x, k in ((A, "A"), (B, "B"), (Lf, "L")):
        nums[k] = until(lambda: x.status()["number"], 20)
    A.cmd("dial", fnum)
    until(lambda: len(F.status()["line"]["members"]) == 2, 30)
    B.cmd("dial", fnum)
    until(lambda: len(F.status()["line"]["members"]) == 3, 30)
    Lf.cmd("dial", fnum)
    check(until(lambda: len(F.status()["line"]["members"]) == 4, 30) is not None, "the leaf joins the line by dialling the founder")
    check(until(lambda: len(up(A)) == 2 and len(up(B)) == 2, 60) is not None, f"the others mesh with each other: A {len(up(A))} B {len(up(B))}")
    time.sleep(40)                       # the others' mesh rings the leaf at 8 s, 16 s, 32 s
    check(up(Lf) == [fnum], f"the leaf holds one session, the founder's: {up(Lf)}")
    check(nums["L"] not in up(A) and nums["L"] not in up(B), "and A and B hold none with it")
    refused = [mm["declined"] for x in (A, B) for mm in x.status()["line"]["members"] if mm["number"] == nums["L"]]
    check(all(d >= 1 for d in refused), f"the members that called it (A and B, not the founder it dialled) were refused and know it is a leaf (declined {refused}); three refusals make the mesh call it every six hours (ADR 0097)")
    st = Lf.status()
    check(st["line"] is not None and len(st["line"]["members"]) == 4, "it still knows the line")
    L.finish("leafline")


main()
