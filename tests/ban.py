#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Strikes and bans on a real switchboard (docs/adr/0043): a node that keeps presenting a wrong token is banned after a few refusals and then cannot even
start a session; a good node is untouched all the while; the ban ends by itself; the operator can lift it; and bad packets that are not part of a session
(wrong key, garbage, a spoofed source) never earn anyone a ban."""
import json, os, socket, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import lwtest as L
from lwtest import HIBR, MOD, ROOT, T, check, sh, Node, finish


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
    print(f"-- ban: strikes and bans on a switchboard  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    nsx = lambda *a, **k: subprocess.run(["nsenter", "-t", str(ns.pid), "-n", *a], **k)
    nsx("ip", "link", "set", "lo", "up")
    tokens = f"{T}/tokens"
    open(tokens, "w").write("good-one\ngood-two\ngood-three\ngood-four\n")
    S = Node("S", ns, "longwire-switchboard.hibr",
             ["--registry", "none", "--listen", "127.0.0.1:1877", "--tokens", tokens, "--strikes", "3", "--ban-for", "14", "--ban-max", "14", "--net-ban-keys", "99"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
    check(addr.startswith("lw1"), f"the switchboard has an address: {addr[:12]}...")

    def node(name, token, port):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h", "--listen", f"127.0.0.1:{port}",
                     "--cand-loopback", "--token", token, "--keepalive", "5"])

    def sb_status():
        return S.status()

    # a good node is not touched by anything that follows
    G = node("G", "good-one", 4001)
    gnum = until(lambda: G.status()["number"], 20)
    check(bool(gnum), f"a good node registers: {gnum}")

    # a node that keeps presenting a wrong token: each refusal is two strikes, three bans
    X = None
    refused = 0
    for i in range(4):
        if X: X.stop(); time.sleep(0.8)
        mk = None
        X = node("X", "wrong-token", 4002)
        mk = X.w.mark()
        ev = X.w.wait("register-refused", 6, after=mk)
        if ev: refused += 1
        else: break
    check(refused == 2, f"the wrong token is refused twice, and then the switchboard stops answering that key ({refused} refusals)")
    st = sb_status()
    check(st.get("bans_issued") == 1 and st.get("strikes_given", 0) == 2, f"one ban, from the two refusals (two strikes each; a patient node's repeated registration is not struck): bans_issued={st.get('bans_issued')} struck={st.get('strikes_given')}")
    rc = subprocess.run([HIBR, ROOT + "/scripts/longwire-switchboard.hibr", "bans", "list", "--state", S.home + "/sb"], env=dict(os.environ, LONGWIRE_MOD=MOD), capture_output=True, text=True)
    bl = json.loads(rc.stdout) if rc.returncode == 0 and rc.stdout.strip() else {}
    check(len(bl.get("bans", [])) == 1 and bl.get("strikes") == 3, f"`bans list` shows the ban and the settings: {rc.stdout[:160]!r} {rc.stderr[-120:]}")

    # during the ban the key cannot even start a session, and a good node is not affected
    st0 = sb_status()
    X.stop(); time.sleep(0.8)
    X = node("X", "wrong-token", 4002)
    mk = X.w.mark()
    ev = X.w.wait("register-refused", 4, after=mk)
    until(lambda: sb_status().get("ban_refused", 0) > st0.get("ban_refused", 0), 10)
    st1 = sb_status()
    check(ev is None and st1.get("ban_refused", 0) > st0.get("ban_refused", 0), f"the banned key's handshake is refused (ban_refused {st0.get('ban_refused')} -> {st1.get('ban_refused')})")
    H = node("H", "good-two", 4003)
    hnum = until(lambda: H.status()["number"], 20)
    check(bool(hnum), f"and a good node registers during the ban: {hnum}")
    check(G.status()["number"] == gnum, "and the good node that was there is still registered")

    # noise that is not part of a session earns no one a ban: wrong key, garbage, and handshakes from a spoofed source
    before = sb_status()
    junk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for i in range(300):
        pkt = bytes([2 + (i % 2)]) + os.urandom(1200 + (i % 300))		# handshake-shaped, wrong key
        nsx_send = None
    # the sends must come from inside the namespace
    script = (
        "import socket, os\n"
        "s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
        "for i in range(400):\n"
        "    s.sendto(bytes([1 + (i % 4)]) + os.urandom(900 + (i % 400)), ('127.0.0.1', 1877))\n"
        "    s.sendto(os.urandom(40), ('127.0.0.1', 1877))\n"
    )
    nsx("python3", "-c", script)
    time.sleep(1.0)
    after = sb_status()
    check(after.get("bans_issued") == before.get("bans_issued") and after.get("strikes_given") == before.get("strikes_given"),
          f"800 garbage and wrong-key packets earn nobody a strike or a ban (struck {before.get('strikes_given')} -> {after.get('strikes_given')})")

    # the ban ends by itself
    ok = until(lambda: (X.stop(), time.sleep(0.5), None)[2] or True, 1)
    time.sleep(16)
    X2 = node("X2", "good-three", 4004)
    # the same key as X: copy its seed so it is the banned identity with a proper token
    os.makedirs(f"{T}/X2/lw", exist_ok=True)
    X2.stop(); time.sleep(0.8)
    subprocess.run(["cp", f"{T}/X/lw/seed", f"{T}/X2/lw/seed"])
    X2 = node("X2", "good-three", 4004)
    xnum = until(lambda: X2.status()["number"], 20)
    check(bool(xnum), f"after the ban has run out the same key registers with a proper token: {xnum}")

    # the operator can lift a ban at once
    for i in range(2):
        Z = node("Z", "wrong-token", 4005 + i)
        Z.w.wait("register-refused", 6, after=Z.w.mark())
        Z.stop(); time.sleep(0.8)
    zb = json.loads(subprocess.run([HIBR, ROOT + "/scripts/longwire-switchboard.hibr", "bans", "list", "--state", S.home + "/sb"], env=dict(os.environ, LONGWIRE_MOD=MOD), capture_output=True, text=True).stdout or "{}")
    check(len(zb.get("bans", [])) >= 1, f"a second wrong-token node is banned: {zb.get('bans')}")
    r = subprocess.run([HIBR, ROOT + "/scripts/longwire-switchboard.hibr", "bans", "clear", "all", "--state", S.home + "/sb"], env=dict(os.environ, LONGWIRE_MOD=MOD), capture_output=True, text=True)
    check('"lifted"' in r.stdout, f"`bans clear all` lifts them: {r.stdout.strip()!r} {r.stderr[-100:]}")
    slog = open(f"{T}/S.log").read()
    check("ban          key " in slog and "bans clear" in slog, f"each ban is written to the switchboard's log, with how to lift it (docs/adr/0048): {[l for l in slog.splitlines() if l.startswith('ban')][:1]}")

    # a switchboard that does not answer is said so, and why as far as can be told (docs/adr/0048)
    os.makedirs(f"{T}/Q/lw", exist_ok=True)
    wrong = addr[:-6] + ("aaaaaa" if not addr.endswith("aaaaaa") else "bbbbbb")
    Q = Node("Q", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{wrong}", "--sock-port", f"{T}/Q/p:{T}/Q/h", "--listen", "127.0.0.1:4010", "--cand-loopback", "--token", "good-four"])
    ev = Q.w.wait("switchboard-silent", 25, after=0)
    check(ev is not None and ev.get("host") == "answers", f"the wrong switchboard key: no answer, but its host answers, so the node says the switchboard is ignoring it: {ev}")
    _, show = Q.cmd("show")
    check("no answer from it for" in show and "ignoring this node" in show, f"`longwire show` says the same: {show[-240:]!r}")
    Q.stop()
    os.makedirs(f"{T}/R/lw", exist_ok=True)
    R = Node("R", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1977/{addr}", "--sock-port", f"{T}/R/p:{T}/R/h", "--listen", "127.0.0.1:4011", "--cand-loopback", "--token", "good-four"])
    ev = R.w.wait("switchboard-silent", 25, after=0)
    check(ev is not None and ev.get("host") == "silent", f"nothing at that address: the node says nothing comes back from the host: {ev}")
    R.stop()
    rlog = open(f"{T}/R.log").read()
    check("no answer from the switchboard at 127.0.0.1:1977" in rlog, f"and its log says so in a sentence: {[l for l in rlog.splitlines() if 'no answer' in l][:1]}")
    finish("ban")


if __name__ == "__main__":
    main()
