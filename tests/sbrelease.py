# SPDX-License-Identifier: GPL-3.0-or-later
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, ROOT, T, check, sh, Node


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


def main():
    print(f"-- the operator releases a number  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    sbargs = ["--registry", "none", "--listen", "127.0.0.1:1877"]
    S = Node("S", ns, "longwire-switchboard.hibr", sbargs)
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                                                 "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5"])

    def release(num):
        r = subprocess.run([HIBR, ROOT + "/scripts/longwire-switchboard.hibr", "release", num], env=S.env, capture_output=True, text=True)
        return r.returncode, (r.stdout + r.stderr).strip()

    def registered(num):
        r = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw sb nodes --sock {S.sock}"], env=S.env, capture_output=True, text=True)
        return num.replace("-", "") in r.stdout.replace("-", "")

    A = node("A", 4001)
    na = until(lambda: A.status()["number"], 20)
    check(bool(na) and registered(na), f"a node registers ({na})")
    rc, out = release(na)
    check(rc != 0 and "online" in out, f"a number that is online is not released: {out}")
    rc, out = release("999-999-999")
    check(rc != 0 and "not registered" in out, f"a number that is not there says so: {out}")
    A.stop()
    got = until(lambda: release(na)[0] == 0, 120, 2)
    check(got is not None and not registered(na), "once its node is gone, the number is released and leaves the registry")
    S.stop()
    until(lambda: S.proc.poll() is not None, 20)
    S2 = Node("S", ns, "longwire-switchboard.hibr", sbargs)
    until(lambda: os.path.exists(S2.sock), 15)
    time.sleep(2)
    check(not registered(na), "after a restart of the switchboard it is still released")
    L.finish("sbrelease")


main()
