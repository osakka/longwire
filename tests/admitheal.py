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


# The mesh calls a member it lacks after 8, 16 and 32 s, then 60 more (line.c mesh_wait): two members can take two minutes to meet when calls are missed under load.
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

    def admit_of(name):
        return [l.split()[1] for l in open(f"{T}/{name}/lw/lines").read().splitlines() if l.startswith("admit ")][0]

    A = node("A", 4001, "known")
    na = until(lambda: A.status()["number"], 20)
    B = node("B", 4002, "known")
    nb = until(lambda: B.status()["number"], 20)
    D = node("D", 4003, "ask")
    nd = until(lambda: D.status()["number"], 20)
    kd = D.status()["key_id"]
    mk = A.w.mark()
    D.cmd("dial", na)
    A.w.wait("ring", 20, after=mk)
    A.cmd("pickup")
    mk = B.w.mark()
    D.cmd("dial", nb)
    B.w.wait("ring", 20, after=mk)
    B.cmd("pickup")
    check(until(lambda: len(up(D)) == 2 and len(up(A)) == 2 and len(up(B)) == 2, 180) is not None, f"three members, all in touch: D {up(D)} A {up(A)} B {up(B)}")
    check(admit_of("D").startswith(kd), "the desktop's admit names its own key")

    # the desktop's admit goes wrong: it holds one made for another member
    D.stop()
    until(lambda: D.proc.poll() is not None, 20)
    check(until(lambda: nd not in up(A) and nd not in up(B), 90) is not None, f"the desktop is gone: A {up(A)} B {up(B)}")
    bad = admit_of("B")
    s = open(f"{T}/D/lw/lines").read()
    open(f"{T}/D/lw/lines", "w").write("\n".join(("admit " + bad) if l.startswith("admit ") else l for l in s.splitlines()) + "\n")
    check(not admit_of("D").startswith(kd), "its saved admit now names another member")
    mka, mkb = A.w.mark(), B.w.mark()
    D = node("D", 4003, "ask")
    check(until(lambda: len(up(D)) == 2, 180) is not None, f"restarted, it is in touch with both members by itself, nobody picking up: {up(D)}")
    rings = [x for x in A.w.events[mka:] + B.w.events[mkb:] if x.get("event") == "ring"]
    check(all(x.get("admit") for x in rings), f"every ring it made was let in by itself: {[(x.get('from'), x.get('admit'), x.get('why')) for x in rings]}")
    rep = D.w.wait("admit-repaired", 30)
    check(rep is not None and until(lambda: admit_of("D").startswith(kd), 10) is not None, f"and it holds a new admit for its own key: {rep}")
    L.finish("admitheal")


main()
