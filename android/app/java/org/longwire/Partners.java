// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import android.content.SharedPreferences;
import java.util.ArrayList;
import java.util.List;

/**
 * The other boxes of a pair that the box the phone dials has said are on its own segment (docs/adr/0094): kept as "number@time", so that when the box in the list is dead the
 * phone can still try the box it learned. They are shown on the screen, can be forgotten, and one not seen again for thirty days goes by itself.
 */
final class Partners {
    private static final String KEY = "partners";
    private static final long KEEP_MS = 30L * 24 * 3600 * 1000;

    private Partners() {}

    /** The learned numbers that have not expired. */
    static List<String> numbers(SharedPreferences p) {
        List<String> out = new ArrayList<>();
        long now = System.currentTimeMillis();
        for (String e : p.getString(KEY, "").split(",")) {
            int at = e.indexOf('@');
            if (at <= 0) {
                continue;
            }
            try {
                if (now - Long.parseLong(e.substring(at + 1)) < KEEP_MS) {
                    out.add(e.substring(0, at));
                }
            } catch (NumberFormatException x) {
                // a damaged entry is dropped
            }
        }
        return out;
    }

    /** Note that this number was vouched for just now. True if it is new (so the caller adds it to its list). */
    static boolean learn(SharedPreferences p, String number) {
        List<String> have = numbers(p);
        boolean isNew = !have.contains(number);
        StringBuilder sb = new StringBuilder();
        long now = System.currentTimeMillis();
        for (String h : have) {
            if (!h.equals(number)) {
                String old = "";
                for (String e : p.getString(KEY, "").split(",")) {
                    if (e.startsWith(h + "@")) {
                        old = e;
                    }
                }
                sb.append(old).append(',');
            }
        }
        sb.append(number).append('@').append(now);
        p.edit().putString(KEY, sb.toString()).apply();
        return isNew;
    }

    /** Forget every learned number. */
    static void forget(SharedPreferences p) {
        p.edit().remove(KEY).apply();
    }
}
