# SPDX-License-Identifier: GPL-3.0-or-later
# The exit (ADR 0056) on the real kernel, with the jack helper run in network namespaces of their own (no root needed, nothing on this
# machine changes); veth pairs stand in for jacks, whose carrier goes when no node holds them.
#
#   the client side: --everything with no gateway points the jack's table at the exit gateway (169.254.77.1, MAC 02:6c:77:65:78:74, fixed),
#   so an application's packet leaves into the jack addressed to that MAC, which the node hands to the exit; with no node, the machine's own road.
#
#   the exit side (needs nft):  CL 192.168.152.10 --[line]-- X 192.168.152.1 | 10.9.0.2 -- I 10.9.0.1 (203.0.113.9)
#                                                              X 10.50.0.1 -- L 10.50.0.9 (the exit's own LAN)
#   --share lets the line's network out through X's way out, masqueraded; lets only answers back; keeps the line off X's own LAN; and is undone exactly.
import os, shutil, socket, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, T, ROOT, check, sh, nsh, mkns, mac_of

H = ROOT + "/packaging/longwire-jack"
os.makedirs(T + "/bin", exist_ok=True)
open(T + "/bin/resolvectl", "w").write("#!/bin/sh\nexit 0\n")		# never the host's systemd-resolved
os.chmod(T + "/bin/resolvectl", 0o755)
PATH = T + "/bin:" + os.environ.get("PATH", "") + ":/usr/sbin:/sbin"


def helper(ns, name, *args):
    """Run the jack helper inside ns, as the namespace's root, for user 1000 (a stand-in /sys says lw0 is that user's)."""
    sysd, run = f"{T}/{name}/sys", f"{T}/{name}/run"
    os.makedirs(sysd + "/lw0", exist_ok=True); os.makedirs(run, exist_ok=True)
    open(sysd + "/lw0/owner", "w").write("1000\n")
    env = dict(os.environ, PATH=PATH, LONGWIRE_SYSNET=sysd, LONGWIRE_RUN=run)
    r = subprocess.run(["nsenter", "-t", str(ns.pid), "-n", HIBR, H, *args, "lw0", "1000"], env=env, capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def get(ns, dst):
    return nsh(ns, f"ip route get {dst}", out=True).splitlines()[0]


def udp_ask(ns, dst, port=7777, timeout=2.0):
    """From inside ns, send one datagram and return the answer (the address the far end saw), or None."""
    code = f"""
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout({timeout})
s.sendto(b"who am i",("{dst}",{port}))
try: print(s.recv(100).decode())
except Exception: print("")
"""
    r = subprocess.run(["nsenter", "-t", str(ns.pid), "-n", sys.executable, "-I", "-c", code], capture_output=True, text=True)
    return r.stdout.strip() or None


def echo_server(ns, port=7777, addr="0.0.0.0"):
    """A server in ns that answers each datagram with the address it came from, and writes that down."""
    code = f"""
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.bind(("{addr}",{port}))		# bound to its address: it answers from the one it was asked on, as a real server does
while True:
    d,a=s.recvfrom(100); open("{T}/seen","a").write(a[0]+"\\n"); s.sendto(a[0].encode(),a)
"""
    p = subprocess.Popen(["nsenter", "-t", str(ns.pid), "-n", sys.executable, "-I", "-c", code])
    L.procs.append(p)
    time.sleep(0.3)


def main():
    print(f"-- the exit on the real kernel  (workdir {T})", flush=True)
    sh("ip link set lo up")

    # ---- the client side ------------------------------------------------------------------------------------------------
    C = mkns()
    nsh(C, "ip link set lo up")
    L.veth(C, "eth9", None, "c_up")
    L.veth(C, "lw0", None, "c_lw0p")
    sh("ip link set c_lw0p down")							# no node on the jack yet: no carrier
    nsh(C, "ip addr add 10.9.0.2/24 dev eth9"); nsh(C, "ip route add default via 10.9.0.1 dev eth9")
    rc, out = helper(C, "C", "--address", "192.168.152.10/24", "--everything")
    check(rc == 0 and "member that shares its internet" in out, f"the helper puts everything through the line with no gateway named: {out}")
    tbl = nsh(C, "ip route show table 7700", out=True)
    check("default via 169.254.77.1 dev lw0 " in tbl and "onlink" in tbl, f"the jack's table goes to the exit gateway, on the jack: {tbl!r}")
    nb = nsh(C, "ip neigh show 169.254.77.1 dev lw0", out=True)
    check("02:6c:77:65:78:74" in nb and "PERMANENT" in nb, f"with the gateway's MAC fixed beside it, so nothing is asked by ARP: {nb!r}")
    check("via 10.9.0.1 dev eth9" in get(C, "203.0.113.9"), f"with no node holding the jack, the machine keeps its own road: {get(C, '203.0.113.9')}")
    sh("ip link set c_lw0p up"); time.sleep(0.3)					# a node takes the jack
    check("via 169.254.77.1 dev lw0" in get(C, "203.0.113.9"), f"with a node on the jack, an application's packet goes to the exit gateway: {get(C, '203.0.113.9')}")
    check("src 192.168.152.10" in get(C, "203.0.113.9"), "from the jack's own address, which the exit lets out")
    # the frame itself: sent into the jack, addressed to the gateway's MAC (what the node's switch hands to the exit)
    rs = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3)); rs.bind(("c_lw0p", 0)); rs.settimeout(2)
    udp_ask(C, "203.0.113.9", timeout=0.3)
    seen = None
    try:
        for _ in range(20):
            f = rs.recv(2000)
            if f[12:14] == b"\x08\x00" and f[30:34] == bytes([203, 0, 113, 9]):
                seen = f; break
    except socket.timeout:
        pass
    check(seen is not None and seen[:6] == bytes.fromhex("026c77657874"), f"the packet leaves into the jack addressed to 02:6c:77:65:78:74: {seen and seen[:6].hex()}")
    sh("ip link set c_lw0p down"); time.sleep(0.3)					# the node stops
    check("via 10.9.0.1 dev eth9" in get(C, "203.0.113.9"), "the node gone, the machine is back on its own road")
    rc, out = helper(C, "C", "--address", "192.168.152.10/24")
    check(rc == 0 and not nsh(C, "ip route show table 7700", out=True) and not nsh(C, "ip rule show pref 7701", out=True),
          "run without --everything, the helper takes the table and its rules away")
    rc, out = helper(C, "C", "--everything")
    check(rc == 2 and "own address" in out, f"everything through the exit needs the jack's own address: {out}")

    # ---- the exit side --------------------------------------------------------------------------------------------------
    if not shutil.which("nft", path=PATH):
        print("skip the exit side: no nft here (the nftables package)")
        L.finish("exitnet")
    X, CL, I, LN = mkns(), mkns(), mkns(), mkns()
    for n in (X, CL, I, LN):
        nsh(n, "ip link set lo up")
    L.veth(CL, "lw0c", X, "lw0")							# the line, as the exit's kernel sees it
    L.veth(X, "eth9", I, "eth0")							# the exit's way out
    L.veth(X, "lan0", LN, "eth0")							# the exit's own LAN
    nsh(CL, "ip addr add 192.168.152.10/24 dev lw0c"); nsh(CL, "ip route add default via 192.168.152.1")
    nsh(X, "ip addr add 10.9.0.2/24 dev eth9"); nsh(X, "ip route add default via 10.9.0.1")
    nsh(X, "ip addr add 10.50.0.1/24 dev lan0")
    for f in ("all", "default", "lw0", "eth9", "lan0"):				# a new namespace may take this machine's forwarding: start from none
        subprocess.run(["nsenter", "-t", str(X.pid), "-n", "sh", "-c", f"echo 0 > /proc/sys/net/ipv4/conf/{f}/forwarding"], check=True)
    nsh(I, "ip addr add 10.9.0.1/24 dev eth0"); nsh(I, "ip addr add 203.0.113.9/32 dev lo")
    nsh(I, "ip route add 192.168.152.0/24 via 10.9.0.2")				# a way in that the exit must not open
    nsh(LN, "ip addr add 10.50.0.9/24 dev eth0"); nsh(LN, "ip route add default via 10.50.0.1")
    echo_server(I, 7777, "203.0.113.9"); echo_server(LN, 7778, "10.50.0.9")
    check(udp_ask(CL, "203.0.113.9") is None, "before sharing, the exit does not forward the line's traffic")
    rc, out = helper(X, "X", "--address", "192.168.152.1/24", "--share")
    check(rc == 0 and "sharing this machine's internet through eth9" in out, f"the helper shares the exit's internet through its way out: {out}")
    seen = udp_ask(CL, "203.0.113.9")
    if seen != "10.9.0.2":				# say where it stopped
        env = dict(os.environ, PATH=PATH)
        for c in (["nft", "list", "ruleset"], ["ip", "neigh"], ["ip", "route"], ["cat", "/proc/sys/net/ipv4/conf/lw0/forwarding"]):
            print("   X:", " ".join(c), subprocess.run(["nsenter", "-t", str(X.pid), "-n", *c], env=env, capture_output=True, text=True).stdout.strip().replace("\n", " | ")[:600])
        print("   I saw:", open(f"{T}/seen").read().split() if os.path.exists(f"{T}/seen") else "nothing")
    check(seen == "10.9.0.2", f"a member's packet goes out through the exit, masqueraded as the exit's own address: the far end saw {seen}")
    check(udp_ask(CL, "10.50.0.9", 7778) is None, "the line cannot reach the exit's own LAN through it")
    check(udp_ask(I, "192.168.152.10", 7777) is None, "nothing from the internet gets in to the line through the exit")
    fw = nsh(X, "cat /proc/sys/net/ipv4/conf/eth9/forwarding /proc/sys/net/ipv4/conf/lan0/forwarding", out=True).split()
    check(fw == ["1", "0"], f"forwarding is on for the jack and the way out only: eth9 {fw[0]}, lan0 {fw[1]}")
    nsh(I, "ip route add 10.50.0.0/24 via 10.9.0.2")				# a neighbour on the way out's far side, who would route through the exit to its other network
    subprocess.run(["nsenter", "-t", str(X.pid), "-n", "sh", "-c", "echo 1 > /proc/sys/net/ipv4/conf/lan0/forwarding"], check=True)	# (the answers could come back only if the exit forwarded for its LAN too)
    check(udp_ask(I, "10.50.0.9", 7778) is None, "and whoever is on the way out's far side cannot route through the exit to its other networks: forwarding was off before sharing, and the table puts that back (docs/adr/0063)")
    subprocess.run(["nsenter", "-t", str(X.pid), "-n", "sh", "-c", "echo 0 > /proc/sys/net/ipv4/conf/lan0/forwarding"], check=True)
    rc, out = helper(X, "X", "--address", "192.168.152.1/24", "--share")
    check(rc == 0 and udp_ask(CL, "203.0.113.9") == "10.9.0.2", "run again, the same table: still shared")
    rc, out = helper(X, "X", "--address", "192.168.152.1/24")
    tables = subprocess.run(["nsenter", "-t", str(X.pid), "-n", "nft", "list", "tables"], env=dict(os.environ, PATH=PATH), capture_output=True, text=True).stdout
    fw = nsh(X, "cat /proc/sys/net/ipv4/conf/eth9/forwarding", out=True)
    check(rc == 0 and "longwire-lw0" not in tables and fw == "0" and not os.path.exists(f"{T}/X/run/share-lw0"),
          f"run without --share, the table goes and the way out's forwarding is put back as it was ({fw})")
    check(udp_ask(CL, "203.0.113.9") is None, "and the line's traffic is no longer forwarded")
    rc, out = helper(X, "X", "--address", "192.168.152.1/24", "--share", "--everything")
    check(rc == 2 and "does not send its own" in out, f"an exit does not also send everything through the line: {out}")
    L.finish("exitnet")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:
        import traceback; traceback.print_exc()
        check(False, f"crashed: {e!r}")
        L.finish("exitnet")
