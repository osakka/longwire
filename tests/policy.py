# SPDX-License-Identifier: GPL-3.0-or-later
# The switchboard's policy script and the engine agree on a token (CHANGELOG 0.5.41): a token is spent only once its node has a number, and a
# verdict that comes too late earns the node no strike. The script is made slow for one registration with a stand-in `date` that sleeps; the
# node is refused for want of a verdict, asks again on its next session, and gets its number with the same token.
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node


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


def main():
    print(f"-- the policy script and its tokens  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    os.makedirs(f"{T}/bin", exist_ok=True)
    real = subprocess.run(["sh", "-c", "command -v date"], capture_output=True, text=True).stdout.strip()
    open(f"{T}/bin/date", "w").write(f"#!/bin/sh\n[ -e {T}/slow ] && rm -f {T}/slow && sleep 12\nexec {real} \"$@\"\n")
    os.chmod(f"{T}/bin/date", 0o755)
    os.environ["PATH"] = f"{T}/bin:" + os.environ["PATH"]
    open(f"{T}/tokens", "w").write("tokSLOW 123456786\n")		# a reserved number (its check digit is right)
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877", "--tokens", f"{T}/tokens"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    sba = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    open(f"{T}/slow", "w").write("")					# the script's next event takes 12 s: longer than the engine waits (10 s)
    os.makedirs(f"{T}/A/lw", exist_ok=True)
    A = Node("A", ns, "longwire.hibr", ["up", "--switchboard", f"127.0.0.1:1877/{sba}", "--sock-port", f"{T}/A/p:{T}/A/h", "--listen", "127.0.0.1:4001",
                                       "--cand-loopback", "--keepalive", "5", "--sb-rekey", "8", "--token", "tokSLOW"])
    ref = A.w.wait("register-refused", 20)
    check(ref is not None and "no decision" in ref["reason"], f"the script is too slow: the node is refused for want of a verdict ({ref and ref['reason']})")
    time.sleep(5)							# the late verdict (12 s) reaches an engine that stopped waiting at 10 s
    check("tokSLOW" in open(f"{T}/tokens").read(), "the late verdict did not spend the token: the node has no number yet")
    num = until(lambda: A.status()["number"], 45)
    check(num == "123-456-786", f"on its next session the node asks again and gets the reserved number with the same token: {num}")
    check("tokSLOW" not in open(f"{T}/tokens").read(), "now that it has its number, the token is spent")
    st = json.loads(subprocess.run([HIBR, "-c", f"mod load {MOD}; lw sb bans --sock {S.sock}"], capture_output=True, text=True).stdout or "{}")
    check(st.get("struck") == 0, f"and the switchboard's own slowness earned the node no strike: {st.get('struck')} struck")
    L.finish("policy")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:
        import traceback; traceback.print_exc()
        check(False, f"crashed: {e!r}")
        L.finish("policy")
