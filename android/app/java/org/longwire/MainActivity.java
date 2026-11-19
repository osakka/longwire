// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.net.VpnService;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

/**
 * One screen: how the line is, a button, what it is joined to, and two switches. The colours are the brand's (docs/BRAND.md): copper for the wire, dial green for connected, ringing amber for working, hang-up red for
 * trouble; the text is monospace because numbers are. What the system's VPN dialog says is the system's (it is asked once).
 */
public class MainActivity extends Activity {
    private static final int CONSENT = 1;
    private static final int COPPER = 0xFFC0632B, GREEN = 0xFF2FB67C, AMBER = 0xFFE0A100, RED = 0xFFD9473F;
    private int bg, card, ink, dim, field;
    private EditText sb, dial, token, name;
    private Switch all, saver;
    private Button go, edit, allow;
    private TextView title, detail, phrase, summary;
    private View dot, form, batt;
    private boolean editing;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        boolean night = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        bg = night ? 0xFF121417 : 0xFFF3EEE4;
        card = night ? 0xFF1C2025 : 0xFFFFFFFF;
        ink = night ? 0xFFF3EEE4 : 0xFF121417;
        dim = night ? 0xFF9AA0A6 : 0xFF5F6368;
        field = night ? 0xFF262B31 : 0xFFEFE9DC;
        SharedPreferences p = getSharedPreferences("longwire", MODE_PRIVATE);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(20), dp(24), dp(20), dp(32));

        // the head: the mark, the name, the tagline
        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo = new ImageView(this);
        int lid = getResources().getIdentifier("ic_launcher_background", "drawable", getPackageName());
        int fid = getResources().getIdentifier("ic_launcher_foreground", "drawable", getPackageName());
        if (lid != 0 && fid != 0) {
            android.graphics.drawable.LayerDrawable ld = new android.graphics.drawable.LayerDrawable(new android.graphics.drawable.Drawable[] {getDrawable(lid), getDrawable(fid)});
            logo.setImageDrawable(ld);
            GradientDrawable clip = new GradientDrawable();
            clip.setCornerRadius(dp(12));
            logo.setClipToOutline(true);
            logo.setBackground(clip);
        }
        head.addView(logo, new LinearLayout.LayoutParams(dp(52), dp(52)));
        LinearLayout names = new LinearLayout(this);
        names.setOrientation(LinearLayout.VERTICAL);
        names.setPadding(dp(14), 0, 0, 0);
        TextView nm = text("longwire", 26, ink, true);
        names.addView(nm);
        names.addView(text("Same LAN. Any distance.", 13, dim, false));
        head.addView(names);
        col.addView(head);

        // how the line is
        LinearLayout st = box(col, 20);
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        dot = new View(this);
        row.addView(dot, new LinearLayout.LayoutParams(dp(14), dp(14)));
        title = text("Off", 22, ink, true);
        title.setPadding(dp(12), 0, 0, 0);
        row.addView(title);
        st.addView(row);
        detail = text("", 14, dim, false);
        detail.setPadding(0, dp(8), 0, 0);
        st.addView(detail);
        phrase = text("", 15, COPPER, true);
        phrase.setPadding(0, dp(12), 0, 0);
        st.addView(phrase);

        go = new Button(this);
        go.setAllCaps(false);
        go.setTextSize(18);
        go.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        go.setOnClickListener(v -> toggle());
        LinearLayout.LayoutParams gl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(58));
        gl.topMargin = dp(16);
        col.addView(go, gl);

        // what it is joined to: the switchboard and the number it dials, said in two lines once they are set, and the fields behind Edit
        LinearLayout cn = box(col, 16);
        LinearLayout ch = new LinearLayout(this);
        ch.setGravity(Gravity.CENTER_VERTICAL);
        TextView ct = text("Line", 16, ink, true);
        ch.addView(ct, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        edit = new Button(this);
        edit.setAllCaps(false);
        edit.setText("Edit");
        edit.setOnClickListener(v -> {
            editing = !editing;
            refresh();
        });
        ch.addView(edit);
        cn.addView(ch);
        summary = text("", 14, dim, false);
        cn.addView(summary);
        form = new LinearLayout(this);
        ((LinearLayout) form).setOrientation(LinearLayout.VERTICAL);
        sb = field((LinearLayout) form, "Switchboard", "host:port/lw1…", p.getString("switchboard", ""));
        dial = field((LinearLayout) form, "Number to dial", "312-884-076", p.getString("dial", ""));
        token = field((LinearLayout) form, "Token (only if the switchboard asks)", "", p.getString("token", ""));
        name = field((LinearLayout) form, "Name this phone gives", "", p.getString("name", ""));
        cn.addView(form);

        // two switches
        LinearLayout op = box(col, 16);
        all = new Switch(this);
        all.setText("Send everything through the line");
        all.setTextColor(ink);
        all.setChecked(p.getBoolean("all", false));
        op.addView(all);
        op.addView(text("Off: only the network the other end is on comes through (its printer, its NAS). On: the whole internet leaves from there.", 12, dim, false));
        saver = new Switch(this);
        saver.setText("Battery saver");
        saver.setTextColor(ink);
        saver.setChecked(p.getBoolean("saver", false));
        saver.setPadding(0, dp(14), 0, 0);
        op.addView(saver);
        op.addView(text("Checks in every 55 s instead of 25 s. Turn it off if the line drops while you are on mobile data. Takes effect the next time you turn it on.", 12, dim, false));

        // Android pauses background apps unless it is told not to
        batt = box(col, 16);
        ((LinearLayout) batt).addView(text("Always on", 16, ink, true));
        TextView bt = text("Android may pause Longwire to save battery, and the line drops while the phone sleeps. Let it run in the background: this keeps the line up, and costs very little.", 13, dim, false);
        ((LinearLayout) batt).addView(bt);
        allow = new Button(this);
        allow.setAllCaps(false);
        allow.setText("Let it run in the background");
        allow.setOnClickListener(v -> allowBackground());
        ((LinearLayout) batt).addView(allow);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(bg);
        sv.setFillViewport(true);
        sv.addView(col);
        setContentView(sv);
        getWindow().setStatusBarColor(bg);
        getWindow().setNavigationBarColor(bg);
        if (Build.VERSION.SDK_INT >= 33) {
            requestPermissions(new String[] {"android.permission.POST_NOTIFICATIONS"}, 2);
        }
        editing = p.getString("switchboard", "").isEmpty() || p.getString("dial", "").isEmpty();
        tick.run();
        enroll(getIntent());
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private TextView text(String s, int sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setTypeface(Typeface.MONOSPACE, bold ? Typeface.BOLD : Typeface.NORMAL);
        return t;
    }

    /** A rounded card in the column. */
    private LinearLayout box(LinearLayout col, int pad) {
        LinearLayout b = new LinearLayout(this);
        b.setOrientation(LinearLayout.VERTICAL);
        b.setPadding(dp(pad), dp(pad), dp(pad), dp(pad));
        GradientDrawable g = new GradientDrawable();
        g.setColor(card);
        g.setCornerRadius(dp(16));
        b.setBackground(g);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(16);
        col.addView(b, lp);
        return b;
    }

    private EditText field(LinearLayout into, String label, String hint, String value) {
        TextView l = text(label, 12, dim, false);
        l.setPadding(0, dp(12), 0, dp(2));
        into.addView(l);
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setText(value);
        e.setSingleLine(true);
        e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        e.setTextColor(ink);
        e.setHintTextColor(dim);
        e.setTypeface(Typeface.MONOSPACE);
        e.setTextSize(14);
        GradientDrawable g = new GradientDrawable();
        g.setColor(field);
        g.setCornerRadius(dp(10));
        e.setBackground(g);
        e.setPadding(dp(12), dp(10), dp(12), dp(10));
        into.addView(e);
        return e;
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            refresh();
            ui.postDelayed(this, 1000);
        }
    };

    private void refresh() {
        int level = LongwireVpnService.level;
        boolean running = LongwireVpnService.running;
        int color = level == 2 ? GREEN : level == 1 ? AMBER : level == 3 ? RED : dim;
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(color);
        dot.setBackground(d);
        title.setText(level == 2 ? "Connected" : level == 1 ? "Connecting" : level == 3 ? "Needs attention" : "Off");
        detail.setText(LongwireVpnService.status);
        String ph = LongwireVpnService.phrase;
        phrase.setText(ph.isEmpty() ? "" : "safety phrase\n" + ph);
        phrase.setVisibility(ph.isEmpty() ? View.GONE : View.VISIBLE);
        GradientDrawable g = new GradientDrawable();
        g.setCornerRadius(dp(29));
        if (running) {
            g.setColor(card);
            g.setStroke(dp(2), RED);
            go.setTextColor(RED);
            go.setText("Turn off");
        } else {
            g.setColor(GREEN);
            go.setTextColor(0xFF121417);
            go.setText("Turn on");
        }
        go.setBackground(g);
        boolean show = editing && !running;
        form.setVisibility(show ? View.VISIBLE : View.GONE);
        summary.setVisibility(show ? View.GONE : View.VISIBLE);
        edit.setVisibility(running ? View.GONE : View.VISIBLE);
        edit.setText(editing ? "Done" : "Edit");
        String s = sb.getText().toString().trim(), h = dial.getText().toString().trim();
        summary.setText(s.isEmpty() && h.isEmpty() ? "Nothing set yet: press Edit, or open an invitation link." : "switchboard  " + (s.contains("/") ? s.substring(0, s.indexOf('/')) : s) + "\ndials        " + h);
        batt.setVisibility(ignoringBattery() ? View.GONE : View.VISIBLE);
    }

    private boolean ignoringBattery() {
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        return pm == null || pm.isIgnoringBatteryOptimizations(getPackageName());
    }

    private void allowBackground() {
        try {
            startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            startActivity(new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        LongwireVpnService.visible = true;
    }

    @Override
    protected void onStop() {
        LongwireVpnService.visible = false;
        super.onStop();
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        enroll(i);
    }

    /**
     * A link from a switchboard's owner (`longwire-switchboard token invite`), opened from a message, an email or a scanned QR code: the switchboard, a token and, if the owner said, which number to dial.
     * It fills the form and says so; it turns nothing on. Anything that is not what such a link carries is refused, whoever sent it.
     */
    private void enroll(Intent i) {
        Uri u = i == null ? null : i.getData();
        if (u == null || !"longwire".equals(u.getScheme()) || !"enroll".equals(u.getHost())) {
            return;
        }
        String s = u.getQueryParameter("sb"), t = u.getQueryParameter("token"), h = u.getQueryParameter("dial"), n = u.getQueryParameter("name");
        boolean ok = s != null && s.matches("[A-Za-z0-9._-]{1,200}:[0-9]{1,5}/lw1[a-z2-7]{52}")
            && (t == null || t.matches("[0-9a-f]{32}"))
            && (h == null || h.matches("[0-9]{3}-[0-9]{3}-[0-9]{3}|lw1[a-z2-7]{52}"))
            && (n == null || (n.length() <= 40 && n.matches("[\\p{L}\\p{N} ._'-]*")));
        if (!ok) {
            Toast.makeText(this, "That link is not a Longwire invitation", Toast.LENGTH_LONG).show();
            return;
        }
        sb.setText(s);
        token.setText(t == null ? "" : t);
        if (h != null) {
            dial.setText(h);
        }
        if (n != null && name.getText().length() == 0) {
            name.setText(n);
        }
        editing = false;
        Toast.makeText(this, "Invitation read: check it, then Turn on", Toast.LENGTH_LONG).show();
    }

    private void toggle() {
        if (LongwireVpnService.running) {
            startService(new Intent(this, LongwireVpnService.class).setAction(LongwireVpnService.STOP));
            return;
        }
        if (sb.getText().toString().trim().isEmpty() || dial.getText().toString().trim().isEmpty()) {
            editing = true;
            Toast.makeText(this, "Set the switchboard and the number to dial first", Toast.LENGTH_LONG).show();
            refresh();
            return;
        }
        getSharedPreferences("longwire", MODE_PRIVATE).edit()
            .putString("switchboard", sb.getText().toString()).putString("dial", dial.getText().toString())
            .putString("token", token.getText().toString()).putString("name", name.getText().toString())
            .putBoolean("all", all.isChecked()).putBoolean("saver", saver.isChecked()).apply();
        Intent consent = VpnService.prepare(this);
        if (consent != null) {
            startActivityForResult(consent, CONSENT);
        } else {
            begin();
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        if (req == CONSENT && res == RESULT_OK) {
            begin();
        }
    }

    private void begin() {
        Intent i = new Intent(this, LongwireVpnService.class).setAction(LongwireVpnService.START);
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(i);
        } else {
            startService(i);
        }
    }

    @Override
    protected void onDestroy() {
        ui.removeCallbacks(tick);
        super.onDestroy();
    }
}
