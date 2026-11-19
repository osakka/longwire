// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import org.json.JSONObject;

/** The engine's control socket, in the app's private directory: one line in, one JSON object out (the words are the desktop's: status, member, dial, hangup). */
final class Ctl {
    private final String path;

    Ctl(String path) {
        this.path = path;
    }

    /** The reply, or null when the engine does not answer. */
    JSONObject ask(String line) {
        try (LocalSocket s = new LocalSocket()) {
            s.connect(new LocalSocketAddress(path, LocalSocketAddress.Namespace.FILESYSTEM));
            s.setSoTimeout(3000);
            OutputStream o = s.getOutputStream();
            o.write((line + "\n").getBytes("UTF-8"));
            o.flush();
            InputStream in = s.getInputStream();
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int r;
            while ((r = in.read(buf)) > 0 && b.size() < (1 << 20)) {
                b.write(buf, 0, r);
            }
            return new JSONObject(b.toString("UTF-8"));
        } catch (Exception e) {
            return null;
        }
    }
}
