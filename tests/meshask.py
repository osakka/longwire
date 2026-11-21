# SPDX-License-Identifier: GPL-3.0-or-later
# F founds a line (answers automatically, as a box does). D, a desktop whose policy is `ask`, dials F and is let in. G, a second box, dials F and is let in. Then F and G are stopped and started
# again (their lines are kept on disk). D must end up with sessions to both without anyone picking up, and no ring it could not answer.
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


def main():
    print(f"-- a desktop that asks, a pair of boxes that restart  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"], root=os.environ.get("SBROOT") or None)  # SBROOT=../my-vpn-0569: the release srv-uk-01 runs
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, policy):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h", "--listen", f"127.0.0.1:{port}",
                                                 "--cand-loopback", "--keepalive", "5", "--answer", policy])

    def up(x):
        return sorted(p["number"] for p in x.status()["peers"] if p["state"] == "up")

    F = node("F", 4001, "auto")
    fnum = until(lambda: F.status()["number"], 20)
    D = node("D", 4002, "ask")
    dnum = until(lambda: D.status()["number"], 20)
    D.cmd("dial", fnum)
    check(until(lambda: len(up(D)) == 1, 30) is not None, "the desktop dials the founder and is let in (the founder answers by itself)")
    G = node("G", 4003, "auto")
    gnum = until(lambda: G.status()["number"], 20)
    G.cmd("dial", fnum)
    check(until(lambda: len(F.status()["line"]["members"]) == 3, 30) is not None, "a second box dials the founder and joins the line")
    # the desktop, which asks, now meets the second box: it is a member of the line, so the desktop lets it in itself
    got = until(lambda: len(up(D)) == 2, 90)
    rings = [e for e in D.w.events if e.get("event") == "ring"]
    check(got is not None, f"the desktop has sessions with both boxes without anyone picking up: {up(D)}, rings it saw: {[(e.get('from'), e.get('admit')) for e in rings]}")

    # both boxes restart, as a roll does it
    mark = D.w.mark()
    F.stop(); G.stop()
    until(lambda: F.proc.poll() is not None and G.proc.poll() is not None, 20)
    time.sleep(3)
    F = node("F", 4001, "auto")
    G = node("G", 4003, "auto")
    check(until(lambda: len(up(D)) < 2, 90) is not None, "the desktop notices the boxes are gone (its sessions with them time out)")
    t0 = time.time()
    both = until(lambda: len(up(D)) == 2, 150)
    rings = [e for e in D.w.events[mark:] if e.get("event") == "ring"]
    check(both is not None, f"after both boxes restart the desktop has both again by itself ({time.time() - t0:.0f} s)")
    check(all(e.get("admit") for e in rings), f"every ring it got was one it could answer itself (admit true): {[(e.get('from'), e.get('admit')) for e in rings]}")
    L.finish("meshask")


main()
