# SPDX-License-Identifier: GPL-3.0-or-later
# A line with an operator (ADR 0026). The operator's key signs off the nodes; the founder names it and the others
# bring its admits. One switchboard on loopback, nodes joined to it, the operator's seed used only by the tools.
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
    print(f"-- operator: a managed line  (workdir {T})", flush=True)
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

    helper = Node("H", ns, "longwire.hibr", ["status"])      # only for its environment: the tools run anywhere
    op, op2 = f"{T}/operator.seed", f"{T}/impostor.seed"
    rc, auth = helper.cmd("operator", "new", op)
    check(rc == 0 and len(auth) == 64, f"the operator makes a key and gets its public half: {auth[:20]}...")
    rc, auth2 = helper.cmd("operator", "new", op2)
    check(helper.cmd("operator", "authority", op)[1] == auth, "...and can show it again")

    F = node("F", 4001, "--authority", auth)
    nums = {}
    nums["F"] = until(lambda: F.status()["number"], 20)
    st = until(lambda: F.status()["line"], 20)
    check(st is not None and st["managed"] is True and st["authority"] == auth and len(st["id_full"]) == 16,
          "the founder has a managed line from the start, with its full id")
    lid = st["id_full"]
    ids = {}

    def key_of(name, extra=()):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        e = dict(os.environ, HOME=f"{T}/{name}", LONGWIRE_MOD=MOD, LW_DIR=f"{T}/{name}/lw")
        return subprocess.run([HIBR, L.ROOT + "/scripts/longwire.hibr", "id"], env=e, capture_output=True, text=True).stdout.strip()

    def admit_for(who, seed=op):
        return helper.cmd("operator", "admit", seed, lid, ids[who])[1]

    for k in ("A", "B", "X", "Y"):
        ids[k] = key_of(k)
    # --- a node with the operator's admit joins, with no one asked ----------------------------
    A = node("A", 4002, "--admit", admit_for("A"))
    nums["A"] = until(lambda: A.status()["number"], 20)
    mk = F.w.mark()
    A.cmd("dial", nums["F"])
    ring = F.w.wait("ring", 15, after=mk)
    check(ring is not None and ring["admit"] is True, "the founder sees the operator's admit on the ring")
    check(until(lambda: A.status()["line"] and A.status()["line"]["managed"], 30) is not None, "A joins and sees a managed line")
    check(until(lambda: len(F.status()["line"]["members"]) == 2, 20) is not None, "...and the founder counts two members")
    # --- no admit, and an admit from the wrong key, are both refused ---------------------------
    X = node("X", 4003)
    nums["X"] = until(lambda: X.status()["number"], 20)
    mk = F.w.mark()
    X.cmd("dial", nums["F"])
    e = F.w.wait("call-failed", 20, after=mk)
    check(e is not None and "managed" in e["reason"], f"a node with no admit is refused, plainly: {e and e['reason']}")
    check(X.status()["line"] is None, "...and holds no line")
    Y = node("Y", 4004, "--admit", admit_for("Y", op2))
    nums["Y"] = until(lambda: Y.status()["number"], 20)
    mk = F.w.mark()
    Y.cmd("dial", nums["F"])
    e = F.w.wait("call-failed", 20, after=mk)
    check(e is not None and "managed" in e["reason"], "an admit signed by some other key is refused")
    check(len(F.status()["line"]["members"]) == 2, "the line is still two")
    # --- B joins through A, not the founder; the mesh builds ----------------------------------
    B = node("B", 4005, "--admit", admit_for("B"))
    nums["B"] = until(lambda: B.status()["number"], 20)
    B.cmd("dial", nums["A"])
    ok = until(lambda: all(len(N.status()["line"]["members"]) == 3 and N.status()["line"]["up"] == 3 for N in (F, A, B)), 60)
    check(ok is not None, "B joins through A; all three are a full mesh")
    # --- members cannot evict -----------------------------------------------------------------
    rc, out = F.cmd("evict", nums["A"])
    check(rc != 0 and "operator" in out, f"a member's own evict is refused: {out[:80]}")
    # --- the operator evicts A; B carries it, and the founder is down at the time -------------
    blob = helper.cmd("operator", "evict", op, lid, ids["A"])[1]
    rc, out = B.cmd("revoke", "00" + blob[2:])
    check(rc != 0, "a mangled eviction is not accepted")
    F.cmd("down")
    time.sleep(1)
    rc, out = B.cmd("revoke", blob)
    check(rc == 0, f"B carries the operator's eviction: {out}")
    check(A.w.wait("evicted", 15) is not None, "A is told it was evicted")
    check(until(lambda: len(B.status()["line"]["members"]) == 2, 15) is not None, "B now lists two members")
    F = node("F", 4001, "--authority", auth)
    ok = until(lambda: F.status()["line"] and len(F.status()["line"]["members"]) == 2 and
               all(m["number"] != nums["A"] for m in F.status()["line"]["members"]) , 40)
    fm = F.status()["line"]
    check(ok is not None, f"the founder, back from being offline, hears of the eviction from B: {fm and [m['number'] for m in fm['members']]}")
    mk = F.w.mark()
    A2 = A.cmd("dial", nums["F"])
    e = F.w.wait("call-failed", 20, after=mk)
    check(e is not None and "evicted" in e["reason"], f"the evicted node is refused if it rings back: {e and e['reason']}")
    L.finish("operator")


main()
