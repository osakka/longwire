# SPDX-License-Identifier: GPL-3.0-or-later
# An admit has an end (ADR 0065): the member is held to it, a renewal keeps the session, an ended admit takes the node off the line until a new one comes, and the
# operator's roll voids every admit made before it. One switchboard on loopback; the admits are signed by tests/mkadmit.c, which (unlike the real tool) can make one
# that ends in seconds.
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
    print(f"-- expiry: admits that end, and a roll  (workdir {T})", flush=True)
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

    helper = Node("H", ns, "longwire.hibr", ["status"])
    op = f"{T}/operator.seed"
    auth = helper.cmd("operator", "new", op)[1]
    F = node("F", 4001, "--authority", auth)
    fnum = until(lambda: F.status()["number"], 20)
    lid = until(lambda: F.status()["line"], 20)["id_full"]

    def key_of(name):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        e = dict(os.environ, HOME=f"{T}/{name}", LONGWIRE_MOD=MOD, LW_DIR=f"{T}/{name}/lw")
        return subprocess.run([HIBR, L.ROOT + "/scripts/longwire.hibr", "id"], env=e, capture_output=True, text=True).stdout.strip()

    def admit(kid, issued, expires):
        r = subprocess.run([L.ROOT + "/build/mkadmit", op, lid, kid, issued, expires], capture_output=True, text=True)
        return r.stdout.strip()

    ka, kb = key_of("A"), key_of("B")
    # --- the real tool: days, and its limits ---------------------------------------------------
    rc, out = helper.cmd("operator", "admit", op, lid, ka, "90")
    check(rc == 0 and len(out) == 145 * 2, "the operator's tool makes an admit for a number of days")
    rc, out = helper.cmd("operator", "admit", op, lid, ka, "401")
    check(rc != 0, "...and not one for more than 400 days")
    rc, out = helper.cmd("operator", "admit", op, lid, ka, "0")
    check(rc != 0, "...or none")

    # --- A's admit ends in 30 s; it is renewed before, and the session goes on -----------------
    A = node("A", 4002, "--admit", admit(ka, "-60", "+30"))
    anum = until(lambda: A.status()["number"], 20)
    mk = A.w.mark()
    A.cmd("dial", fnum)
    check(until(lambda: A.status()["line"] and len(F.status()["line"]["members"]) == 2, 30) is not None, "A, with an admit that has 30 s left, joins")
    e = A.w.wait("admit-expiring", 10, after=mk)
    check(e is not None and e["days"] == 1, f"it is told its admit is ending: {e}")
    check(A.status()["line"]["admit_expires"] > time.time(), "the status says when")
    fresh = admit(ka, "-5", "+" + str(3 * 86400))
    rc, out = A.cmd("renew", fresh)
    check(rc == 0, f"the operator's renewed admit is taken: {out}")
    time.sleep(35)                                              # past the first admit's end
    check(F.status()["line"]["up"] == 2 and len(F.status()["line"]["members"]) == 2, "past the old end the session and the membership go on")
    check(A.status()["line"] is not None, "...and A is still on the line")

    # --- B's admit ends and is not renewed: it is off the line, quiet, and comes back with a new one
    B = node("B", 4003, "--admit", admit(kb, "-60", "+20"))
    bnum = until(lambda: B.status()["number"], 20)
    B.cmd("dial", fnum)
    check(until(lambda: len(F.status()["line"]["members"]) == 3, 30) is not None, "B joins with an admit that has 20 s left")
    mkf, mkb = F.w.mark(), B.w.mark()
    e = F.w.wait("member-expired", 40, after=mkf)
    check(e is not None and e["number"] == bnum, f"when B's admit ends, the line is told: {e}")
    check(B.w.wait("admit-ended", 10, after=mkb) is not None, "and B is told its admit ended")
    check(len(F.status()["line"]["members"]) == 2, "B is off the founder's line")
    mkf = F.w.mark()
    time.sleep(12)
    check(len(F.status()["line"]["members"]) == 2 and F.w.wait("member-joined", 1, after=mkf) is None, "B stays out: it makes no calls while its admit has ended")
    rc, out = B.cmd("renew", admit(kb, "-5", "+" + str(3 * 86400)))
    check(rc == 0, f"a new admit is taken: {out}")
    B.cmd("dial", fnum)
    check(until(lambda: len(F.status()["line"]["members"]) == 3, 30) is not None, "B is back on the line")

    # --- the roll: nothing made before it is good ----------------------------------------------
    roll = helper.cmd("operator", "roll", op, lid)[1]
    check(len(roll) == 112 * 2, "the operator signs a roll")
    mka, mkb = A.w.mark(), B.w.mark()
    time.sleep(1.2)
    rc, out = F.cmd("roll", roll)
    check(rc == 0, f"the founder carries it: {out}")
    check(A.w.wait("admit-ended", 20, after=mka) is not None and B.w.wait("admit-ended", 20, after=mkb) is not None,
          "A and B, whose admits were made before the roll, are told theirs ended")
    check(until(lambda: len(F.status()["line"]["members"]) == 1, 20) is not None, "the founder (whose own admit is the root) holds the line alone")
    rc, out = A.cmd("renew", admit(ka, "-5", "+" + str(3 * 86400)))
    check(rc != 0, "an admit made before the roll is not taken")
    rc, out = A.cmd("renew", admit(ka, "+0", "+" + str(3 * 86400)))
    check(rc == 0, f"one made after it is: {out}")
    A.cmd("dial", fnum)
    check(until(lambda: len(F.status()["line"]["members"]) == 2, 30) is not None, "A is back on the line")
    L.finish("expiry")


main()
