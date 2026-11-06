# SPDX-License-Identifier: GPL-3.0-or-later
# A line of 14 members: the founder admits each caller in turn, and the line message to the 12th, 13th and 14th is longer than one session message holds, so it travels in two parts.
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node

N = 14


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
    print(f"-- a line of {N} members  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    sb = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", "--answer", "auto", "--maxpeers", "24"])

    F = node("F", 4001)
    fnum = until(lambda: F.status()["number"], 20)
    nodes = [F]
    for i in range(1, N):
        x = node(f"N{i}", 4001 + i)
        nodes.append(x)
        until(lambda: x.status()["number"], 20)
        x.cmd("dial", fnum)
        got = until(lambda: len(F.status()["line"]["members"]) == i + 1, 30)
        if got is None:
            check(False, f"member {i + 1} joins the founder's line (the founder lists {len(F.status()['line']['members'])})")
            break
    else:
        check(True, f"all {N} members are on the founder's line")
        ok = until(lambda: all(x.status()["line"] and len(x.status()["line"]["members"]) == N for x in nodes), 60)
        check(ok is not None, f"and every one of them lists all {N}: " + str([len(x.status()["line"]["members"]) if x.status()["line"] else 0 for x in nodes]))
        sp = F.status()
        check(sp.get("ctl_split", 0) >= 1, f"the founder sent a line message in two parts ({sp.get('ctl_split')})")
        ss = sb.status()
        check(ss.get("ctl_split", 0) >= 1, f"and the switchboard sent rings that carry an admit in two parts, as the nodes said it may ({ss.get('ctl_split')}) (docs/adr/0068)")
    L.finish("bigline")


main()
