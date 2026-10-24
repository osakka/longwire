# SPDX-License-Identifier: GPL-3.0-or-later
# M1 end-to-end test: a switchboard script and node scripts, talking across a
# real (if tiny) internet built from unprivileged network namespaces:
#
#      site A 10.0.1.0/24 --[NAT .101]--+
#                                       +-- public 192.0.2.0/24 -- switchboard .10
#      site B 10.0.2.0/24 --[NAT .102]--+
#
# The NAT is a userspace router (below) on raw packet sockets: port-restricted
# cone ("cone") or symmetric ("sym"), so a direct path exists only if the hole
# punching really works. Run via tests/m1.sh, which wraps this in `unshare -Urn`.
import json, os, random, socket, struct, subprocess, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import ROOT, HIBR, MOD, T, check, sh, nsh, mac_of, mkns, Watcher, Node
from lwnat import aton, mac_b, csum, Site, Router, Collector

MODE = sys.argv[1] if len(sys.argv) > 1 else "cone"

# ---------------------------------------------------------------- the network
def damm_number(rnd):
    t = [[0,3,1,7,5,9,8,6,4,2],[7,0,9,2,1,5,4,8,6,3],[4,2,0,6,8,7,1,3,5,9],[1,7,5,0,9,8,3,4,2,6],[6,1,2,3,0,4,5,9,7,8],
         [3,6,7,4,2,0,9,5,8,1],[5,8,6,9,7,2,0,1,3,4],[8,9,4,5,3,6,2,0,1,7],[9,4,3,8,6,1,7,2,0,5],[2,5,8,1,4,3,6,7,9,0]]
    d = [rnd.randrange(1, 10)] + [rnd.randrange(10) for _ in range(7)]
    x = 0
    for v in d: x = t[x][v]
    return "".join(map(str, d)) + str(x)

def fmt(n): return f"{n[:3]}-{n[3:6]}-{n[6:]}"

def frame(dst, src, payload): return dst + src + b"\x08\x00" + payload

# ---------------------------------------------------------------- the test
def build_net(mode_a, mode_b):
    """site A (mode_a) and site B (mode_b) behind NATs, a public net with the switchboard host"""
    sh("ip link set lo up")
    open("/proc/sys/net/ipv4/ip_forward", "w").write("0")	# only the userspace NAT may move packets
    nsA, nsB, nsS = mkns(), mkns(), mkns()
    sites = []
    for key, ns, rif, vif, rip, vip, pub, mode in [("A", nsA, "rA", "vA", "10.0.1.1", "10.0.1.2", "192.0.2.101", mode_a),
                                                   ("B", nsB, "rB", "vB", "10.0.2.1", "10.0.2.2", "192.0.2.102", mode_b)]:
        sh(f"ip link add {rif} type veth peer name {vif}"); sh(f"ip link set {vif} netns {ns.pid}")
        sh(f"ip addr add {rip}/24 dev {rif}"); sh(f"ip link set {rif} up")
        nsh(ns, "ip link set lo up"); nsh(ns, f"ip addr add {vip}/24 dev {vif}"); nsh(ns, f"ip link set {vif} up")
        nsh(ns, f"ip route add default via {rip}")
        nsh(ns, f"ip neigh replace {rip} lladdr {mac_of(rif)} dev {vif} nud permanent")
        sites.append(Site(key, rif, rip, vip, mac_of(vif, ns), mac_of(rif), pub, mode))
    sh("ip link add rS type veth peer name vS"); sh(f"ip link set vS netns {nsS.pid}")
    sh("ip addr add 192.0.2.1/24 dev rS"); sh("ip link set rS up")
    nsh(nsS, "ip link set lo up"); nsh(nsS, "ip addr add 192.0.2.10/24 dev vS"); nsh(nsS, "ip link set vS up")
    nsh(nsS, "ip route add default via 192.0.2.1")
    for ip in ("192.0.2.1", "192.0.2.101", "192.0.2.102"):
        nsh(nsS, f"ip neigh replace {ip} lladdr {mac_of('rS')} dev vS nud permanent")
    router = Router(sites, "rS", mac_of("rS"), {"192.0.2.10": mac_of("vS", nsS)})
    router.start()
    return nsA, nsB, nsS, router

def main():
    rnd = random.Random(7)
    print(f"-- M1 over NAT: site A's NAT is 'cone', site B's is '{MODE}'  (workdir {T})", flush=True)
    nsA, nsB, nsS, router = build_net("cone", MODE)

    # --- the switchboard: a script, with token policy ---------------------------
    num_b = damm_number(rnd)
    num_e = damm_number(rnd)
    open(f"{T}/tokens", "w").write(f"tokA\ntokB {num_b}\ntokD\ntokE {num_e}\ntokF {num_b}\n")
    sb = Node("S", nsS, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "192.0.2.10:1877", "--tokens", f"{T}/tokens", "--rings-per-minute", "50"])
    for _ in range(100):
        if os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock): break
        time.sleep(0.1)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
    check(addr.startswith("lw1") and len(addr) == 55, f"the switchboard has a full address: {addr[:12]}...")

    def node(name, ns, ip, port, token, answer, extra=()):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        n = Node(name, ns, "longwire.hibr",
                 ["up", "--switchboard", f"192.0.2.10:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                  "--listen", f"{ip}:{port}", "--answer", answer, "--token", token, "--keepalive", "5", "--rekey", "8", "--sb-rekey", "8", *extra])
        n.frames = Collector(f"{T}/{name}/h")
        return n
    # D's pin store already claims E's (reserved) number belongs to some other key
    os.makedirs(f"{T}/D/lw", exist_ok=True)
    open(f"{T}/D/lw/known", "w").write(f"{num_e} {'ab' * 32}\n")
    A = node("A", nsA, "10.0.1.2", 4001, "tokA", "auto")
    B = node("B", nsB, "10.0.2.2", 4002, "tokB", "ask")
    D = node("D", nsA, "10.0.1.2", 4003, "tokD", "auto")
    E = node("E", nsB, "10.0.2.2", 4005, "tokE", "auto")
    C = node("C", nsA, "10.0.1.2", 4004, "tokA", "auto")        # tokA was spent by A: must be refused
    F = node("F", nsA, "10.0.1.2", 4006, "tokF", "auto")        # tokF reserves B's number, which B already holds

    def registered(n, timeout=20):
        end = time.time() + timeout
        while time.time() < end:
            try:
                num = n.status()["number"]
                if num: return {"number": num}
            except Exception: pass
            time.sleep(0.2)
    ra, rb, rd, re_ = registered(A), registered(B), registered(D), registered(E)
    check(bool(ra and rb and rd and re_), "A, B, D and E register through the switchboard's script policy")
    check(rb is not None and rb["number"] == fmt(num_b), f"a token can reserve a number: B got {rb and rb['number']} (wanted {fmt(num_b)})")
    rc = C.w.wait("register-refused")
    check(rc is not None and "token" in rc["reason"], f"a spent token is refused: {rc and rc['reason']}")
    st = sb.status()
    check(st["numbers"] == 4 and st["online"] == 4, f"switchboard counts 4 numbers, 4 online ({st['numbers']}, {st['online']})")
    sa = A.status()
    check(sa["reflexive"].startswith("192.0.2.101:"), f"A learns its public address from the switchboard: {sa['reflexive']}")
    check(sa["number"] == ra["number"], "A's status shows its number")

    # --- dialing mistakes -------------------------------------------------------
    rcd, out = A.cmd("dial", "123-456-789")
    check(rcd != 0 and "not a valid number" in out, "a number with a bad check digit is refused before anything is sent")
    ghost = fmt(damm_number(rnd))
    mk = A.w.mark(); A.cmd("dial", ghost)
    e = A.w.wait("call-failed", 8, after=mk)
    check(e is not None and e["reason"] == "unknown number", f"dialing an unregistered number: {e and e['reason']}")

    # --- a pinned number that suddenly has a different key ------------------------
    mk = D.w.mark(); D.cmd("dial", fmt(num_e))
    e = D.w.wait("key-changed", 15, after=mk)
    check(e is not None, "pin mismatch: D (pinned to another key for E's number) drops the call when E answers")
    time.sleep(0.5)
    check(D.status()["call"] == "idle" and not any(p["state"] == "up" for p in D.status()["peers"]), "...and no session was made")

    # --- the call ---------------------------------------------------------------
    mkA, mkB = A.w.mark(), B.w.mark()
    rcd, out = A.cmd("dial", rb["number"])
    check(rcd == 0, f"dial accepted: {out}")
    ring = B.w.wait("ring", 10, after=mkB)
    check(ring is not None and ring["from"] == ra["number"] and ring["known"] is False and len(ring["phrase"].split()) == 6,
          f"B rings: from {ring and ring['from']}, first call, phrase '{ring and ring['phrase']}'")
    time.sleep(1.2)
    check(B.w.wait("connected", 0.1, after=mkB) is None, "B's 'ask' policy waits for a person: nothing connects by itself")
    rcp, out = B.cmd("pickup")
    check(rcp == 0, f"pickup: {out}")
    ca = A.w.wait("connected", 20, after=mkA); cb = B.w.wait("connected", 20, after=mkB)
    check(bool(ca and cb), "both sides report connected through the NATs")
    if ca and cb:
        check(ca["phrase"] == cb["phrase"] == ring["phrase"], "the safety phrase is the same on both screens and on the ring")
        check(ca["ep"].startswith("192.0.2.102:") and cb["ep"].startswith("192.0.2.101:"),
              f"the path is NAT-to-NAT: A talks to {ca['ep']}, B to {cb['ep']}")
    s = router.stats
    check(s["A"]["out"] > 0 and s["B"]["in"] > 0 and s["A"]["in"] > 0, f"traffic really crossed both NATs {s}")

    # --- frames across the punched path -------------------------------------------
    MA, MB = bytes.fromhex("020000000a01"), bytes.fromhex("020000000b01")
    A.frames.drain(); B.frames.drain()
    ha = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); hb = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    N = 150
    for i in range(N):
        ha.sendto(frame(b"\xff" * 6 if i == 0 else MB, MA, struct.pack(">I", i) + os.urandom(500)), f"{T}/A/p")
        hb.sendto(frame(MA, MB, struct.pack(">I", i) + os.urandom(500)), f"{T}/B/p")
        if i % 10 == 9: time.sleep(0.03)
    ga, gb = A.frames.drain(), B.frames.drain()
    check(len(ga) == N and len(gb) == N, f"{N} frames each way over the punched path (A got {len(ga)}, B got {len(gb)})")

    # --- hang up; pins; decline; call again -----------------------------------------
    macs_up = A.status()["macs"]
    mkB = B.w.mark()
    A.cmd("hangup")
    e = B.w.wait("hangup", 10, after=mkB)
    check(e is not None and e["by"] == "them", "A hangs up; B is told")
    time.sleep(0.3)
    B.frames.drain(); ha.sendto(frame(MB, MA, b"x" * 50), f"{T}/A/p")
    check(B.frames.drain() == [], "after hangup nothing crosses")
    check(A.status()["call"] == "idle" and B.status()["call"] == "idle", "both nodes are idle again")
    check(macs_up >= 2 and A.status()["macs"] == 1, f"hanging up forgets the addresses learned behind that peer (MAC table {macs_up} -> {A.status()['macs']}: only A's own remains)")

    mkA, mkB = A.w.mark(), B.w.mark()
    A.cmd("dial", rb["number"])
    ring2 = B.w.wait("ring", 10, after=mkB)
    check(ring2 is not None and ring2["known"] is True, "second call: B knows this number now (pinned)")
    B.cmd("reject")
    e = A.w.wait("call-failed", 10, after=mkA)
    check(e is not None and e["reason"] == "declined", f"B declines; A hears: {e and e['reason']}")
    time.sleep(0.3)
    mkA, mkB = A.w.mark(), B.w.mark()
    A.cmd("dial", rb["number"])
    B.w.wait("ring", 10, after=mkB); B.cmd("pickup")
    check(bool(A.w.wait("connected", 20, after=mkA) and B.w.wait("connected", 20, after=mkB)), "a third call connects again after hangup and decline")
    # --- the node script dies and is restarted: it attaches to the worker that never stopped ------
    la_first = open(f"{T}/A.log").read()               # the log of the script about to die (the new one reuses the file)
    A.proc.kill()
    A.proc.wait()
    A2 = Node("A", nsA, "longwire.hibr", A.args)
    time.sleep(3)
    la2 = open(f"{T}/A.log").read()                   # the restarted script's own log: narrated live, no flush needed
    check("already running: attaching" in la2, "a restarted `longwire up` attaches to the running worker instead of failing")
    check("your number" in la2, "...and is told at once where registration stands (a new subscriber is replayed it)")

    # --- SIGTERM to `longwire up` stops its worker promptly (what a service manager sends) -------------
    F.proc.terminate()
    gone = False
    for _ in range(40):
        time.sleep(0.25)
        try: F.status()
        except Exception: gone = True; break
    check(gone and F.proc.poll() is not None, "SIGTERM to `longwire up` ends the script and stops its worker within seconds")

    # --- a wrong phrase must be recoverable: unpin makes the next call a first call again ---
    mkB = B.w.mark(); A.cmd("hangup"); B.w.wait("hangup", 10, after=mkB); time.sleep(0.5)
    check("hung up (us)" in open(f"{T}/A.log").read(), "...and its event loop is live: the restarted script narrates the hang-up it just made")
    rcu, out = B.cmd("unpin", ra["number"])
    check(rcu == 0 and "unpinned" in out, f"unpin: {out}")
    rcu2, out2 = B.cmd("unpin", ra["number"])
    check(rcu2 != 0 and "not pinned" in out2, "unpinning twice is an error, not a silent success")
    mkA, mkB = A.w.mark(), B.w.mark()
    A.cmd("dial", rb["number"])
    ring3 = B.w.wait("ring", 10, after=mkB)
    check(ring3 is not None and ring3["known"] is False, "after unpin, B treats the next call from A as a first call")
    B.cmd("reject"); A.w.wait("call-failed", 10, after=mkA); time.sleep(0.3)
    check(sb.status()["calls_ringing"] == 0, "the switchboard keeps no call once it has made the introduction")

    # --- the other end dies mid-call: the call must not stay "up" for ever ---------------
    mkA, mkB = A.w.mark(), B.w.mark()
    A.cmd("dial", rb["number"])
    B.w.wait("ring", 10, after=mkB); B.cmd("pickup")
    check(bool(A.w.wait("connected", 20, after=mkA) and B.w.wait("connected", 20, after=mkB)), "a fourth call connects")
    mkA = A.w.mark()
    ns_b = os.readlink(f"/proc/{nsB.pid}/ns/net")
    for pid in subprocess.run(["pgrep", "-x", "lw-worker"], capture_output=True, text=True).stdout.split():
        try:
            if os.readlink(f"/proc/{pid}/ns/net") == ns_b:         # the workers living in site B's namespace
                os.kill(int(pid), 9)
        except OSError:
            pass
    lost = A.w.wait("call-lost", 60, after=mkA)
    check(lost is not None and lost["number"] == rb["number"] and lost["we_called"] is True,
          f"B's worker is killed mid-call: A reports it ({lost and lost['reason']})")
    check(A.status()["call"] == "idle" and not A.status()["peers"], "...and A is idle, with no peer left behind")

    time.sleep(max(0, 38 - A.status()["uptime"]))          # let the switchboard session rekey several times (--rekey 8)
    regs, rks = A.status()["registrations"], A.status()["sb_rekeys"]
    check(rks >= 3 and regs >= rks, f"A re-registered after each of its {rks} switchboard rekeys ({regs} registrations) ...")
    seen_reg = [e.get("event") for e in A.w.events].count("registered")
    check(seen_reg <= 1, f"...and 'registered' was announced only for news ({seen_reg}: at most the first registration, never the {rks} re-registrations)")
    refused = [e for e in C.w.events if e.get("event") == "register-refused"]
    crk = C.status()["sb_rekeys"]
    check(0 < len(refused) <= crk + 1, f"a refused node asks once per session, not every 2 s: {len(refused)} refusals over {crk + 1} sessions")
    if L.fails:
        for n in (A, B, D, E):
            print("   events seen by the test on", n.name, [e.get("event") for e in n.w.events])
    # --- shut down; the scripts told the story ------------------------------------
    for n in (A2, B, D, E, C, sb):
        try: n.cmd("down") if n is not sb else subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {n.sock}"], capture_output=True)
        except Exception: pass
    time.sleep(0.8)
    lb = open(f"{T}/B.log").read()
    check("your number" in la_first and "connected to" in la_first, "A's script printed its number and the connection")
    check("is calling" in lb and "safety phrase" in lb and "first call from this number" in lb, "B's script announced the ring with the phrase")
    check("KEY CHANGED" in open(f"{T}/D.log").read(), "D's script shouted about the changed key")
    check("refused to register" in open(f"{T}/C.log").read(), "C's script reported the refusal")
    lf = open(f"{T}/F.log").read()
    check("refused to register" in lf and "reserved" in lf and "your number" not in lf,
          "a token whose reserved number is taken is refused, and F is NOT given a different number")

def main_sym():
    """Site B behind a symmetric NAT: hole punching can't work; the call must fail cleanly."""
    a_mode = "sym" if MODE == "symsym" else "cone"
    print(f"-- M1 over NAT: site A's NAT is '{a_mode}', site B's is 'sym'  (workdir {T})", flush=True)
    nsA, nsB, nsS, router = build_net(a_mode, "sym")
    sb = Node("S", nsS, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "192.0.2.10:1877"])
    for _ in range(100):
        if os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock): break
        time.sleep(0.1)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
    def node(name, ns, ip, port, answer):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        return Node(name, ns, "longwire.hibr", ["up", "--switchboard", f"192.0.2.10:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                    "--listen", f"{ip}:{port}", "--answer", answer, "--keepalive", "5"])
    A, B = node("A", nsA, "10.0.1.2", 4001, "auto"), node("B", nsB, "10.0.2.2", 4002, "auto")
    def registered(n, timeout=20):
        end = time.time() + timeout
        while time.time() < end:
            try:
                num = n.status()["number"]
                if num: return {"number": num}
            except Exception: pass
            time.sleep(0.2)
    ra, rb = registered(A), registered(B)
    check(bool(ra and rb), "both register (the switchboard is reachable through either NAT)")
    mkA, mkB = A.w.mark(), B.w.mark()
    A.cmd("dial", rb["number"])
    ea, eb = A.w.wait("call-failed", 25, after=mkA), B.w.wait("call-failed", 25, after=mkB)
    check(ea is not None and "no direct path" in ea["reason"], f"A is told plainly: {ea and ea['reason']}")
    check(eb is not None and "no direct path" in eb["reason"], f"B is told plainly: {eb and eb['reason']}")
    check(A.status()["call"] == "idle" and B.status()["call"] == "idle" and not any(p["state"] == "up" for p in A.status()["peers"] + B.status()["peers"]),
          "no half-open call is left behind")
    check(router.stats["A"]["filtered"] + router.stats["B"]["filtered"] > 0, f"the NAT really did block the punches: {router.stats}")
    for n in (A, B):
        n.cmd("down")
    subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {sb.sock}"], capture_output=True)

try:
    (main_sym if MODE in ("sym", "symsym") else main)()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("M1 (" + MODE + ")")
