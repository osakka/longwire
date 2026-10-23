# SPDX-License-Identifier: GPL-3.0-or-later
# M3 end-to-end test, stage 1: a LINE of four sites. Real kernel hosts on four LANs that are one subnet, spliced by four
# nodes that find each other through a switchboard script and join by *number*. Nobody is asked about a member that a
# member vouched for.
#
#   a1 --[sa]--+                     +--[sc]-- c1        all hosts 10.0.0.x/24
#   b1 --[sb]--+-- underlay + sw. ---+--[sd]-- d1
import os, re, signal, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import check, sh, nsh, nsrun


def ping(ns, ip, count=2, wait=2):
    r = nsrun(ns, ["ping", "-c", str(count), "-W", str(wait), "-i", "0.2", ip], timeout=count * wait + 10)
    m = re.search(r"(\d+) received", r.stdout)
    return int(m.group(1)) if m else 0


def until(fn, timeout=30, step=0.5):
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
    print(f"-- M3: a line of four sites  (workdir {L.T})", flush=True)
    sh("ip link set lo up")
    sh("ip link add brU type bridge"); sh("ip link set brU up")
    names = ["a", "b", "c", "d"]
    host = {k: L.mkns() for k in names}
    node = {k: L.mkns() for k in names}
    ss = L.mkns()
    for i, k in enumerate(names, 1):
        L.veth(host[k], "eth0", node[k], "lan")
        L.veth(node[k], "ul", None, f"r_{k}", bridge_y="brU")
        for ns in (host[k], node[k]):
            nsh(ns, "ip link set lo up")
            nsrun(ns, ["sh", "-c", "echo 1 > /proc/sys/net/ipv6/conf/all/disable_ipv6"])
        nsh(host[k], f"ip addr add 10.0.0.{i}/24 dev eth0")
        nsh(node[k], f"ip addr add 10.9.0.{i}/24 dev ul")
    L.veth(ss, "ul", None, "r_s", bridge_y="brU")
    nsh(ss, "ip link set lo up"); nsh(ss, "ip addr add 10.9.0.10/24 dev ul")

    sb = L.Node("S", ss, "longwire-switchboard.hibr", ["--listen", "10.9.0.10:1877"])
    until(lambda: os.path.exists(sb.home + "/sb/seed") and os.path.exists(sb.sock), 15)
    addr = subprocess.run([L.HIBR, "-c", f"mod load {L.MOD}; lw key address {sb.home}/sb/seed"], capture_output=True, text=True).stdout.strip()
    args = {}
    N = {}
    for i, k in enumerate(names, 1):
        args[k] = ["up", "--switchboard", f"10.9.0.10:1877/{addr}", "--splice", "lan", "--listen", f"10.9.0.{i}:4000",
                   "--keepalive", "3", "--rekey", "40"]
    for k in "abc":                                  # d joins later
        N[k] = L.Node("n" + k, node[k], "longwire.hibr", args[k])
    num = {}
    for k in "abc":
        num[k] = until(lambda k=k: N[k].status()["number"], 20)
    check(all(num[k] for k in "abc"), f"three nodes register: {num}")

    def settled(keys, timeout=30):
        """a box that has just joined a line listens for 3 s before it forwards anything (a second box on its segment
        would otherwise loop): wait until it is forwarding, as a person would before testing"""
        return until(lambda: all(N[k].status()["site"] == "active" for k in keys), timeout)

    # --- the first two: a rings b; b is asked and picks up; b founds the line ----------------------
    N["a"].cmd("dial", num["b"])
    ring = N["b"].w.wait("ring", 15)
    check(ring is not None and ring["admit"] is False, "a first call is an ordinary ring: b is asked (no admit yet)")
    N["b"].cmd("pickup")
    ok = until(lambda: N["a"].status()["line"] and N["b"].status()["line"] and N["a"].status()["line"]["up"] == 2, 20)
    check(bool(ok), "a and b are on one line: b (the callee) founded it and admitted a")
    sa, sbb = N["a"].status()["line"], N["b"].status()["line"]
    check(sa["id"] == sbb["id"] and sbb["founder"] is True and sa["founder"] is False, f"same line {sa['id']}; b is the founder")
    check(bool(settled("ab")), "after a short listen, both boxes are forwarding (a box that has just joined waits 3 s)")
    check(ping(host["a"], "10.0.0.2", 3) == 3, "a1 pings b1: two LANs, one subnet")

    # --- a third site: c rings a. a is the hub. c then calls b by itself, and nobody is asked about it ----
    mark_b = N["b"].w.mark()
    N["c"].cmd("dial", num["a"])
    ring = N["a"].w.wait("ring", 15)
    check(ring is not None and ring["admit"] is False, "c is new to the line: a is asked once, by the ordinary ring")
    N["a"].cmd("pickup")
    ok = until(lambda: all(N[k].status()["line"] and N[k].status()["line"]["up"] == 3 for k in "abc"), 40)
    check(bool(ok), "the mesh builds itself: a, b and c each reach both others, c having dialed b alone")
    r = N["b"].w.wait("ring", 5, after=mark_b)
    check(r is not None and r["admit"] is True, "b was rung by c with an admit from a member, and was never asked (the script picked up)")
    settled("abc")
    for x, y in (("a", 2), ("a", 3), ("b", 1), ("b", 3), ("c", 1), ("c", 2)):
        check(ping(host[x], f"10.0.0.{y}", 2) == 2, f"{x}1 pings 10.0.0.{y}")
    check(all(N[k].w.wait("line-complete", 30) is not None for k in "abc"), "every member reports line-complete")

    # --- broadcasts reach every site -------------------------------------------------------------
    rcv = {}
    for k in "bc":
        rcv[k] = subprocess.Popen(["nsenter", "-t", str(host[k].pid), "-n", "python3", "-I", "-c",
            "import socket\ns=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('',6767));s.settimeout(5)\n"
            "try: print(s.recv(100).decode())\nexcept Exception: print('nothing')"], stdout=subprocess.PIPE, text=True)
        L.procs.append(rcv[k])
    time.sleep(0.6)
    nsrun(host["a"], ["python3", "-I", "-c", "import socket\ns=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)\n"
                      "s.sendto(b'to everyone',('10.0.0.255',6767))"])
    got = {k: rcv[k].communicate(timeout=10)[0].strip() for k in "bc"}
    check(got == {"b": "to everyone", "c": "to everyone"}, f"a UDP broadcast from a1 reaches b1 and c1: {got}")

    # --- a fourth site joins through c, not a: the hub is whichever member is called ---------------
    N["d"] = L.Node("nd", node["d"], "longwire.hibr", args["d"])
    num["d"] = until(lambda: N["d"].status()["number"], 20)
    N["d"].cmd("dial", num["c"])
    ring = N["c"].w.wait("ring", 15)
    check(ring is not None and ring["admit"] is False, "d rings c (a different member): ordinary ring")
    N["c"].cmd("pickup")
    ok = until(lambda: all(N[k].status()["line"] and N[k].status()["line"]["up"] == 4 for k in "abcd"), 60)
    check(bool(ok), "d joined through c and the full mesh of four built itself, with no further prompt")
    settled("abcd")
    for x in "abcd":
        for y in range(1, 5):
            if "abcd"[y - 1] != x:
                if ping(host[x], f"10.0.0.{y}", 2) != 2:
                    check(False, f"{x}1 cannot reach 10.0.0.{y}")
    check(not [f for f in L.fails if "cannot reach" in f], "all twelve host pairs of a four-site line ping each other")

    # --- eviction: a evicts c ------------------------------------------------------------------------
    marks = {k: N[k].w.mark() for k in "abcd"}
    rc, out = N["a"].cmd("evict", num["c"])
    check(rc == 0 and "evicted" in out, f"a evicts c: {out}")
    check(N["c"].w.wait("evicted", 15, after=marks["c"]) is not None, "c is told it was evicted")
    for k in "bd":
        check(N[k].w.wait("member-evicted", 15, after=marks[k]) is not None, f"{k} records the eviction")
    ok = until(lambda: all(N[k].status()["line"] and len(N[k].status()["line"]["members"]) == 3 for k in "abd"), 20)
    check(bool(ok), "a, b and d now list three members; c is gone")
    check(N["c"].status()["line"] is None, "c holds no line any more")
    time.sleep(2)
    check(ping(host["c"], "10.0.0.1", 2, wait=1) == 0 and ping(host["a"], "10.0.0.3", 2, wait=1) == 0,
          "c's host and a's host can no longer reach each other")
    check(ping(host["a"], "10.0.0.2", 2) == 2 and ping(host["b"], "10.0.0.4", 2) == 2 and ping(host["d"], "10.0.0.1", 2) == 2,
          "the rest of the line is untouched: a-b, b-d and d-a still ping")
    mk = N["a"].w.mark()
    N["c"].cmd("dial", num["a"])
    r = N["a"].w.wait("call-failed", 15, after=mk)
    check(r is not None and "evicted" in r["reason"], f"c cannot dial back in: a refuses ({r and r['reason']})")

    # --- a member dies and is restarted: it rejoins on the admit it persisted, nobody is asked --------------
    mk_a = N["a"].w.mark()
    N["b"].proc.kill(); N["b"].proc.wait()
    for pid in L.workers_in(node["b"]):
        os.kill(pid, signal.SIGKILL)
    part = N["a"].w.wait("line-partial", 60, after=mk_a)
    check(part is not None, "while b is down, a reports the line is not a full mesh (line-partial)")
    mk_a = N["a"].w.mark()
    N["b"] = L.Node("nb", node["b"], "longwire.hibr", args["b"])
    ok = until(lambda: all(N[k].status()["line"] and N[k].status()["line"]["up"] == 3 for k in "abd"), 90)
    check(bool(ok), "b restarts and the mesh heals by itself, from the line state b persisted")
    settled("b")
    r = N["a"].w.wait("ring", 10, after=mk_a)
    check(r is not None and r["admit"] is True, "a was rung by the restarted b with its persisted admit and was not asked")
    check(ping(host["b"], "10.0.0.1", 3) == 3 and ping(host["b"], "10.0.0.4", 3) == 3, "b's host reaches a and d again")

    # --- leaving: d hangs up and leaves the line ---------------------------------------------------------
    mk = {k: N[k].w.mark() for k in "abd"}
    N["d"].cmd("hangup")
    ok = until(lambda: N["d"].status()["line"] is None and all(len(N[k].status()["line"]["members"]) == 2 for k in "ab"), 20)
    check(bool(ok), "d hangs up: it leaves the line, and a and b list two members")
    check(N["a"].w.wait("member-left", 5, after=mk["a"]) is not None, "a is told that d left")
    for n_ in N.values():
        try: n_.cmd("down")
        except Exception: pass
    sb.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("M3")
