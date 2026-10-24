# SPDX-License-Identifier: GPL-3.0-or-later
# longwire key export / import: the identity as words and back (no network needed).
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lwtest import HIBR, MOD, ROOT


def lw(home, *a, stdin=None):
    env = dict(os.environ, HOME=home, LONGWIRE_MOD=MOD, LW_DIR=home + "/lw")
    return subprocess.run([HIBR, ROOT + "/scripts/longwire.hibr", *a], env=env, input=stdin,
                          capture_output=True, text=True)


def check(c, m):
    print(("ok   " if c else "FAIL ") + m)
    if not c:
        sys.exit(1)


a, b = tempfile.mkdtemp(), tempfile.mkdtemp()
idA = lw(a, "id").stdout.strip()
words = lw(a, "key", "export").stdout
check(len(words.split()) == 33, "export gives 33 words")
check(oct(os.stat(a + "/lw/seed").st_mode & 0o777) == "0o600", "the seed file is mode 0600")

r = lw(b, "key", "import", stdin=words)
check(r.returncode == 0, "import accepts the words (%s)" % r.stderr.strip())
check(lw(b, "id").stdout.strip() == idA, "the restored node has the same key id")
check(oct(os.stat(b + "/lw/seed").st_mode & 0o777) == "0o600", "the restored seed is mode 0600")
check(lw(b, "key", "export").stdout == words, "export after import gives the same words")

r = lw(b, "key", "import", stdin=words)
check(r.returncode != 0 and "already has an identity" in r.stderr, "import refuses to overwrite an identity")

c = tempfile.mkdtemp()
w = words.split()
w[5], w[6] = w[6], w[5]
if w[5] == w[6]:
    w[5] = "amber" if w[6] != "amber" else "anchor"
r = lw(c, "key", "import", stdin=" ".join(w))
check(r.returncode != 0 and "checksum" in r.stderr and not os.path.exists(c + "/lw/seed"), "a slip is caught by the checksum")
r = lw(c, "key", "import", stdin=" ".join(words.split()[:30]))
check(r.returncode != 0 and not os.path.exists(c + "/lw/seed"), "too few words are refused")
r = lw(c, "key", "import", stdin=words.upper().replace("\n", "  \n "))
check(r.returncode == 0, "case and spacing do not matter")
print("key: all ok")
