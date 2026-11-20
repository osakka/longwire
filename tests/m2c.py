# SPDX-License-Identifier: GPL-3.0-or-later
# vpn-1 is a splice box on a LAN with a host a1; a remote site has a box and a host b1; they are on one line. Then vpn-2 starts on a1's LAN segment, registers with the same switchboard and dials vpn-1: it joins the
# line, the two boxes get their site secret from the line key and elect one active and one standby. All the while a1 broadcasts twenty tagged frames a second. What a bridge owes: a1 is never sent its own frames
# back, the other site gets each one a bounded number of times while the pair settles, and once it has, exactly once.
#
#   a1 10.0.0.1 --[brA]--+-- v1 --+                    +-- r --- b1 10.0.0.2
#                        +-- v2 --+-- underlay brU ---+-- sw (switchboard)
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import check, sh, nsh, nsrun

WANT = sys.argv[1]            # "lower": vpn-2's key id is lower than vpn-1's; "higher": the other way

GEN = r'''
import socket, sys, time
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW); s.bind(("eth0", 0))
src = bytes.fromhex(sys.argv[1]); end = time.time() + float(sys.argv[2]); i = 0
while time.time() < end:
    s.send(b"\xff" * 6 + src + b"\x90\x00" + ("TK%06d" % i).encode().ljust(60, b"\0")); i += 1; time.sleep(0.05)
'''
RECV = r'''
import socket, sys, time, json, collections
s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3)); s.bind(("eth0", 0)); s.settimeout(0.2)
mine = bytes.fromhex(sys.argv[1]); end = time.time() + float(sys.argv[2]); got = []
while time.time() < end:
    try: d, a = s.recvfrom(2048)
    except socket.timeout: continue
    if a[2] == socket.PACKET_OUTGOING or d[14:16] != b"TK": continue
    got.append((int(d[16:22]), time.time(), d[6:12] == mine))
print(json.dumps(got))
'''


def main():
    print(f"-- M2c: a second box joins a first on one segment, newcomer's key id {WANT}  (workdir {L.T})", flush=True)
    sh("ip link set lo up")
    sh("ip link add brA type bridge"); sh("ip link set brA up")
    sh("ip link add brU type bridge"); sh("ip link set brU up")
    a1, b1, v1, v2, r, sw = (L.mkns() for _ in range(6))

    def veth(ns_x, name_x, ns_y, name_y, bridge_y=None):
        sh("ip link add vx type veth peer name vy")
        for end, ns, name, br in (("vx", ns_x, name_x, None), ("vy", ns_y, name_y, bridge_y)):
            if ns is None:
                sh(f"ip link set {end} name {name}"); sh(f"ip link set {name} master {br}"); sh(f"ip link set {name} up")
            else:
                sh(f"ip link set {end} netns {ns.pid}")
                nsh(ns, f"ip link set {end} name {name}"); nsh(ns, f"ip link set {name} up")

    veth(a1, "eth0", None, "r_a1", bridge_y="brA")
    veth(v1, "lan", None, "r_v1", bridge_y="brA")
    veth(v2, "lan", None, "r_v2", bridge_y="brA")
    veth(v1, "ul", None, "r_v1u", bridge_y="brU")
    veth(v2, "ul", None, "r_v2u", bridge_y="brU")
    veth(r, "ul", None, "r_ru", bridge_y="brU")
    veth(sw, "ul", None, "r_swu", bridge_y="brU")
    veth(r, "lan", b1, "eth0")
    for ns in (a1, b1, v1, v2, r, sw):
        nsh(ns, "ip link set lo up")
        nsrun(ns, ["sh", "-c", "echo 1 > /proc/sys/net/ipv6/conf/all/disable_ipv6"])
    nsh(a1, "ip addr add 10.0.0.1/24 dev eth0"); nsh(b1, "ip addr add 10.0.0.2/24 dev eth0")
    for ns, ip in ((v1, "10.9.0.1"), (v2, "10.9.0.3"), (r, "10.9.0.2"), (sw, "10.9.0.9")):
        nsh(ns, f"ip addr add {ip}/24 dev ul")
    mac_a1 = L.mac_of("eth0", a1).replace(":", "")

    def kid(name):
        env = dict(os.environ, HOME=f"{L.T}/{name}", LONGWIRE_MOD=L.MOD, LW_DIR=f"{L.T}/{name}/lw")
        return subprocess.run([L.HIBR, L.ROOT + "/scripts/longwire.hibr", "id"], env=env, capture_output=True, text=True).stdout.strip()

    global name
    L.identity("v1")
    k1 = kid("v1")
    for i in range(40):                                       # a newcomer whose key id is lower, or higher, than the first box's
        name = f"v2_{i}"
        L.identity(name)
        if (kid(name) < k1) == (WANT == "lower"):
            break
    else:
        print("no identity of the wanted order in 40 tries"); sys.exit(2)

    S = L.Node("S", sw, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "10.9.0.9:1877"])
    deadline = time.time() + 20
    while time.time() < deadline and not os.path.exists(S.home + "/sb/seed"):
        time.sleep(0.2)
    addr = subprocess.run([L.HIBR, "-c", f"mod load {L.MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(nm, ns, ip, extra):
        return L.Node(nm, ns, "longwire.hibr", ["up", "--switchboard", f"10.9.0.9:1877/{addr}", "--splice", "lan", "--listen", f"{ip}:4000", "--keepalive", "3", "--rekey", "40",
                                                  "--answer", "auto", "--name", nm, *extra])

    def until(fn, timeout=30, step=0.3):
        end = time.time() + timeout
        while time.time() < end:
            try:
                v = fn()
                if v: return v
            except Exception:
                pass
            time.sleep(step)
        return None

    V1 = node("v1", v1, "10.9.0.1", [])
    R = node("r", r, "10.9.0.2", [])
    n1 = until(lambda: V1.status()["number"])
    nr = until(lambda: R.status()["number"])
    check(bool(n1 and nr), f"vpn-1 and the remote box register: {n1} {nr}")
    R.cmd("dial", n1)
    check(until(lambda: V1.status()["site"] != "alone" and len(V1.status()["line"]["members"]) == 2) is not None, "the remote box dials vpn-1: they are a line, and vpn-1 has its site secret from the line key")
    check(until(lambda: V1.status()["site"] == "active", 15) is not None, "vpn-1, alone on its segment, is active")

    total = 60
    ga = subprocess.Popen(["nsenter", "-t", str(a1.pid), "-n", "python3", "-I", "-c", GEN, mac_a1, str(total)])
    ra = subprocess.Popen(["nsenter", "-t", str(a1.pid), "-n", "python3", "-I", "-c", RECV, mac_a1, str(total + 2)], stdout=subprocess.PIPE, text=True)
    rb = subprocess.Popen(["nsenter", "-t", str(b1.pid), "-n", "python3", "-I", "-c", RECV, mac_a1, str(total + 2)], stdout=subprocess.PIPE, text=True)
    time.sleep(5)
    t_join = time.time()
    V2 = node(name, v2, "10.9.0.3", [])
    n2 = until(lambda: V2.status()["number"])
    check(bool(n2), f"vpn-2 registers with the same switchboard: {n2}")
    V2.cmd("dial", n1)
    roles = until(lambda: sorted([V1.status()["site"], V2.status()["site"]]) == ["active", "standby"] and sorted([V1.status()["site"], V2.status()["site"]]), 40)
    t_settled = time.time()
    check(roles == ["active", "standby"], f"vpn-2 joins vpn-1's line and the pair elects one active and one standby ({roles}, in {t_settled - t_join:.1f} s)")
    low = "v2" if (kid(name) < k1) else "v1"
    act = "v1" if V1.status()["site"] == "active" else "v2"
    check(act == low, f"the lower key id is the active one ({act}; lower is {low})")
    ga.wait()
    echoes = json_load(ra.communicate()[0])
    copies = json_load(rb.communicate()[0])

    def per_token(rows):
        d = {}
        for tok, t, self_ in rows:
            d.setdefault(tok, []).append(t)
        return d
    ea, eb = per_token(echoes), per_token(copies)
    check(not any(self_ for _, _, self_ in echoes), "no frame of a1's own is ever delivered back to it as its own")
    echo_all = sum(len(v) for v in ea.values())
    check(echo_all <= 3, f"a1 is sent its own broadcasts back {echo_all} times in all, through the whole join (a bridge owes none; a few while two boxes both forward and have not yet met)")
    check(max((len(v) for v in eb.values()), default=0) <= 2, f"the other site gets any broadcast at most twice, while the pair settles (most: {max((len(v) for v in eb.values()), default=0)})")
    late_e = sum(len(v) for tok, v in ea.items() for t in v if t > t_settled + 2)
    late_bad = sum(1 for tok, v in eb.items() if v and min(v) > t_settled + 2 and len(v) != 1)
    late_n = sum(1 for tok, v in eb.items() if v and min(v) > t_settled + 2)
    check(late_e == 0 and late_n > 100 and late_bad == 0, f"once the pair has settled: {late_n} broadcasts, each reaches the other site exactly once ({late_bad} otherwise), and none comes back to a1 ({late_e})")
    st = {nm: nd.status() for nm, nd in (("v1", V1), ("v2", V2))}
    check(any(s_["drop_standby"] > 0 for s_ in st.values()), f"the standby box kept quiet: {[(k, s_['site'], s_['drop_standby']) for k, s_ in st.items()]}")
    L.finish("m2c")


def json_load(s):
    try:
        return json.loads(s)
    except ValueError:
        return []


name = "v2_0"
main()
