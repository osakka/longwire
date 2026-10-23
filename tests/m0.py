# SPDX-License-Identifier: GPL-3.0-or-later
# M0 end-to-end test driver. tests/m0.sh starts the nodes (through hibr); this
# talks to their frame ports and control sockets and sits between them as a
# hostile network: it can duplicate, corrupt and drop datagrams.
import json, os, socket, struct, sys, threading, time

T = sys.argv[1]
A_PORT, B_PORT, PROXY, C_PORT = 41001, 41002, 41010, 41003
fails = []

def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)

def ctl(name):
    s = socket.socket(socket.AF_UNIX); s.settimeout(3)
    s.connect(f"{T}/ctl{name}"); s.sendall(b"status\n")
    d = b""
    while True:
        c = s.recv(65536)
        if not c: break
        d += c
    return json.loads(d)

def usock(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try: os.unlink(path)
    except FileNotFoundError: pass
    s.bind(path); s.settimeout(0.3)
    return s

class Proxy(threading.Thread):
    """B dials the proxy; the proxy forwards to A. Replies come back the same way."""
    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 << 20)
        self.sock.bind(("127.0.0.1", PROXY)); self.sock.settimeout(0.2)
        self.mode = "clean"; self.b_addr = None; self.run_ = True
        self.seen = {"data": 0, "hs": 0}
    def run(self):
        n = 0
        while self.run_:
            try: d, src = self.sock.recvfrom(4096)
            except socket.timeout: continue
            to_a = src != ("127.0.0.1", A_PORT)
            if to_a: self.b_addr = src
            dst = ("127.0.0.1", A_PORT) if to_a else self.b_addr
            if dst is None: continue
            if d[:1] == b"\x04":
                self.seen["data"] += 1; n += 1
                if self.mode == "dup": self.sock.sendto(d, dst)
                if self.mode == "corrupt" and n % 4 == 0 and len(d) > 30:
                    d = d[:20] + bytes([d[20] ^ 1]) + d[21:]
            else:
                self.seen["hs"] += 1
            self.sock.sendto(d, dst)

def frame(dst, src, payload):
    return dst + src + b"\x08\x00" + payload

MAC_A = bytes.fromhex("020000000a01")
MAC_B = bytes.fromhex("020000000b01")
BCAST = b"\xff" * 6

def wait_up(deadline=15):
    t = time.time() + deadline
    while time.time() < t:
        try:
            if ctl("A")["peers"][0]["state"] == "up" and ctl("B")["peers"][0]["state"] == "up":
                return True
        except Exception: pass
        time.sleep(0.2)
    return False

class Collector(threading.Thread):
    """Reads a frame port continuously: a Unix datagram queue holds ~10 packets."""
    def __init__(self, sock):
        super().__init__(daemon=True)
        self.sock, self.q, self.lock = sock, [], threading.Lock()
        self.start()
    def run(self):
        self.sock.settimeout(0.05)
        while True:
            try: d = self.sock.recv(4096)
            except socket.timeout: continue
            except OSError: return
            with self.lock: self.q.append(d)

def drain(c):
    time.sleep(0.15)
    with c.lock:
        out, c.q[:] = list(c.q), []
    return out

px = Proxy(); px.start()
hostA, hostB = usock(f"{T}/Ahost"), usock(f"{T}/Bhost")
colA, colB = Collector(hostA), Collector(hostB)
portA, portB = f"{T}/Aport", f"{T}/Bport"

check(wait_up(), "A and B complete the hybrid handshake and reach 'up'")
a, b = ctl("A"), ctl("B")
check(a["peers"][0]["state"] == "up" and b["peers"][0]["state"] == "up", "both sides report the session up")

# --- the switch -----------------------------------------------------------
hostA.sendto(frame(BCAST, MAC_A, b"hello-broadcast"), portA)
time.sleep(0.3)
got = drain(colB)
check(got == [frame(BCAST, MAC_A, b"hello-broadcast")], "broadcast from A's port arrives once, intact, at B's port")

hostB.sendto(frame(MAC_A, MAC_B, b"unicast-to-A"), portB)
time.sleep(0.3)
got = drain(colA)
check(got == [frame(MAC_A, MAC_B, b"unicast-to-A")], "B->A frame arrives (B learned A's MAC from the broadcast)")

hostA.sendto(frame(MAC_B, MAC_A, b"unicast-to-B"), portA)
time.sleep(0.3)
check(drain(colB) == [frame(MAC_B, MAC_A, b"unicast-to-B")], "A->B unicast uses the learned MAC (no flood)")
check(ctl("A")["flood"] == 1, "exactly one flood so far on A (the broadcast)")

hostA.sendto(frame(MAC_A, bytes.fromhex("020000000a02"), b"local-local"), portA)
time.sleep(0.3)
check(drain(colB) == [], "a frame between two local MACs never crosses the line")

# --- throughput / integrity, both directions at once -------------------------
N = 600
for i in range(N):
    hostA.sendto(frame(MAC_B, MAC_A, struct.pack(">I", i) + os.urandom(900)), portA)
    hostB.sendto(frame(MAC_A, MAC_B, struct.pack(">I", i) + os.urandom(900)), portB)
    if i % 25 == 24: time.sleep(0.02)
time.sleep(0.8)
ra, rb = drain(colA), drain(colB)
check(len(ra) == N and len(rb) == N, f"{N} frames each way delivered (got {len(ra)} and {len(rb)})")
if len(ra) != N or len(rb) != N:
    for nm in "AB":
        d = ctl(nm); p = d["peers"][0]
        print(f"     debug {nm}: local_rx={d['local_rx']} local_tx={d['local_tx']} tx_frames={p['tx_frames']} rx_frames={p['rx_frames']} auth={p['drop_auth']} replay={p['drop_replay']} garbage={d['drop_garbage']} noroute={d['drop_noroute']}")
check(sorted(struct.unpack(">I", f[14:18])[0] for f in rb) == list(range(N)), "no loss, reorder-safe, no duplicates A->B")

# --- hostile network: duplicates must be dropped, once-only delivery -----------
px.mode = "dup"
before = ctl("B")["peers"][0]["drop_replay"] + ctl("A")["peers"][0]["drop_replay"]
for i in range(100):
    hostA.sendto(frame(MAC_B, MAC_A, struct.pack(">I", i) + b"x" * 200), portA)
time.sleep(0.6)
rb = drain(colB)
after = ctl("B")["peers"][0]["drop_replay"] + ctl("A")["peers"][0]["drop_replay"]
check(len(rb) == 100, f"duplicated datagrams: each of 100 frames delivered exactly once (got {len(rb)})")
check(after - before >= 100, f"replays counted and dropped ({after - before})")

# --- hostile network: tampered packets must fail authentication ----------------
px.mode = "corrupt"
before = ctl("B")["peers"][0]["drop_auth"] + ctl("A")["peers"][0]["drop_auth"]
for i in range(100):
    hostA.sendto(frame(MAC_B, MAC_A, struct.pack(">I", i) + b"y" * 200), portA)
    time.sleep(0.003)
time.sleep(0.6)
rb = drain(colB)
after = ctl("B")["peers"][0]["drop_auth"] + ctl("A")["peers"][0]["drop_auth"]
check(after - before >= 20, f"bit-flipped packets rejected by the AEAD tag ({after - before})")
check(len(rb) == 100 - (after - before), f"every packet either authenticated or dropped, none delivered corrupt (delivered {len(rb)})")
px.mode = "clean"

# --- rekeying while traffic flows ---------------------------------------------
r0 = ctl("A")["peers"][0]["rekeys"] + ctl("B")["peers"][0]["rekeys"]
sent = 0
t_end = time.time() + 14
while time.time() < t_end:
    hostA.sendto(frame(MAC_B, MAC_A, b"rk" + struct.pack(">I", sent) + b"z" * 100), portA); sent += 1
    time.sleep(0.01)
time.sleep(0.5)
got = drain(colB)
r1 = ctl("A")["peers"][0]["rekeys"] + ctl("B")["peers"][0]["rekeys"]
check(r1 - r0 >= 4, f"sessions rekeyed repeatedly under load (rekeys +{r1 - r0} over 14 s, --rekey 4)")
check(len(got) >= sent * 0.99, f"traffic survived the rekeys: {len(got)} of {sent} frames (a handful may drop in a tick)")

# --- an unknown peer is refused ------------------------------------------------
time.sleep(1)
c = ctl("C")
a = ctl("A")
check(c["peers"][0]["state"] in ("handshaking", "idle"), "node C (unknown to A) never reaches 'up'")
check(a["drop_garbage"] > 0, f"A counted C's handshakes as garbage ({a['drop_garbage']}) and did not answer")

# --- handshake floods and cookies ------------------------------------------------
import hashlib
def b2(data, key=None, size=32): return hashlib.blake2b(data, digest_size=size, key=key or b"").digest()
kid_a = bytes.fromhex(ctl("A")["key_id"])
mk = b2(b"mac1" + kid_a)
def crafted_init(i):
    m = os.urandom(2384)                                   # garbage body...
    m += b2(m, key=mk, size=16) + bytes(16)                # ...with a valid whole-message mac1
    out = []
    for fi, off in enumerate((0, 1212)):
        chunk = m[off:off + 1212]
        pkt = bytes([1]) + struct.pack("<H", i & 0xffff) + bytes([fi << 4 | 2]) + chunk
        pkt += b2(pkt, key=mk, size=16)                    # valid per-fragment tag (an attacker knows A's public key id)...
        out.append(pkt + bytes(16))                        # ...but no cookie: it never received one
    return out
us = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); us.settimeout(0.3)
cs0 = ctl("A")["cookies_sent"]
for i in range(400):
    for pkt in crafted_init(i): us.sendto(pkt, ("127.0.0.1", A_PORT))
time.sleep(0.8)
replies = []
try:
    while True: replies.append(us.recv(2048))
except socket.timeout: pass
sent = ctl("A")["cookies_sent"] - cs0
check(sent >= 300, f"a flood of 400 well-formed INITs without cookies: A answered {sent} with a cookie and did no handshake work")
check(replies and all(len(r) == 59 and r[0] == 3 for r in replies), f"...each reply is a 59-byte COOKIE ({len(replies)} received by the flooder)")
check(ctl("A")["peers"][0]["state"] == "up", "...and the existing session is unharmed")

# the test that matters: while the flood continues, a legitimate handshake (B's rekey, every 4 s) still completes
stop_flood = threading.Event()
def flood():
    j = 1000
    while not stop_flood.is_set():
        for pkt in crafted_init(j): us.sendto(pkt, ("127.0.0.1", A_PORT))
        j += 1
        time.sleep(0.003)
th = threading.Thread(target=flood, daemon=True); th.start()
got0 = ctl("B")["cookies_got"]; sent0 = ctl("A")["cookies_sent"]
ages, down, t_end = [], 0, time.time() + 14
while time.time() < t_end:
    a_, b_ = ctl("A")["peers"][0], ctl("B")["peers"][0]
    if a_["state"] != "up" or b_["state"] != "up": down += 1
    ages.append(b_["age"])
    time.sleep(0.25)
stop_flood.set(); th.join()
resets = sum(1 for x, y in zip(ages, ages[1:]) if y < x)      # B's session got younger: it was replaced
check(resets >= 2, f"during a sustained flood B still completed {resets} legitimate rekeys (A was under load, so B had to present a cookie)")
check(down == 0, f"...and the session was up at every one of {len(ages)} samples ({down} lapses)")
check(ctl("B")["cookies_got"] > got0 and ctl("A")["cookies_sent"] - sent0 > 500, f"B was handed a cookie ({ctl('B')['cookies_got'] - got0}) while A answered {ctl('A')['cookies_sent'] - sent0} flood fragments with cookies")

px.run_ = False
print("\nFAILED: " + "; ".join(fails) if fails else "\nall M0 python checks passed")
sys.exit(1 if fails else 0)
