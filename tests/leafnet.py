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


GW6 = bytes.fromhex("fe800000000000000000000000000001")		# the router's link-local address
GW6G = bytes.fromhex("20010db8000100000000000000000001")	# and one of its global ones, on the advertised prefix


def icmp6_sum(src, dst, msg):
    return csum(src + dst + struct.pack(">IBBBB", len(msg), 0, 0, 0, 58) + msg)


def ip6(src, dst, nh, payload, hop=64):
    return struct.pack(">IHBB", 0x60000000, len(payload), nh, hop) + src + dst + payload


def icmp6(src, dst, typ, body, hop=255, bad=False):
    msg = bytes([typ, 0, 0, 0]) + body
    c = icmp6_sum(src, dst, msg) ^ (0xFFFF if bad else 0)
    return ip6(src, dst, 58, msg[:2] + struct.pack(">H", c) + msg[4:], hop)


def ra_body(prefix, flags=0xC0, life=1800, valid=2592000, plen=64):
    return (bytes([64, 0]) + struct.pack(">H", life) + b"\0" * 8 + bytes([1, 1]) + GWMAC +
            bytes([3, 4, plen, flags]) + struct.pack(">II", valid, 604800) + b"\0" * 4 + prefix + b"\0" * (16 - len(prefix)))


def udp6(src, dst, sp, dp, data):
    u = struct.pack(">HHHH", sp, dp, 8 + len(data), 0) + data
    c = csum(src + dst + struct.pack(">IBBBB", len(u), 0, 0, 0, 17) + u) or 0xFFFF
    return u[:6] + struct.pack(">H", c) + u[8:]


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

    def start_phone(rounds, via=None):
        return subprocess.Popen([os.path.abspath("build/leafnet"), f"{T}/phone", via or f"{T}/bundlehome@127.0.0.1:41701", str(eng_end.fileno()), str(rounds)], pass_fds=(eng_end.fileno(),), stdout=subprocess.PIPE, text=True)

    seen = {"discover": 0, "request": 0, "arp": 0, "echo": 0, "inet": 0, "rs": 0, "ns6": 0, "na6": 0, "echo6": 0}
    state = {"bound": None, "ra": bytes.fromhex("20010db800010000"), "tlla": None}

    def pump(until):
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
                        k = 0
                        while k < len(o) and o[k] != 255:
                            if o[k] == 12:
                                seen["host"] = o[k + 2:k + 2 + o[k + 1]].decode()
                            k += 2 + o[k + 1] if o[k] else 1
                        lan.sendto(dhcp_reply(f, 2), f"{T}/Hport")
                    elif t == 3:
                        seen["request"] += 1
                        lan.sendto(dhcp_reply(f, 5), f"{T}/Hport")
                elif et == 0x0806 and f[38:42] == GW and struct.unpack(">H", f[20:22])[0] == 1:
                    seen["arp"] += 1
                    lan.sendto(eth(f[6:12], GWMAC, 0x0806, struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2) + GWMAC + GW + f[22:28] + f[28:32]), f"{T}/Hport")
                elif et == 0x86dd and len(f) >= 54:
                    src6, dst6 = f[22:38], f[38:54]
                    if f[20] == 58 and f[54] == 133:			# a router solicitation: the router advertises its prefix
                        seen["rs"] += 1
                        lan.sendto(eth(f[6:12], GWMAC, 0x86dd, icmp6(GW6, src6, 134, ra_body(state["ra"])[0:])), f"{T}/Hport")
                    elif f[20] == 58 and f[54] == 135 and f[62:78] in (GW6, GW6G):	# who has the router: it does
                        seen["ns6"] += 1
                        lan.sendto(eth(f[6:12], GWMAC, 0x86dd, icmp6(f[62:78], src6, 136, bytes([0x60, 0, 0, 0]) + f[62:78] + bytes([2, 1]) + GWMAC)), f"{T}/Hport")
                    elif f[20] == 58 and f[54] == 136:
                        seen["na6"] += 1
                        state["tlla"] = f[78 + 2:78 + 8] if len(f) >= 86 and f[78] == 2 else None
                    elif f[20] == 17 and f[:6] == GWMAC:			# UDP for the router's MAC: echoed back from where it was sent to
                        sp, dp = struct.unpack(">HH", f[54:58])
                        seen["echo6"] += 1
                        lan.sendto(eth(f[6:12], GWMAC, 0x86dd, ip6(dst6, src6, 17, udp6(dst6, src6, dp, sp, f[62:]))), f"{T}/Hport")
                elif et == 0x0800 and f[:6] == GWMAC and f[14 + 9] == 17:    # a UDP packet for the gateway's MAC: echo it back from where it was sent to
                    ihl = (f[14] & 15) * 4
                    src, dst = f[14 + 12:14 + 16], f[14 + 16:14 + 20]
                    sp, dp = struct.unpack(">HH", f[14 + ihl:14 + ihl + 4])
                    data = f[14 + ihl + 8:]
                    seen["echo" if dst == GW else "inet"] += 1
                    lan.sendto(eth(f[6:12], GWMAC, 0x0800, ip4(dst, src, 17, udp(dp, sp, data))), f"{T}/Hport")
            if state["bound"] is None:
                try:
                    m = ctl(f"{T}/phone/ctl", "member")
                    if m.get("member") == "bound":
                        state["bound"] = m
                except Exception:
                    pass

    def wait_bound(what):
        state["bound"] = None
        t0 = time.time()
        while state["bound"] is None and time.time() < t0 + 45:
            pump(time.time() + 0.5)
        check(state["bound"] is not None, f"{what}: on the line, and the home LAN's DHCP gave it an address ({int(time.time() - t0)} s)")
        return state["bound"]

    def exchange(payload, dst, what):
        os_end.send(ip4(ME, dst, 17, udp(5000, 7, payload)))
        got = None
        for _ in range(40):
            pump(time.time() + 0.2)
            try:
                got = os_end.recv(2048)
                break
            except socket.timeout:
                pass
        check(got is not None and got[12:16] == dst and got[16:20] == ME and got[9] == 17 and got[28:] == payload and csum(got[:20]) == 0, f"{what}: {got[28:] if got else None}")

    phone = start_phone(2)
    check(phone.stdout.readline().strip() == "attached", "the phone's engine runs on a thread and takes its tunnel from another (leaf_attach)")
    bound = wait_bound("the phone")
    if bound:
        check(bound["ip"] == "192.168.1.50" and bound["gateway"] == "192.168.1.1" and bound["prefix"] == 24 and bound["dns"] == ["192.168.1.1"] and bound["lease"] == 3600 and bound["tun"],
              f"the lease is the LAN's: {bound['ip']}/{bound['prefix']} via {bound['gateway']}, dns {bound['dns']}")
    # a UDP packet from the OS to the gateway: ARP, then the packet across, then the echo back to the OS
    exchange(b"hello from the phone", GW, "a packet from the phone's OS crosses the line, the LAN answers it, and the answer reaches the OS as the packet it is")
    check(seen.get("host") == "test-phone", f"the DHCP server was told the phone's name, made into a host name: {seen.get('host')}")
    check(seen["arp"] >= 1 and seen["echo"] == 1, f"...the phone asked who has the gateway (ARP) before the first packet, which crossed once: {seen}")
    # off the link: the internet is the gateway's business; the frame goes to the gateway's MAC
    exchange(b"to the internet", socket.inet_aton("8.8.8.8"), "a packet for the internet goes to the gateway's MAC and its answer comes back")
    check(seen["inet"] == 1, f"...once: {seen}")
    # a spoofed source never leaves
    before = dict(seen)
    os_end.send(ip4(socket.inet_aton("10.9.9.9"), GW, 17, udp(5002, 7, b"spoof")))
    pump(time.time() + 1.5)
    check(seen == before, "a packet from the OS with another source address is not put on the line")
    st = ctl(f"{T}/phone/ctl", "member")
    check(st["os_in"] >= 2 and st["os_out"] >= 2 and st["drop_os"] >= 1, f"the adapter counts what it did: {st['os_in']} in, {st['os_out']} out, {st['drop_os']} refused")

    # IPv6 (docs/adr/0098): an address from the router's advertisement, neighbour discovery, and packets both ways
    import ipaddress
    m6 = {}
    t0 = time.time()
    while time.time() < t0 + 25 and not m6.get("ip6"):
        pump(time.time() + 0.5)
        m6 = ctl(f"{T}/phone/ctl", "member")
    mac = bytes.fromhex(m6["mac"].replace(":", ""))
    want6 = ipaddress.IPv6Address(bytes.fromhex("20010db800010000") + bytes([mac[0] ^ 2]) + mac[1:3] + b"\xff\xfe" + mac[3:6])
    check(m6.get("ip6") == str(want6) and m6["prefix6"] == 64 and m6["gateway6"] == "fe80::1", f"the router advertised 2001:db8:1::/64: the phone made its address from the prefix and its MAC (SLAAC): {m6.get('ip6')}/{m6.get('prefix6')} via {m6.get('gateway6')}")
    check(seen["rs"] >= 1, f"...after asking for it (router solicitation): {seen['rs']}")
    me6 = want6.packed

    def exchange6(payload, dst, what, expect=True):
        os_end.send(ip6(me6, dst, 17, udp6(me6, dst, 5100, 7, payload)))
        got = None
        for _ in range(40):
            pump(time.time() + 0.2)
            try:
                got = os_end.recv(2048)
                break
            except socket.timeout:
                pass
        ok = got is not None and got[0] >> 4 == 6 and got[8:24] == dst and got[24:40] == me6 and got[6] == 17 and got[48:] == payload
        if ok:
            u = got[40:]
            ok = csum(got[8:40] + struct.pack(">IBBBB", len(u), 0, 0, 0, 17) + u) == 0
        check(ok == expect, f"{what}: {got[48:] if got else None}")

    n0 = seen["echo6"]
    exchange6(b"six hello", bytes.fromhex("20010db8ffff00000000000000000007"), "a packet for another network goes through the router (its MAC came with the advertisement) and the echo reaches the OS")
    check(seen["echo6"] == n0 + 1 and seen["ns6"] == 0, f"...once, with no neighbour solicitation needed for the router: {seen['echo6'] - n0} and {seen['ns6']}")
    exchange6(b"on link", GW6G, "a packet for a host on the prefix asks who has it (neighbour solicitation), and its answer lets the packet go")
    check(seen["ns6"] >= 1, f"...the phone asked: {seen['ns6']}")
    # the router asks who has the phone's address
    nbefore = seen["na6"]
    sol = bytes([0, 0, 0, 0]) + me6 + bytes([1, 1]) + GWMAC
    lan.sendto(eth(m6["mac"] and mac, GWMAC, 0x86dd, icmp6(GW6, me6, 135, sol)), f"{T}/Hport")
    pump(time.time() + 1.5)
    check(seen["na6"] == nbefore + 1 and state["tlla"] == mac, f"the phone answers a neighbour solicitation for its address with its MAC: {seen['na6'] - nbefore} answer, {state['tlla'].hex() if state['tlla'] else None}")
    # what must not change the address
    for what, frame in (
            ("an advertisement with a bad checksum", icmp6(GW6, me6, 134, ra_body(bytes.fromhex("20010db800bad000")), bad=True)),
            ("one with a hop limit that says it came from beyond the link", icmp6(GW6, me6, 134, ra_body(bytes.fromhex("20010db800bad000")), hop=64)),
            ("one that does not come from a link-local address", icmp6(GW6G, me6, 134, ra_body(bytes.fromhex("20010db800bad000")))),
            ("a prefix that says it may not be made into an address (no A flag)", icmp6(GW6, me6, 134, ra_body(bytes.fromhex("20010db800bad000"), flags=0x80))),
            ("a prefix longer than 64 bits", icmp6(GW6, me6, 134, ra_body(bytes.fromhex("20010db800bad000"), plen=96)))):
        lan.sendto(eth(mac, GWMAC, 0x86dd, frame), f"{T}/Hport")
        pump(time.time() + 0.8)
        check(ctl(f"{T}/phone/ctl", "member")["ip6"] == str(want6), f"{what} does not change the address")
    gen0 = ctl(f"{T}/phone/ctl", "member")["gen"]
    lan.sendto(eth(mac, GWMAC, 0x86dd, icmp6(GW6, me6, 134, ra_body(bytes.fromhex("20010db800030000")))), f"{T}/Hport")
    pump(time.time() + 1.0)
    m7 = ctl(f"{T}/phone/ctl", "member")
    check(m7["ip6"].startswith("2001:db8:3:") and m7["gen"] == gen0 + 1, f"a good advertisement of another prefix makes a new address, and `gen` moves so that the app builds its tunnel again: {m7['ip6']} gen {gen0} -> {m7['gen']}")
    me6 = ipaddress.IPv6Address(m7["ip6"]).packed

    # the phone's system says the network changed: the engine tells its peers at once, and the line carries on
    check(ctl(f"{T}/phone/ctl", "road") == {"road": "kicked"}, "the `road` word makes the engine tell its peers the road changed")
    exchange(b"after the road changed", GW, "a packet crosses after it")
    # a peer that hangs up on the engine's reply must not end the process that holds it (SIGPIPE; the app's own process is the engine's)
    for _ in range(30):
        c = socket.socket(socket.AF_UNIX)
        c.connect(f"{T}/phone/ctl")
        c.sendall(b"status\n")
        c.close()
    time.sleep(0.5)
    check(ctl(f"{T}/phone/ctl", "member").get("member") == "bound", "thirty callers that hang up before the reply do not end the process or the engine")

    # turned off and on in the same process, as an app does it: nothing is left open, and the line comes back
    phone.send_signal(15)
    line = phone.stdout.readline().split()
    check(line[:1] == ["stopped"] and line[1] == line[2], f"the engine stops when it is told, its thread joined; no descriptor is left open ({line})")
    check(phone.stdout.readline().strip() == "attached", "it starts again in the same process")
    wait_bound("started again, with its state kept")
    exchange(b"after a restart", GW, "a packet crosses again")
    phone.send_signal(15)
    line = phone.stdout.readline().split()
    check(line[:1] == ["stopped"] and line[1] == line[2] and phone.wait(timeout=10) == 0, f"...and a second time: still no descriptor left open ({line})")

    # the app killed by the system: the control socket is left behind, and the next start must not be refused
    phone = start_phone(1)
    check(phone.stdout.readline().strip() == "attached", "a new process starts")
    wait_bound("the new process")
    phone.kill()
    phone.wait()
    check(os.path.exists(f"{T}/phone/ctl"), "killed hard, it leaves its control socket behind")
    phone = start_phone(1)
    check(phone.stdout.readline().strip() == "attached", "the next start is not refused by the socket left behind")
    wait_bound("after the kill")
    exchange(b"after a kill", GW, "a packet crosses")
    phone.send_signal(15)
    phone.wait(timeout=10)
    lw("stop", "--sock", f"{T}/ctlhome")

    # the way the app does it: a switchboard, a call made over the control socket, and a home that goes away and comes back while the phone holds its address
    time.sleep(1)
    lw("key", "new", f"{T}/seedsb")
    sba = lw("key", "address", f"{T}/seedsb").stdout.strip()
    r = lw("sb", "start", "--seed", f"{T}/seedsb", "--sock", f"{T}/ctlsb", "--listen", "127.0.0.1:41800", "--registry", "none", "--cand-loopback")
    check(r.returncode == 0 and sba.startswith("lw1"), f"a switchboard starts: {r.stderr[-200:]}")

    def start_home():
        return lw("start", "--seed", f"{T}/seedhome", "--sock", f"{T}/ctlhome", "--listen", "127.0.0.1:41701", "--sock-port", f"{T}/Hport:{T}/Hhost", "--switchboard", f"127.0.0.1:41800/{sba}",
                  "--cand-loopback", "--keepalive", "2", "--rekey", "4")

    check(start_home().returncode == 0, "the home node starts and registers")
    num = None
    t0 = time.time()
    while not num and time.time() < t0 + 20:
        try:
            num = ctl(f"{T}/ctlhome", "status").get("number")
        except Exception:
            pass
        time.sleep(0.3)
    check(bool(num), f"the home node has a number: {num}")
    phone = start_phone(1, f"sb:127.0.0.1:41800/{sba}")
    check(phone.stdout.readline().strip() == "attached", "the phone's engine starts with the switchboard's address, no peer")

    def phone_state():
        try:
            st = ctl(f"{T}/phone/ctl", "status")
            return st.get("switchboard"), st.get("call")
        except Exception:
            return None, None

    def call_home():
        t1 = time.time()
        while time.time() < t1 + 90:
            sbs, call = phone_state()
            if call == "up":
                return True
            if sbs == "dial tone" and call == "idle":
                try:
                    ctl(f"{T}/phone/ctl", "dial " + num)
                except Exception:
                    pass
            try:
                if ctl(f"{T}/ctlhome", "status").get("call") == "ringing":	# the home node's person (or its script) answers
                    ctl(f"{T}/ctlhome", "pickup")
            except Exception:
                pass
            pump(time.time() + 1.0)
        print("  gave up", int(time.time() - t1))
        try:
            hs = ctl(f"{T}/ctlhome", "status")
            print("  home:", hs.get("number"), hs.get("call"), hs.get("switchboard"), "phone:", phone_state(), [(p["number"], p["state"]) for p in ctl(f"{T}/phone/ctl", "status").get("peers", [])])
        except Exception as e:
            print("  status failed", e)
        return False

    import threading
    def watch(path, tag):
        try:
            w = socket.socket(socket.AF_UNIX); w.connect(path); w.sendall(b"watch\n")
            buf = b""
            while True:
                d = w.recv(4096)
                if not d: break
                buf += d
                while b"\n" in buf:
                    l, buf = buf.split(b"\n", 1)
                    try:
                        e = json.loads(l)
                        if tag.startswith("home") and e.get("event") == "ring":	# the home node's script: auto answer (here, the test is the script)
                            ctl(f"{T}/ctlhome", "pickup " + e["call"])
                    except Exception:
                        pass
        except Exception as e:
            print("  watch ended", tag, e)
    threading.Thread(target=watch, args=(f"{T}/phone/ctl", "phone"), daemon=True).start()
    threading.Thread(target=watch, args=(f"{T}/ctlhome", "home"), daemon=True).start()
    check(call_home(), "the phone, with a dial tone, calls the home number over its control socket, and the call is up")
    wait_bound("through the switchboard")
    exchange(b"through a switchboard", GW, "a packet crosses")
    lw("stop", "--sock", f"{T}/ctlhome")
    t1 = time.time()
    gone = None
    while gone is None and time.time() < t1 + 90:
        pump(time.time() + 1.0)
        sbs, call = phone_state()
        if call == "idle":
            gone = time.time() - t1
    check(gone is not None, f"home goes away: the phone's engine says the call is over, as the app needs to hear it ({int(gone or -1)} s)")
    check(ctl(f"{T}/phone/ctl", "member").get("member") == "bound", "...while it keeps its address (the tunnel and what runs over it are not torn down for a line that comes back)")
    time.sleep(2)
    check(start_home().returncode == 0, "home comes back")
    time.sleep(1)
    threading.Thread(target=watch, args=(f"{T}/ctlhome", "home2"), daemon=True).start()
    num2 = None
    t1 = time.time()
    while not num2 and time.time() < t1 + 20:
        try:
            num2 = ctl(f"{T}/ctlhome", "status").get("number")
        except Exception:
            pass
        time.sleep(0.3)
    num = num2 or num
    check(call_home(), f"the phone calls again and the call is up (home's number {'unchanged' if num2 == num else 'is ' + str(num2)})")
    exchange(b"after home came back", GW, "a packet crosses with the same address")
    phone.send_signal(15)
    phone.wait(timeout=10)
    lw("stop", "--sock", f"{T}/ctlhome")
    lw("stop", "--sock", f"{T}/ctlsb")
    print("FAILED: " + "; ".join(fails) if fails else "all leafnet checks passed")
    sys.exit(1 if fails else 0)


main()
