package io.github.hugonl31.vectrexemu;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.Locale;

/** The rows of the settings screens (the settings and the in-game menu): titles, notes, buttons and one
 *  row per setting - a check box, a choice that steps on with each press, a slider - all reachable with a
 *  D-pad, a controller or a TV remote. A changed setting is saved and handed to `changed` (the in-game
 *  menu: to the running emulator). */
final class Rows {
    static final int GREEN = Color.rgb(124, 255, 176), DIM = Color.rgb(90, 160, 120);
    interface Changed { void set(String key, String value); }

    final Context c; final LinearLayout col; final Changed changed; final float scale;
    Rows(Context c, LinearLayout col, Changed changed, float textScale) { this.c = c; this.col = col; this.changed = changed; this.scale = textScale; }

    int dp(int v) { return Math.round(v * c.getResources().getDisplayMetrics().density); }

    void title(String s, int size) {
        TextView t = new TextView(c); t.setText(s); t.setTextColor(GREEN); t.setTextSize(size * scale); t.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        col.addView(t);
    }
    void header(String s) {
        TextView t = new TextView(c); t.setText(s); t.setTextColor(GREEN); t.setTextSize(20 * scale); t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(0, dp(16), 0, dp(4)); col.addView(t);
    }
    void text(String s, int color) {
        TextView t = new TextView(c); t.setText(s); t.setTextColor(color); t.setTextSize(14 * scale); t.setPadding(dp(8), 0, 0, dp(6));
        col.addView(t);
    }
    Button button(String s, View.OnClickListener l) {
        Button bt = new Button(c); bt.setText(s); bt.setAllCaps(false); bt.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        bt.setTextSize(16 * scale); bt.setOnClickListener(l);
        col.addView(bt, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return bt;
    }

    private void save(String key, String v) {
        Prefs.get(c).edit().putString(key, v).apply();
        if (changed != null) changed.set(key, v);
    }

    // a slider's value: shown at once, written to the settings file when it rests (a quarter second: a drag or
    // a held key moves it many times a second, and each write rewrites the file)
    private final android.os.Handler later = new android.os.Handler(android.os.Looper.getMainLooper());
    private final java.util.Map<String, String> pending = new java.util.LinkedHashMap<>();
    private final Runnable write = this::flush;
    void saveLater(String key, String v) {
        pending.put(key, v);
        later.removeCallbacks(write); later.postDelayed(write, 250);
    }
    void flush() {
        later.removeCallbacks(write);
        if (pending.isEmpty()) return;
        Prefs.Store.Editor e = Prefs.get(c).edit();
        for (java.util.Map.Entry<String, String> kv : pending.entrySet()) e.putString(kv.getKey(), kv.getValue());
        e.apply();
        pending.clear();
    }

    void item(Prefs.Item it) {
        switch (it.kind) {
            case BOOL: check(it); break;
            case CHOICE: choice(it); break;
            case SLIDER: slider(it); break;
        }
        if (it.help != null) text(it.help, DIM);
    }
    void check(Prefs.Item it) {
        CheckBox cb = new CheckBox(c); cb.setText(it.label); cb.setTextColor(Color.WHITE); cb.setTextSize(16 * scale);
        cb.setChecked("1".equals(Prefs.value(Prefs.get(c), it)));
        cb.setOnCheckedChangeListener((v, on) -> save(it.key, on ? "1" : "0"));
        col.addView(cb);
    }
    void choice(Prefs.Item it) {
        Button bt = button("", null);
        Runnable show = () -> {
            String v = Prefs.value(Prefs.get(c), it), n = v;
            for (int i = 0; i < it.values.length; i++) if (it.values[i].equals(v)) n = it.names[i];
            bt.setText(it.label + ":  " + n);
        };
        show.run();
        bt.setOnClickListener(v -> {
            String cur = Prefs.value(Prefs.get(c), it); int k = 0;
            for (int i = 0; i < it.values.length; i++) if (it.values[i].equals(cur)) k = i;
            save(it.key, it.values[(k + 1) % it.values.length]);
            show.run();
        });
    }
    void slider(Prefs.Item it) {
        TextView t = new TextView(c); t.setTextColor(Color.WHITE); t.setTextSize(16 * scale); t.setPadding(dp(8), dp(8), 0, 0);
        SeekBar sb = new SeekBar(c); sb.setMax(100); sb.setKeyProgressIncrement(2);
        float v; try { v = Float.parseFloat(Prefs.value(Prefs.get(c), it)); } catch (NumberFormatException e) { v = Float.parseFloat(it.def); }
        sb.setProgress(Math.round((v - it.min) / (it.max - it.min) * 100));
        float def = Float.parseFloat(it.def);
        Runnable show = () -> {
            float x = it.min + (it.max - it.min) * sb.getProgress() / 100f;
            t.setText(String.format(Locale.ROOT, "%s:  %.3g%s", it.label, x, Math.abs(x - def) < (it.max - it.min) * 0.006 ? "  (default)" : ""));
        };
        show.run();
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int pr, boolean user) {
                if (!user) return;
                float x = it.min + (it.max - it.min) * pr / 100f;
                if (Math.abs(x - def) < (it.max - it.min) * 0.006) x = def;          // (the default exactly, near it)
                saveLater(it.key, Float.toString(x)); if (changed != null) changed.set(it.key, Float.toString(x));
                show.run();
            }
            public void onStartTrackingTouch(SeekBar s) { }
            public void onStopTrackingTouch(SeekBar s) { }
        });
        col.addView(t); col.addView(sb);
    }
}
