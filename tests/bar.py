# SPDX-License-Identifier: GPL-3.0-or-later
# Longwire on the menu bar and the Control Strip. hibr's desktop is not run: the functions the item calls from it are stubbed to record what
# they were asked, the pane, the bar item and the strip module are sourced, and the item is driven against real nodes: off, on, ringing, connected.
# What this cannot check is how it is drawn.
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lwtest as L
from lwtest import HIBR, MOD, T, check, sh, Node

PANE = L.ROOT + "/control-panel/longwire.hibr"
BAR = L.ROOT + "/menubar/longwire.hibr"
STRIP = L.ROOT + "/control-strip/longwire.hibr"

DRIVER = r'''
declare -gA CP GL DTJ
GL[ellipsis]="..."
mod load %(mod)s
LOG=%(log)s
DT_INK=ink; DT_FACE=face; DT_DIM=dim; DT_GOOD=good; DT_WARN=warn; DT_BAD=bad; DT_INFO=info
fn dt_has(f) { command -v "$f" > /dev/null; }
fn dt_want(int ms) { return 0; }
fn dt_ms() { ret "${DRV_MS:-0}"; }
fn dt_notep(prio, text, ...rest) { printf 'NOTE %%s %%s\n' "$prio" "$(printf "$text" "${rest[@]}")" >> "$LOG"; }
fn dt_confirm(msg, cmd) { printf 'CONFIRM %%s -> %%s\n' "$msg" "$cmd" >> "$LOG"; DRV_CMD=$cmd; }
fn dt_ask(title, label, value, then, hint = "", ...rest) { printf 'ASK %%s -> %%s\n' "$title" "$then" >> "$LOG"; DRV_THEN=$then; }
fn cp_save() { return 0; }
fn cp_toggle(var) { return 0; }
fn dt_job(kind, tag, then, ...cmd) { printf 'JOB %%s\n' "${cmd[*]}" >> "$LOG"; ret 1; }
fn dt_ptyjob(kind, tag, then, prompt, ...cmd) { printf 'PTYJOB %%s\n' "${cmd[*]}" >> "$LOG"; ret 7; }
fn dt_baritem(name, text = "", ...cmd) { printf 'BARITEM %%s|%%s\n' "$name" "$text" >> "$LOG"; }
fn dt_bariright(name, ...cmd) { return 0; }
fn dt_barmenu(name, who, id = "") { printf 'BARMENU %%s\n' "$who" >> "$LOG"; }
fn dt_bariswitch(name, var, fn = "") { return 0; }
fn dt_barilabel(name, label) { return 0; }
fn dt_ticker(f) { return 0; }
fn dt_menu(t) { printf 'MENU %%s\n' "$t" >> "$LOG"; }
fn dt_item(label, key, ...cmd) { printf 'ITEM %%s|%%s|%%s\n' "$label" "$key" "${cmd[*]}" >> "$LOG"; }
fn dt_dim(label) { printf 'DIM %%s\n' "$label" >> "$LOG"; }
fn dt_sep() { printf 'SEP\n' >> "$LOG"; }
fn panel_goto(...a) { return 0; }
fn dt_panelpane(p) { printf 'PANE %%s\n' "$p" >> "$LOG"; }
fn dt_context_open(who, id, int r, int c) { printf 'CTX %%s %%s %%s\n' "$who" "$r" "$c" >> "$LOG"; }
fn cs_module(...a) { echo "CSMOD ${a[*]}"; return 0; }
fn console(...a) { printf 'CONSOLE %%s\n' "${a[*]}" >> "$LOG"; }
fn cp_pane(...a) { return 0; }
. %(pane)s
. %(bar)s
%(pre)s
. %(strip)s
: > "$LOG"
%(body)s
'''


def drive(env, body, pre=""):
    log = f"{T}/bar.log"
    open(log, "w").close()
    src = DRIVER % dict(log=log, pane=PANE, bar=BAR, strip=STRIP, mod=MOD, body=body, pre=pre)
    e = dict(os.environ, **env, HIBR_MODPATH=os.path.dirname(MOD))
    e["PATH"] = f"{T}/bin:" + e["PATH"]
    r = subprocess.run([HIBR, "-c", src], env=e, capture_output=True, text=True, timeout=40)
    return open(log).read().splitlines(), r


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
    print(f"-- bar: longwire on the menu bar and the Control Strip  (workdir {T})", flush=True)
    sh("ip link set lo up")
    ns = L.mkns()
    subprocess.run(["nsenter", "-t", str(ns.pid), "-n", "ip", "link", "set", "lo", "up"])
    S = Node("S", ns, "longwire-switchboard.hibr", ["--registry", "none", "--listen", "127.0.0.1:1877"])
    until(lambda: os.path.exists(S.home + "/sb/seed") and os.path.exists(S.sock), 15)
    addr = subprocess.run([HIBR, "-c", f"mod load {MOD}; lw key address {S.home}/sb/seed"], capture_output=True, text=True).stdout.strip()

    def node(name, port, *extra):
        return Node(name, ns, "longwire.hibr",
                    ["up", "--switchboard", f"127.0.0.1:1877/{addr}", "--sock-port", f"{T}/{name}/p:{T}/{name}/h",
                     "--listen", f"127.0.0.1:{port}", "--cand-loopback", "--keepalive", "5", *extra])

    os.makedirs(f"{T}/bin")
    open(f"{T}/bin/longwire", "w").write(f'#!/bin/sh\nexec {HIBR} {L.ROOT}/scripts/longwire.hibr "$@"\n')
    os.chmod(f"{T}/bin/longwire", 0o755)
    nobody = dict(HOME=f"{T}/nobody", LW_SOCK=f"{T}/none.sock", XDG_CONFIG_HOME=f"{T}/nobody/cfg")

    # --- off ---------------------------------------------------------------------------------------------------------------
    log, r = drive(nobody, "lwb_tick; lwb_context 1")
    check("BARITEM lwb|☎ ○" in log, f"with no node the bar shows the telephone and an empty circle: {[l for l in log if l.startswith('BARITEM')]} {r.stderr[-200:]}")
    check("MENU Longwire" in log and any(l.startswith("DIM Off") and "no connection" in l for l in log) and not any(l.startswith("ITEM Turn On") for l in log),
          f"off with nothing set up: the menu says so and offers no Turn On: {[l for l in log if l.startswith(('DIM', 'ITEM'))]}")
    check(any(l.startswith("ITEM Longwire Settings") for l in log), "...and Settings is always there")
    log, r = drive(nobody, "lwb_tick; lwb_tick")
    check(len([l for l in log if l.startswith("BARITEM")]) == 1, "an unchanged state does not redraw the item")
    log, r = drive(dict(nobody, DT_LWBAR="0"), "lwb_tick")
    check(log == ["BARITEM lwb|"], f"with the switch off the item is empty: {log}")
    log, r = drive(dict(nobody, DT_GLYPHSET="ascii"), "lwb_tick")
    check("BARITEM lwb|tel -" in log, f"in the ASCII glyph set it writes tel -: {log}")

    # a glyph set whose telephones carry the state (Blit's): the bar is that one picture, and the ring falls back to the "on" one
    body = 'GL[phoneoff]=$\'\\U00100020\'; GL[phonewait]=$\'\\U00100021\'; GL[phoneon]=$\'\\U00100022\'; lwb_tick'
    log, r = drive(nobody, body)
    check("BARITEM lwb|\U00100020" in log, f"where the glyph set has telephones, off is its outline telephone alone: {[l for l in log if l.startswith('BARITEM')]} {r.stderr[-200:]}")
    log, r = drive(nobody, body.replace("lwb_tick", 'LWB_ST=ring; LWB_RINGNUM=111-222-333; t := lwb_text; echo "RING [$t]"; LWB_ST=on; LWB_PEERS=2; t := lwb_text; echo "ON [$t]"; LWB_ST=wait; t := lwb_text; echo "WAIT [$t]"'))
    check("RING [\U00100022 111-222-333]" in r.stdout and "ON [\U00100022 2]" in r.stdout and "WAIT [\U00100021]" in r.stdout,
          f"on is the filled one with a count, waiting the dashed one, and a ring (no ring icon given) the filled one with the caller: {r.stdout.strip()}")
    log, r = drive(nobody, body.replace("lwb_tick", "lwstrip_draw 1 4"))
    check(any(l.startswith("CONSOLE put 1 4 \U00100020") for l in log), f"and the strip draws it too: {log}")

    # Phone Style: the classic set when asked for and present, the modern set when the classic is absent or the style is modern
    base = 'GL[phoneoff]=$\'\\U00100020\'; GL[phonewait]=$\'\\U00100021\'; GL[phoneon]=$\'\\U00100022\'; '
    classic = 'GL[phoneoffc]=$\'\\U00100030\'; GL[phonewaitc]=$\'\\U00100031\'; GL[phoneonc]=$\'\\U00100032\'; '
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + "lwb_tick")
    check("BARITEM lwb|\U00100030" in log, f"Phone Style classic: the bar draws the classic outline telephone when off: {[l for l in log if l.startswith('BARITEM')]}")
    log, r = drive(dict(nobody, DT_LWSTYLE="modern"), base + classic + "lwb_tick")
    check("BARITEM lwb|\U00100020" in log, "Phone Style modern: the modern one, though the classic is there")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + "lwb_tick")
    check("BARITEM lwb|\U00100020" in log, "classic asked for but the glyph set has none: the modern one, not nothing")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), "lwb_tick")
    check("BARITEM lwb|\u260e \u25cb" in log, "and with neither, the characters")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + 'LWB_ST=ring; LWB_RINGNUM=111-222-333; t := lwb_text; echo "RING [$t]"')
    check("RING [\U00100032 111-222-333]" in r.stdout, f"a ring in the classic style, with no classic ring icon, is the classic filled one with the caller: {r.stdout.strip()}")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + "lwstrip_draw 1 4")
    check(any(l.startswith("CONSOLE put 1 4 \U00100030") for l in log), f"the strip follows the style: {log}")

    # the classic telephone in two cells, under Blit's glyphs (docs: Blit 0.3.57's phone-classic-wide-*, GL[phoneoffcw] and the rest)
    wide = 'GL[phoneoffcw]=$\'\\U001002EC\\U001002ED\'; GL[phonewaitcw]=$\'\\U001002EE\\U001002EF\'; GL[phoneoncw]=$\'\\U001002F0\\U001002F1\'; GL[phoneringcw]=$\'\\U001002F2\\U001002F3\'; '
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + wide + "lwb_tick")
    check("BARITEM lwb|\U001002EC\U001002ED" in log, f"Phone Style classic under Blit: the two-cell classic telephone (off) in the menu bar: {[l for l in log if l.startswith('BARITEM')]}")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + wide + 'LWB_ST=ring; LWB_RINGNUM=111-222-333; t := lwb_text; echo "RING [$t]"')
    check("RING [\U001002F2\U001002F3 111-222-333]" in r.stdout, f"...and the two-cell ringing one with the caller: {r.stdout.strip()}")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + wide + 'LWB_ST=wait; t := lwb_text; echo "WAIT [$t]"')
    check("WAIT [\U001002EE\U001002EF]" in r.stdout, f"...and the waiting one: {r.stdout.strip()}")
    log, r = drive(dict(nobody, DT_LWSTYLE="modern"), base + classic + wide + "lwb_tick")
    check("BARITEM lwb|\U00100020" in log, "Phone Style modern: the modern one-cell telephone, though the wide classic one is there")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + "lwb_tick")
    check("BARITEM lwb|\U00100030" in log, "classic without the two-cell glyphs: the one-cell classic one, as before")
    log, r = drive(dict(nobody, DT_LWSTYLE="classic"), base + classic + wide + "lwstrip_draw 1 4")
    check(any(l.startswith("CONSOLE put 1 4 \U001002EC\U001002ED") for l in log), f"the strip draws the two-cell one too: {log}")
    log, r = drive(dict(nobody), "true", pre=wide)
    check("CSMOD lwstrip Longwire 2" in r.stdout, f"the strip module is two cells wide where the glyph set has the two-cell telephone: {r.stdout.strip()}")
    log, r = drive(dict(nobody), "true")
    check("CSMOD lwstrip Longwire 1" in r.stdout, f"and one cell wide where it has not: {r.stdout.strip()}")

    # a profile exists: the menu offers Turn On
    envP = dict(nobody, HOME=f"{T}/withprofile", XDG_CONFIG_HOME=f"{T}/withprofile/cfg", LW_SOCK=f"{T}/none2.sock", LW_DIR=f"{T}/withprofile/lw",
                LONGWIRE_MOD=MOD)
    os.makedirs(f"{T}/withprofile/cfg/longwire")
    open(f"{T}/withprofile/cfg/longwire/profiles", "w").write("default home\n\nprofile home\n  switchboard = 127.0.0.1:1877/x\n  attach = jack\n  interface = lo\n")
    log, r = drive(envP, "lwb_tick; lwb_context 1")
    check(any(l == "DIM Off, profile home" for l in log) and any(l.startswith("ITEM Turn On|o") for l in log),
          f"off with a profile: the menu names it and offers Turn On: {[l for l in log if l.startswith(('DIM', 'ITEM'))]}")
    log, r = drive(envP, "lwb_on")
    check("JOB longwire on home" in log, f"Turn On starts `longwire on home`: {log}")

    # --- on ----------------------------------------------------------------------------------------------------------------
    A = node("A", 4001, "--answer", "ask", "--name", "Alice", "--listed")
    B = node("B", 4002, "--answer", "ask")
    num = {k: until(lambda n=n: n.status()["number"], 20) for k, n in (("A", A), ("B", B))}
    check(all(num.values()), f"two nodes register: {num}")
    envB = dict(HOME=B.home, LW_DIR=B.home + "/lw", LW_SOCK=B.sock, XDG_CONFIG_HOME=f"{T}/nobody/cfg")
    log, r = drive(envB, "lwb_tick; lwb_context 1")
    check("BARITEM lwb|☎ ●" in log, f"with a number the bar shows a filled circle: {[l for l in log if l.startswith('BARITEM')]} {r.stderr[-200:]}")
    check(any(l.startswith("DIM On, your number is +") for l in log) and any(l.startswith("ITEM Turn Off|f") for l in log) and any(l.startswith("ITEM Dial...|d") for l in log),
          f"on: the menu says the number and offers Dial and Turn Off: {[l for l in log if l.startswith(('DIM', 'ITEM'))]}")
    check(not any(l.startswith("ITEM Turn On") for l in log), "...and not Turn On")

    # --- a ring --------------------------------------------------------------------------------------------------------------
    mk = B.w.mark()
    A.cmd("dial", num["B"])
    ring = B.w.wait("ring", 15, after=mk)
    check(ring is not None, "B is ringing")
    time.sleep(1.2)
    log, r = drive(envB, "lwb_tick; lwb_tick; lwb_context 1")
    bar = [l for l in log if l.startswith("BARITEM")]
    check(bar and "◉" in bar[-1] and num["A"] in bar[-1], f"a ring: the bar shows the ringing mark and who: {bar}")
    check(len([l for l in log if l.startswith("NOTE high") and "is calling" in l]) == 1, "...a note, once, however many frames pass")
    check(len([l for l in log if l.startswith("CONFIRM")]) == 1 and "rings. Pick up? It joins your network." in "".join(log), "...and one dialog, in one short line: who rings, and that picking up joins your network")
    check(any(l.startswith("ITEM Pick up") for l in log), "...and the menu offers to pick up")
    log, r = drive(envB, "lwb_tick; $DRV_CMD")
    check(B.w.wait("connected", 20, after=mk) is not None, "answering yes from the bar's dialog picks up and the call connects")

    # --- connected -------------------------------------------------------------------------------------------------------------
    time.sleep(2.5)
    log, r = drive(envB, "lwb_tick; lwb_context 1")
    check(any(l.startswith("BARITEM lwb|☎ ● 1") for l in log), f"connected: the bar counts the connection: {[l for l in log if l.startswith('BARITEM')]}")
    check(any(l.startswith("DIM " + num["A"]) and "up" in l for l in log), f"...and the menu lists who: {[l for l in log if l.startswith('DIM')]}")

    # the safety phrase of the connected call, on the desktop (docs/adr/0040): once agreed, a note once, a line in the menu, a row in the pane
    def phrase_of(n):
        for p in n.status().get("peers", []):
            if p.get("phrase"): return p["phrase"]
    ph = until(lambda: phrase_of(B), 20)
    check(bool(ph) and len(ph.split()) == 8, f"the call's eight-word phrase is agreed: {ph!r}")
    log, r = drive(envB, "lwb_tick; lwb_tick; lwb_tick; lwb_context 1; lwp_poll; longwire_rows 1; for ((i = 0; i < 80; i++)); do [ \"${CP[1][$i][text]-}\" = '  safety phrase' ] && echo \"ROW ${CP[1][$i][val]}\"; done")
    notes = [l for l in log if l.startswith("NOTE high Safety phrase with " + num["A"])]
    check(len(notes) == 1 and ph in notes[0] and "hang up if it differs" in notes[0], f"the bar tells the phrase once, as a note, the moment it is agreed: {notes} {r.stderr[-200:]}")
    check(any(l == "DIM   phrase: " + ph for l in log), f"...the bar's menu shows it under the call: {[l for l in log if 'phrase' in l]}")
    check(f"ROW {ph}" in r.stdout, f"...and the pane has a safety phrase row under the connected call: {r.stdout[-300:]!r}")

    # longwire verify (docs/adr/0054): the words the other end shows, typed here, compared for you
    rc, out = B.cmd("verify")
    check(rc == 0 and num["A"] in out and "not verified" in out and ph in out, f"`longwire verify` lists the call, its phrase, and that it is not verified yet: {out!r}")
    rc, out = B.cmd("verify", num["A"], ph.upper().replace(" ", ",  "))
    check(rc == 0 and "the words match" in out, f"the words typed (in any case, with commas) match: recorded: {out!r}")
    rc, out = B.cmd("show")
    check(f"{ph}   (verified)" in out, f"show marks the phrase verified: {out[-300:]!r}")
    log, r = drive(envB, "lwp_poll; longwire_rows 1; for ((i = 0; i < 80; i++)); do [ \"${CP[1][$i][text]-}\" = '  safety phrase' ] && echo \"ROW ${CP[1][$i][val]}\"; [ \"${CP[1][$i][key]-}\" = verify:%s ] && echo VERIFYROW; done" % num["A"])
    check(f"ROW {ph}  (verified)" in r.stdout and "VERIFYROW" not in r.stdout, f"the pane marks it verified, and offers no Verify row for it: {r.stdout[-200:]!r}")

    rc, out = B.cmd("verify", num["A"], "wrong words entirely")
    check(rc != 0 and "the words differ" in out and "ended" in out, f"words that differ end the call and forget the key: {out!r}")
    gone = until(lambda: not any(p.get("number") == num["A"] and p.get("state") == "up" for p in B.status().get("peers", [])), 15)
    check(bool(gone), "and the call is indeed ended")
    rc, out = B.cmd("verify")
    check(f"{num['A']}  verified" not in out, f"it is no longer recorded as verified: {out!r}")

    # --- the strip --------------------------------------------------------------------------------------------------------------
    log, r = drive(envB, "lwstrip_draw 1 4")
    check("CONSOLE pen good face" in log and any(l.startswith("CONSOLE put 1 4 ☎") for l in log), f"on, the strip's telephone is green: {log}")
    log, r = drive(nobody, "lwstrip_draw 1 4")
    check("CONSOLE pen dim face" in log, f"off, it is dim: {log}")
    log, r = drive(envB, "lwstrip_click 2 5")
    check("CTX lwstrip 2 5" in log, f"a click opens its menu where it was pressed: {log}")
    log, r = drive(envB, "lwstrip_context 1")
    check("MENU Longwire" in log and any(l.startswith("ITEM Turn Off") for l in log), "and the menu is the bar item's")

    for n in (A, B, S):
        n.stop()


try:
    main()
except Exception:
    import traceback
    traceback.print_exc()
    L.fails.append("the test itself raised an exception")
finally:
    L.finish("bar")
