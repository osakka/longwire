# SPDX-License-Identifier: GPL-3.0-or-later
# Run as root of a user namespace that also has users 1..3 and groups 1..3 (tests/group.sh).
import json, os, socket, stat, subprocess, sys, time

T, HIBR, MOD = sys.argv[1:4]
fails = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    if not ok: fails.append(what)


def lw(*a):
    return subprocess.run([HIBR, "-c", "mod load " + MOD + "; lw " + " ".join(a) + '; rc=$?; [ $rc -eq 0 ] || echo "$ERRMSG" >&2; exit $rc'],
                          capture_output=True, text=True,
                          env=dict(os.environ, HIBR_MODPATH=os.path.dirname(MOD)))


PROBE = r'''
import socket, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(3)
try:
    s.connect(sys.argv[1]); s.sendall(b"status\n"); d = s.recv(4096)
    print("answered" if b"number" in d or b"role" in d or d.startswith(b"{") else "silent")
except PermissionError:
    print("refused")
except Exception as e:
    print("error", type(e).__name__)
'''


def as_user(uid, gid, groups, sock):
    """Connect to `sock` as uid:gid with these supplementary groups; what happened."""
    g = ["--groups=" + ",".join(map(str, groups))] if groups else ["--clear-groups"]
    r = subprocess.run(["setpriv", f"--reuid={uid}", f"--regid={gid}", *g, "python3", "-I", "-c", PROBE, sock],
                       capture_output=True, text=True)
    return r.stdout.strip() or r.stderr.strip()[-120:]


ASK = r'''
import socket, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(3)
s.connect(sys.argv[1]); s.sendall(sys.argv[2].encode() + b"\n"); print(s.recv(4096).decode().strip())
'''


def ask_as(uid, gid, groups, sock, cmd):
    g = ["--groups=" + ",".join(map(str, groups))] if groups else ["--clear-groups"]
    r = subprocess.run(["setpriv", f"--reuid={uid}", f"--regid={gid}", *g, "python3", "-I", "-c", ASK, sock, cmd], capture_output=True, text=True)
    return r.stdout.strip() or r.stderr.strip()[-120:]


def start(name, *extra):
    d = f"{T}/{name}"
    os.makedirs(d, exist_ok=True)
    os.chmod(d, 0o755)
    lw("key new", f"{d}/seed")
    r = lw("start --seed", f"{d}/seed --sock {d}/ctl --sock-port {d}/p:{d}/h --listen 127.0.0.1:0", *extra)
    for _ in range(50):
        if os.path.exists(f"{d}/ctl"): break
        time.sleep(0.1)
    return f"{d}/ctl", r


def stop(sock):
    lw("stop --sock", sock)


# a node with no group: as before, the owner only
sock, r = start("plain")
st = os.stat(sock)
check(stat.S_IMODE(st.st_mode) == 0o600, f"with no group the socket is mode 0600: {oct(stat.S_IMODE(st.st_mode))}")
check(as_user(0, 0, [], sock) == "answered", "...and its owner is answered")
check(as_user(1, 1, [2], sock) == "refused", "...and another user is refused by the file mode, whatever groups it is in")
stop(sock)

# a node in group 2
sock, r = start("grp", "--group 2")
st = os.stat(sock)
check(stat.S_IMODE(st.st_mode) == 0o660 and st.st_gid == 2, f"--group 2 makes the socket mode 0660, group 2: {oct(stat.S_IMODE(st.st_mode))} gid {st.st_gid}")
check(as_user(1, 1, [2], sock) == "answered", "...a user whose supplementary group is 2 (primary group 1) is answered")
check(as_user(2, 2, [], sock) == "answered", "...a user whose primary group is 2 is answered")
check(as_user(3, 3, [], sock) == "refused", "...a user in neither is refused")
check(as_user(3, 3, [1], sock) == "refused", "...and so is one in another group")
check(as_user(0, 0, [], sock) == "answered", "...the owner still is answered")
r1 = ask_as(1, 1, [2], sock, "stop")
check("only the user this runs as" in r1 and as_user(1, 1, [2], sock) == "answered", f"...a member of the group cannot stop it, and it goes on answering: {r1[:80]}")
stop(sock)

# a group that does not exist is refused at the start, plainly, not ignored
sock, r = start("nogrp", "--group nosuchgroup_lw")
check(not os.path.exists(sock) and "no group called nosuchgroup_lw" in (r.stderr + r.stdout), f"an unknown group name stops the start: {(r.stderr + r.stdout).strip()[-100:]}")
sock, r = start("badgid", "--group 77777")
check(not os.path.exists(sock) and r.returncode != 0, f"a group id that cannot own the socket stops the start: {(r.stderr + r.stdout).strip()[-120:]}")

sys.exit(1 if fails else 0)
