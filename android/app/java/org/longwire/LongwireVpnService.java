// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.net.VpnService;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import java.io.File;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The phone's longwire (docs/adr/0076). The system gives an IP tunnel; the engine (a node whose port is the phone's adapter) puts it on the line: it asks the far LAN for an address, and this builds the tunnel
 * with what the LAN said. This service starts the engine, dials its number when the line has a dial tone, and builds the tunnel again when the address moves. The engine's own sockets stay outside the
 * tunnel because the app is not routed through it (addDisallowedApplication): otherwise the line would be carried through itself.
 */
public class LongwireVpnService extends VpnService {
    static final String START = "org.longwire.START", STOP = "org.longwire.STOP";
    /** What the screen shows; written here, read there. */
    static volatile String status = "Off";
    static volatile String phrase = "";
    /** One line for the notification: who it is joined to, and through which switchboard. The address and the gateway are for the screen. */
    static volatile String note = "";
    static volatile boolean running;
    /** 0 off, 1 working on it, 2 connected, 3 needs attention: what the screen's colour says. */
    static volatile int level;
    /** The screen is showing: the loop looks every second; with it dark, every five (a phone that is on all day should not wake itself to look at nothing). */
    static volatile boolean visible = true;

    private Thread sup;
    private volatile boolean run;
    private ParcelFileDescriptor tun;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && STOP.equals(intent.getAction())) {
            run = false;
            if (sup == null) {
                stopSelf();
            }
            return START_NOT_STICKY;
        }
        if (sup != null) {
            return START_NOT_STICKY;
        }
        foreground("Starting");
        run = true;
        running = true;
        sup = new Thread(this::supervise, "longwire");
        sup.start();
        return START_NOT_STICKY;	// the person turned it on: a restart the system makes is not theirs
    }

    @Override
    public void onRevoke() {			// another VPN took over, or the person turned this one off in Settings
        run = false;
    }

    @Override
    public void onDestroy() {
        run = false;
    }

    private void foreground(String text) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(new NotificationChannel("line", "Line", NotificationManager.IMPORTANCE_LOW));
        }
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, "line") : new Notification.Builder(this);
        int icon = getResources().getIdentifier("ic_stat", "drawable", getPackageName());
        b.setSmallIcon(icon != 0 ? icon : android.R.drawable.ic_lock_idle_lock).setContentTitle("Longwire").setContentText(text).setOngoing(true);
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(1, b.build(), ServiceInfo.FOREGROUND_SERVICE_TYPE_SYSTEM_EXEMPTED);
        } else {
            startForeground(1, b.build());
        }
    }

    private void supervise() {
        SharedPreferences p = getSharedPreferences("longwire", MODE_PRIVATE);
        String dir = getFilesDir().getPath();
        String dial = p.getString("dial", "").trim();
        String sbHost = p.getString("switchboard", "").trim();
        if (sbHost.contains("/")) {
            sbHost = sbHost.substring(0, sbHost.indexOf('/'));
        }
        boolean all = p.getBoolean("all", false);
        String err = Engine.start(dir, p.getString("switchboard", "").trim(), p.getString("token", "").trim(), (p.getString("name", "").trim().isEmpty() ? Build.MODEL : p.getString("name", "").trim()), false, p.getBoolean("saver", false) ? 55 : 25);
        if (!err.isEmpty()) {
            finish("Cannot start: " + err);
            return;
        }
        level = 1;
        String shown = "";
        Ctl ctl = new Ctl(new File(dir, "ctl").getPath());
        long lastDial = 0, wait = 5000;
        int gen = -1;
        while (run) {
            JSONObject m = ctl.ask("member"), st = ctl.ask("status");
            boolean bound = m != null && "bound".equals(m.optString("member")) || m != null && "renewing".equals(m.optString("member"));
            String call = st == null ? "" : st.optString("call"), sbs = st == null ? "" : st.optString("switchboard");
            if (bound && m.optInt("gen") != gen) {
                gen = m.optInt("gen");
                if (!buildTunnel(m, all)) {
                    level = 3;
                    status = "The system would not build the tunnel";
                    gen = -1;
                }
            } else if (!bound && tun != null) {
                if (Engine.attach(-1)) {		// closed only once the engine has let go of it
                    close();
                    gen = -1;
                }
            }
            phrase = "";
            boolean lineUp = false;			// a session to a member of the line, which is not the same as having an address: the address is kept when the other end goes away
            String peerNum = "";
            if (st != null) {
                JSONArray peers = st.optJSONArray("peers");
                for (int i = 0; peers != null && i < peers.length(); i++) {
                    JSONObject q = peers.optJSONObject(i);
                    if (q != null && "up".equals(q.optString("state"))) {
                        lineUp = true;
                        if (peerNum.isEmpty()) {
                            peerNum = q.optString("number");
                        }
                        if (!q.optString("phrase").isEmpty()) {
                            phrase = q.optString("phrase");
                        }
                    }
                }
            }
            level = 1;
            if (st == null) {
                level = 3;
                status = "The engine does not answer";
            } else if (bound && lineUp) {
                wait = 5000;
                level = 2;
                String who = peerNum.isEmpty() ? dial : peerNum;
                status = "Connected to " + who + "\nthrough " + sbHost + "\nyour address " + m.optString("ip") + ", gateway " + m.optString("gateway");
                note = "Connected to " + who + " · " + sbHost;
            } else if (!lineUp && "dial tone".equals(sbs)) {
                // no session with the number: call it. This holds with an address too: the other end restarted, or the road changed, and the address is kept while the line is made again.
                status = bound ? "Lost " + dial + ": calling it again (the address is kept)" : "On the line, calling " + dial;
                long now = System.currentTimeMillis();
                if (!dial.isEmpty() && "idle".equals(call) && now - lastDial > wait) {
                    lastDial = now;
                    wait = Math.min(wait * 2, 120000);	// the other end is down: a phone that is on all day does not knock every five seconds
                    JSONObject r = ctl.ask("dial " + dial);
                    if (r != null && r.has("error") && !r.optString("error").startsWith("already")) {	// "already calling": the engine is on it
                        level = 3;
                        status = "Cannot dial: " + r.optString("error");
                    }
                }
            } else if (m != null && "asking".equals(m.optString("member")) && lineUp) {
                status = "Connected to " + (peerNum.isEmpty() ? dial : peerNum) + ", asking its network for an address";
            } else {
                status = "Reaching the switchboard (" + (sbs.isEmpty() ? "?" : sbs) + ")";
            }
            String line = level == 2 && !note.isEmpty() ? note : status.split("\n")[0];
            if (!line.equals(shown)) {		// the notification says who the line is to, when that changes and not otherwise
                shown = line;
                foreground(line);
            }
            try {
                Thread.sleep(visible ? 1000 : 5000);
            } catch (InterruptedException e) {
                break;
            }
        }
        Engine.stop();				// the engine's thread is joined first: nothing reads the tunnel when it is closed
        close();
        finish("Off");
    }

    private boolean buildTunnel(JSONObject m, boolean all) {
        try {
            int prefix = m.getInt("prefix"), mtu = m.optInt("mtu", 1400);
            Builder b = new Builder().setSession("Longwire").setMtu(Math.min(mtu > 0 ? mtu : 1400, 1400));
            b.addAddress(m.getString("ip"), prefix);
            if (all) {
                b.addRoute("0.0.0.0", 0);
                b.addAddress("fd6c:7700::2", 128);	// IPv6 is not carried: this, and a route for all of it, sends it nowhere instead of past the line
                b.addRoute("::", 0);
            } else {
                b.addRoute(network(m.getString("ip"), prefix), prefix);
            }
            JSONArray dns = m.optJSONArray("dns");
            for (int i = 0; dns != null && i < dns.length(); i++) {
                b.addDnsServer(dns.getString(i));
            }
            if (!m.optString("domain").isEmpty()) {
                b.addSearchDomain(m.getString("domain"));
            }
            b.addDisallowedApplication(getPackageName());	// the engine's own sockets must not go into the line they carry
            ParcelFileDescriptor fresh = b.establish();
            if (fresh == null) {
                return false;
            }
            if (!Engine.attach(fresh.getFd())) {	// the engine did not take it: it still holds the old one, which stays open, and this one is not used
                fresh.close();
                return false;
            }
            ParcelFileDescriptor old = tun;
            tun = fresh;
            if (old != null) {
                old.close();			// the engine holds the new one: nothing reads the old
            }
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private static String network(String ip, int prefix) throws Exception {
        byte[] a = java.net.InetAddress.getByName(ip).getAddress();
        int mask = prefix == 0 ? 0 : -1 << (32 - prefix);
        int v = ((a[0] & 255) << 24 | (a[1] & 255) << 16 | (a[2] & 255) << 8 | (a[3] & 255)) & mask;
        return (v >>> 24) + "." + (v >> 16 & 255) + "." + (v >> 8 & 255) + "." + (v & 255);
    }

    private void close() {
        try {
            if (tun != null) {
                tun.close();
            }
        } catch (Exception e) {
            // nothing to do about a descriptor that will not close
        }
        tun = null;
    }

    private void finish(String last) {
        running = false;
        level = last.startsWith("Cannot") ? 3 : 0;
        sup = null;
        status = last;
        phrase = "";
        stopForeground(true);
        stopSelf();
    }
}
