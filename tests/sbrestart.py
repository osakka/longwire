# SPDX-License-Identifier: GPL-3.0-or-later
# Two nodes register with one-use tokens at a switchboard; it is stopped and started again (same seed, same state); they must be registered again, as the same numbers, without a person.
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node


def until(fn, timeout=20, step=0.5):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v: return v
        except Exception:
            pass
        time.sleep(step)
    return None


SBROOT = os.environ.get("SBROOT") or None		# another tree for the switchboard: SBROOT=../my-vpn-0569 runs the release srv-uk-01 runs under nodes of today


def main():
    ka = int(os.environ.get("KA", "25"))
    print(f"-- the switchboard is restarted under two nodes (keepalive {ka})  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    tokens = f"{T}/tokens"
    open(tokens, "w").write("tok-one\ntok-two\n")
    sbargs = ["--registry", "none", "--listen", "127.0.0.1:1877", "--tokens", tokens]
    S = Node("S", ns, "longwire-switchboard.hibr", sbargs, root=SBROOT)
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, tok):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--token", tok, "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", str(ka), "--answer", "auto"])

    A = node("A", 4001, "tok-one")
    B = node("B", 4002, "tok-two")
    na = until(lambda: A.status()["number"], 20)
    nb = until(lambda: B.status()["number"], 20)
    check(bool(na and nb), f"both nodes register: {na} {nb}")
    S.stop()
    until(lambda: S.proc.poll() is not None, 20)
    time.sleep(float(os.environ.get("DOWN", "3")))
    S2 = Node("S", ns, "longwire-switchboard.hibr", sbargs, root=SBROOT)
    until(lambda: os.path.exists(S2.sock), 15)
    t0 = time.time()
    ok = until(lambda: S2.status().get("registrations", 0) >= 2 and A.status()["number"] == na and B.status()["number"] == nb, 240, 1.0)
    check(ok is not None, f"after the restart both register with the switchboard again as the same numbers (took {time.time() - t0:.0f} s): registrations={S2.status().get('registrations')}")
    A.cmd("dial", nb)
    got = until(lambda: any(p["state"] == "up" for p in A.status()["peers"]), 40)
    check(got is not None, "and a call from one to the other connects")
    L.finish("sbrestart")


main()
