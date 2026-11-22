# SPDX-License-Identifier: GPL-3.0-or-later
# --pair (ADR 0109): two headless splice boxes started with each other's number pair by themselves. Built on the pairrestart setup:
# vpn-1 and vpn-2 are two splice boxes on one LAN segment (the second joins the first's line through a real switchboard); a DHCP server answers on that LAN (a1). A phone-style client (the engine of the Android app,
# embedded, tests/leafnet.c) is on the underlay and dials vpn-1 only. It must get its lease whichever box is active, renew it, and renew it again after vpn-1 is stopped: the guard lets the box vpn-1 says is on its
# own segment answer for it.
#
#   a1 (DHCP server) --[brA]--+-- v1 --+
#                             +-- v2 --+-- underlay brU ---+-- sw (switchboard)
#                                                          +-- c (the phone)
import json, os, socket, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import check, sh, nsh, nsrun

WANT = "lower"
SBROOT = os.environ.get("SBROOT") or None      # the tree the switchboard runs from (SBROOT=../my-vpn-0569: the release srv-uk-01 runs)
DOWN = float(os.environ.get("DOWN", "5"))


DHCPD = r'''
import socket, struct, sys, time
GWMAC = bytes.fromhex("525400123456"); GW = bytes([192, 168, 1, 1]); ME = bytes([192, 168, 1, 50])
def csum(b):
    if len(b) % 2: b += b"\0"
    s = sum(struct.unpack(">%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF
def reply(frame, mtype):
    ihl = (frame[14] & 15) * 4; b = frame[14 + ihl + 8:]
    xid, chaddr = b[4:8], b[28:34]
    opts = b"\x35\x01" + bytes([mtype]) + b"\x36\x04" + GW + b"\x01\x04\xff\xff\xff\x00" + b"\x03\x04" + GW + b"\x06\x04" + GW + b"\x33\x04" + struct.pack(">I", 60) + b"\xff"
    bootp = struct.pack(">BBBB4sHH4s4s4s4s", 2, 1, 6, 0, xid, 0, 0x8000, b"\0" * 4, ME, b"\0" * 4, b"\0" * 4) + chaddr + b"\0" * 10 + b"\0" * 192 + b"\x63\x82\x53\x63" + opts
    udp = struct.pack(">HHHH", 67, 68, 8 + len(bootp), 0) + bootp
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0, GW, b"\xff" * 4)
    ip = ip[:10] + struct.pack(">H", csum(ip)) + ip[12:] + b""
    return b"\xff" * 6 + GWMAC + b"\x08\x00" + ip + udp
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3)); s.bind(("eth0", 0)); s.settimeout(0.3)
end = time.time() + float(sys.argv[1]); n = 0
while time.time() < end:
    try: d, a = s.recvfrom(2048)
    except socket.timeout: continue
    if a[2] == socket.PACKET_OUTGOING or len(d) < 300 or d[12:14] != b"\x08\x00" or d[23] != 17: continue
    ihl = (d[14] & 15) * 4
    if d[14 + ihl:14 + ihl + 4] != b"\x00\x44\x00\x43": continue
    o = d[14 + ihl + 8 + 240:]
    t = o[2] if o[:2] == b"\x35\x01" else 0
    if t in (1, 3):
        s.send(reply(d, 2 if t == 1 else 5)); n += 1
print(n)
'''


def main():
    global name
    print(f"-- M2d: a client's lease through a pair of boxes, newcomer's key id {WANT}  (workdir {L.T})", flush=True)
    sh("ip link set lo up")
    sh("ip link add brA type bridge"); sh("ip link set brA up")
    sh("ip link add brU type bridge"); sh("ip link set brU up")
    a1, v1, v2, c, sw = (L.mkns() for _ in range(5))

    def veth(ns_x, name_x, ns_y, name_y, bridge_y=None):
        sh("ip link add vx type veth peer name vy")
        for end, ns, name_, br in (("vx", ns_x, name_x, None), ("vy", ns_y, name_y, bridge_y)):
            if ns is None:
                sh(f"ip link set {end} name {name_}"); sh(f"ip link set {name_} master {br}"); sh(f"ip link set {name_} up")
            else:
                sh(f"ip link set {end} netns {ns.pid}")
                nsh(ns, f"ip link set {end} name {name_}"); nsh(ns, f"ip link set {name_} up")

    veth(a1, "eth0", None, "r_a1", bridge_y="brA")
    veth(v1, "lan", None, "r_v1", bridge_y="brA")
    veth(v2, "lan", None, "r_v2", bridge_y="brA")
    veth(v1, "ul", None, "r_v1u", bridge_y="brU")
    veth(v2, "ul", None, "r_v2u", bridge_y="brU")
    veth(c, "ul", None, "r_cu", bridge_y="brU")
    veth(sw, "ul", None, "r_swu", bridge_y="brU")
    for ns in (a1, v1, v2, c, sw):
        nsh(ns, "ip link set lo up")
        nsrun(ns, ["sh", "-c", "echo 1 > /proc/sys/net/ipv6/conf/all/disable_ipv6"])
    for ns, ip in ((v1, "10.9.0.1"), (v2, "10.9.0.3"), (c, "10.9.0.5"), (sw, "10.9.0.9")):
        nsh(ns, f"ip addr add {ip}/24 dev ul")

    def kid(nm):
        env = dict(os.environ, HOME=f"{L.T}/{nm}", LONGWIRE_MOD=L.MOD, LW_DIR=f"{L.T}/{nm}/lw")
        return subprocess.run([L.HIBR, L.ROOT + "/scripts/longwire.hibr", "id"], env=env, capture_output=True, text=True).stdout.strip()

    L.identity("v1")
    k1 = kid("v1")
    for i in range(40):
        name = f"v2_{i}"
        L.identity(name)
        if kid(name):                  # any order: the pair does not depend on which key is lower
            break
    else:
        print("no identity of the wanted order in 40 tries"); sys.exit(2)

    SBARGS = ["--registry", "none", "--listen", "10.9.0.9:1877"]
    S = L.Node("S", sw, "longwire-switchboard.hibr", SBARGS, root=SBROOT)
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(S.home + "/sb/seed"):
        time.sleep(0.2)
    addr = subprocess.run([L.HIBR, "-c", f"mod load {L.MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(nm, ns, ip, extra=()):
        return L.Node(nm, ns, "longwire.hibr", ["up", "--switchboard", f"10.9.0.9:1877/{addr}", "--splice", "lan", "--listen", f"{ip}:4000", "--keepalive", "3", "--answer", "known", "--name", nm, *extra])

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

    def member(sockpath):
        s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(sockpath); s.sendall(b"member\n")
        d = b""
        while True:
            x = s.recv(65536)
            if not x: break
            d += x
        return json.loads(d)

    def ctl(sockpath, line):
        s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(sockpath); s.sendall((line + "\n").encode())
        d = b""
        while True:
            x = s.recv(65536)
            if not x: break
            d += x
        return json.loads(d)

    V1 = node("v1", v1, "10.9.0.1")
    n1 = until(lambda: V1.status()["number"])
    V2 = node(name, v2, "10.9.0.3")
    n2 = until(lambda: V2.status()["number"])
    check(bool(n1 and n2), f"two fresh boxes register: {n1} {n2}")
    time.sleep(5)
    check(V1.status()["site"] in ("", None, "off", "alone") or V1.status().get("site_members", 0) == 0, f"without a pair named they stay apart: {V1.status()['site']} {V1.status().get('site_members')}")
    for X in (V1, V2):
        X.stop()
    until(lambda: V1.proc.poll() is not None and V2.proc.poll() is not None, 20)
    t0 = time.time()
    V1 = node("v1", v1, "10.9.0.1", ("--pair", n2))
    V2 = node(name, v2, "10.9.0.3", ("--pair", n1))
    roles = until(lambda: sorted([V1.status()["site"], V2.status()["site"]]) == ["active", "standby"] and sorted([V1.status()["site"], V2.status()["site"]]), 120, 1)
    check(roles == ["active", "standby"], f"started with each other's number, they pair by themselves, nobody picking up: one active, one standby ({roles}, {time.time() - t0:.0f} s)")
    check(V1.status().get("site_members") == 1 and V2.status().get("site_members") == 1, "each counts the other on its segment")
    V1.stop()
    until(lambda: V1.proc.poll() is not None, 20)
    V1 = node("v1", v1, "10.9.0.1", ("--pair", n2))
    roles = until(lambda: sorted([V1.status()["site"], V2.status()["site"]]) == ["active", "standby"] and sorted([V1.status()["site"], V2.status()["site"]]), 120, 1)
    check(roles == ["active", "standby"], f"and pair again after a restart ({roles})")

    # a desktop on the line sees the pair as one, with each box's role (ADR 0111)
    D = L.Node("d", c, "longwire.hibr", ["up", "--switchboard", f"10.9.0.9:1877/{addr}", "--sock-port", f"{L.T}/d/p:{L.T}/d/h", "--listen", "10.9.0.5:4000", "--keepalive", "3", "--answer", "ask"])
    until(lambda: D.status()["number"])
    mk = V1.w.mark()
    D.cmd("dial", n1)
    V1.w.wait("ring", 30, after=mk)
    V1.cmd("pickup")
    def sites():
        return sorted((p["number"], p.get("site", "")) for p in D.status()["peers"] if p["state"] == "up")
    want = sorted([(n1, V1.status()["site"]), (n2, V2.status()["site"])])
    got = until(lambda: sites() == want and sites(), 90, 1)
    check(got == want, f"the desktop has both boxes, each with its role: {sites()} (want {want})")
    rc, out = D.cmd("show")
    pl = [l for l in out.splitlines() if l.strip().startswith("pair")]
    check(len(pl) == 1 and "active" in pl[0] and "standby" in pl[0] and n1 in pl[0] and n2 in pl[0], f"and longwire show says it in one line: {pl}")
    L.finish("pairauto")


main()
