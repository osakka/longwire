// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

/** The engine (liblongwire.so, built by TCC): started once, given the tunnel when there is one, stopped. Everything else goes through its control socket (Ctl). */
final class Engine {
    static {
        System.loadLibrary("longwire");
    }

    private Engine() {}

    /** "" when it is running, or what stopped it. */
    static native String start(String dir, String switchboard, String token, String name, boolean listed, int keepalive);

    /** The tunnel's descriptor, or -1 for none. True when the engine has taken it, after which the old one may be closed; false, and the old one must stay open. */
    static native boolean attach(int fd);

    static native void stop();
}
