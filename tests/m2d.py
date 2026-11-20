# SPDX-License-Identifier: GPL-3.0-or-later
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

WANT = sys.argv[1]            # "lower": vpn-2's key id is lower than vpn-1's; "higher": the other way


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
        if (kid(name) < k1) == (WANT == "lower"):
            break
    else:
        print("no identity of the wanted order in 40 tries"); sys.exit(2)

    S = L.Node("S", sw, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "10.9.0.9:1877"])
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(S.home + "/sb/seed"):
        time.sleep(0.2)
    addr = subprocess.run([L.HIBR, "-c", f"mod load {L.MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(nm, ns, ip):
        return L.Node(nm, ns, "longwire.hibr", ["up", "--switchboard", f"10.9.0.9:1877/{addr}", "--splice", "lan", "--listen", f"{ip}:4000", "--keepalive", "3", "--rekey", "40", "--answer", "auto", "--name", nm])

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
    check(bool(n1 and n2), f"vpn-1 and vpn-2 register: {n1} {n2}")
    V2.cmd("dial", n1)
    roles = until(lambda: sorted([V1.status()["site"], V2.status()["site"]]) == ["active", "standby"] and sorted([V1.status()["site"], V2.status()["site"]]), 40)
    check(roles == ["active", "standby"], f"vpn-2 joins vpn-1's line: one active, one standby ({roles})")
    low = "v2" if kid(name) < k1 else "v1"
    act = "v1" if V1.status()["site"] == "active" else "v2"
    check(act == low, f"the lower key id is active ({act})")

    total = 190
    dh = subprocess.Popen(["nsenter", "-t", str(a1.pid), "-n", "python3", "-I", "-c", DHCPD, str(total)], stdout=subprocess.PIPE, text=True)
    pdir = f"{L.T}/phone"
    os.makedirs(pdir, exist_ok=True)
    os.makedirs(f"{pdir}/home", exist_ok=True)
    os_end, eng_end = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    penv = dict(os.environ, LEAFNET_LISTEN="0")
    phone = subprocess.Popen(["nsenter", "-t", str(c.pid), "-n", os.path.abspath("build/leafnet"), pdir, f"sb:10.9.0.9:1877/{addr}", str(eng_end.fileno()), "1"], pass_fds=(eng_end.fileno(),), stdout=subprocess.PIPE, text=True, env=penv)
    check(phone.stdout.readline().strip() == "attached", "the phone's engine starts on its own network")
    psock = f"{pdir}/ctl"
    until(lambda: ctl(psock, "status").get("switchboard") == "dial tone", 30)
    t0 = time.time()
    ctl(psock, "dial " + n1)                          # it dials vpn-1 only
    got = until(lambda: member(psock).get("member") == "bound" and member(psock), 60)
    check(bool(got) and time.time() - t0 < 15, f"the phone dialled vpn-1 only, and gets its lease from the LAN's DHCP server through the active box ({act}) in {time.time() - t0:.0f} s, under 15 (without ADR 0092 it takes 25 s: the other box has to ring a phone that refuses it)")
    pe = until(lambda: [q for q in ctl(psock, "status")["peers"] if q["state"] == "up"], 20)
    check(len([q for q in ctl(psock, "status")["peers"] if q["state"] == "up"]) == 2,
          "the phone, which dialled one box, keeps a session with both boxes of the pair (ADR 0092): a standby box forwards nothing")
    if got:
        check(got["ip"] == "192.168.1.50" and got["gateway"] == "192.168.1.1", f"the lease is the LAN's: {got['ip']} via {got['gateway']}")
        sent0 = got["dhcp_sent"]; gen = got["gen"]
        again = until(lambda: member(psock)["dhcp_sent"] > sent0 and member(psock), 50, 1.0)
        check(bool(again) and again["member"] == "bound" and again["gen"] == gen, f"half a lease later the phone renews it and stays bound, same address (sent {sent0} -> {again and again['dhcp_sent']})")
        V1.stop()
        time.sleep(0.5)
        sent1 = member(psock)["dhcp_sent"]
        survivor = V2
        up2 = until(lambda: survivor.status()["site"] == "active", 15)
        check(bool(up2), "vpn-1 is stopped: the other box takes over (active)")
        again2 = until(lambda: member(psock)["dhcp_sent"] > sent1 + 0 and member(psock)["member"] == "bound" and member(psock), 80, 1.0)
        check(bool(again2) and again2["gen"] == gen, f"and the phone's lease is renewed again, through the survivor, with vpn-1 gone, still the same address ({again2 and again2['dhcp_sent']})")
    phone.terminate()
    dh.terminate()
    L.finish("m2d")


name = "v2_0"
main()
