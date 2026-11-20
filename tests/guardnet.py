#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import json, os, socket, struct, sys, time

T = sys.argv[1]
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def ctl(path, line):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(3)
    s.connect(path)
    s.sendall((line + "\n").encode())
    b = b""
    while True:
        d = s.recv(65536)
        if not d:
            break
        b += d
    return json.loads(b)


def dhcp_reply(src_mac):
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + 8 + 240, 0, 0, 64, 17, 0, bytes([192, 168, 1, 1]), b"\xff" * 4)
    udp = struct.pack(">HHHH", 67, 68, 8 + 240, 0) + b"\x02\x01\x06\x00" + b"\0" * 236
    return b"\xff" * 6 + src_mac + b"\x08\x00" + ip + udp


def request(src_mac):
    ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + 8 + 240, 0, 0, 64, 17, 0, b"\0" * 4, b"\xff" * 4)
    udp = struct.pack(">HHHH", 68, 67, 8 + 240, 0) + b"\x01\x01\x06\x00" + b"\0" * 236
    return b"\xff" * 6 + src_mac + b"\x08\x00" + ip + udp


def until(fn, t=40):
    end = time.time() + t
    while time.time() < end:
        try:
            v = fn()
            if v:
                return v
        except Exception:
            pass
        time.sleep(0.3)
    return None


def peers_up(sock):
    return sum(1 for p in ctl(sock, "status")["peers"] if p["state"] == "up")


lan = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
lan.bind(f"{T}/Ahost")
lan.settimeout(0.3)
check(until(lambda: peers_up(f"{T}/ctlA") == 2), "A is on a line with both: B, which it dialled, and C, which rang it")
MB, MC = bytes.fromhex("020000000b01"), bytes.fromhex("020000000c01")


def send(name, frame):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.sendto(frame, f"{T}/{name}port")


def drain():
    out = []
    try:
        while True:
            out.append(lan.recv(2048))
    except socket.timeout:
        pass
    return out


drain()
for _ in range(3):
    send("B", dhcp_reply(MB))
    time.sleep(0.15)
got = drain()
check(sum(1 for f in got if f[6:12] == MB and f[12:14] == b"\x08\x00") == 3, f"a DHCP reply from the member A dialled reaches A's LAN (3 of 3: {len(got)})")
drain()
for _ in range(3):
    send("C", dhcp_reply(MC))
    time.sleep(0.15)
got = drain()
check(not any(f[6:12] == MC for f in got), f"a DHCP reply from the member that rang A does not ({len(got)} frames of C's)")
send("C", request(MC))
time.sleep(0.4)
got = drain()
check(any(f[6:12] == MC for f in got), "...while C's other frames (a DHCP request) still do: it is on the line, it may not be the network's server")
st = ctl(f"{T}/ctlA", "status")
check(st.get("dhcp_guarded") == 3, f"A counts the three it refused: {st.get('dhcp_guarded')}")
print("FAILED: " + "; ".join(fails) if fails else "all guardnet checks passed")
sys.exit(1 if fails else 0)
