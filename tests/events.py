#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""An event that arrives torn by the one-second deadline of `read -t`: the first half, a pause longer than the deadline, the second half.
`longwire watch` must join them (it once threw the first half away and then choked on the second: "json: malformed input near ,"phrase":...").
A fake control socket stands in for the node, so the timing is exact."""
import os, socket, subprocess, sys, tempfile, threading, time
sys.path.insert(0, os.path.dirname(__file__))
from lwtest import HIBR, MOD, ROOT, T, check, finish

def main():
    d = T
    sock = d + "/ctl"
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(sock)
    srv.listen(8)
    ev1 = b'{"event":"member-joined","number":"111-222-333","note":"a fairly long field so that the line is cut well inside it"}\n'
    ev2 = b'{"event":"member-left","number":"444-555-666"}\n'
    stop = threading.Event()

    def serve(c):
        try:
            line = b""
            while not line.endswith(b"\n"):
                x = c.recv(1)
                if not x:
                    return
                line += x
            if line.startswith(b"status"):
                c.sendall(b'{"node":"up","number":"123-456-789","full":"123-456-789"}\n')
            elif line.startswith(b"stop"):
                c.sendall(b'{"node":"stopping"}\n')
            elif line.startswith(b"watch"):
                c.sendall(b'{"event":"watching"}\n')
                time.sleep(0.3)
                cut = 40
                c.sendall(ev1[:cut])              # the first half
                time.sleep(1.8)                   # longer than the script's one-second wait
                c.sendall(ev1[cut:])              # the rest
                time.sleep(0.5)
                c.sendall(ev2)                    # and a whole one after it
                time.sleep(1.5)
        except OSError:
            pass
        finally:
            c.close()

    def accept():
        srv.settimeout(0.5)
        while not stop.is_set():
            try:
                c, _ = srv.accept()
            except (socket.timeout, OSError):
                continue
            threading.Thread(target=serve, args=(c,), daemon=True).start()
    threading.Thread(target=accept, daemon=True).start()

    env = dict(os.environ, HOME=d, LW_SOCK=sock, LW_DIR=d, LONGWIRE_MOD=MOD, XDG_CONFIG_HOME=d + "/cfg")
    r = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", "watch"], env=env, capture_output=True, text=True, timeout=30)
    out = r.stdout + r.stderr
    check("111-222-333 joined the line" in out, f"an event torn by the deadline is joined, not thrown away: {out!r}")
    check("444-555-666 left the line" in out, f"and the one after it is read whole: {out!r}")
    check("malformed" not in out and "could not be read" not in out, f"with no parse error: {out!r}")
    r2 = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", "doctor"], env=env, capture_output=True, text=True, timeout=30)
    out2 = r2.stdout + r2.stderr
    check(r2.returncode == 1 and "running node has engine older than 0.5.10" in out2, f"doctor names a running node that does not say its version (an older engine): {out2!r}")
    # `longwire off` for a profile that says keep_jack=no takes the jack away after the node has stopped (the helper is a stand-in that writes down what it was asked)
    os.makedirs(d + "/cfg/longwire", exist_ok=True)
    open(d + "/cfg/longwire/profiles", "w").write("default j\n\nprofile j\n  switchboard = x:1/y\n  attach = jack\n  interface = lo\n  keep_jack = no\n")
    helper = d + "/fake-helper"
    open(helper, "w").write("#!/bin/sh\necho \"helper $*\" >> " + d + "/helper.log\n")
    os.chmod(helper, 0o755)
    os.makedirs(d + "/bin", exist_ok=True)
    open(d + "/bin/pkexec", "w").write('#!/bin/sh\nexec "$@"\n')
    os.chmod(d + "/bin/pkexec", 0o755)
    env3 = dict(env, LONGWIRE_JACK=helper, PATH=d + "/bin:" + env.get("PATH", ""))
    r3 = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", "off"], env=env3, capture_output=True, text=True, timeout=30)
    hl = open(d + "/helper.log").read() if os.path.exists(d + "/helper.log") else ""
    check("helper --remove lo" in hl, f"off with keep_jack=no asks the helper to remove the jack: rc={r3.returncode} {hl!r} {r3.stderr[-200:]}")
    os.remove(d + "/helper.log") if os.path.exists(d + "/helper.log") else None
    open(d + "/cfg/longwire/profiles", "w").write("default j\n\nprofile j\n  switchboard = x:1/y\n  attach = jack\n  interface = lo\n")
    r4 = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", "off"], env=env3, capture_output=True, text=True, timeout=30)
    check(not os.path.exists(d + "/helper.log"), "and with the default (keep it) it does not")
    stop.set()
    finish("events")


if __name__ == "__main__":
    main()
