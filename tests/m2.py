# SPDX-License-Identifier: GPL-3.0-or-later
# M2 end-to-end test: two LAN segments that are one subnet. Real kernel hosts,
# real ARP, ICMP, TCP and broadcasts, carried by splice nodes (AF_PACKET on a
# NIC, no interface created) that are the real hibr scripts:
#
#   a1 10.0.0.1 --[brA]--+-- sa  --+                   +-- sb --- b1 10.0.0.2
#                        +-- sa2 -+-- underlay brU ---+
#
# sa and sa2 are two splice boxes on the same LAN segment (a site group); the
# test kills the one that forwards and watches the other take over. Run via
# tests/m2.sh, which wraps this in `unshare -Urn`.
import hashlib, os, random, re, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import check, sh, nsh, nsrun


def noipv6(ns):
    """no IPv6 chatter from the hosts: keeps the frame counters in the tests meaningful"""
    nsrun(ns, ["sh", "-c", "echo 1 > /proc/sys/net/ipv6/conf/all/disable_ipv6"])


def tcp_stats(ns):
    """(RetransSegs, OutSegs) of the namespace's TCP stack"""
    rows = [l.split() for l in nsrun(ns, ["cat", "/proc/net/snmp"]).stdout.splitlines() if l.startswith("Tcp:")]
    d = dict(zip(rows[0][1:], map(int, rows[1][1:])))
    return d["RetransSegs"], d["OutSegs"]


def ping(ns, ip, count=3, size=None, wait=2):
    argv = ["ping", "-c", str(count), "-W", str(wait), "-i", "0.2"] + (["-s", str(size), "-M", "do"] if size else []) + [ip]
    r = nsrun(ns, argv, timeout=count * wait + 10)
    m = re.search(r"(\d+) received", r.stdout)
    return int(m.group(1)) if m else 0, r.stdout


def main():
    print(f"-- M2: one subnet over two LANs  (workdir {L.T})", flush=True)
    sh("ip link set lo up")
    sh("ip link add brA type bridge"); sh("ip link set brA up")
    sh("ip link add brU type bridge"); sh("ip link set brU up")
    a1, b1, sa, sa2, sb = (L.mkns() for _ in range(5))

    def veth(ns_x, name_x, ns_y, name_y, bridge_x=None, bridge_y=None):
        sh(f"ip link add vx type veth peer name vy")
        for end, ns, name, br in (("vx", ns_x, name_x, bridge_x), ("vy", ns_y, name_y, bridge_y)):
            if ns is None:                                   # stays in the bridge's namespace (ours)
                sh(f"ip link set {end} name {name}"); sh(f"ip link set {name} master {br}"); sh(f"ip link set {name} up")
            else:
                sh(f"ip link set {end} netns {ns.pid}")
                nsh(ns, f"ip link set {end} name {name}"); nsh(ns, f"ip link set {name} up")

    # LAN A: hosts and splice boxes on bridge brA. The underlay: brU. LAN B: b1 straight to sb.
    veth(a1, "eth0", None, "r_a1", bridge_y="brA")
    veth(sa, "lan", None, "r_sa", bridge_y="brA")
    veth(sa2, "lan", None, "r_sa2", bridge_y="brA")
    veth(sa, "ul", None, "r_sau", bridge_y="brU")
    veth(sa2, "ul", None, "r_sa2u", bridge_y="brU")
    veth(sb, "ul", None, "r_sbu", bridge_y="brU")
    veth(sb, "lan", b1, "eth0")
    for ns in (a1, b1):
        nsh(ns, "ip link set lo up"); noipv6(ns)
    nsh(a1, "ip addr add 10.0.0.1/24 dev eth0"); nsh(b1, "ip addr add 10.0.0.2/24 dev eth0")
    for ns, ip in ((sa, "10.9.0.1"), (sa2, "10.9.0.3"), (sb, "10.9.0.2")):
        nsh(ns, "ip link set lo up"); nsh(ns, f"ip addr add {ip}/24 dev ul")
        noipv6(ns)

    # identities, and the secret the two boxes of site A share
    b_sa, b_sa2, b_sb = L.identity("sa"), L.identity("sa2"), L.identity("sb")
    sitekey = f"{L.T}/sitekey"
    open(sitekey, "w").write(os.urandom(32).hex())
    common = ["up", "--keepalive", "3", "--rekey", "40"]
    SA = L.Node("sa", sa, "longwire.hibr", common + ["--splice", "lan", "--listen", "10.9.0.1:4000", "--peer", f"{b_sb}@10.9.0.2:4000", "--site-key", sitekey])
    SB = L.Node("sb", sb, "longwire.hibr", common + ["--splice", "lan", "--listen", "10.9.0.2:4000", "--peer", b_sa, "--peer", b_sa2])

    def until(fn, timeout=20, step=0.25):
        end = time.time() + timeout
        while time.time() < end:
            try:
                v = fn()
                if v: return v
            except Exception:
                pass
            time.sleep(step)
        return None

    up = lambda n: n.status()["peers"] and all(p["state"] == "up" for p in n.status()["peers"] if p["state"] != "waiting")
    check(bool(until(lambda: SA.status()["site"] == "active" and SA.status()["peers"][0]["state"] == "up" and
                     any(p["state"] == "up" for p in SB.status()["peers"]))),
          "sa (alone in its site group, after listening 3 s) goes active and its session with sb comes up")

    # --- ARP and ICMP cross the line: one subnet --------------------------------------
    got, out = ping(a1, "10.0.0.2", 3)
    check(got == 3, f"ping 10.0.0.2 from a host on LAN A to a host on LAN B: {got}/3 replies (ARP crossed the line first)")
    a0 = SA.status()
    time.sleep(2.5)
    a1s = SA.status()
    check(a1s["local_rx"] - a0["local_rx"] < 15, f"no loop: an idle LAN adds {a1s['local_rx'] - a0['local_rx']} frames in 2.5 s (frames we inject are not read back)")
    check(a1s["local_rx"] < 150, f"...and only {a1s['local_rx']} frames were ever read from the wire after several pings")

    # --- frames that don't fit one datagram ------------------------------------------------
    s0, j0 = SA.status()["frames_split"], SB.status()["frames_joined"]
    got, out = ping(a1, "10.0.0.2", 4, size=1472)
    st_a, st_b = SA.status(), SB.status()
    check(got == 4, f"full-size frames (1514 bytes, ping -s 1472 -M do): {got}/4 replies")
    check(st_a["frames_split"] - s0 >= 4 and st_b["frames_joined"] - j0 >= 4,
          f"each was split in two on the way ({st_a['frames_split'] - s0} split by sa) and put back together ({st_b['frames_joined'] - j0} joined by sb)")
    time.sleep(1.0)
    # every frame either way, whole or in two halves, is counted once, at its own length, at both ends
    st_a, st_b = SA.status(), SB.status()
    tx_a, rx_b = sum(p["tx_bytes"] for p in st_a["peers"]), sum(p["rx_bytes"] for p in st_b["peers"])
    tx_b, rx_a = sum(p["tx_bytes"] for p in st_b["peers"]), sum(p["rx_bytes"] for p in st_a["peers"])
    check(tx_a > 6000 and tx_a == rx_b and tx_b == rx_a,
          f"split frames are counted once at their whole length, and sa and sb agree: sa sent {tx_a}, sb got {rx_b}; sb sent {tx_b}, sa got {rx_a}")

    # --- TCP: the MSS clamp keeps segments in one datagram -----------------------------------
    data = random.Random(5).randbytes(3_000_000)

    def transfer():
        srv = subprocess.Popen(["nsenter", "-t", str(b1.pid), "-n", "python3", "-I", "-c",
            "import socket,hashlib\ns=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('10.0.0.2',5001));s.listen(1)\n"
            "c,_=s.accept();h=hashlib.sha256();n=0\nwhile True:\n d=c.recv(65536)\n if not d: break\n h.update(d);n+=len(d)\nprint(n,h.hexdigest())"],
            stdout=subprocess.PIPE, text=True)
        L.procs.append(srv)
        time.sleep(0.5)
        r0 = tcp_stats(a1)
        t0 = time.time()
        cli = nsrun(a1, ["python3", "-I", "-c", "import socket,random\nd=random.Random(5).randbytes(3000000)\n"
                         "s=socket.create_connection(('10.0.0.2',5001));s.sendall(d);s.close()"], timeout=60)
        out = srv.communicate(timeout=30)[0].split()
        dt = time.time() - t0
        r1 = tcp_stats(a1)
        return (cli.returncode == 0 and bool(out) and out[0] == "3000000" and out[1] == hashlib.sha256(data).hexdigest(),
                r1[0] - r0[0], r1[1] - r0[1], dt)

    # The machine is shared: a neighbour's burst of work looks like packet loss. Wait for a quiet moment, and if the
    # transfer still loses more than 2% say so and try again (up to three times); every attempt is reported.
    s0 = SA.status()["frames_split"] + SB.status()["frames_split"]
    attempts = []
    for _ in range(3):
        print(f"   (host {L.quiet_machine() * 100:.0f}% idle before the TCP transfer)", flush=True)
        ok, rtx, segs, dt = transfer()
        attempts.append(rtx)
        if not ok or rtx <= segs * 0.02:
            break
    check(ok, f"3 MB over TCP across the line arrives intact ({3000000 * 8 / dt / 1e6:.0f} Mbit/s, through a TCC build and two veth hops)")
    check(rtx <= segs * 0.02, f"...with {rtx} retransmitted segments of {segs} (attempts: {attempts}; a NIC's 64 KB coalesced frames are cut into line-sized TCP segments, not dropped)")
    check(SA.status()["resegmented"] > 0 and SA.status()["drop_oversize"] == 0, f"sa re-segmented {SA.status()['resegmented']} oversize frames and dropped none")
    s1 = SA.status()["frames_split"] + SB.status()["frames_split"]
    check(s1 - s0 <= 6, f"the MSS clamp worked: only {s1 - s0} frames needed splitting during the transfer (about 2,000 segments)")

    # --- broadcasts: what DHCP and mDNS are made of ------------------------------------------
    rcv = subprocess.Popen(["nsenter", "-t", str(b1.pid), "-n", "python3", "-I", "-c",
        "import socket\ns=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('',6767));s.settimeout(4)\n"
        "try: print(s.recv(100).decode())\nexcept Exception: print('nothing')"], stdout=subprocess.PIPE, text=True)
    L.procs.append(rcv)
    time.sleep(0.5)
    nsrun(a1, ["python3", "-I", "-c", "import socket\ns=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)\n"
               "s.sendto(b'hello from LAN A',('10.0.0.255',6767))"])
    check(rcv.communicate(timeout=10)[0].strip() == "hello from LAN A", "a UDP broadcast on LAN A is heard on LAN B")

    # --- ARP answered locally -------------------------------------------------------------------
    n0 = SA.status()["arp_answered"]
    nsh(a1, "ip neigh flush dev eth0")
    got, _ = ping(a1, "10.0.0.2", 2)
    n1 = SA.status()["arp_answered"]
    check(got == 2 and n1 > n0, f"after a1 forgets b1, sa answers its ARP request itself (arp_answered {n0} -> {n1}); it never crosses the line")

    # --- IPv6 neighbour discovery is answered locally, too ------------------------------------------
    for h in (a1, b1):                          # the hosts have IPv6 off to keep the counters quiet: on, for this check only
        nsrun(h, ["sh", "-c", "echo 0 > /proc/sys/net/ipv6/conf/all/disable_ipv6; echo 0 > /proc/sys/net/ipv6/conf/eth0/disable_ipv6"])
    nsh(a1, "ip -6 addr add fd01::1/64 dev eth0 nodad"); nsh(b1, "ip -6 addr add fd01::2/64 dev eth0 nodad")
    time.sleep(1)
    got, _ = ping(a1, "fd01::2", 3)
    check(got == 3, f"ping6 from a host on LAN A to a host on LAN B: {got}/3 replies (neighbour discovery crossed the line first)")
    n0 = SA.status()["arp_answered"]
    nsh(a1, "ip -6 neigh flush dev eth0")
    got, _ = ping(a1, "fd01::2", 2)
    n1 = SA.status()["arp_answered"]
    check(got == 2 and n1 > n0, f"after a1 forgets b1, sa answers its neighbour solicitation itself (answered {n0} -> {n1})")
    for h in (a1, b1):
        nsrun(h, ["sh", "-c", "echo 1 > /proc/sys/net/ipv6/conf/all/disable_ipv6"])
    time.sleep(0.5)

    # --- a broadcast storm is held to a budget ----------------------------------------------------
    rcv = subprocess.Popen(["nsenter", "-t", str(b1.pid), "-n", "python3", "-I", "-c",
        "import socket,time\ns=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3));s.bind(('eth0',0));s.settimeout(0.3)\n"
        "n=0;end=time.time()+7\nwhile time.time()<end:\n try: f=s.recv(2000)\n except Exception: continue\n"
        " if f[12:14]==b'\\x88\\xb6': n+=1\nprint(n)"], stdout=subprocess.PIPE, text=True)
    L.procs.append(rcv)
    time.sleep(0.8)
    d0 = SA.status()["drop_flood"]
    nsrun(a1, ["python3", "-I", "-c", "import socket\ns=socket.socket(socket.AF_PACKET,socket.SOCK_RAW);s.bind(('eth0',0))\n"
               "import time\nf=b'\\xff'*6+bytes.fromhex('02000000aaaa')+b'\\x88\\xb6'+b'x'*100\nfor i in range(2500):\n s.send(f)\n if i%50==49: time.sleep(0.01)"], timeout=30)
    arrived = int(rcv.communicate(timeout=20)[0].strip() or -1)
    dropped = SA.status()["drop_flood"] - d0
    check(0 < arrived < 1200 and dropped > 700, f"2500 broadcast frames in half a second: {arrived} reached LAN B, {dropped} were dropped by sa's flood budget (burst 200 + 1000 a second)")
    got, _ = ping(a1, "10.0.0.2", 2)
    check(got == 2, "...and ordinary traffic is unaffected afterwards")

    # --- privilege: the workers hold nothing -----------------------------------------------------
    caps = []
    for ns in (sa, sb):
        for pid in L.workers_in(ns):
            st = open(f"/proc/{pid}/status").read()
            caps.append((re.search(r"CapEff:\s+(\w+)", st).group(1), re.search(r"NoNewPrivs:\s+(\d)", st).group(1)))
    check(caps and all(c == ("0000000000000000", "1") for c in caps),
          f"started with CAP_NET_RAW (root in the namespace), every worker ended up with no capabilities: {caps}")

    # --- a second box on the same segment: one forwards, one waits -----------------------------------
    SA2 = L.Node("sa2", sa2, "longwire.hibr", common + ["--splice", "lan", "--listen", "10.9.0.3:4000", "--peer", f"{b_sb}@10.9.0.2:4000", "--site-key", sitekey])
    roles = until(lambda: sorted([SA.status()["site"], SA2.status()["site"]]) == ["active", "standby"] and
                  SA.status()["site_members"] == 1 and SA2.status()["site_members"] == 1 and
                  SA2.status()["peers"][0]["state"] == "up" and sorted([SA.status()["site"], SA2.status()["site"]]), timeout=25)
    check(roles == ["active", "standby"], f"sa2 joins the site group: one box active, one standby ({roles})")
    nodes = {"sa": (SA, sa), "sa2": (SA2, sa2)}
    active = next(k for k, (n, _) in nodes.items() if n.status()["site"] == "active")
    standby = "sa2" if active == "sa" else "sa"
    check(active == ("sa" if bytes.fromhex(SA.status()["key_id"]) < bytes.fromhex(SA2.status()["key_id"]) else "sa2"),
          f"the box with the lowest key id forwards ({active})")
    nsh(a1, "ip neigh flush dev eth0")                       # makes a1 broadcast an ARP request: both boxes hear it
    got, out = ping(a1, "10.0.0.2", 5)
    check(got == 5 and "DUP" not in out, f"two boxes on one segment, no loop and no duplicates: {got}/5 replies, none duplicated")
    check(nodes[standby][0].status()["drop_standby"] > 0, f"the standby box ({standby}) kept quiet: it dropped {nodes[standby][0].status()['drop_standby']} frames instead of forwarding them")

    # --- the active box dies: the other takes over --------------------------------------------------------
    synced = until(lambda: nodes[standby][0].status()["site_synced"] >= 1, timeout=8)
    check(bool(synced), f"the active box shares the remote hosts it knows with the standby (standby holds {nodes[standby][0].status()['site_synced']})")
    ev = nodes[standby][0].w.mark()
    for pid in L.workers_in(nodes[active][1]):
        os.kill(pid, 9)
    t0 = time.time()
    back = until(lambda: ping(a1, "10.0.0.2", 1, wait=1)[0] == 1, timeout=25, step=0.2)
    took = time.time() - t0
    check(back is not None and took < 14, f"the active box is killed: pings resume after {took:.1f} s with nobody touching a1")
    e = nodes[standby][0].w.wait("site", 5, pred=lambda e: e["role"] == "active", after=ev)
    check(nodes[standby][0].status()["site"] == "active", f"the standby box announced itself active ({e})")
    got, out = ping(a1, "10.0.0.2", 5)
    check(got == 5 and "DUP" not in out, f"and traffic is clean after the takeover: {got}/5")

    rcs, unit = SA.cmd("setup", "splice", "lan")
    check(rcs == 0 and "AmbientCapabilities=CAP_NET_RAW" in unit and "--splice lan" in unit, "`longwire setup splice lan` prints a service unit with the ambient capability")
    for n in (SA, SA2, SB):
        n.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("M2")
