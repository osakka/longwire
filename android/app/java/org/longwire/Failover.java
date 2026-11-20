// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import java.util.ArrayList;
import java.util.List;

/**
 * Which number the phone calls and when it gives up (docs/adr/0084, 0092, 0094): the decisions of the supervisor, with nothing of Android in them, so that a JVM test can drive them
 * (tests/failover.sh). The loop in LongwireVpnService asks it and does what it says.
 */
final class Failover {
    private final List<String> targets = new ArrayList<>();
    private final int maxTries, maxMin;
    private final long longest;
    private int idx, failsHere, tries;
    private long downSince, lastDial, wait = 5000;

    Failover(List<String> numbers, int maxTries, int maxMin, long longestMs, long now) {
        targets.addAll(numbers);
        this.maxTries = maxTries;
        this.maxMin = maxMin;
        this.longest = longestMs;
        this.downSince = now;
    }

    /** The number to call now: the first, or the one that answered last. */
    String dial() {
        return targets.isEmpty() ? "" : targets.get(idx);
    }

    int count() {
        return targets.size();
    }

    int position() {
        return idx;
    }

    int tries() {
        return tries;
    }

    long waitMs() {
        return wait;
    }

    /** A number learned from a box of the pair: tried after the others. */
    void add(String number) {
        for (String t : targets) {
            if (t.replace("-", "").equals(number.replace("-", ""))) {
                return;
            }
        }
        targets.add(number);
    }

    /** A session with a member of the line is up (peerNum answered): the count and the clock start again, and the number that answered is the one to call next time. */
    void lineUp(String peerNum, long now) {
        tries = 0;
        failsHere = 0;
        downSince = now;
        String digits = peerNum.replace("-", "");
        for (int i = 0; i < targets.size(); i++) {
            if (targets.get(i).replace("-", "").equals(digits)) {
                idx = i;
            }
        }
    }

    /** Why to turn the line off, or null. Never while a session with the line is up: a dead first number with a survivor carrying traffic is not a failure. */
    String gaveUp(boolean lineUp, long now) {
        if (lineUp) {
            return null;
        }
        if (maxTries > 0 && tries >= maxTries) {
            return "no answer after " + tries + " tries";
        }
        if (maxMin > 0 && now - downSince > maxMin * 60000L) {
            return "no answer for " + maxMin + " minutes";
        }
        return null;
    }

    /** Whether to place a call now: there is a number, the engine is idle, and the wait since the last one has passed. */
    boolean due(String callState, long now) {
        return !dial().isEmpty() && "idle".equals(callState) && now - lastDial > wait;
    }

    /** A call is being placed: the wait grows (up to the longest), the try is counted, and after two without an answer the next number is the one. Returns the number to call. */
    String place(long now) {
        lastDial = now;
        wait = Math.min(wait * 2, longest);
        tries++;
        if (++failsHere > 2 && targets.size() > 1) {
            idx = (idx + 1) % targets.size();
            failsHere = 1;
        }
        return dial();
    }

    /** The line is up: the wait before a call starts again from five seconds. */
    void connected() {
        wait = 5000;
    }

    boolean limited() {
        return maxTries > 0 || maxMin > 0;
    }

    int maxTries() {
        return maxTries;
    }

    /** The network came back: try at once, and start the waits again. */
    void networkBack() {
        wait = 5000;
        lastDial = 0;
    }
}
