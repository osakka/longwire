# SPDX-License-Identifier: GPL-3.0-or-later
# M3 end-to-end test, stage 2: relaying. Three members of one line:
#
#      site A --[symmetric NAT .101]--+
#                                     +-- public 192.0.2.0/24 -- switchboard .10, member B .10:4002
#      site C --[symmetric NAT .102]--+
#
# A and C cannot punch through each other, so they go through B, the only member with a public address. Frames flow
# between A and C across B. Then both NATs are loosened to cone and the pair must find its own way and drop the relay.
import json, os, socket, struct, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, nsh, mac_of, mkns, Node
from lwnat import Site, Router, Collector


def until(fn, timeout=30, step=0.3):
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


def main():
    print(f"-- M3 stage 2: relay between two members behind symmetric NATs  (workdir {T})", flush=True)
    sh("ip link set lo up")
    open("/proc/sys/net/ipv4/ip_forward", "w").write("0")
    nsA, nsC, nsS = mkns(), mkns(), mkns()
    sites = []
    for key, ns, rif, vif, rip, vip, pub in [("A", nsA, "rA", "vA", "10.0.1.1", "10.0.1.2", "192.0.2.101"),
                                             ("C", nsC, "rC", "vC", "10.0.2.1", "10.0.2.2", "192.0.2.102")]:
        sh(f"ip link add {rif} type veth peer name {vif}"); sh(f"ip link set {vif} netns {ns.pid}")
        sh(f"ip addr add {rip}/24 dev {rif}"); sh(f"ip link set {rif} up")
        nsh(ns, "ip link set lo up"); nsh(ns, f"ip addr add {vip}/24 dev {vif}"); nsh(ns, f"ip link set {vif} up")
        nsh(ns, f"ip route add default via {rip}")
        nsh(ns, f"ip neigh replace {rip} lladdr {mac_of(rif)} dev {vif} nud permanent")
        sites.append(Site(key, rif, rip, vip, mac_of(vif, ns), mac_of(rif), pub, "sym"))
    sh("ip link add rS type veth peer name vS"); sh(f"ip link set vS netns {nsS.pid}")
    sh("ip addr add 192.0.2.1/24 dev rS"); sh("ip link set rS up")
    nsh(nsS, "ip link set lo up"); nsh(nsS, "ip addr add 192.0.2.10/24 dev vS"); nsh(nsS, "ip link set vS up")
    nsh(nsS, "ip route add default via 192.0.2.1")
    for ip in ("192.0.2.1", "192.0.2.101", "192.0.2.102"):
        nsh(nsS, f"ip neigh replace {ip} lladdr {mac_of('rS')} dev vS nud permanent")
    router = Router(sites, "rS", mac_of("rS"), {"192.0.2.10": mac_of("vS", nsS)})
    router.start()

    sb = Node("S", nsS, "longwire-switchboard.hibr", ["--listen", "192.0.2.10:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, ns, ip, port):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        n = Node(name, ns, "longwire.hibr",
                 ["up", "--switchboard", f"192.0.2.10:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                  "--listen", f"{ip}:{port}", "--answer", "auto", "--keepalive", "3", "--rekey", "60", "--direct-retry", "6"])
        n.frames = Collector(f"{T}/{name}/h")
        return n
    B = node("B", nsS, "192.0.2.10", 4002)
    A = node("A", nsA, "10.0.1.2", 4001)
    C = node("C", nsC, "10.0.2.2", 4003)

    def number(n):
        return until(lambda: n.status()["number"], 20)
    nb, na, nc = number(B), number(A), number(C)
    check(bool(na and nb and nc), "A, B and C register")
    check(A.status()["reflexive"].startswith("192.0.2.101:") and B.status()["reflexive"].startswith("192.0.2.10:"),
          "A is behind a NAT, B has a public address")

    # --- one host cannot drain a node's HELLO budget ------------------------------------
    kid8 = bytes.fromhex(B.status()["key_id"])[:8]
    us = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); us.bind(("192.0.2.1", 0)); us.settimeout(0.3)
    for _ in range(60): us.sendto(b"\x08\x00" + kid8, ("192.0.2.10", 4002))
    got = 0
    try:
        while True: us.recv(2000); got += 1
    except socket.timeout: pass
    check(1 <= got <= 8, f"60 bundle requests from one source get {got} answers (budget 2/s, burst 4)")

    # --- the line: B founds it, A and C join through B ------------------------------
    mkA, mkB, mkC = A.w.mark(), B.w.mark(), C.w.mark()
    A.cmd("dial", nb)
    check(A.w.wait("line-joined", 25, after=mkA) is not None, "A dials B and joins the line B founded")
    C.cmd("dial", nb)
    check(C.w.wait("line-joined", 25, after=mkC) is not None, "C dials B and joins the line")
    check(until(lambda: len(B.status()["line"]["members"]) == 3, 10) is not None, "B lists three members")

    # --- A and C cannot see each other: B carries their packets -------------------------
    ea = A.w.wait("connected", 90, pred=lambda e: e["number"] == nc, after=mkA)
    ec = C.w.wait("connected", 30, pred=lambda e: e["number"] == na, after=mkC)
    check(ea is not None and ea["path"] == "relay" and ea["via"] == nb, f"A reaches C through B: {ea and (ea['path'], ea.get('via'))}")
    check(ec is not None and ec["path"] == "relay" and ec["via"] == nb, f"C reaches A through B: {ec and (ec['path'], ec.get('via'))}")
    if ea and ec:
        check(ea["phrase"] == ec["phrase"], "the two ends agree on the safety phrase: the handshake is theirs, not the relay's")
    pa = {p["number"]: p for p in A.status()["peers"]}
    check(nc in pa and pa[nc]["path"] == "relay" and pa[nc]["state"] == "up", "A's status shows C as a relayed peer that is up")
    check(until(lambda: A.status()["line"]["up"] == 3 and not A.status()["line"]["partial"], 15) is not None,
          "the line is a full mesh from A's side, though one edge is relayed")
    st = B.status()
    check(st["relay"]["routes"] >= 2, f"B holds a route for each direction: {st['relay']}")

    MA, MC = bytes.fromhex("020000000a01"), bytes.fromhex("020000000c01")
    A.frames.drain(); C.frames.drain(); B.frames.drain()
    ha = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); hc = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

    def exchange(tag, n=120):
        A.frames.drain(); C.frames.drain()
        for i in range(n):
            ha.sendto(frame(b"\xff" * 6 if i == 0 else MC, MA, struct.pack(">I", i) + os.urandom(800)), f"{T}/A/p")
            hc.sendto(frame(MA, MC, struct.pack(">I", i) + os.urandom(800)), f"{T}/C/p")
            if i % 10 == 9: time.sleep(0.03)
        ga, gc = A.frames.drain(), C.frames.drain()
        return len(ga), len(gc)
    ga, gc = exchange("relay")
    check(ga >= 115 and gc >= 115, f"frames cross the relay both ways (A got {ga}, C got {gc} of 120)")
    fwd = B.status()["relay"]["forwarded"]
    check(fwd > 200, f"and they really went through B ({fwd} datagrams forwarded)")
    big = os.urandom(1500 - 14)
    A.frames.drain(); C.frames.drain()
    for _ in range(20):
        ha.sendto(frame(MC, MA, big), f"{T}/A/p")
        time.sleep(0.01)
    check(len(C.frames.drain()) >= 18, "full-size frames (two fragments each) cross the relay as well")

    # --- the NATs relax: the pair finds a direct path and lets the relay go ---------------
    mkA, mkC = A.w.mark(), C.w.mark()
    for s in sites: s.mode = "cone"
    pa_ = A.w.wait("path", 150, pred=lambda e: e["number"] == nc, after=mkA)
    pc_ = C.w.wait("path", 30, pred=lambda e: e["number"] == na, after=mkC)
    check(pa_ is not None and pa_["path"] == "direct", f"A sees the path to C turn direct: {pa_ and pa_['ep']}")
    check(pc_ is not None and pc_["path"] == "direct", f"C sees the path to A turn direct: {pc_ and pc_['ep']}")
    pa = {p["number"]: p for p in A.status()["peers"]}
    check(nc in pa and pa[nc]["path"] == "direct" and pa[nc]["state"] == "up" and pa[nc]["ep"].startswith("192.0.2.102:"),
          f"A's status: C is direct at {pa.get(nc, {}).get('ep')}")
    f0 = B.status()["relay"]["forwarded"]
    ga, gc = exchange("direct")
    check(ga >= 115 and gc >= 115, f"frames flow on the direct path (A got {ga}, C got {gc} of 120)")
    time.sleep(0.5)
    check(B.status()["relay"]["forwarded"] - f0 < 20, "and B no longer carries them")

    # --- the relay goes away: the line heals around it -----------------------------------
    mkA = A.w.mark()
    B.cmd("down")
    check(A.w.wait("call-lost", 40, pred=lambda e: e["number"] == nb, after=mkA) is not None, "A notices B is gone")
    ga, gc = exchange("after B")
    check(ga >= 115 and gc >= 115, f"A and C, now direct, do not depend on B any more (A got {ga}, C got {gc})")

    for n in (A, C):
        n.cmd("down")
    subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {sb.sock}"], capture_output=True)


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("M3 stage 2")
