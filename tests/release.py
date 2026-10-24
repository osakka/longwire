# SPDX-License-Identifier: GPL-3.0-or-later
# Number release: a number unheard for the release time is released, held back for as long again, and given back to
# its own key if that comes home in the meantime. Release time here is 8 seconds, not a year.
import os, subprocess, sys, time

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
    print(f"-- number release  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])

    def switchboard():
        sb = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877", "--release-after", "8s"])
        until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
        return sb
    S = switchboard()
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port):
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "2", "--answer", "auto"])
    A, B = node("A", 4001), node("B", 4002)
    nA = until(lambda: A.status()["number"], 20)
    nB = until(lambda: B.status()["number"], 20)
    check(nA and nB and nA != nB, f"two nodes register: {nA} {nB}")
    check(S.status()["numbers"] == 2, "the switchboard holds two numbers")

    B.stop()
    check(until(lambda: S.status()["numbers"] == 1, 30), "B goes quiet and its number is released after the release time")
    check(S.status()["numbers"] == 1 and S.status()["online"] <= 1, "A, still heard, keeps its number")
    B = node("B", 4002)
    check(until(lambda: B.status()["number"], 20) == nB, "B comes home within the hold and gets the same number back")
    check(S.status()["numbers"] == 2, "and the switchboard holds two again")

    B.stop()
    check(until(lambda: S.status()["numbers"] == 1, 30), "B leaves again and is released again")
    S.stop(); time.sleep(1.5)
    S = switchboard()
    check(S.status()["numbers"] == 1 or until(lambda: S.status()["numbers"] == 1, 5), "the release survives a restart of the switchboard")
    B = node("B", 4002)
    check(until(lambda: B.status()["number"], 20) == nB, "and so does the hold: B still gets its own number back")
    check(until(lambda: A.status()["number"] == nA and S.status()["numbers"] == 2, 10), "A kept its number through all of it")
    for n in (A, B, S):
        n.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("release")
