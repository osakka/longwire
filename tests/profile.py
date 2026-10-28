# SPDX-License-Identifier: GPL-3.0-or-later
# Saved connections (ADR 0034). A switchboard that wants a token, and a person's `longwire` run only through its profile commands:
# set a profile, turn the node on in the background and get a number, turn it off, and have `up` with nothing said use the default.
import json, os, re, stat, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, ROOT, T, check, sh, Node


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
    print(f"-- profile: saved connections  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    nsx = lambda *a, **k: subprocess.run(["nsenter", "-t", str(ns.pid), "-n", *a], **k)
    nsx("ip", "link", "set", "lo", "up")

    tokens = f"{T}/tokens"
    open(tokens, "w").write("good-token-one\ngood-token-two\n")
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877", "--tokens", tokens])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    home = f"{T}/pf"
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ, HOME=home, LONGWIRE_MOD=MOD, LW_DIR=home + "/lw", LONGWIRE_HIBR=HIBR, XDG_CONFIG_HOME=home + "/cfg")
    env.pop("LW_SOCK", None)

    def lwc(*a, timeout=60):
        r = nsx(HIBR, ROOT + "/scripts/longwire.hibr", *a, env=env, capture_output=True, text=True, timeout=timeout)
        return r.returncode, (r.stdout + r.stderr).strip()

    sbarg = f"switchboard=127.0.0.1:1877/{addr}"
    peer = f"{home}/p:{home}/h"

    # --- the file ------------------------------------------------------------------------------------------------------
    rc, out = lwc("profile", "list")
    check(rc == 0 and out == "", "no profile yet: the list is empty")
    rc, out = lwc("on")
    check(rc == 2 and "no profile yet" in out and "profile set" in out, f"on with no profile says how to make one: {out[:90]!r}")
    rc, out = lwc("profile", "set", "bad", sbarg, "token=bad-token", "attach=sock", f"interface={peer}", "answer=known")
    check(rc == 0, f"a profile is set: {out!r}")
    rc, out = lwc("profile", "set", "good", sbarg, "token=good-token-one", "attach=sock", f"interface={peer}", "answer=known", "name=Test Box")
    check(rc == 0, f"a second one with a good token: {out!r}")
    path = f"{home}/cfg/longwire/profiles"
    mode = stat.S_IMODE(os.stat(path).st_mode)
    check(mode == 0o600 and stat.S_IMODE(os.stat(os.path.dirname(path)).st_mode) == 0o700, f"the file holds a token, so it is for the owner only: {oct(mode)}")
    rc, out = lwc("profile", "list")
    check("* bad" in out and "  good" in out, f"the first profile is the default: {out!r}")
    rc, out = lwc("profile", "show", "good")
    check("good-token-one" not in out and "token = (set, 14 characters)" in out and "name = Test Box" in out, f"show does not print the token: {out!r}")
    rc, out = lwc("profile", "set", "good", "attach=banana")
    check(rc == 2 and "attach is jack or splice" in out, f"a wrong value is refused plainly: {out!r}")
    rc, out = lwc("profile", "set", "good", "colour=red")
    check(rc == 2 and "unknown key" in out, f"so is an unknown key: {out!r}")
    rc, out = lwc("profile", "set", "bad name", "name=x")
    check(rc == 2, "and a name with a space")

    # A profile that never set Attach is shown as "jack" by the pane, so it must mean jack to the node (it once meant nothing, and
    # Turn on said "need --splice --jack or sock-port"). Here there is no such tap device, so the node says it cannot open the tap.
    rc, out = lwc("profile", "set", "bare", sbarg, "interface=lwnone0")
    rc, out = lwc("up", "--profile", "bare", timeout=40)
    check("need --splice" not in out and ("tun" in out or "lwnone0" in out), f"a profile with no Attach attaches as a jack: {out[-200:]!r}")
    rc, out = lwc("profile", "remove", "bare")

    # --- rename: the settings, the token, the place in the list and the default all go with it -----------------------------
    before = open(path).read()
    rc, out = lwc("profile", "rename", "good", "good2")
    check(rc == 0, f"a profile is renamed: {out!r}")
    after = open(path).read()
    check(after == before.replace("profile good\n", "profile good2\n"), "only the name changed: the token and every setting are as they were, in the same place")
    rc, out = lwc("profile", "list")
    check("  good2" in out and "good\n" not in out + "\n".replace("good2", "") and "* bad" in out, f"the list shows the new name, the default is still bad: {out!r}")
    rc, out = lwc("profile", "rename", "bad", "bad2")
    rc, out = lwc("profile", "list")
    check("* bad2" in out, f"renaming the default profile keeps it the default: {out!r}")
    rc, out = lwc("profile", "rename", "bad2", "good2")
    check(rc == 1 and "already a profile" in out, f"a name already taken is refused: {out!r}")
    rc, out = lwc("profile", "rename", "nope", "x")
    check(rc == 1 and "no profile called" in out, f"so is a profile that is not there: {out!r}")
    rc, out = lwc("profile", "rename", "good2", "bad name")
    check(rc == 2, "and a name with a space")
    rc, out = lwc("profile", "rename", "good2")
    check(rc == 2 and "usage" in out, f"and a missing new name: {out!r}")
    rc, out = lwc("profile", "rename", "good2", "good")
    rc, out = lwc("profile", "rename", "bad2", "bad")
    check(rc == 0 and open(path).read() == before, "and back again, the file is byte for byte what it was")

    # --- the address the jack gets -------------------------------------------------------------------------------------------
    rc, out = lwc("profile", "set", "good", "address=10.77.0.2/24")
    rc2, out2 = lwc("profile", "show", "good")
    check(rc == 0 and "address = 10.77.0.2/24" in out2, f"a profile keeps the address of its jack: {out2!r}")
    for a in ("banana", "10.77.0.2", "10.77.0.2/24;id", "10.77/24", "/24"):
        rc, out = lwc("profile", "set", "good", f"address={a}")
        check(rc == 2 and "address is like" in out, f"address {a!r} is refused plainly: {out!r}")
    rc, out = lwc("profile", "set", "good", "address=")
    rc2, out2 = lwc("profile", "show", "good")
    check(rc == 0 and "address" not in out2, f"an empty address removes it: {out2!r}")
    rc, out = lwc("jack", "--address")
    check(rc == 2 and "usage" in out, f"jack --address with no value says how: {out!r}")
    rc, out = lwc("jack", "--address", "127.0.0.1/8", "lo")
    check(rc == 0 and "lo is there, with 127.0.0.1/8" in out, f"jack with the address already there needs no password: {out!r}")

    # A switchboard that does not answer: the node runs, so `on` is not a failure; it says it is waiting (it once said "did not turn on" and
    # showed only the last line of the log).
    rc, out = lwc("profile", "set", "quiet", f"switchboard=127.0.0.1:1999/{addr}", "attach=sock", f"interface={peer}")
    rc, out = lwc("on", "quiet", timeout=60)
    check(rc == 0 and "waiting for the switchboard" in out, f"a switchboard that does not answer: on says the node is on and waiting, and succeeds: rc={rc} {out[-200:]!r}")
    rc, out = lwc("off")
    until(lambda: lwc("status")[0] != 0, 10)
    rc, out = lwc("profile", "remove", "quiet")

    # --- on, with a token the switchboard does not have ----------------------------------------------------------------
    rc, out = lwc("on", "bad", timeout=40)
    check(rc == 1 and "refused" in out.lower() and "valid token" in out, f"a bad token: on says it did not get a number, and why: {out[-160:]!r}")
    rc, out = lwc("off")
    check(until(lambda: lwc("status")[0] != 0, 10) is not None, "off stops it")

    # --- on, with a good token -------------------------------------------------------------------------------------------
    rc, out = lwc("on", "good", timeout=40)
    num = out.split("your number is ")[-1].split()[0] if "your number is" in out else ""
    check(rc == 0 and len(num) == 11, f"on starts the node in the background and says the number it got: {out!r}")
    rc, out = lwc("status")
    st = json.loads(out) if rc == 0 else {}
    check(st.get("number") == num, f"the node is up on that number: {st.get('number')}")
    rc, out = lwc("on", "good")
    check(rc == 0 and "already on" in out and num in out, f"on again says so, and the number: {out!r}")
    rc, out = lwc("off")
    gone = until(lambda: lwc("status")[0] != 0, 10)
    check(gone is not None, "off turns it off")

    # --- the same key needs no token the second time ------------------------------------------------------------------
    rc, out = lwc("profile", "set", "good", "token=")
    check(rc == 0 and "token" not in lwc("profile", "show", "good")[1], "a token can be taken out of a profile")
    rc, out = lwc("on", "good", timeout=40)
    check(rc == 0 and num in out, f"the same identity gets its number back without a token: {out!r}")
    lwc("off")
    until(lambda: lwc("status")[0] != 0, 10)

    # --- up with nothing said uses the default profile -----------------------------------------------------------------
    rc, out = lwc("profile", "default", "good")
    check(rc == 0 and "* good" in lwc("profile", "list")[1], "the default can be changed")
    p = subprocess.Popen(["nsenter", "-t", str(ns.pid), "-n", HIBR, ROOT + "/scripts/longwire.hibr", "up"], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    L.procs.append(p)
    got = until(lambda: json.loads(lwc("status")[1]).get("number"), 25)
    check(got == num, f"up with no arguments connects with the default profile: {got}")
    lwc("down")
    try:
        said = p.communicate(timeout=10)[0]
    except subprocess.TimeoutExpired:
        p.kill(); said = p.communicate()[0]
    check("profile good" in said, f"and says which profile it used: {said.strip()[:100]!r}")

    # --- longwire with nothing said, show, watch ------------------------------------------------------------------------------
    rc, out = lwc("show")
    check(rc == 0 and "longwire is off" in out, f"show with the node off says so: {out!r}")
    envx = dict(env, XDG_CONFIG_HOME=home + "/cfg-none", LW_DIR=home + "/lw-none")
    r0 = nsx(HIBR, ROOT + "/scripts/longwire.hibr", env=envx, capture_output=True, text=True, timeout=30)
    check(r0.returncode == 0 and "no connection saved yet" in r0.stdout and "Control Panel > Longwire" in r0.stdout,
          f"longwire alone with no connection says how to make one, and that the Control Panel does it too: {r0.stdout.strip()[:100]!r}")
    rc, out = lwc("profile", "default", "good")
    rc, out = lwc(timeout=40)
    check(rc == 0 and "your number is" in out and "☎  your number +" in out and out.count(num) == 2 and "switchboard" in out and "(profile good)" in out,
          f"longwire alone turns the node on from the default profile and then says how it is: {out!r}")
    rc, out = lwc("show")
    check(rc == 0 and out.count(num) == 1 and "switchboard   dial tone" in out, f"show: the number once, and the switchboard: {out!r}")

    A = Node("A", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--token", "good-token-two", "--sock-port", f"{T}/A/p:{T}/A/h",
                                         "--listen", "127.0.0.1:4001", "--cand-loopback", "--keepalive", "5", "--answer", "ask", "--name", "Alice"])
    anum = until(lambda: A.status()["number"], 20)
    check(bool(anum), f"a second node registers (with its own token): {anum}")
    w = subprocess.Popen(["nsenter", "-t", str(ns.pid), "-n", HIBR, ROOT + "/scripts/longwire.hibr", "watch"], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    L.procs.append(w)
    # wait until the watcher says it is watching (a loaded machine takes longer than a fixed pause to start hibr), then give it a moment to subscribe
    head = ""
    os.set_blocking(w.stdout.fileno(), False)
    t0 = time.time()
    while "watching" not in head and time.time() - t0 < 30:
        try:
            head += os.read(w.stdout.fileno(), 4096).decode()
        except (BlockingIOError, OSError):
            time.sleep(0.2)
    os.set_blocking(w.stdout.fileno(), True)
    time.sleep(2)
    A.cmd("dial", num)
    time.sleep(4)
    rc, out = lwc("show")
    check(re.search(r"engine +\d+\.\d+\.\d+", out) is not None, f"show names the engine version that runs (so nobody has to guess what is installed): {out[:200]!r}")
    check("RINGING" in out and anum in out and "safety phrase" in out, f"show names a ringing call, its caller and the phrase: {out!r}")
    rc, out = lwc("pickup")
    time.sleep(4)
    w.terminate()
    try:
        said = head + w.communicate(timeout=10)[0]
    except subprocess.TimeoutExpired:
        w.kill(); said = head + w.communicate()[0]
    check("watching" in said and f"{anum} is calling" in said and "safety phrase" in said and "run: longwire pickup" in said,
          f"watch narrates the ring as sentences, with the phrase and what to run: {said.strip()[:200]!r}")
    check("connected to" in said and "malformed" not in said, f"and narrates the connection, with no parse error: {said.strip()[-400:]!r}")
    A.cmd("hangup")
    A.stop()
    lwc("off")
    until(lambda: lwc("status")[0] != 0, 10)

    # --- a profile is removed ----------------------------------------------------------------------------------------------
    rc, out = lwc("profile", "remove", "bad")
    check(rc == 0 and "bad" not in lwc("profile", "list")[1], "a profile can be removed")
    rc, out = lwc("on", "bad")
    check(rc == 1 and "no profile called 'bad'" in out, f"and then it is not there: {out!r}")

    L.finish("profile")


main()
