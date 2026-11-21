# SPDX-License-Identifier: GPL-3.0-or-later
# lw-dhcp on a real veth: see tests/dhcpnet.sh.
import os, socket, struct, subprocess, sys, tempfile, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LWD = ROOT + "/build/lw-dhcp"
fails = []
GWMAC = bytes.fromhex("525400123456")
GW = bytes([192, 168, 77, 1])


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)


def sh(c):
    return subprocess.run(c, shell=True, capture_output=True, text=True)


def csum(b):
    if len(b) % 2: b += b"\0"
    s = sum(struct.unpack(">%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF


def ip4(src, dst, payload):
    h = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), 0, 0, 64, 17, 0, src, dst)
    return h[:10] + struct.pack(">H", csum(h)) + h[12:] + payload


class Lan(threading.Thread):
    """The LAN's DHCP server, on the other end of the veth: answers DISCOVER with an OFFER and REQUEST with an ACK, for the address it is told to give."""
    def __init__(self, ifname, lease, mask=b"\xff\xff\xff\x00"):
        super().__init__(daemon=True)
        self.s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
        self.s.bind((ifname, 0x0800))
        self.s.settimeout(0.3)
        self.lease, self.mask, self.on, self.seen, self.host, self.stop = lease, mask, False, [], None, False

    def run(self):
        while not self.stop:
            try:
                f = self.s.recv(2048)
            except socket.timeout:
                continue
            if len(f) < 300 or f[12:14] != b"\x08\x00" or f[23] != 17:
                continue
            ihl = (f[14] & 15) * 4
            if f[14 + ihl + 2:14 + ihl + 4] != b"\x00\x43":
                continue
            b = f[14 + ihl + 8:]
            opts = b[240:]
            t = 0
            k = 0
            while k < len(opts) and opts[k] != 255:
                if opts[k] == 0:
                    k += 1
                    continue
                if opts[k] == 53: t = opts[k + 2]
                if opts[k] == 12: self.host = opts[k + 2:k + 2 + opts[k + 1]].decode()
                k += 2 + opts[k + 1]
            self.seen.append(t)
            if not self.on or t not in (1, 3):
                continue
            mt = 2 if t == 1 else 5
            xid, chaddr = b[4:8], b[28:34]
            o = b"\x35\x01" + bytes([mt]) + b"\x36\x04" + GW + b"\x01\x04" + self.mask + b"\x33\x04" + struct.pack(">I", 3600) + b"\xff"
            bootp = struct.pack(">BBBB4sHH4s4s4s4s", 2, 1, 6, 0, xid, 0, 0x8000, b"\0" * 4, self.lease, b"\0" * 4, b"\0" * 4) + chaddr + b"\0" * 10 + b"\0" * 192 + b"\x63\x82\x53\x63" + o
            u = struct.pack(">HHHH", 67, 68, 8 + len(bootp), 0) + bootp
            self.s.send(b"\xff" * 6 + GWMAC + b"\x08\x00" + ip4(GW, b"\xff" * 4, u))


def addrs():
    return sh("ip -4 -o addr show dev lw7").stdout


def until(fn, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if fn():
            return True
        time.sleep(0.3)
    return False


def start(d, host="test-box"):
    pf, st = f"{d}/pid", f"{d}/state"
    r = subprocess.run([LWD, "--no-drop", "--hostname", host, "--pidfile", pf, "--state", st, "lw7"], capture_output=True, text=True)
    return r, pf, st


def main():
    print("-- lw-dhcp on a real veth", flush=True)
    sh("ip link set lo up")
    r = sh("ip link add lw7 type veth peer name lw7p && ip link set lw7 up && ip link set lw7p up")
    if r.returncode:
        print("skip: cannot make a veth here:", r.stderr.strip())
        return
    d = tempfile.mkdtemp(prefix="lwdhcp.")

    # the client starts before any server answers: it keeps asking
    lan = Lan("lw7p", bytes([192, 168, 77, 50]))
    lan.start()
    r, pf, st = start(d)
    check(r.returncode == 0 and os.path.exists(pf), f"it starts as a daemon and writes its pid file: rc={r.returncode} {r.stderr.strip()[:100]}")
    time.sleep(6)
    check(1 in lan.seen and "192.168.77.50" not in addrs(), f"with nobody answering it asks (DISCOVERs seen: {lan.seen.count(1)}) and has no address")
    check(lan.host == "test-box", f"and tells the server the machine's name: {lan.host}")
    lan.on = True
    check(until(lambda: "192.168.77.50/24" in addrs(), 30), f"when a server answers it has the address on the jack: {addrs().strip()[:100]}")
    check(open(st).read().strip() == "bound 192.168.77.50", f"and says so in its state file: {open(st).read().strip()}")
    routes = sh("ip -4 route show dev lw7").stdout
    check("default" not in routes and "192.168.77.0/24" in routes, f"the only route is the kernel's own for the address, no default route: {routes.strip()}")
    check(addrs().count("inet ") == 1, "and no other address")
    pid = int(open(pf).read())
    os.kill(pid, 15)
    check(until(lambda: "192.168.77.50" not in addrs() and not os.path.exists(pf), 10), "stopped, it takes its address off and leaves no pid file")

    # a lease that is not a LAN's is refused: a route over a public network would carry its traffic
    lan.lease = bytes([8, 8, 8, 50])
    r, pf, st = start(d)
    time.sleep(10)
    check("8.8.8.50" not in addrs() and "8.8.8" not in sh("ip -4 route show dev lw7").stdout, "a lease for 8.8.8.50 is not put on the jack, and there is no route over that network")
    os.kill(int(open(pf).read()), 15)
    until(lambda: not os.path.exists(pf), 10)

    # the address is replaced when the lease changes, and nothing is left of the old one
    lan.lease = bytes([192, 168, 77, 60])
    r, pf, st = start(d)
    check(until(lambda: "192.168.77.60/24" in addrs(), 30), "a lease for another address is taken")
    os.kill(int(open(pf).read()), 15)
    until(lambda: not os.path.exists(pf), 10)
    lan.stop = True
    print("FAILED: " + "; ".join(fails) if fails else "all dhcpnet checks passed")
    sys.exit(1 if fails else 0)


main()
