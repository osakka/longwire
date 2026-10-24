# SPDX-License-Identifier: GPL-3.0-or-later
import json, os, socket, subprocess, sys, time

T, HIBR, MOD = sys.argv[1:4]
fails = []

def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok: fails.append(what)

def lw(*a):
    return subprocess.run([HIBR, "-c", "mod load " + MOD + "; lw " + " ".join(a)], check=True, capture_output=True)

def ctl(name):
    s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(f"{T}/ctl{name}"); s.sendall(b"status\n")
    d = b""
    while True:
        c = s.recv(65536)
        if not c: break
        d += c
    return json.loads(d)

frame = b"\xff" * 6 + b"\x02\0\0\0\0\x01" + b"\x08\x00" + b"x" * 60
ROUNDS = 12
good = both = 0
for r in range(ROUNDS):
    d = f"{T}/r{r}"
    os.makedirs(d)
    for n in "AB":
        lw("key new", f"{d}/seed{n}"); open(f"{d}/bundle{n}", "wb").write(lw("key bundle", f"{d}/seed{n}").stdout.strip())
    host = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); host.bind(f"{d}/Ahost")
    got = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); got.bind(f"{d}/Bhost"); got.settimeout(0.2)
    back = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    pa, pb = 42000 + 2 * r, 42001 + 2 * r
    lw("start --seed", f"{d}/seedA --sock {T}/ctlA --listen 127.0.0.1:{pa} --sock-port {d}/Aport:{d}/Ahost --peer {d}/bundleB@127.0.0.1:{pb}")
    lw("start --seed", f"{d}/seedB --sock {T}/ctlB --listen 127.0.0.1:{pb} --sock-port {d}/Bport:{d}/Bhost --peer {d}/bundleA@127.0.0.1:{pa}")
    end = time.time() + 15
    while time.time() < end:
        try:
            if ctl("A")["peers"][0]["state"] == "up" and ctl("B")["peers"][0]["state"] == "up": break
        except Exception: pass
        time.sleep(0.1)
    time.sleep(1)
    ok = False
    for _ in range(5):
        host.sendto(frame, f"{d}/Aport")
        try:
            got.recv(2000); ok = True; break
        except socket.timeout:
            pass
    good += ok
    for n in "AB":
        subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {T}/ctl{n}"], capture_output=True)
    time.sleep(0.4)
check(good == ROUNDS, f"two nodes started together carried a frame within a second, {good} of {ROUNDS} times")
sys.exit(1 if fails else 0)
