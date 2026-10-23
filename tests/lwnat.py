# SPDX-License-Identifier: GPL-3.0-or-later
# A NAT in userspace, shared by the tests that need one: the kernel's own forwarding is off, so nothing crosses
# between a site and the public net without passing through Router, which is "cone" (port-restricted) or "sym".
import random, socket, struct, threading, time

def aton(ip): return socket.inet_aton(ip)
def mac_b(m): return bytes.fromhex(m.replace(":", ""))

def csum(b):
    s = sum(struct.unpack("!%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xffff) + (s >> 16)
    return ~s & 0xffff

class Site:
    def __init__(self, key, rif, rip, host_ip, host_mac, rmac, pub, mode):
        self.key, self.rif, self.rip, self.host_ip, self.host_mac = key, rif, rip, host_ip, mac_b(host_mac)
        self.rmac, self.pub, self.mode = mac_b(rmac), pub, mode
        self.out, self.inmap, self.allowed = {}, {}, {}

    def map_out(self, sport, dip, dport):
        k = sport if self.mode == "cone" else (sport, dip, dport)
        if k not in self.out:
            p = sport if self.mode == "cone" and sport not in self.inmap else random.randrange(30000, 60000)
            while p in self.inmap: p = random.randrange(30000, 60000)
            self.out[k], self.inmap[p], self.allowed[p] = p, sport, set()
        p = self.out[k]
        self.allowed[p].add((dip, dport))
        return p

class Router(threading.Thread):
    """A NAT between the sites and the public net, in userspace. The kernel's
    own forwarding is off, so nothing crosses without passing through here."""
    def __init__(self, sites, public_if, public_mac, hosts):
        super().__init__(daemon=True)
        self.sites, self.hosts = {s.key: s for s in sites}, {ip: mac_b(m) for ip, m in hosts.items()}
        self.pub_if, self.pub_mac = public_if, mac_b(public_mac)
        self.stats = {s.key: {"out": 0, "in": 0, "filtered": 0} for s in sites}
        self.socks = {}
        for name in [s.rif for s in sites] + [public_if]:
            s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
            s.bind((name, 0))
            self.socks[name] = s
        self.running = True

    def emit(self, ifname, smac, dmac, sip, dip, sport, dport, payload):
        udp = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
        hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), random.randrange(65536), 0x4000, 63, 17, 0, aton(sip), aton(dip))
        hdr = hdr[:10] + struct.pack("!H", csum(hdr)) + hdr[12:]
        self.socks[ifname].send(dmac + smac + b"\x08\x00" + hdr + udp)

    def route(self, sip, sport, dip, dport, payload):
        for site in self.sites.values():
            if dip == site.pub:                                   # inbound through a NAT
                inside = site.inmap.get(dport)
                if inside is None or (sip, sport) not in site.allowed[dport]:
                    self.stats[site.key]["filtered"] += 1          # nobody inside asked for this
                    return
                self.stats[site.key]["in"] += 1
                self.emit(site.rif, site.rmac, site.host_mac, sip, site.host_ip, sport, inside, payload)
                return
        if dip in self.hosts:
            self.emit(self.pub_if, self.pub_mac, self.hosts[dip], sip, dip, sport, dport, payload)

    def run(self):
        import select
        by_sock = {s: n for n, s in self.socks.items()}
        site_by_if = {s.rif: s for s in self.sites.values()}
        while self.running:
            r, _, _ = select.select(list(self.socks.values()), [], [], 0.2)
            for s in r:
                try:
                    data, addr = s.recvfrom(65535)
                except OSError:
                    return                  # the interface went away: the test is over
                if addr[2] == 4 or len(data) < 42 or data[12:14] != b"\x08\x00":   # our own, or not IPv4
                    continue
                ihl = (data[14] & 15) * 4
                if data[23] != 17 or struct.unpack("!H", data[20:22])[0] & 0x3fff:
                    continue
                sip, dip = socket.inet_ntoa(data[26:30]), socket.inet_ntoa(data[30:34])
                sport, dport, ulen = struct.unpack("!HHH", data[14 + ihl:14 + ihl + 6])
                payload = data[14 + ihl + 8:14 + ihl + ulen]
                site = site_by_if.get(by_sock[s])
                if site:                                           # from inside: translate the source
                    sport = site.map_out(sport, dip, dport)
                    sip = site.pub
                    self.stats[site.key]["out"] += 1
                self.route(sip, sport, dip, dport, payload)

class Collector(threading.Thread):
    def __init__(self, path):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.sock.bind(path); self.sock.settimeout(0.05)
        self.q, self.lock = [], threading.Lock()
        self.start()
    def run(self):
        while True:
            try: d = self.sock.recv(4096)
            except socket.timeout: continue
            except OSError: return
            with self.lock: self.q.append(d)
    def drain(self):
        time.sleep(0.2)
        with self.lock: out, self.q[:] = list(self.q), []
        return out
