# SPDX-License-Identifier: GPL-3.0-or-later
import json, socket, subprocess, sys, threading, time

T, HIBR, MOD = sys.argv[1:4]
DELAY = 0.8
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

def up():
    try: return ctl("A")["peers"][0]["state"] == "up" and ctl("B")["peers"][0]["state"] == "up"
    except Exception: return False

class Proxy(threading.Thread):
    """Listens on `port`, forwards everything to `dst` a second late."""
    def __init__(self, port, dst):
        super().__init__(daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.s.bind(("127.0.0.1", port))
        self.dst, self.q, self.out = dst, [], None     # `out` is the other proxy's socket, so replies come back to it
    def run(self):
        while True:
            d, _ = self.s.recvfrom(4000)
            self.q.append((time.time() + DELAY, d))
    def pump(self):
        while True:
            while self.q and self.q[0][0] <= time.time():
                self.out.sendto(self.q.pop(0)[1], ("127.0.0.1", self.dst))
            time.sleep(0.005)

def lw(*a):
    subprocess.run([HIBR, "-c", "mod load " + MOD + "; lw " + " ".join(a)], check=True, capture_output=True)

pa, pb = Proxy(41103, 41102), Proxy(41104, 41101)
pa.out, pb.out = pb.s, pa.s
pa.start(); pb.start()
threading.Thread(target=pa.pump, daemon=True).start(); threading.Thread(target=pb.pump, daemon=True).start()      # A talks to :41103 (-> B), B to :41104 (-> A)
lw("start --seed", f"{T}/seedA --sock {T}/ctlA --listen 127.0.0.1:41101 --sock-port {T}/Aport:{T}/Ahost --peer {T}/bundleB@127.0.0.1:41103 --rekey 6 --keepalive 4")
time.sleep(0.4)
lw("start --seed", f"{T}/seedB --sock {T}/ctlB --listen 127.0.0.1:41102 --sock-port {T}/Bport:{T}/Bhost --peer {T}/bundleA@127.0.0.1:41104 --rekey 6 --keepalive 4")

end = time.time() + 15
while time.time() < end and not up(): time.sleep(0.2)
check(up(), "both nodes dialed at once and the link came up")
r0 = ctl("A")["peers"][0]["rekeys"] + ctl("B")["peers"][0]["rekeys"]
down = 0
for _ in range(120):                       # 24 s of rekeys, every 2 s, from both sides
    time.sleep(0.2)
    if not up(): down += 1
r1 = ctl("A")["peers"][0]["rekeys"] + ctl("B")["peers"][0]["rekeys"]
check(r1 - r0 >= 4, f"it keeps rekeying (rekeys +{r1 - r0} in 24 s)")
check(down <= 3, f"and never stalls: not 'up' in {down} of 120 samples")
sys.exit(1 if fails else 0)
