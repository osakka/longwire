# SPDX-License-Identifier: GPL-3.0-or-later
# NAT-PMP: a node whose default gateway answers gets a mapped address and port; one told not to asks nothing.
# The gateway here is a small fake on a veth in this namespace; the node is in another.
import os, socket, struct, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import T, check, sh, nsh, Node

PUBLIC, EXTERNAL = "203.0.113.9", 45678
seen = []          # (internal port, suggested port, lifetime) of every mapping request
asked0 = []        # op 0 requests


def gateway(refuse):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("10.9.9.1", 5351))
    def run():
        while True:
            d, a = s.recvfrom(100)
            if len(d) == 2 and d[1] == 0:
                asked0.append(a)
                s.sendto(struct.pack("!BBHI4s", 0, 128, 2 if refuse else 0, 1, socket.inet_aton(PUBLIC)), a)
            elif len(d) == 12 and d[1] == 1:
                i, sg, life = struct.unpack("!HHI", d[4:12])
                seen.append((i, sg, life))
                s.sendto(struct.pack("!BBHIHHI", 0, 129, 2 if refuse else 0, 1, i, EXTERNAL, life), a)
    threading.Thread(target=run, daemon=True).start()
    return s


def until(fn, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        try:
            v = fn()
            if v: return v
        except Exception:
            pass
        time.sleep(0.3)


def node(name, ns, *extra):
    os.makedirs(f"{T}/{name}/lw", exist_ok=True)
    return Node(name, ns, "longwire.hibr", ["up", "--sock-port", f"{T}/{name}/p:{T}/{name}/h", "--listen", "10.9.9.2:4001", *extra])


def main():
    print(f"-- NAT-PMP  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    sh("ip link add gw type veth peer name nd"); sh(f"ip link set nd netns {ns.pid}")
    sh("ip addr add 10.9.9.1/24 dev gw"); sh("ip link set gw up")
    nsh(ns, "ip link set lo up"); nsh(ns, "ip addr add 10.9.9.2/24 dev nd"); nsh(ns, "ip link set nd up")
    nsh(ns, "ip route add default via 10.9.9.1")
    g = gateway(False)

    A = node("A", ns)
    m = until(lambda: A.status()["mapped"])
    check(m == f"{PUBLIC}:{EXTERNAL}", f"a node behind a NAT-PMP router learns its mapped address: {m}")
    check(seen and seen[0][0] == 4001 and seen[0][1] == 4001 and seen[0][2] == 3600, f"it asked for its own port, for an hour: {seen[:1]}")
    A.stop(); time.sleep(1)

    n0 = len(asked0)
    B = node("B", ns, "--nat-pmp", "off")
    time.sleep(6)
    check(len(asked0) == n0 and B.status()["mapped"] == "", "--nat-pmp off asks the router nothing")
    B.stop(); time.sleep(1)


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("pmp")
