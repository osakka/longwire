# SPDX-License-Identifier: GPL-3.0-or-later
# Shared pieces of the end-to-end tests: unprivileged network namespaces, nodes
# run as the real hibr scripts, and watchers on their event streams. The tests
# themselves (m1.py, m2.py) are scripts run under `unshare -Urn`, so everything
# here happens in a throwaway network namespace.
import json, os, shlex, shutil, socket, subprocess, tempfile, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HIBR = os.environ.get("HIBR", ROOT + "/../hibr/build/hibr")
MOD = ROOT + "/build/longwire.so"
T = tempfile.mkdtemp(prefix="lwtest.")
fails, procs, namespaces = [], [], []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)


def finish(label):
    """Kill what we started, remove the work directory, say how it went, exit."""
    import sys, signal
    for ns in namespaces:                    # workers detach from their parents: find them by namespace
        for pid in workers_in(ns):
            try: os.kill(pid, signal.SIGKILL)
            except OSError: pass
    for p in procs:
        try: p.kill()
        except Exception: pass
    time.sleep(0.2)
    shutil.rmtree(T, ignore_errors=True)
    print("\nFAILED: " + "; ".join(fails) if fails else "\nall " + label + " checks passed")
    sys.exit(1 if fails else 0)


def sh(cmd):
    subprocess.run(shlex.split(cmd), check=True)


def mkns():
    p = subprocess.Popen(["unshare", "-n", "sleep", "1000"])
    procs.append(p)
    namespaces.append(p)
    mine = os.readlink("/proc/self/ns/net")
    for _ in range(200):
        try:
            if os.readlink(f"/proc/{p.pid}/ns/net") != mine:
                return p
        except OSError:
            pass
        time.sleep(0.05)
    raise SystemExit("could not create a network namespace")


def nsh(ns, cmd, out=False):
    r = subprocess.run(["nsenter", "-t", str(ns.pid), "-n"] + shlex.split(cmd), check=True,
                       capture_output=out, text=True)
    return r.stdout.strip() if out else None


def nsrun(ns, argv, timeout=30, input=None):
    """Run a command inside a namespace and return the CompletedProcess (no exception on failure)."""
    return subprocess.run(["nsenter", "-t", str(ns.pid), "-n"] + argv, capture_output=True, text=True,
                          timeout=timeout, input=input)


def mac_of(ifname, ns=None):
    # /sys shows the namespace it was mounted in, so ask `ip` (in the right namespace)
    cmd = ["ip", "-o", "link", "show", "dev", ifname]
    if ns is not None:
        cmd = ["nsenter", "-t", str(ns.pid), "-n"] + cmd
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    return out.split("link/ether")[1].split()[0]


def workers_in(ns):
    """pids of the longwire workers living in a network namespace"""
    want = os.readlink(f"/proc/{ns.pid}/ns/net")
    out = []
    for pid in subprocess.run(["pgrep", "-x", "lw-worker"], capture_output=True, text=True).stdout.split():
        try:
            if os.readlink(f"/proc/{pid}/ns/net") == want:
                out.append(int(pid))
        except OSError:
            pass
    return out


class Watcher(threading.Thread):
    """Reads a node's event stream (`watch` on its control socket) into a list."""
    def __init__(self, sockpath):
        super().__init__(daemon=True)
        self.events, self.cv, self.path = [], threading.Condition(), sockpath
        self.start()

    def run(self):
        for _ in range(100):
            try:
                s = socket.socket(socket.AF_UNIX); s.connect(self.path); break
            except OSError:
                time.sleep(0.1)
        else:
            return
        s.sendall(b"watch\n")
        buf = b""
        while True:
            try: d = s.recv(4096)
            except OSError: return
            if not d: return
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                try: ev = json.loads(line)
                except ValueError: continue
                with self.cv:
                    self.events.append(ev); self.cv.notify_all()

    def wait(self, name, timeout=15, pred=lambda e: True, after=0):
        end = time.time() + timeout
        with self.cv:
            while True:
                for e in self.events[after:]:
                    if e.get("event") == name and pred(e):
                        return e
                left = end - time.time()
                if left <= 0: return None
                self.cv.wait(left)

    def mark(self):
        with self.cv: return len(self.events)


class Node:
    """A node (or the switchboard) run as its real hibr script inside a namespace."""
    def __init__(self, name, ns, script, args):
        self.name, self.script, self.ns, self.args = name, script, ns, args
        self.home = f"{T}/{name}"
        os.makedirs(self.home + "/lw", exist_ok=True)
        self.env = dict(os.environ, HOME=self.home, LONGWIRE_MOD=MOD, LW_DIR=self.home + "/lw", SB_DIR=self.home + "/sb")
        self.sock = self.env["SB_DIR" if "switchboard" in script else "LW_DIR"] + "/ctl"
        self.env["LW_SOCK"] = self.sock
        self.log = open(f"{T}/{name}.log", "w")
        self.proc = subprocess.Popen(["nsenter", "-t", str(ns.pid), "-n", HIBR, ROOT + "/scripts/" + script] + args,
                                     env=self.env, stdout=self.log, stderr=subprocess.STDOUT)
        procs.append(self.proc)
        self.w = Watcher(self.sock)

    def cmd(self, *a):
        r = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", *a], env=self.env, capture_output=True, text=True)
        return r.returncode, (r.stdout + r.stderr).strip()

    def status(self):
        s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(self.sock); s.sendall(b"status\n")
        d = b""
        while True:
            c = s.recv(65536)
            if not c: break
            d += c
        return json.loads(d)

    def stop(self):
        subprocess.run([HIBR, "-c", f"mod load {MOD}; lw stop --sock {self.sock}"], capture_output=True)


def identity(name):
    """Create a node's identity ahead of time and return the path of its public bundle (hex)."""
    home = f"{T}/{name}"
    os.makedirs(home + "/lw", exist_ok=True)
    env = dict(os.environ, HOME=home, LONGWIRE_MOD=MOD, LW_DIR=home + "/lw")
    out = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", "bundle"], env=env, capture_output=True, text=True).stdout.strip()
    path = f"{T}/{name}.bundle"
    open(path, "w").write(out)
    return path


def veth(ns_x, name_x, ns_y, name_y, bridge_x=None, bridge_y=None):
    """A veth pair. An end whose namespace is None stays in ours, optionally enslaved to a bridge (also ours)."""
    sh("ip link add vx type veth peer name vy")
    for end, ns, name, br in (("vx", ns_x, name_x, bridge_x), ("vy", ns_y, name_y, bridge_y)):
        if ns is None:
            sh(f"ip link set {end} name {name}")
            if br:
                sh(f"ip link set {name} master {br}")
            sh(f"ip link set {name} up")
        else:
            sh(f"ip link set {end} netns {ns.pid}")
            nsh(ns, f"ip link set {end} name {name}")
            nsh(ns, f"ip link set {name} up")
