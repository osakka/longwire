# SPDX-License-Identifier: GPL-3.0-or-later
# Longwire's Control Panel pane, menu bar item and Control Strip module on hibr's real desktop (hibr 0.99.560 and later, hibr ADR 0050): installed into a
# scratch data folder as the catalogue installs them, this tree's files in place of the catalogue's, and each run by the desktop in a process of its own.
# Driven through hibr's own screen harness (tests/screen.py beside $HIBR). Nothing here sources a longwire file: the desktop reads them, as it reads any
# installable. No node runs: the pane and the bar say the line is off.
import os, sys

HIBR = os.environ.get("HIBR") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../hibr/build/hibr")
HTREE = os.path.dirname(os.path.dirname(os.path.realpath(HIBR)))
if not os.path.exists(os.path.join(HTREE, "tests/screen.py")):
    print(f"skip: no hibr test harness beside {HIBR}")
    sys.exit(0)
os.environ["HIBR"] = os.path.realpath(HIBR)
LW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# one module folder for the harness (HIBR_TESTMODS): hibr's modules, and this tree's longwire.so, which the catalogue's install asks for (`need longwire`)
import tempfile
MODS = tempfile.mkdtemp(prefix="lw-mods-")
import atexit, shutil
atexit.register(shutil.rmtree, MODS, True)		# report() exits: a line after it would never run
HM = os.environ.get("HIBR_MODPATH", "").split(":")[-1] or os.path.join(HTREE, "build/mods")
for f in os.listdir(HM):
    os.symlink(os.path.join(HM, f), os.path.join(MODS, f))
os.symlink(os.path.join(LW, "build/longwire.so"), os.path.join(MODS, "longwire.so"))
os.environ["HIBR_TESTMODS"] = MODS
sys.path.insert(0, os.path.join(HTREE, "tests"))
import screen as sx
from screen import Term, check, report, load, tree

data = sx.install(["longwire"], sx.scratch("lw-data"), local={
    "control-panel/longwire.hibr": os.path.join(LW, "control-panel/longwire.hibr"),
    "menubar/longwire.hibr": os.path.join(LW, "menubar/longwire.hibr"),
    "control-strip/longwire.hibr": os.path.join(LW, "control-strip/longwire.hibr"),
})
conf = sx.scratch("lw-conf")
os.makedirs(conf, exist_ok=True)
env = {"XDG_DATA_HOME": data, "XDG_CONFIG_HOME": conf, "LW_SOCK": "/nonexistent/ctl", "LW_DIR": sx.scratch("lw-dir"),
       "HIBR_MODPATH": tree("build/mods"), "DT_TICK": "200"}
ses = sx.scratch("lw-hosted.hibr")
open(ses, "w").write("%s. %s\ndt_regload %s\ncp_panes\ndt_open\ndt_new \"Control Panel\" 22 70 2 2 panel \"Longwire\"\ndt_run\ndt_close\n"
                     % (load("console"), tree("programs/desktop/desktop.hibr"), tree("programs/desktop/system/panel.hibr")))
t = Term(ses, rows=26, cols=80, settle=0.6, env=env)
pn = sx.Panel(t, 2, 2, 22, 70)
check("the pane, installed and run in its own process, says the line is off", pn.shows("Longwire is off", 15), pn)
check("...and offers to turn it on, further down its rows", pn.find("Turn on") is not None, pn)
top = t.text.splitlines()[0] if t.text else ""
check("the menu bar item shows the telephone and the line off, drawn by its own process", "☎" in top and "○" in top, top)
t.quit(b"qy", 1.0)
t.close()
report()
