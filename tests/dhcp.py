# SPDX-License-Identifier: GPL-3.0-or-later
import json, os, socket, struct, subprocess, sys, time

T, HIBR, MOD = sys.argv[1:4]
fails = []

def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok: fails.append(what)

def ctl(name):
    s = socket.socket(socket.AF_UNIX); s.settimeout(3)
    s.connect(f"{T}/ctl{name}"); s.sendall(b"status\n")
    d = b""
    while True:
        c = s.recv(65536)
        if not c: break
        d += c
    return json.loads(d)

def lw(*a):
    subprocess.run([HIBR, "-c", "mod load " + MOD + "; lw " + " ".join(a)], check=True, capture_output=True)

def csum(b):
    if len(b) % 2: b += b"\0"
    s = sum(struct.unpack("!%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xffff) + (s >> 16)
    return ~s & 0xffff

def reply(server, mac, kind):
    """An Ethernet frame holding a DHCP reply (offer = 2, ack = 5) from `server`, broadcast."""
    bootp = struct.pack("!BBBB", 2, 1, 6, 0) + b"\0" * 232 + bytes([99, 130, 83, 99, 53, 1, kind, 255])
    udp = struct.pack("!HHHH", 67, 68, 8 + len(bootp), 0) + bootp
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 1, 0, 64, 17, 0, socket.inet_aton(server), b"\xff" * 4)
    ip = ip[:10] + struct.pack("!H", csum(ip)) + ip[12:]
    return b"\xff" * 6 + mac + b"\x08\x00" + ip + udp

hosts = {}
for n in "AB":
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); s.bind(f"{T}/{n}host"); hosts[n] = s

lw("start --seed", f"{T}/seedA --sock {T}/ctlA --listen 127.0.0.1:41101 --sock-port {T}/Aport:{T}/Ahost --peer {T}/bundleB@127.0.0.1:41102")
lw("start --seed", f"{T}/seedB --sock {T}/ctlB --listen 127.0.0.1:41102 --sock-port {T}/Bport:{T}/Bhost --peer {T}/bundleA")
end = time.time() + 15
while time.time() < end:
    try:
        if ctl("A")["peers"] and ctl("A")["peers"][0]["state"] == "up": break
    except Exception: pass
    time.sleep(0.2)
check(ctl("A")["peers"][0]["state"] == "up", "the two nodes are connected")

def inject(n, frame):
    hosts[n].sendto(frame, f"{T}/{n}port"); time.sleep(0.4)

mac1, mac2 = b"\x02\0\0\0\0\x01", b"\x02\0\0\0\0\x02"
inject("A", reply("10.0.0.1", mac1, 2)); inject("A", reply("10.0.0.1", mac1, 2))
check(ctl("A")["dhcp_conflicts"] == 0 and ctl("B")["dhcp_conflicts"] == 0, "one server, offering twice: no warning")
inject("B", reply("10.0.0.2", mac2, 5))
check(ctl("A")["dhcp_conflicts"] == 0 and ctl("B")["dhcp_conflicts"] == 0, "a second host that only acknowledges is not a second offer")
inject("B", reply("10.0.0.2", mac2, 2))
check(ctl("B")["dhcp_conflicts"] == 1, "the second server's offer is reported on its own side")
check(ctl("A")["dhcp_conflicts"] == 1, "and on the other side of the line")
inject("B", reply("10.0.0.2", mac2, 2))
check(ctl("B")["dhcp_conflicts"] == 1, "it is said once, not on every offer")
sys.exit(1 if fails else 0)
