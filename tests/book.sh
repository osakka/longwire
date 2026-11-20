#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Dial by name (ADR 0082): the longwire numbers in the address book, through a stand-in for hibr's `contacts` command. Needs ../hibr/build/hibr (or $HIBR) and the module.
cd "$(dirname "$0")/.." || exit 1
HIBR=${HIBR:-../hibr/build/hibr}
SCRIPT=$PWD/scripts/longwire.hibr
T=$(mktemp -d /tmp/lwbook.XXXXXX) || exit 1
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin"
cat > "$T/bin/contacts" <<'STUB'
#!/bin/sh
[ "$1" = find ] || { echo "contacts $*" >> "$STUBLOG"; echo "uid-new"; exit 0; }
TAB=$(printf '\t')
A=lw1esofly7erajwre2phtwujsyeh4fkxqeozssa75asjx4nd4vbt57a
case $2 in
ana) printf 'ana-1%sAna Smith%sphone:+44 20 7946 0000%snote:Met at the conference\n' "$TAB" "$TAB" "$TAB"
     printf 'ana-2%sAna Berg%sphone:312-884-076%snote:longwire %s\n' "$TAB" "$TAB" "$TAB" "$A" ;;
bo)  printf 'bo-1%sBo Berg%sphone:312-884-076\n' "$TAB" "$TAB" ;;
cy)  printf 'cy-1%sCy Offline%snote:longwire lw1c3pnotlongenough\n' "$TAB" "$TAB" ;;
two) printf 'd-1%sDee One%sphone:111-222-333\n' "$TAB" "$TAB"; printf 'd-2%sDee Two%sphone:444-555-666\n' "$TAB" "$TAB" ;;
both) printf 'e-1%sEve Both%sphone:111-222-333%snote:longwire %s\n' "$TAB" "$TAB" "$TAB" "$A" ;;
nobody) exit 0 ;;
*) echo "contacts: home has never been synced here: contacts sync brings it" >&2; exit 1 ;;
esac
STUB
chmod +x "$T/bin/contacts"
export PATH="$T/bin:$PATH" LONGWIRE_MOD="$PWD/build/longwire.so" STUBLOG="$T/log" LW_SOCK="$T/none.sock" HOME="$T/home"
mkdir -p "$HOME"
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
lwr() { "$HIBR" "$SCRIPT" "$@"; }
A=lw1esofly7erajwre2phtwujsyeh4fkxqeozssa75asjx4nd4vbt57a

out=$(lwr contact find ana 2> /dev/null); rc=$?
[ $rc = 0 ] && [ "$out" = "Ana Berg	$A" ] && ok "find gives the person who has a longwire address, and not the one who has only a phone number" || bad "find ana: rc=$rc [$out]"
out=$(lwr contact find bo 2> /dev/null)
[ "$out" = "Bo Berg	312-884-076" ] && ok "a phone like 312-884-076 is a number" || bad "find bo: [$out]"
out=$(lwr contact find cy 2> /dev/null)
[ -z "$out" ] && ok "a note that is not a full address is not one" || bad "find cy: [$out]"
out=$(lwr contact find both 2> /dev/null)
[ "$out" = "Eve Both	$A" ] && ok "with both, the address wins: it names the key, not a number" || bad "find both: [$out]"
lwr contact find zzz > "$T/o" 2> "$T/e"; rc=$?
[ $rc = 1 ] && grep -q "never been synced" "$T/e" && ok "a book never synced is contacts' own status 1 and its own words" || bad "unsynced: rc=$rc $(cat "$T/e")"

lwr dial nobody > "$T/o" 2> "$T/e"; rc=$?
[ $rc = 1 ] && grep -q "nobody called 'nobody'" "$T/e" && grep -q "longwire contact add" "$T/e" && ok "dial of a name that is nobody says so, and how to add one" || bad "dial nobody: rc=$rc $(cat "$T/e")"
lwr dial two > "$T/o" 2> "$T/e"; rc=$?
[ $rc = 2 ] && grep -q "Dee One   111-222-333" "$T/e" && grep -q "Dee Two   444-555-666" "$T/e" && ok "a name that is two people lists them with their numbers and dials neither" || bad "dial two: rc=$rc $(cat "$T/e")"
lwr dial bo > "$T/o" 2> "$T/e"; rc=$?
grep -q "calling Bo Berg (312-884-076)" "$T/o" "$T/e" && ok "a name that is one person is dialled, and the call says to whom" || bad "dial bo: rc=$rc $(cat "$T/o" "$T/e")"
lwr dial 312-884-076 > "$T/o" 2> "$T/e"; ! grep -q "calling" "$T/o" "$T/e" && ok "a number is dialled as it is, the address book is not asked" || bad "dial number asked the book"
lwr dial zzz > "$T/o" 2> "$T/e"; rc=$?
[ $rc = 1 ] && grep -q "never been synced" "$T/e" && ok "a dial by name with the book never synced stops with contacts' words" || bad "dial zzz: rc=$rc $(cat "$T/e")"

: > "$T/log"
lwr contact add "Bo Berg" 312-884-076 > /dev/null 2>&1
grep -q '^contacts put --name Bo Berg --phone 312-884-076$' "$T/log" && ok "add keeps a number as a phone" || bad "add number: $(cat "$T/log")"
: > "$T/log"
lwr contact add "Ana Berg" "$A" > /dev/null 2>&1
grep -q "^contacts put --name Ana Berg --note longwire $A\$" "$T/log" && ok "add keeps a full address as a note line, which is how dial reads it" || bad "add address: $(cat "$T/log")"
: > "$T/log"
lwr contact add Zed lw1short > /dev/null 2>&1; r1=$?
lwr contact add Zed 12345 > /dev/null 2>&1; r2=$?
[ $r1 = 2 ] && [ $r2 = 2 ] && [ ! -s "$T/log" ] && ok "add refuses what is not a number or a full address, and writes nothing" || bad "add junk: $r1 $r2 $(cat "$T/log")"
# a ring: the name in the address book, beside the caller's own claim
cat > "$T/ev.hibr" <<'EVT'
EVT
out=$(LONGWIRE_TEST_RING='{"event":"ring","call":"abc","from":"312-884-076","name":"someone else","key_id":"00","known":false,"admit":false}' lwr contact find bo 2> /dev/null)
[ "$out" = "Bo Berg	312-884-076" ] && ok "(the ring's lookup is the same lookup: a number finds the person who has it)" || bad "ring lookup"
[ $fail = 0 ] && echo "all book checks passed"
exit $fail
