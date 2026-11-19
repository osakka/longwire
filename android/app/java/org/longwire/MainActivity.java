// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.VpnService;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** One screen: where the line is, which number is home, and a button. What the system's VPN dialog says is the system's (it is asked once). */
public class MainActivity extends Activity {
    private static final int CONSENT = 1;
    private EditText sb, home, token, name;
    private CheckBox all;
    private Button go;
    private TextView state, phrase;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        SharedPreferences p = getSharedPreferences("longwire", MODE_PRIVATE);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        col.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("Longwire");
        title.setTextSize(26);
        col.addView(title);
        state = new TextView(this);
        state.setTextSize(16);
        col.addView(state);
        phrase = new TextView(this);
        phrase.setTextSize(20);
        col.addView(phrase);
        sb = field(col, "Switchboard (host:port/lw1…)", p.getString("switchboard", ""), InputType.TYPE_TEXT_VARIATION_URI);
        home = field(col, "Home number (312-884-076 or lw1…)", p.getString("home", ""), InputType.TYPE_CLASS_TEXT);
        token = field(col, "Token (if the switchboard asks for one)", p.getString("token", ""), InputType.TYPE_CLASS_TEXT);
        name = field(col, "Name this phone gives", p.getString("name", ""), InputType.TYPE_CLASS_TEXT);
        all = new CheckBox(this);
        all.setText("Send everything through home");
        all.setChecked(p.getBoolean("all", false));
        col.addView(all);
        go = new Button(this);
        go.setOnClickListener(v -> toggle());
        col.addView(go);
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        if (Build.VERSION.SDK_INT >= 33) {
            requestPermissions(new String[] {"android.permission.POST_NOTIFICATIONS"}, 2);
        }
        tick.run();
        enroll(getIntent());
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        enroll(i);
    }

    /**
     * A link from a switchboard's owner (`longwire-switchboard token invite`), opened from a message, an email or a scanned QR code: the switchboard, a token and, if the owner said, which number is home.
     * It fills the form and says so; it turns nothing on. Anything that is not what such a link carries is refused, whoever sent it.
     */
    private void enroll(Intent i) {
        android.net.Uri u = i == null ? null : i.getData();
        if (u == null || !"longwire".equals(u.getScheme()) || !"enroll".equals(u.getHost())) {
            return;
        }
        String s = u.getQueryParameter("sb"), t = u.getQueryParameter("token"), h = u.getQueryParameter("home"), n = u.getQueryParameter("name");
        boolean ok = s != null && s.matches("[A-Za-z0-9._-]{1,200}:[0-9]{1,5}/lw1[a-z2-7]{52}")
            && (t == null || t.matches("[0-9a-f]{32}"))
            && (h == null || h.matches("[0-9]{3}-[0-9]{3}-[0-9]{3}|lw1[a-z2-7]{52}"))
            && (n == null || (n.length() <= 40 && n.matches("[\\p{L}\\p{N} ._'-]*")));
        if (!ok) {
            android.widget.Toast.makeText(this, "That link is not a Longwire invitation", android.widget.Toast.LENGTH_LONG).show();
            return;
        }
        sb.setText(s);
        token.setText(t == null ? "" : t);
        if (h != null) {
            home.setText(h);
        }
        if (n != null && name.getText().length() == 0) {
            name.setText(n);
        }
        android.widget.Toast.makeText(this, "Invitation read: check it, then Turn on", android.widget.Toast.LENGTH_LONG).show();
    }

    private EditText field(LinearLayout col, String hint, String value, int type) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setText(value);
        e.setSingleLine(true);
        e.setInputType(type);
        col.addView(e);
        return e;
    }

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            state.setText(LongwireVpnService.status);
            String ph = LongwireVpnService.phrase;
            phrase.setText(ph.isEmpty() ? "" : "Safety phrase: " + ph);
            go.setText(LongwireVpnService.running ? "Turn off" : "Turn on");
            ui.postDelayed(this, 1000);
        }
    };

    private void toggle() {
        if (LongwireVpnService.running) {
            startService(new Intent(this, LongwireVpnService.class).setAction(LongwireVpnService.STOP));
            return;
        }
        getSharedPreferences("longwire", MODE_PRIVATE).edit()
            .putString("switchboard", sb.getText().toString()).putString("home", home.getText().toString())
            .putString("token", token.getText().toString()).putString("name", name.getText().toString())
            .putBoolean("all", all.isChecked()).apply();
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
