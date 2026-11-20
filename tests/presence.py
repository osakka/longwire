# SPDX-License-Identifier: GPL-3.0-or-later
# One node with a keep-alive of 120 s registers at a switchboard. Its presence there must not lapse: the switchboard counts a node online if it was heard within 80 s, and a node heard only as often as its keep-alive would show as gone between.
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node


def until(fn, timeout=20, step=0.3):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v: return v
        except Exception:
            pass
        time.sleep(step)
    return None


def main():
    ka = os.environ.get("KA", "120")
    secs = int(os.environ.get("SECS", "170"))
    print(f"-- presence: a node with a keep-alive of {ka} s stays online  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
    os.makedirs(f"{T}/A/lw", exist_ok=True)
    A = Node("A", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/A/p:{T}/A/h", "--listen", "127.0.0.1:4001", "--cand-loopback", "--keepalive", ka, "--answer", "auto"])
    num = until(lambda: A.status()["number"], 20)
    check(bool(num), f"the node registers: {num}")

    def entry():
        r = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw sb nodes --sock {S.sock}"], capture_output=True, text=True)
        return json.loads(r.stdout.strip().splitlines()[-1])["nodes"][0]

    t0 = time.time()
    worst, offline = 0, 0
    while time.time() - t0 < secs:
        e = entry()
        worst = max(worst, e["age"])
        offline += 0 if e["online"] else 1
        time.sleep(4)
    check(offline == 0, f"over {secs} s the switchboard never showed the node offline (oldest it was heard: {worst} s, the limit is 80 s)")
    L.finish("presence")


main()
