# SPDX-License-Identifier: GPL-3.0-or-later
# A stand-in node (a socket that answers `status` with a jack node's) and a profile with name servers; `longwire show` is run with systemd-resolved absent, present with
# nothing set on the jack, and present with the servers set. PATH holds only a directory of links to what the script needs, so a real resolvectl on this machine is not seen.
import json, os, shutil, socket, subprocess, sys, tempfile, threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, ROOT, check

T = tempfile.mkdtemp(prefix="lwshowdns.")
ver = open(ROOT + "/VERSION").read().strip()
STATUS = (json.dumps({"node": "up", "key_id": "00", "number": "107916502", "full": "+0798933 107-916-502", "name": "", "switchboard": "dial tone", "nat": "easy",
                      "version": ver, "port": "jack:lw7", "peers": [], "calls": [], "line": None, "sandbox": {"installed": False}, "road": {"state": "up"},
                      "exit": {"share": False, "use": False}, "jack_nets": 0, "underlay_state": ""}) + "\n").encode()
sock = T + "/ctl"
srv = socket.socket(socket.AF_UNIX)
srv.bind(sock)
srv.listen(8)


def serve():
    while True:
        try:
            c, _ = srv.accept()
        except OSError:
            return
        c.recv(100)
        c.sendall(STATUS)
        c.close()


threading.Thread(target=serve, daemon=True).start()
os.makedirs(T + "/cfg/longwire")
os.makedirs(T + "/bin")
for tool in ("chmod", "mkdir", "cat", "date", "sed", "id"):
    p = shutil.which(tool)
    if p:
        os.symlink(p, f"{T}/bin/{tool}")
open(T + "/cfg/longwire/profiles", "w").write(
    "default home\n\nprofile home\n  switchboard = 127.0.0.1:1877/x\n  attach = jack\n  interface = lw7\n  address = 192.168.152.10/24\n  dns = 192.168.152.254\n")


def show(resolvectl_says=None):
    for f in (T + "/bin/resolvectl",):
        if os.path.lexists(f):
            os.unlink(f)
    if resolvectl_says is not None:
        open(T + "/bin/resolvectl", "w").write(f"#!/bin/sh\necho '{resolvectl_says}'\n")
        os.chmod(T + "/bin/resolvectl", 0o755)
    env = dict(HOME=T, XDG_CONFIG_HOME=T + "/cfg", LW_SOCK=sock, LONGWIRE_MOD=MOD, PATH=T + "/bin")
    r = subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", "show"], env=env, capture_output=True, text=True, timeout=30)
    return r.stdout


out = show(None)
check("names" in out and "192.168.152.254" in out and "systemd-resolved is not installed" in out,
      f"the profile asks name servers and systemd-resolved is not there: show says so, and that names go to the machine's own server: {[l for l in out.splitlines() if 'names' in l]}")
out = show("Link 7 (lw7):")
check("names" in out and "not set on the jack" in out and "longwire off, then on" in out,
      f"systemd-resolved is there but the jack has no servers: show says they are not set, and what to do: {[l for l in out.splitlines() if 'names' in l]}")
out = show("Link 7 (lw7): 192.168.152.254")
check("names         through the line, from 192.168.152.254" in out and "⚠  names" not in out,
      f"the servers are set on the jack: show says names go through the line, from them, with no warning: {[l for l in out.splitlines() if 'names' in l]}")
open(T + "/cfg/longwire/profiles", "w").write(
    "default home\n\nprofile home\n  switchboard = 127.0.0.1:1877/x\n  attach = jack\n  interface = lw7\n  address = 192.168.152.10/24\n")
out = show(None)
check("names" not in out, f"a profile with no name servers says nothing about names: {[l for l in out.splitlines() if 'names' in l]}")
shutil.rmtree(T, ignore_errors=True)
L.finish("showdns")
