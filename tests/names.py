# SPDX-License-Identifier: GPL-3.0-or-later
# Names (ADR 0023). One switchboard that carries names and keeps a directory, one that forbids both, and nodes that
# announce a name, list it, or do neither. Everything runs on loopback in one namespace.
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
    print(f"-- names: caller ID and the directory  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    sh_ = lambda *a: subprocess.run(["nsenter", "-t", str(ns.pid), "-n", *a])
    sh_("ip", "link", "set", "lo", "up")

    def switchboard(name, port, *extra):
        sb = Node(name, ns, "longwire-switchboard.hibr", ["--listen", f"127.0.0.1:{port}", *extra])
        until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
        addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
        return sb, addr

    S1, a1 = switchboard("S1", 1877)
    S2, a2 = switchboard("S2", 1878, "--caller-id", "off", "--directory", "off")

    def node(name, port, sbaddr, sbport, *extra):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:{sbport}/{sbaddr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", *extra])
    weird = 'Osama\'s "laptop" \\ home'
    A = node("A", 4001, a1, 1877, "--answer", "auto", "--name", weird, "--listed")
    B = node("B", 4002, a1, 1877, "--answer", "ask")
    C = node("C", 4003, a1, 1877, "--answer", "auto", "--name", "Hidden Quiet", )
    D = node("D", 4004, a2, 1878, "--answer", "ask", "--name", "Forbidden", "--listed")
    E = node("E", 4005, a2, 1878, "--answer", "ask")

    num = {}
    for k, n in zip("ABCDE", (A, B, C, D, E)):
        num[k] = until(lambda n=n: n.status()["number"], 20)
    check(all(num.values()), f"five nodes register on two switchboards: {num}")
    addr = {k: subprocess.run([HIBR, ROOT_SCRIPT, "address"], env=n.env, capture_output=True, text=True).stdout.strip()
            for k, n in zip("ABCDE", (A, B, C, D, E))}
    check(all(a.startswith("lw1") for a in addr.values()), "every node can say its full address")
    check(B.status()["switchboard_offers"] == ["caller-id", "directory"] and D.status()["switchboard_offers"] == [],
          "a node knows what its switchboard offers: B sees caller-id and directory, D sees none")

    # --- caller ID -----------------------------------------------------------------
    mk = B.w.mark()
    A.cmd("dial", num["B"])
    ring = B.w.wait("ring", 15, after=mk)
    check(ring is not None and ring["name"] == weird, f"B's ring shows the name A announced, quotes and backslash intact: {ring and ring['name']!r}")
    check(ring is not None and ring["from"] == num["A"], "...next to the number, which is what identifies the caller")
    B.cmd("pickup")
    check(B.w.wait("connected", 20, after=mk) is not None, "the call connects")
    mk = A.w.mark()
    C_ring_mk = A.w.mark()
    A.cmd("hangup")
    time.sleep(1)
    mk = B.w.mark()
    C.cmd("dial", num["B"])
    ring = B.w.wait("ring", 15, after=mk)
    check(ring is not None and ring["name"] == "Hidden Quiet", "an unlisted node's name still shows on its calls: caller ID and listing are separate")
    B.cmd("reject")
    mk = D.w.mark()
    E.cmd("dial", num["D"])
    ring = D.w.wait("ring", 15, after=mk)
    check(ring is not None and ring["name"] == "", "a switchboard with --caller-id off drops the name")

    # --- the directory -------------------------------------------------------------
    rc, out = B.cmd("who", "laptop")
    r = json.loads(out.splitlines()[-1]) if rc == 0 else {}
    res = r.get("results", [])
    check(len(res) == 1 and res[0]["number"] == num["A"] and res[0]["name"] == weird and res[0]["key_id"] == A.status()["key_id"],
          f"who NAME finds the listed node by part of its name, with number and key id: {res}")
    rc, out = B.cmd("who", "hidden")
    check(rc == 0 and json.loads(out.splitlines()[-1])["results"] == [], "an unlisted node is not found by name")
    rc, out = B.cmd("who", addr["A"])
    check(rc == 0 and [x["number"] for x in json.loads(out.splitlines()[-1])["results"]] == [num["A"]], "a listed node is found by its full address")
    rc, out = B.cmd("who", addr["C"])
    check(rc == 0 and json.loads(out.splitlines()[-1])["results"] == [], "an unlisted node is not found by its full address either")
    rc, out = B.cmd("who", "xy")
    check(rc != 0 and "3 to" in out, f"a name to look for must be at least 3 characters: {out!r}")

    mk = A.w.mark()
    rc, out = B.cmd("dial", addr["A"])
    check(rc == 0, f"dial by full address is accepted: {out}")
    check(until(lambda: any(p["state"] == "up" and p["number"] == num["A"] for p in B.status()["peers"]), 20) is not None,
          "...and connects to the node that address belongs to (A answers automatically)")
    mk = B.w.mark()
    B.cmd("hangup")
    import socket
    for _ in range(8):                                      # the directory is not a free enumeration service
        s = socket.socket(socket.AF_UNIX); s.connect(B.sock); s.sendall(b"lookup burst-query\n"); s.recv(200); s.close()
    lim = B.w.wait("found", 3, pred=lambda e: e.get("limited") is True, after=mk)
    check(lim is not None, "a burst of lookups is told to slow down (1 a second, burst 5), not silently dropped")
    time.sleep(6)
    mk = B.w.mark()
    rc, out = B.cmd("dial", addr["C"])
    e = B.w.wait("call-failed", 10, after=mk)
    check(e is not None and "not listed" in e["reason"], f"dialing the full address of an unlisted node fails plainly: {e and e['reason']}")

    rc, out = D.cmd("who", "forb")
    check(rc != 0 and "no directory" in out, f"a switchboard with --directory off has none: {out!r}")
    rc, out = E.cmd("dial", addr["D"])
    check(rc != 0 and "no directory" in out, f"...and a full address cannot be dialed there: {out!r}")

    # --- a bad name never reaches the wire -----------------------------------------
    os.makedirs(f"{T}/F/lw", exist_ok=True)
    F = Node("F", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{a1}", "--sock-port", f"{T}/F/p:{T}/F/h",
                                        "--listen", "127.0.0.1:4006", "--name", "bad\tname"])
    time.sleep(2)
    check(F.proc.poll() is not None and "--name must be" in open(f"{T}/F.log").read(), "a name with a control character stops the node from starting, with a plain message")

    for n in (A, B, C, D, E):
        n.cmd("down")
    for sb in (S1, S2):
        subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {sb.sock}"], capture_output=True)


ROOT_SCRIPT = L.ROOT + "/scripts/longwire.hibr"
try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("names")
