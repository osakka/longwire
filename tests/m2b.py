# SPDX-License-Identifier: GPL-3.0-or-later
# Two splice boxes on one LAN segment with NO site secret: both always forward, which is what a failed election (probes blocked, clocks apart) or a box that has just joined and not yet got its secret looks like.
# The frames of a host must not come back to it, however many boxes carry them (the echo rules), and spanning tree, LLDP and CDP must cross in neither direction (ADR 0083). Real kernel, a bridge,
# real frames:
#
#   a1 10.0.0.1 --[brA]--+-- sa  --+                   +-- sb --- b1 10.0.0.2
#                        +-- sa2 -+-- underlay brU ---+
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import check, sh, nsh, nsrun

SEND = r'''
import socket, sys
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
s.bind(("eth0", 0))
src = bytes.fromhex(sys.argv[1]); dst = bytes.fromhex(sys.argv[2]); tok = sys.argv[3].encode()
et = int(sys.argv[4], 16)
s.send(dst + src + et.to_bytes(2, "big") + tok.ljust(60, b"\0"))
'''
COUNT = r'''
import socket, sys, time
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
s.bind(("eth0", 0)); s.settimeout(0.2)
tok = sys.argv[1].encode(); n = 0; end = time.time() + float(sys.argv[2])
while time.time() < end:
    try:
        d, a = s.recvfrom(2048)
    except socket.timeout:
        continue
    if a[2] != socket.PACKET_OUTGOING and tok in d:
        n += 1
print(n)
'''


def main():
    print(f"-- M2b: two boxes that both forward  (workdir {L.T})", flush=True)
    sh("ip link set lo up")
    sh("ip link add brA type bridge"); sh("ip link set brA up")
    sh("ip link add brU type bridge"); sh("ip link set brU up")
    a1, b1, sa, sa2, sb = (L.mkns() for _ in range(5))

    def veth(ns_x, name_x, ns_y, name_y, bridge_y=None):
        sh("ip link add vx type veth peer name vy")
        for end, ns, name, br in (("vx", ns_x, name_x, None), ("vy", ns_y, name_y, bridge_y)):
            if ns is None:
                sh(f"ip link set {end} name {name}"); sh(f"ip link set {name} master {br}"); sh(f"ip link set {name} up")
            else:
                sh(f"ip link set {end} netns {ns.pid}")
                nsh(ns, f"ip link set {end} name {name}"); nsh(ns, f"ip link set {name} up")

    veth(a1, "eth0", None, "r_a1", bridge_y="brA")
    veth(sa, "lan", None, "r_sa", bridge_y="brA")
    veth(sa2, "lan", None, "r_sa2", bridge_y="brA")
    veth(sa, "ul", None, "r_sau", bridge_y="brU")
    veth(sa2, "ul", None, "r_sa2u", bridge_y="brU")
    veth(sb, "ul", None, "r_sbu", bridge_y="brU")
    veth(sb, "lan", b1, "eth0")
    for ns in (a1, b1, sa, sa2, sb):
        nsh(ns, "ip link set lo up")
        nsrun(ns, ["sh", "-c", "echo 1 > /proc/sys/net/ipv6/conf/all/disable_ipv6"])
    nsh(a1, "ip addr add 10.0.0.1/24 dev eth0"); nsh(b1, "ip addr add 10.0.0.2/24 dev eth0")
    for ns, ip in ((sa, "10.9.0.1"), (sa2, "10.9.0.3"), (sb, "10.9.0.2")):
        nsh(ns, f"ip addr add {ip}/24 dev ul")
    mac_a1, mac_b1 = L.mac_of("eth0", a1), L.mac_of("eth0", b1)

    b_sa, b_sa2, b_sb = L.identity("sa"), L.identity("sa2"), L.identity("sb")
    common = ["up", "--keepalive", "3", "--rekey", "40"]
    SA = L.Node("sa", sa, "longwire.hibr", common + ["--splice", "lan", "--listen", "10.9.0.1:4000", "--peer", f"{b_sb}@10.9.0.2:4000", "--peer", f"{b_sa2}@10.9.0.3:4000"])
    SA2 = L.Node("sa2", sa2, "longwire.hibr", common + ["--splice", "lan", "--listen", "10.9.0.3:4000", "--peer", f"{b_sb}@10.9.0.2:4000", "--peer", b_sa])
    SB = L.Node("sb", sb, "longwire.hibr", common + ["--splice", "lan", "--listen", "10.9.0.2:4000", "--peer", b_sa, "--peer", b_sa2])

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

    check(bool(until(lambda: sum(1 for p in SA.status()["peers"] if p["state"] == "up") == 2 and sum(1 for p in SA2.status()["peers"] if p["state"] == "up") == 2 and
                     sum(1 for p in SB.status()["peers"] if p["state"] == "up") == 2)),
          "sa and sa2, two boxes on one segment with no site secret and linked to each other, and sb: all the sessions come up")
    check(SA.status()["site"] == "alone" and SA2.status()["site"] == "alone", "neither box is in a site group: both forward")

    def count(ns, tok, secs):
        return subprocess.Popen(["nsenter", "-t", str(ns.pid), "-n", "python3", "-I", "-c", COUNT, tok, str(secs)], stdout=subprocess.PIPE, text=True)

    def send(ns, src, dst, tok, et="9000"):
        nsrun(ns, ["python3", "-I", "-c", SEND, src, dst, tok, et])

    # a broadcast from a host on the LAN: it must not come back to the LAN (the loop sa -> sb -> sa2 -> LAN is the one that would run)
    mka, mka2 = SA.w.mark(), SA2.w.mark()
    ca, cb = count(a1, "ECHOTEST1", 5), count(b1, "ECHOTEST1", 5)
    time.sleep(1.0)
    send(a1, mac_a1.replace(":", ""), "ffffffffffff", "ECHOTEST1")
    na, nb = int(ca.communicate()[0] or -1), int(cb.communicate()[0] or -1)
    check(na == 0, f"a broadcast from a1 comes back to its LAN {na} times: never, though two boxes that are linked to each other both forward it")
    check(nb >= 1, f"...and it reaches the other site ({nb} copy: two, one from each box, is what two forwarding boxes give; no more)")
    check(nb <= 2, f"...and no more than one from each box ({nb})")
    s0 = [SA.status()["local_rx"], SA2.status()["local_rx"]]
    time.sleep(3)
    s1 = [SA.status()["local_rx"], SA2.status()["local_rx"]]
    check(s1[0] - s0[0] < 15 and s1[1] - s0[1] < 15, f"no storm: the idle LAN adds {s1[0] - s0[0]} and {s1[1] - s0[1]} frames in 3 s")
    check(SA.status()["drop_echo"] + SA2.status()["drop_echo"] >= 1, f"the boxes dropped what came round ({SA.status()['drop_echo']} and {SA2.status()['drop_echo']})")
    check(any(e.get("event") == "loop-guard" for e in SA.w.events + SA2.w.events), "and said so: another box on this segment is forwarding")

    # ping works through all of that
    r = nsrun(a1, ["ping", "-c", "3", "-W", "2", "-i", "0.3", "10.0.0.2"], timeout=30)
    check(" 3 received" in r.stdout or "3 packets received" in r.stdout or "0% packet loss" in r.stdout, f"a1 reaches b1 across the line, through two forwarding boxes: {r.stdout.strip().splitlines()[-2:]}")

    # spanning tree, LLDP and CDP stay on their own wire, from either side
    for name, dst, tok in (("a BPDU", "0180c2000000", "BPDUTEST"), ("LLDP", "0180c200000e", "LLDPTEST"), ("CDP", "01000ccccccc", "CDPTEST")):
        cb = count(b1, tok, 3)
        ca = count(a1, tok, 3)
        time.sleep(0.8)
        send(a1, mac_a1.replace(":", ""), dst, tok, "0026")
        send(b1, mac_b1.replace(":", ""), dst, tok, "0026")
        nb, na = int(cb.communicate()[0] or -1), int(ca.communicate()[0] or -1)
        check(nb == 0 and na == 0, f"{name} sent on either LAN does not reach the other ({nb} and {na} copies)")
    check(SA.status()["drop_ctrl"] + SA2.status()["drop_ctrl"] + SB.status()["drop_ctrl"] >= 3, "and the boxes counted them")
    cb = count(b1, "MCASTTEST", 3)
    time.sleep(0.8)
    send(a1, mac_a1.replace(":", ""), "01005e000001", "MCASTTEST", "9000")
    nb = int(cb.communicate()[0] or -1)
    check(nb >= 1, f"while IP multicast (01:00:5E) is carried ({nb} copies)")

    L.finish("m2b")


main()
