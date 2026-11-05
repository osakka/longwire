# SPDX-License-Identifier: GPL-3.0-or-later
# The road under the node (docs/adr/0061): a machine that changes address or loses its network is found again at once, retries are quiet, and nothing is sent
# while there is no network. Real nodes and a real switchboard in network namespaces on a bridge, so the kernel's own routing, addresses and link state change.
#
#   root namespace: bridge br0 10.1.0.1/24, the "internet"          S 10.1.0.10 (switchboard)    A 10.1.0.2     B 10.1.0.3
#
# What is measured is the time for traffic to flow again, in each direction, after the change; the bounds are generous (seconds, where before it was up to three
# keepalives), and the number of datagrams a node sends while it has no network.
import json, os, socket, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, nsh, mkns, Node
from lwnat import Collector

KEEP = "5"						# keepalive: a session dies after three of them and a second (16 s), which keeps the test short


def until(fn, timeout=30, step=0.2):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v: return v
        except Exception:
            pass
        time.sleep(step)
    return None


def frame(dst, src, payload): return dst + src + b"\x08\x00" + payload


def main():
    print(f"-- the road: address changes, lost networks  (workdir {T})", flush=True)
    sh("ip link set lo up")
    sh("ip link add br0 type bridge"); sh("ip addr add 10.1.0.1/24 dev br0"); sh("ip link set br0 up")
    nsS, nsA, nsB = mkns(), mkns(), mkns()
    for ns, name, ip in ((nsS, "S", "10.1.0.10"), (nsA, "A", "10.1.0.2"), (nsB, "B", "10.1.0.3")):
        sh(f"ip link add h{name} type veth peer name e{name}")
        sh(f"ip link set e{name} netns {ns.pid}")
        sh(f"ip link set h{name} master br0"); sh(f"ip link set h{name} up")
        nsh(ns, "ip link set lo up"); nsh(ns, f"ip addr add {ip}/24 dev e{name}"); nsh(ns, f"ip link set e{name} up")
        nsh(ns, "ip route add default via 10.1.0.1")

    sb = Node("S", nsS, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "10.1.0.10:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, ns, port):
        os.makedirs(f"{T}/{name}/lw", exist_ok=True)
        n = Node(name, ns, "longwire.hibr", ["up", "--switchboard", f"10.1.0.10:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                                             "--listen", str(port), "--answer", "auto", "--keepalive", KEEP, "--rekey", "30"])
        n.frames = Collector(f"{T}/{name}/h")
        return n

    A, B = node("A", nsA, 4001), node("B", nsB, 4002)
    na = until(lambda: A.status()["number"], 30)
    nb = until(lambda: B.status()["number"], 30)
    check(bool(na and nb), f"two nodes register through the switchboard: {na} {nb}")
    mk = A.w.mark()
    A.cmd("dial", nb)
    check(A.w.wait("connected", 30, after=mk) is not None, "A calls B and they connect")
    MA, MB = bytes.fromhex("020000000a01"), bytes.fromhex("020000000b01")
    sa, sb_ = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM), socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

    def one_way(src_sock, path, dst_mac, src_mac, collector, tag, timeout):
        """Send one marked frame every 0.2 s toward `path` until one arrives at `collector`; the seconds it took, or None."""
        t0 = time.time()
        collector.drain()
        while time.time() - t0 < timeout:
            try: src_sock.sendto(frame(dst_mac, src_mac, tag + os.urandom(40)), path)
            except OSError: pass
            time.sleep(0.2)
            if any(f[14:14 + len(tag)] == tag for f in collector.drain()):
                return time.time() - t0
        return None

    # warm up: both ends have sent, so the MAC tables know each other
    one_way(sa, f"{T}/A/p", MB, MA, B.frames, b"warm", 10); one_way(sb_, f"{T}/B/p", MA, MB, A.frames, b"warm", 10)
    st = A.status()
    check(st["road"]["state"] == "up" and st["road"]["changes"] == 0, f"a node that has its road says up and has seen no change: {st['road']}")

    # --- 1. A changes address while idle: B must find A again without waiting for A's keepalive -------------------------------------------------------
    mkA, mkB = A.w.mark(), B.w.mark()
    nsh(nsA, "ip addr add 10.1.0.22/24 dev eA"); nsh(nsA, "ip addr del 10.1.0.2/24 dev eA"); nsh(nsA, "ip route replace default via 10.1.0.1")
    t = one_way(sb_, f"{T}/B/p", MA, MB, A.frames, b"b2a1", 40)		# B talks; A says nothing (idle)
    check(t is not None and t < 7.0, f"A changes address while idle: B's frames reach A again in {t and round(t, 1)} s (was up to three keepalives, 16 s here)")
    ev = A.w.wait("road-changed", 5, after=mkA)
    check(ev is not None and ev["road"] == "changed", f"A says its road changed: {ev}")
    t = one_way(sa, f"{T}/A/p", MB, MA, B.frames, b"a2b1", 15)
    check(t is not None and t < 4.0, f"and A's frames reach B: {t and round(t, 1)} s")

    # --- 2. the network goes away: the node goes quiet, and returns the moment it is back ------------------------------------------------------------------
    mkA = A.w.mark()
    nsh(nsA, "ip link set eA down")
    ev = A.w.wait("road-changed", 10, after=mkA, pred=lambda e: e["road"] == "down")
    check(ev is not None, f"the link goes down: A says there is no network: {ev}")
    check(until(lambda: A.status()["road"]["state"] == "down", 5) is not None, "and its status says down")
    time.sleep(float(KEEP) * 3 + 4)						# long enough for its sessions to die
    s1 = A.status()["road"]["sent"]
    time.sleep(30)								# half a minute with no network
    s2 = A.status()["road"]["sent"]
    check(s2 - s1 <= 2, f"half a minute with no network: A sends {s2 - s1} datagrams (it was a handshake every few seconds, to every peer)")
    mkA = A.w.mark()
    nsh(nsA, "ip link set eA up"); nsh(nsA, "ip route replace default via 10.1.0.1")
    t0 = time.time()
    ev = A.w.wait("road-changed", 10, after=mkA, pred=lambda e: e["road"] == "back")
    check(ev is not None, f"the link comes back: A says the network is back: {ev}")
    t = one_way(sb_, f"{T}/B/p", MA, MB, A.frames, b"b2a2", 40)
    check(t is not None and time.time() - t0 < 10.0, f"and the line is back in {round(time.time() - t0, 1)} s (it had been dead 30 s: no waiting out a back-off)")
    st = A.status()
    check(st["road"]["state"] == "up" and any(p["state"] == "up" for p in st["peers"]), f"A is up again with a peer: {st['road']}")
    if os.environ.get("ROAD_DEBUG"):
        print("   A events:", [e.get("event") + ":" + str(e.get("reason", e.get("road", ""))) for e in A.w.events[-14:]])
        print("   A peers:", [(p["number"], p["state"], p["path"]) for p in st["peers"]], "line:", st.get("line") and {k: st["line"][k] for k in ("up", "members", "partial")}, "registered:", st["switchboard"], "call:", st["call"])
        print("   A calls:", st["calls"], "A sb:", st["switchboard"], "road:", st["road"])
        sbs = sb.status(); print("   S:", {k: sbs[k] for k in sbs if k in ("online", "numbers", "rings", "put_through", "ended", "registrations", "calls_ringing")})
        sb_ = B.status(); print("   B calls:", sb_["calls"], "B peers:", [(p["number"], p["state"], p["path"]) for p in sb_["peers"]], "B events:", [e.get("event") + ":" + str(e.get("reason", "")) for e in B.w.events[-8:]])

    if os.environ.get("ROAD_ONLY") == "2":
        L.finish("road")
    # --- 3. backoff is capped, with jitter, so a dead peer is cheap ---------------------------------------------------------------------------------------
    s1 = A.status()["road"]["sent"]
    time.sleep(60)
    steady = A.status()["road"]["sent"] - s1					# what a minute costs with every peer up: keepalives and heartbeats
    nsh(nsB, "ip link set eB down")
    time.sleep(float(KEEP) * 3 + 6)
    time.sleep(70)
    s1 = A.status()["road"]["sent"]
    time.sleep(60)
    gone = A.status()["road"]["sent"] - s1
    check(gone - steady <= 6, f"a peer that stays gone for minutes adds {gone - steady} datagrams a minute to A's {steady} (a handshake in half a minute, not one every few seconds)")
    nsh(nsB, "ip link set eB up"); nsh(nsB, "ip route replace default via 10.1.0.1")
    t = one_way(sa, f"{T}/A/p", MB, MA, B.frames, b"a2b2", 90)
    check(t is not None, f"B comes back, and the line with it: {t and round(t, 1)} s")
    L.finish("road")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:
        import traceback; traceback.print_exc()
        check(False, f"crashed: {e!r}")
        L.finish("road")
