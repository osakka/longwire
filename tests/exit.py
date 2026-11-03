# SPDX-License-Identifier: GPL-3.0-or-later
# The exit (ADR 0056), in the engine: members that share their internet say so, and a node that sends everything through the line hands
# every frame for the exit gateway (02:6c:77:65:78:74) to one of them, with that member's own MAC put in, and to no one else. Real
# switchboard and node scripts on loopback in one namespace, frames through their sock ports. The kernel's side (the route to the exit
# gateway, the forwarding and the NAT on the exit) is tests/exitnet.sh.
import json, os, socket, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node
from lwnat import Collector

EXIT_MAC = bytes.fromhex("026c77657874")


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


def frame(dst, src, payload): return dst + src + b"\x08\x00" + payload


def peer(st, num):
    return next((p for p in st["peers"] if p["number"] == num), None)


def main():
    print(f"-- the exit: a member that shares its internet  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])

    sb = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    sba = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, *extra):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        n = Node(name, ns, "longwire.hibr",
                 ["up", "--switchboard", f"127.0.0.1:1877/{sba}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                  "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", "--answer", "auto", *extra])
        n.frames = Collector(f"{T}/{name}/h")
        return n

    MX, MY = bytes.fromhex("525400000058"), bytes.fromhex("525400000059")
    X = node("X", 4001, "--exit-mac", "52:54:00:00:00:58")
    C = node("C", 4002, "--use-exit")
    D = node("D", 4003)
    num = {k: until(lambda n=n: n.status()["number"], 20) for k, n in (("X", X), ("C", C), ("D", D))}
    check(all(num.values()), f"three nodes have numbers: {num}")

    mk = C.w.mark()
    C.cmd("dial", num["X"])
    check(C.w.wait("connected", 20, after=mk) is not None, "C calls X, which shares its internet")
    D.cmd("dial", num["X"])
    check(until(lambda: peer(C.status(), num["D"]) and peer(C.status(), num["D"])["state"] == "up", 30) is not None,
          "D joins the line through X and the mesh connects it to C")

    ev = C.w.wait("exit", 20, pred=lambda e: e["number"] == num["X"])
    check(ev is not None and ev["offers"] == 1, f"C hears that X shares its internet, and uses it: {ev}")
    st = C.status()
    check(st["exit"]["use"] is True and st["exit"]["using"] == num["X"], f"C's status names X as the way out: {st['exit']}")
    check(peer(st, num["X"])["exit"] is True and peer(st, num["D"])["exit"] is False, "the peers say which of them is an exit: X is, D is not")
    sx = X.status()
    check(sx["exit"]["share"] is True and sx["exit"]["use"] is False, f"X's status says it shares: {sx['exit']}")

    # --- frames for the exit gateway go to X, with X's MAC, and to nobody else ----------------------------------------------
    MC, MD = bytes.fromhex("020000000c01"), bytes.fromhex("020000000d01")
    hc = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); hd = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    X.frames.drain(); D.frames.drain(); C.frames.drain()
    for i in range(20):					# paced, as the other tests send: a busy machine drops a burst on loopback
        hc.sendto(frame(EXIT_MAC, MC, b"out%02d" % i + os.urandom(300)), f"{T}/C/p")
        time.sleep(0.02)
    gx, gd = X.frames.drain(), D.frames.drain()
    check(len(gx) == 20 and all(f[:6] == MX and f[6:12] == MC for f in gx),
          f"20 frames for the exit gateway reach X, addressed to X's own MAC (X got {len(gx)})")
    check(not any(f[14:17] == b"out" for f in gd), f"none of them reach D: the gateway is never flooded (D got {len(gd)})")
    check(C.status()["exit"]["sent"] >= 20, "C counts what it handed to the exit")
    hx = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    hx.sendto(frame(MC, MX, b"back" + os.urandom(100)), f"{T}/X/p")
    back = C.frames.drain()
    check(any(f[:6] == MC and f[14:18] == b"back" for f in back), "the answer from X comes back to C as any frame does")

    # --- a node that does not use an exit drops frames for the gateway: never floods them -----------------------------------
    hd.sendto(frame(EXIT_MAC, MD, b"leak" + os.urandom(100)), f"{T}/D/p")
    gx, gc = X.frames.drain(), C.frames.drain()
    check(not any(f[14:18] == b"leak" for f in gx + gc) and D.status()["exit"]["dropped"] >= 1,
          "D, which uses no exit, drops a frame for the gateway instead of flooding it")

    # --- a second exit: the lowest number is used unless one is asked for ---------------------------------------------------
    Y = node("Y", 4004, "--exit-mac", "52:54:00:00:00:59")
    num["Y"] = until(lambda: Y.status()["number"], 20)
    Y.cmd("dial", num["X"])
    low = min(num["X"], num["Y"])
    check(until(lambda: C.status()["exit"]["offers"] == 2, 30) is not None, "C hears that Y shares its internet too")
    check(C.status()["exit"]["using"] == low, f"with two exits and none asked for, C uses the lower number ({low})")
    E = node("E", 4005, "--exit-through", num["Y"])
    num["E"] = until(lambda: E.status()["number"], 20)
    E.cmd("dial", num["X"])
    check(until(lambda: E.status()["exit"]["using"] == num["Y"], 30) is not None and E.status()["exit"]["wanted"] == num["Y"],
          f"E, told to go out through {num['Y']}, uses it although {num['X']} shares too")
    he = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    Y.frames.drain(); X.frames.drain()
    he.sendto(frame(EXIT_MAC, bytes.fromhex("020000000e01"), b"viaY" + os.urandom(80)), f"{T}/E/p")
    gy, gx = Y.frames.drain(), X.frames.drain()
    check(any(f[:6] == MY and f[14:18] == b"viaY" for f in gy) and not any(f[14:18] == b"viaY" for f in gx),
          "E's frame for the gateway reaches Y, with Y's MAC, and not X")

    # --- the exit goes: the next one is used; with none, nothing goes out --------------------------------------------------
    hi = "X" if low == num["Y"] else "Y"
    lo_ = "Y" if hi == "X" else "X"
    mk = C.w.mark()
    {"X": X, "Y": Y}[lo_].stop()
    ev = C.w.wait("exit", 30, after=mk, pred=lambda e: e["number"] == num[hi])
    check(ev is not None, f"the exit C used stops; C goes out through the other ({num[hi]}) at once")
    mk = C.w.mark()
    {"X": X, "Y": Y}[hi].stop()
    ev = C.w.wait("exit", 30, after=mk, pred=lambda e: e["number"] == "")
    check(ev is not None and ev["offers"] == 0, f"the last exit stops; C says no member shares its internet: {ev}")
    before = C.status()["exit"]["dropped"]
    D.frames.drain(); E.frames.drain()
    hc.sendto(frame(EXIT_MAC, MC, b"none" + os.urandom(100)), f"{T}/C/p")
    gd, ge = D.frames.drain(), E.frames.drain()
    check(not any(f[14:18] == b"none" for f in gd + ge) and C.status()["exit"]["dropped"] > before,
          "with no exit, a frame for the gateway is dropped and counted, not sent to another member")

    # --- what lw start refuses ---------------------------------------------------------------------------------------------
    def start(*a):
        r = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw start --seed {T}/z/seed --sock {T}/z/ctl --state {T}/z " + " ".join(a) + " || echo \"ERR $ERRMSG\""],
                           capture_output=True, text=True)
        return r.stdout + r.stderr
    os.makedirs(f"{T}/z", exist_ok=True)
    out = start("--sock-port", f"{T}/z/p:{T}/z/h", "--exit", "--use-exit")
    check("does not send its own" in out, f"a node is not an exit and a user of one at once: {out.strip()[:120]}")
    out = start("--sock-port", f"{T}/z/p:{T}/z/h", "--exit")
    check("--exit-mac" in out, f"--exit without a jack needs --exit-mac: {out.strip()[:120]}")
    out = start("--sock-port", f"{T}/z/p:{T}/z/h", "--exit-through", "12-34")
    check("number like" in out, f"--exit-through wants a number: {out.strip()[:120]}")
    out = start("--sock-port", f"{T}/z/p:{T}/z/h", "--exit-mac", "01:00:5e:00:00:01")
    check("MAC address like" in out, f"--exit-mac refuses a group address: {out.strip()[:120]}")

    # --- the switchboard's name is looked up before the jack is attached: once a jack takes everything, there is no way out to look it up -----
    os.makedirs(f"{T}/z2", exist_ok=True)
    r = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw start --seed {T}/z2/seed --sock {T}/z2/ctl --state {T}/z2 --switchboard nosuchhost.invalid:6741/{sba} "
                        f"--sock-port {T}/z2/p:{T}/z2/h || echo \"ERR $ERRMSG\""], capture_output=True, text=True)
    out = r.stdout + r.stderr
    check("cannot resolve nosuchhost.invalid" in out and not os.path.exists(f"{T}/z2/p"),
          f"a switchboard name is looked up before the port is attached: it fails with the port never opened ({out.strip()[:90]})")

    L.finish("exit")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:
        import traceback; traceback.print_exc()
        check(False, f"crashed: {e!r}")
        L.finish("exit")
