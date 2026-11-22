# SPDX-License-Identifier: GPL-3.0-or-later
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, nsh, Node


def main():
    print(f"-- a splice box whose card disappears  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    nsh(ns, "ip link set lo up")
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    end = time.time() + 15
    while time.time() < end and not os.path.exists(S.home + "/sb/seed"):
        time.sleep(0.2)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
    sh("ip link add vx type veth peer name vy")
    sh(f"ip link set vx netns {ns.pid}")
    nsh(ns, "ip link set vx name lan")
    nsh(ns, "ip link set lan up")
    sh("ip link set vy up")
    B = Node("B", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--splice", "lan", "--listen", "127.0.0.1:4001", "--answer", "known"])
    end = time.time() + 20
    num = None
    while time.time() < end and not num:
        try:
            num = B.status()["number"]
        except Exception:
            time.sleep(0.3)
    check(bool(num), f"the box is up on its card ({num})")
    mk = B.w.mark()
    sh("ip link del vy")
    e = B.w.wait("port-lost", 15, after=mk)
    check(e is not None and e.get("port") == "lan", f"the card goes: the box says so ({e})")
    end = time.time() + 15
    while time.time() < end and B.proc.poll() is None:
        time.sleep(0.3)
    check(B.proc.poll() is not None, "and ends, instead of spinning, for its service to start it again")
    L.finish("portlost")


main()
