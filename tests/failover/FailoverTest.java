// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import java.util.Arrays;

/** The phone's choice of number and its giving up (ADR 0084, 0092, 0094), driven without a phone: the class has nothing of Android in it. */
public class FailoverTest {
    static int bad;

    static void check(boolean ok, String what) {
        System.out.println((ok ? "ok   " : "FAIL ") + what);
        if (!ok) {
            bad = 1;
        }
    }

    public static void main(String[] a) {
        long t = 1_000_000L;

        // two numbers, the first dead: two tries on it, then the second
        Failover f = new Failover(Arrays.asList("111", "222"), 0, 0, 120_000L, t);
        check(f.dial().equals("111"), "it starts with the first number");
        String c1 = f.place(t), c2 = f.place(t + 10_000), c3 = f.place(t + 30_000);
        check(c1.equals("111") && c2.equals("111") && c3.equals("222"), "two tries without an answer on the first number, then the next: " + c1 + " " + c2 + " " + c3);
        check(f.tries() == 3, "every try is counted");

        // the wait grows to the longest and no more
        Failover g = new Failover(Arrays.asList("111"), 0, 0, 40_000L, t);
        g.place(t);
        check(g.waitMs() == 10_000, "the wait doubles from five seconds: " + g.waitMs());
        g.place(t + 20_000);
        g.place(t + 50_000);
        g.place(t + 100_000);
        check(g.waitMs() == 40_000, "and stops at the longest the person chose: " + g.waitMs());
        check(!g.due("idle", t + 100_001) && g.due("idle", t + 140_001), "a call is due only after the wait, and only when the engine is idle");
        check(!g.due("dialing", t + 999_999), "and never while a call is under way");
        g.networkBack();
        check(g.due("idle", t + 100_002) && g.waitMs() == 5000, "the network coming back is a call at once, with the waits started again");

        // the survivor carries the line: the dead first number must not trip the limits
        Failover h = new Failover(Arrays.asList("111", "222"), 3, 1, 120_000L, t);
        h.place(t);
        h.place(t + 10_000);
        h.place(t + 30_000);
        check(h.gaveUp(false, t + 31_000) != null && h.gaveUp(false, t + 31_000).startsWith("no answer after 3 tries"), "three failed tries with no line turn it off: " + h.gaveUp(false, t + 31_000));
        check(h.gaveUp(true, t + 31_000) == null, "...but not while a session with the line is up (the survivor of a pair)");
        h.lineUp("222", t + 40_000);
        check(h.tries() == 0 && h.dial().equals("222"), "a session through the second number starts the counts again and makes it the first to call next time");
        check(h.gaveUp(true, t + 40_000 + 3_600_000L) == null, "an hour on the survivor's line, and still no give-up");
        check(h.gaveUp(false, t + 40_000 + 61_000L).startsWith("no answer for 1 minutes"), "with the line gone for the time limit, it turns off");

        // the dead box comes back: the number that answered is the one to call
        Failover k = new Failover(Arrays.asList("111", "222"), 0, 0, 120_000L, t);
        k.lineUp("2-2-2", t);
        check(k.dial().equals("222"), "numbers match without their dashes");
        k.lineUp("111", t);
        check(k.dial().equals("111"), "and the first is the one to call again when it answers");

        // a number learned from the pair is tried after the listed ones, once
        Failover l = new Failover(Arrays.asList("111"), 0, 0, 120_000L, t);
        l.add("333");
        l.add("3-3-3");
        l.add("111");
        check(l.count() == 2, "a learned number is added once, and one already listed is not added again: " + l.count());
        l.place(t);
        l.place(t + 10_000);
        check(l.place(t + 30_000).equals("333"), "after two tries on the listed number the learned one is called");

        // restart with the dialled box dead: the learned partner is the only way
        Failover m = new Failover(Arrays.asList("111", "333"), 0, 0, 120_000L, t);
        m.place(t);
        m.place(t + 10_000);
        check(m.place(t + 30_000).equals("333"), "a restart with the dialled box dead reaches the learned one in three tries");
        System.out.println(bad == 0 ? "\nall failover checks passed" : "\nFAILED");
        System.exit(bad);
    }
}
