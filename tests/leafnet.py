#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# The phone's engine on a real line (docs/adr/0076): a node whose port is the adapter, embedded as the app embeds it, dials a home node whose port is a socket; this script is the home LAN behind that
# socket (a DHCP server, a gateway that answers ARP and echoes UDP) and the phone's OS (the tunnel). The phone must get its lease from the LAN, and IP packets must cross both ways, unchanged.
import os, socket, struct, subprocess, sys, time, json

T = sys.argv[1]
GWMAC = bytes.fromhex("525400123456")
GW, ME = socket.inet_aton("192.168.1.1"), socket.inet_aton("192.168.1.50")
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def csum(b):
    if len(b) % 2:
        b += b"\0"
    s = sum(struct.unpack(f">{len(b)//2}H", b))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF


def ip4(src, dst, proto, payload):
    h = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), 0, 0, 64, proto, 0, src, dst)
    h = h[:10] + struct.pack(">H", csum(h)) + h[12:]
    return h + payload


def udp(sp, dp, data):
    return struct.pack(">HHHH", sp, dp, 8 + len(data), 0) + data


def eth(dst, src, et, payload):
    return dst + src + struct.pack(">H", et) + payload


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
    s.close()
    return json.loads(b)


def dhcp_reply(frame, mtype):
    """OFFER or ACK to the DHCP message in `frame`."""
    ihl = (frame[14] & 15) * 4
    b = frame[14 + ihl + 8:]
    xid, chaddr = b[4:8], b[28:34]
    opts = b"\x35\x01" + bytes([mtype]) + b"\x36\x04" + GW + b"\x01\x04\xff\xff\xff\x00" + b"\x03\x04" + GW + b"\x06\x04" + GW + b"\x33\x04" + struct.pack(">I", 3600) + b"\xff"
    bootp = struct.pack(">BBBB4sHH4s4s4s4s", 2, 1, 6, 0, xid, 0, 0x8000, b"\0" * 4, ME, b"\0" * 4, b"\0" * 4) + chaddr + b"\0" * 10 + b"\0" * 192 + b"\x63\x82\x53\x63" + opts
    return eth(b"\xff" * 6, GWMAC, 0x0800, ip4(GW, b"\xff" * 4, 17, udp(67, 68, bootp)))


def main():
    hibr = os.environ.get("HIBR", "../hibr/build/hibr")
    mod = os.path.abspath("build/longwire.so")

    def lw(*a):
        return subprocess.run([hibr, "-c", "mod load %s; lw %s" % (mod, " ".join(a))], capture_output=True, text=True, timeout=60)

    for n in ("home", "phone"):
        lw("key", "new", f"{T}/seed{n}")
        # a bundle is binary: the shell writes it
        subprocess.run(f"{hibr} -c 'mod load {mod}; lw key bundle {T}/seed{n}' > {T}/bundle{n}", shell=True, check=True)
    os.makedirs(f"{T}/phone", exist_ok=True)
    os.rename(f"{T}/seedphone", f"{T}/phone/seed")

    lan = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    lan.bind(f"{T}/Hhost")
    lan.settimeout(0.2)
    r = lw("start", "--seed", f"{T}/seedhome", "--sock", f"{T}/ctlhome", "--listen", "127.0.0.1:41701", "--sock-port", f"{T}/Hport:{T}/Hhost", "--peer", f"{T}/bundlephone", "--rekey", "4", "--keepalive", "2")
    check(r.returncode == 0, "the home node starts (its port is the socket this script holds: the home LAN)")

    os_end, eng_end = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    os_end.settimeout(0.2)
    phone = subprocess.Popen([os.path.abspath("build/leafnet"), f"{T}/phone", f"{T}/bundlehome@127.0.0.1:41701", str(eng_end.fileno())], pass_fds=(eng_end.fileno(),), stdout=subprocess.PIPE, text=True)
    check(phone.stdout.readline().strip() == "attached", "the phone's engine runs on a thread and takes its tunnel from another (leaf_attach)")

    seen = {"discover": 0, "request": 0, "arp": 0, "echo": 0, "inet": 0}
    deadline, bound = time.time() + 45, None

    def pump(until):
        nonlocal bound
        while time.time() < until:
            try:
                f = lan.recv(2048)
            except socket.timeout:
                f = None
            if f and len(f) > 14:
                et = struct.unpack(">H", f[12:14])[0]
                if et == 0x0800 and f[23] == 17 and f[14 + (f[14] & 15) * 4 + 2:14 + (f[14] & 15) * 4 + 4] == b"\x00\x43":
                    ihl = (f[14] & 15) * 4
                    o = f[14 + ihl + 8 + 240:]
                    t = o[2] if o[:2] == b"\x35\x01" else 0
                    if t == 1:
                        seen["discover"] += 1
                        lan.sendto(dhcp_reply(f, 2), f"{T}/Hport")
                    elif t == 3:
                        seen["request"] += 1
                        lan.sendto(dhcp_reply(f, 5), f"{T}/Hport")
                elif et == 0x0806 and f[38:42] == GW and struct.unpack(">H", f[20:22])[0] == 1:
                    seen["arp"] += 1
                    lan.sendto(eth(f[6:12], GWMAC, 0x0806, struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2) + GWMAC + GW + f[22:28] + f[28:32]), f"{T}/Hport")
                elif et == 0x0800 and f[:6] == GWMAC and f[14 + 9] == 17:    # a UDP packet for the gateway's MAC: echo it back from where it was sent to
                    ihl = (f[14] & 15) * 4
                    src, dst = f[14 + 12:14 + 16], f[14 + 16:14 + 20]
                    sp, dp = struct.unpack(">HH", f[14 + ihl:14 + ihl + 4])
                    data = f[14 + ihl + 8:]
                    seen["echo" if dst == GW else "inet"] += 1
                    lan.sendto(eth(f[6:12], GWMAC, 0x0800, ip4(dst, src, 17, udp(dp, sp, data))), f"{T}/Hport")
            if bound is None:
                try:
                    m = ctl(f"{T}/phone/ctl", "member")
                    if m.get("member") == "bound":
                        bound = m
                except Exception:
                    pass

    t0 = time.time()
    while bound is None and time.time() < deadline:
        pump(time.time() + 0.5)
    check(bound is not None, f"the phone is on the line and the home LAN's DHCP gave it an address ({int(time.time() - t0)} s)")
    if bound:
        check(bound["ip"] == "192.168.1.50" and bound["gateway"] == "192.168.1.1" and bound["prefix"] == 24 and bound["dns"] == ["192.168.1.1"] and bound["lease"] == 3600 and bound["tun"],
              f"the lease is the LAN's: {bound['ip']}/{bound['prefix']} via {bound['gateway']}, dns {bound['dns']}")
    # a UDP packet from the OS to the gateway: ARP, then the packet across, then the echo back to the OS
    p = ip4(ME, GW, 17, udp(5000, 7, b"hello from the phone"))
    os_end.send(p)
    got = None
    for _ in range(40):
        pump(time.time() + 0.2)
        try:
            got = os_end.recv(2048)
            break
        except socket.timeout:
            pass
    check(got is not None and got[12:16] == GW and got[16:20] == ME and got[9] == 17 and got[28:] == b"hello from the phone" and csum(got[:20]) == 0,
          f"a packet from the phone's OS crosses the line, the LAN answers it, and the answer reaches the OS as the packet it is: {got[28:] if got else None}")
    check(seen["arp"] >= 1 and seen["echo"] == 1, f"...the phone asked who has the gateway (ARP) before the first packet, which crossed once: {seen}")
    # off the link: the internet is the gateway's business; the frame goes to the gateway's MAC
    os_end.send(ip4(ME, socket.inet_aton("8.8.8.8"), 17, udp(5001, 7, b"to the internet")))
    got = None
    for _ in range(40):
        pump(time.time() + 0.2)
        try:
            got = os_end.recv(2048)
            break
        except socket.timeout:
            pass
    check(got is not None and got[12:16] == socket.inet_aton("8.8.8.8") and got[28:] == b"to the internet" and seen["inet"] == 1, f"a packet for the internet goes to the gateway's MAC and its answer comes back: {seen}")
    # a spoofed source never leaves
    before = dict(seen)
    os_end.send(ip4(socket.inet_aton("10.9.9.9"), GW, 17, udp(5002, 7, b"spoof")))
    pump(time.time() + 1.5)
    check(seen == before, "a packet from the OS with another source address is not put on the line")
    st = ctl(f"{T}/phone/ctl", "member")
    check(st["os_in"] >= 2 and st["os_out"] >= 2 and st["drop_os"] >= 1, f"the adapter counts what it did: {st['os_in']} in, {st['os_out']} out, {st['drop_os']} refused")

    phone.terminate()
    check(phone.wait(timeout=10) == 0, "the engine stops when it is told, its thread joined, nothing left")
    lw("stop", "--sock", f"{T}/ctlhome")
    print("FAILED: " + "; ".join(fails) if fails else "all leafnet checks passed")
    sys.exit(1 if fails else 0)


main()
