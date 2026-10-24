# SPDX-License-Identifier: GPL-3.0-or-later
# IPv6 (docs/PROTOCOL.md section 4): a switchboard and nodes that speak only IPv6, then nodes that reach the
# switchboard over IPv4 and each other over IPv6. Throwaway network namespaces; no router, so no NAT in either.
import json, os, socket, struct, subprocess, sys, time

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
    print(f"-- IPv6  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    sb = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "[::1]:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, *extra):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"[::1]:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"[::1]:{port}", "--cand-loopback", "--keepalive", "5", "--answer", "auto", *extra])
    A, B = node("A", 4001), node("B", 4002)
    na = until(lambda: A.status()["number"], 20)
    nb = until(lambda: B.status()["number"], 20)
    check(bool(na and nb), "both register with a switchboard that listens on IPv6 only")
    mk = A.w.mark(); mkb = B.w.mark()
    A.cmd("dial", nb)
    ca = A.w.wait("connected", 25, after=mk); cb = B.w.wait("connected", 25, after=mkb)
    check(ca is not None and cb is not None, "A calls B and they connect")
    if ca:
        check(ca["path"] == "direct" and ca["ep"].startswith("[::1]:"), f"...directly, over IPv6: {ca['ep']}")
    for n in (A, B):
        n.cmd("down")
    subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {sb.sock}"], capture_output=True)

    # --- IPv4 to the switchboard, IPv6 between the nodes --------------------------------------
    # A and B each have an IPv4 link to S, and one IPv6-only link to each other. Without forwarding, an IPv4 address of
    # A's is no use to B, so the only way across is IPv6.
    nsA, nsB, nsS = L.mkns(), L.mkns(), L.mkns()
    L.veth(nsA, "toS", nsS, "toA")
    L.veth(nsB, "toS", nsS, "toB")
    L.veth(nsA, "toB", nsB, "toA")
    for x, a4, s4 in ((nsA, "10.0.1.2/24", "10.0.1.1/24"), (nsB, "10.0.2.2/24", "10.0.2.1/24")):
        L.nsh(x, "ip link set lo up"); L.nsh(x, f"ip addr add {a4} dev toS")
    L.nsh(nsS, "ip link set lo up"); L.nsh(nsS, "ip addr add 10.0.1.1/24 dev toA"); L.nsh(nsS, "ip addr add 10.0.2.1/24 dev toB")
    L.nsh(nsA, "ip addr add fd00::a/64 dev toB nodad"); L.nsh(nsB, "ip addr add fd00::b/64 dev toA nodad")
    sb2 = Node("S2", nsS, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "1877"])
    until(lambda: os.path.exists(sb2.home + "/sb/seed") and os.path.exists(sb2.sock), 15)
    addr2 = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb2.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node2(name, ns_, sbip, port):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns_, "longwire.hibr",
                    ["up", "--switchboard", f"{sbip}:1877/{addr2}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", str(port), "--keepalive", "5", "--answer", "auto"])
    C, D = node2("C", nsA, "10.0.1.1", 4003), node2("D", nsB, "10.0.2.1", 4004)
    nc = until(lambda: C.status()["number"], 20)
    nd = until(lambda: D.status()["number"], 20)
    check(bool(nc and nd), "both register over IPv4 with a switchboard that listens on both")
    mk = C.w.mark(); mkd = D.w.mark()
    C.cmd("dial", nd)
    cc = C.w.wait("connected", 40, after=mk); cd = D.w.wait("connected", 40, after=mkd)
    check(cc is not None and cd is not None, "C calls D and they connect, though no IPv4 address of either reaches the other")
    if cc:
        check(cc["path"] == "direct" and cc["ep"].startswith("[fd00::b]:"), f"...directly, over IPv6: {cc['ep']}")
        check(cc["phrase"] == cd["phrase"], "...with the same safety phrase on both screens")
    MC, MD = bytes.fromhex("020000000c01"), bytes.fromhex("020000000d01")
    from lwnat import Collector
    fc, fd_ = Collector(f"{T}/C/h"), Collector(f"{T}/D/h")
    hc = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); hd = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    for i in range(100):
        hc.sendto(MD + MC + b"\x08\x00" + struct.pack(">I", i) + os.urandom(300), f"{T}/C/p")
        hd.sendto(MC + MD + b"\x08\x00" + struct.pack(">I", i) + os.urandom(300), f"{T}/D/p")
        if i % 10 == 9: time.sleep(0.03)
    gc, gd = fc.drain(), fd_.drain()
    check(len(gc) >= 98 and len(gd) >= 98, f"100 frames each way over IPv6 (C got {len(gc)}, D got {len(gd)})")
    L.finish("ipv6")


main()
