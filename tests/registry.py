# SPDX-License-Identifier: GPL-3.0-or-later
# The registry of switchboard codes. A registry R, a switchboard X it gives +1, switchboards Y and W that get the next codes,
# a switchboard V that registers with a switchboard that is not a registry, and Z, a private one with no registry at all.
# Everything runs on loopback in one namespace.
import json, os, re, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node


def until(fn, timeout=25, step=0.3):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v: return v
        except Exception:
            pass
        time.sleep(step)
    return None


def lw(*a):
    return subprocess.run([HIBR, "-c", f"mod load {MOD}; lw " + " ".join(a)], capture_output=True, text=True).stdout.strip()


def main():
    print(f"-- registry of switchboard codes  (workdir {T})", flush=True)
    # the numbering plan itself, without any network
    plan = [lw("plan", str(k)) for k in (0, 1, 29, 30, 84, 85, 178, 179, 180)]
    check(plan == ["20", "21", "49", "500", "554", "556", "649", "6500", "6501"],
          f"the plan runs 20..49, then 500..649 without 555, then 6500: {plan}")
    codes = [lw("plan", str(k)) for k in range(0, 400)]
    prefix_free = not any(a != b and b.startswith(a) for a in codes[:200] for b in codes)
    check(prefix_free and len(set(codes)) == 400 and not any(c.startswith(("0", "1", "8", "9")) for c in codes),
          "400 codes in a row: all different, none a prefix of another, none starting with 0, 1, 8 or 9")

    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])

    def addr_of(name):
        os.makedirs(f"{T}/{name}/sb", exist_ok=True)
        lw("key", "new", f"{T}/{name}/sb/seed")
        return lw("key", "address", f"{T}/{name}/sb/seed")
    ax = addr_of("X")

    def switchboard(name, port, registry, *extra):
        sb = Node(name, ns, "longwire-switchboard.hibr", ["--listen", f"127.0.0.1:{port}", "--registry", registry, *extra])
        until(lambda: os.path.exists(sb.sock), 15)
        return sb
    R = switchboard("R", 1900, "none", "--issue-codes", "--public-switchboard", ax)
    ar = lw("key", "address", f"{T}/R/sb/seed")
    reg = f"127.0.0.1:1900/{ar}"
    X = switchboard("X", 1901, reg)
    Y = switchboard("Y", 1902, reg)
    ay = lw("key", "address", f"{T}/Y/sb/seed")
    W = switchboard("W", 1903, reg)                       # the third from this address in a day: refused
    V = switchboard("V", 1904, f"127.0.0.1:1902/{ay}")    # Y is not a registry
    Z = switchboard("Z", 1905, "none")
    az = lw("key", "address", f"{T}/Z/sb/seed")

    code_of = lambda sb: until(lambda: sb.status()["code"], 25)
    cx, cy = code_of(X), code_of(Y)
    check(cx == "1", f"the public switchboard is given +1: {cx}")
    check(cy == "20", f"the next switchboard is given the first code in the plan: {cy}")
    ew = until(lambda: W.status()["code_refused"], 25)
    check(ew is not None and "too many codes" in ew, f"a third request from one address in a day is refused: {ew}")
    ev = until(lambda: V.status()["code_refused"], 25)
    check(ev is not None and "does not issue codes" in ev, f"a switchboard that is not a registry refuses to issue one: {ev}")
    check(not W.status()["code"] and not V.status()["code"], "and neither of them has a code")
    check(not os.path.exists(f"{T}/Z/sb/codes.log"), "a private switchboard (--registry none) asks nobody")

    def node(name, port, sbport, sbaddr):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:{sbport}/{sbaddr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--answer", "auto", "--keepalive", "5"])
    a_ = node("a", 4101, 1901, ax); b_ = node("b", 4102, 1901, ax)
    c_ = node("c", 4103, 1902, ay); d_ = node("d", 4104, 1905, az)
    num = {}
    for k, n in zip("abcd", (a_, b_, c_, d_)):
        num[k] = until(lambda n=n: n.status()["number"], 25)
    check(all(num.values()), "four nodes register on three switchboards")
    full = {k: n.status()["full"] for k, n in zip("abcd", (a_, b_, c_, d_))}
    check(until(lambda: a_.status()["full"].startswith("+1 "), 15) is not None, f"a node on the public switchboard is +1: {a_.status()['full']}")
    check(until(lambda: c_.status()["full"].startswith("+20 "), 15) is not None, f"a node on the next one is +20: {c_.status()['full']}")
    check(d_.status()["full"].startswith("+0"), f"a node on the private switchboard keeps the derived code: {d_.status()['full']}")
    codes_a = a_.status()["switchboard_codes"]
    check(len(codes_a) == 2 and codes_a[0] == "1" and codes_a[1].startswith("0"), f"a node can see both codes its switchboard answers to: {codes_a}")

    mk = b_.w.mark()
    rc, out = a_.cmd("dial", num["b"])
    check(rc == 0 and b_.w.wait("connected", 20, after=mk) is not None, "dialing the local number works as before")
    a_.cmd("hangup"); time.sleep(1)
    mk = b_.w.mark()
    rc, out = a_.cmd("dial", "+1" + num["b"])
    check(rc == 0 and b_.w.wait("connected", 20, after=mk) is not None, "dialing +1 and the number works, with no separator at all (the plan is prefix-free)")
    a_.cmd("hangup"); time.sleep(1)
    mk = b_.w.mark()
    rc, out = a_.cmd("dial", "+" + codes_a[1] + " " + num["b"])
    check(rc == 0 and b_.w.wait("connected", 20, after=mk) is not None, "the derived code still works, now that there is a short one: both are valid for good")
    a_.cmd("hangup")
    rc, out = a_.cmd("dial", "+20 " + num["c"])
    check(rc != 0 and "another switchboard" in out, f"another switchboard's short code is refused until federation: {out!r}")

    # --- the registry keeps its book -------------------------------------------------------
    for sb in (X, Y):
        sb.stop()
    R.stop(); time.sleep(1)
    R = switchboard("R", 1900, "none", "--issue-codes", "--public-switchboard", ax)
    X = Node("X", ns, "longwire-switchboard.hibr", ["--listen", "127.0.0.1:1901", "--registry", reg])
    Y = Node("Y", ns, "longwire-switchboard.hibr", ["--listen", "127.0.0.1:1902", "--registry", reg])
    cx, cy = code_of(X), code_of(Y)
    check(cx == "1" and cy == "20", f"after the registry and both switchboards restart, the codes are the same: +{cx}, +{cy}")
    book = open(f"{T}/R/sb/codes.log").read().split("\n")
    check(len([l for l in book if l]) == 2, f"the registry's book holds the two it issued: {book}")

    for n in (a_, b_, c_, d_):
        n.cmd("down")
    for sb in (X, Y, W, V, Z, R):
        sb.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("registry")
