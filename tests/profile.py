# SPDX-License-Identifier: GPL-3.0-or-later
# Saved connections (ADR 0034). A switchboard that wants a token, and a person's `longwire` run only through its profile commands:
# set a profile, turn the node on in the background and get a number, turn it off, and have `up` with nothing said use the default.
import json, os, stat, subprocess, sys, time

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

    # --- a profile is removed ----------------------------------------------------------------------------------------------
    rc, out = lwc("profile", "remove", "bad")
    check(rc == 0 and "bad" not in lwc("profile", "list")[1], "a profile can be removed")
    rc, out = lwc("on", "bad")
    check(rc == 1 and "no profile called 'bad'" in out, f"and then it is not there: {out!r}")

    L.finish("profile")


main()
