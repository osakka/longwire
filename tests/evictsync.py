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

    def fp(x):
        return x.status()["line"]["key_fp"]

    A = node("A", 4001, "known")
    na = until(lambda: A.status()["number"], 20)
    B = node("B", 4002, "known")
    nb = until(lambda: B.status()["number"], 20)
    C = node("C", 4004, "known")
    nc = until(lambda: C.status()["number"], 20)
    D = node("D", 4003, "ask")
    nd = until(lambda: D.status()["number"], 20)
    for X, nx in ((A, na), (B, nb), (C, nc)):
        mk = X.w.mark()
        D.cmd("dial", nx)
        X.w.wait("ring", 20, after=mk)
        X.cmd("pickup")
        until(lambda: D.status().get("line") and D.status()["line"]["id"] == X.status()["line"]["id"], 30)
    check(until(lambda: all(len(up(X)) == 3 for X in (A, B, C, D)), 180) is not None, f"four members, all in touch")
    check(until(lambda: len({fp(X) for X in (A, B, C, D)}) == 1, 30) is not None, f"all hold one line key: {[fp(X) for X in (A, B, C, D)]}")

    # the desktop is away while C is evicted on B: the key changes on those in touch
    D.stop()
    until(lambda: D.proc.poll() is not None, 20)
    check(until(lambda: nd not in up(A) and nd not in up(B), 90) is not None, "the desktop is away")
    old = fp(A)
    rc, out = B.cmd("evict", nc)
    check(rc == 0 and until(lambda: fp(A) == fp(B) != old, 20) is not None, f"B evicts C: A and B take a new key ({fp(A)} {fp(B)}, was {old})")
    D = node("D", 4003, "ask")
    check(until(lambda: na in up(D) and nb in up(D), 180) is not None, f"the desktop comes back, in touch with A and B: {up(D)}")
    check(until(lambda: nc.replace("-", "") not in D.cmd("peers")[1].replace("-", ""), 30) is not None, "it takes C off its record by itself")
    same = until(lambda: fp(A) == fp(B) == fp(D) and fp(A), 60)	# members retell their key every 20 s
    check(same is not None, f"and A, B and the desktop end with one key, the newest: {[fp(X) for X in (A, B, D)]}")
    L.finish("evictsync")


main()
