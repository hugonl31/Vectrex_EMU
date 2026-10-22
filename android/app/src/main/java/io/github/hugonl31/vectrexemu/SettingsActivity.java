package io.github.hugonl31.vectrexemu;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;

/** The launcher's entry: the folders the app may read (granted once: the Storage Access Framework, no
 *  broad storage permission), the speech ROM, every setting, and how to set the app up in a front end.
 *  Plain views, all reachable with a D-pad or a TV remote. */
public class SettingsActivity extends Activity {
    static final int REQ_ROM_TREE = 1, REQ_OVERLAY_TREE = 2, REQ_SPEECH = 3, REQ_PLAY = 4;
    static final int GREEN = Color.rgb(124, 255, 176), DIM = Color.rgb(90, 160, 120);
    private LinearLayout col;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        build();
    }

    private void build() {
        inGame = false;
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.BLACK);
        col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        col.setPadding(pad, pad, pad, pad);
        sv.addView(col);
        setContentView(sv);
        SharedPreferences p = Prefs.get(this);

        title("Vectrex_EMU", 28);
        text("A Vectrex emulator that doesn't try to be perfect. It tries to be a Vectrex. (Hugo Bokhorst; GPL v3)", DIM);
        button("Play Mine Storm (no cartridge)", v -> startActivity(new Intent(this, LaunchActivity.class).setAction("io.github.hugonl31.vectrexemu.PLAY")));
        button("Play a ROM...", v -> pick(Intent.ACTION_OPEN_DOCUMENT, REQ_PLAY));

        header("Folders");
        button("Choose ROM folder" + folder(p.getString(Prefs.ROM_TREE, null)), v -> pick(Intent.ACTION_OPEN_DOCUMENT_TREE, REQ_ROM_TREE));
        text("Front ends that pass a file path (HyperSpin) need the folder with your ROMs granted here, once.", DIM);
        button("Choose overlay folder" + folder(p.getString(Prefs.OVERLAY_TREE, null)), v -> pick(Intent.ACTION_OPEN_DOCUMENT_TREE, REQ_OVERLAY_TREE));
        text("MiSTer .art overlays (overlays.zip of the MiSTer Vectrex core, unpacked) or PNGs named like the ROMs."
                + " An \"overlays\" folder next to the ROMs is found too.", DIM);
        File al2 = new File(getFilesDir(), "al2.bin");
        button("Choose speech ROM (al2.bin)" + (al2.exists() ? "  [installed]" : ""), v -> pick(Intent.ACTION_OPEN_DOCUMENT, REQ_SPEECH));
        text("The SP0256-AL2 ROM for the VecVoice / VecVox: a free download (spatula-city.org/~im14u2c/sp0256-al2/).", DIM);

        java.util.List<String> games = Prefs.recent(this);
        if (!games.isEmpty()) {
            header("Games");
            text("Each game's overlay - automatic, another one, or none - and its position and size.", DIM);
            for (String g : games) {
                String ch = p.getString(Prefs.overlayFor(g), "");
                button(g + (ch.isEmpty() ? "" : ch.equals("none") ? "   [no overlay]" : "   [" + ch + "]"), v -> buildGame(g));
            }
        }

        for (Object o : Prefs.ITEMS) {
            if (o instanceof String) { header((String) o); continue; }
            Prefs.Item it = (Prefs.Item) o;
            switch (it.kind) {
                case BOOL: check(it, p); break;
                case CHOICE: choice(it, p); break;
                case SLIDER: slider(it, p); break;
            }
            if (it.help != null) text(it.help, DIM);
        }
        button("New random scratches", v -> {
            Prefs.get(this).edit().putInt(Prefs.SEED, 2 + new java.util.Random().nextInt(99998)).apply();
            Toast.makeText(this, "New scratches on every overlay, from the next game on", Toast.LENGTH_SHORT).show();
        });
        button("Reset all settings to their defaults", v -> {
            SharedPreferences.Editor e = Prefs.get(this).edit();
            for (Object o : Prefs.ITEMS) if (o instanceof Prefs.Item) e.remove(((Prefs.Item) o).key);
            e.remove(Prefs.SEED);
            e.apply(); build();
        });
        text("(The folders and each game's overlay settings are kept.)", DIM);

        header("Front ends (HyperSpin)");
        text("Start the game with an intent to\n  package  " + getPackageName() + "\n  activity  " + LaunchActivity.class.getName()
                + "\n  action  android.intent.action.VIEW\n  data  the ROM: a file path, file:// or content:// URI\n"
                + "(or the path in an extra named ROM, rom, path or file). Zipped ROMs work.\n"
                + "In game: Back, or Select + Start, returns to the front end.", GREEN);
    }

    /** one game: its overlay (automatic, another, none) and the chosen overlay's position and size */
    private void buildGame(String game) {
        inGame = true;
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.BLACK);
        col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(24);
        col.setPadding(pad, pad, pad, pad);
        sv.addView(col);
        setContentView(sv);
        SharedPreferences p = Prefs.get(this);
        title(game, 24);
        java.util.List<Overlays.Cand> cs = Overlays.candidates(this, null);
        Overlays.Cand auto = Overlays.find(cs, game);
        java.util.List<String> names = new java.util.ArrayList<>();
        names.add(""); names.add("none");
        for (Overlays.Cand c : cs) if (!names.contains(c.name)) names.add(c.name);
        String key = Prefs.overlayFor(game);
        Button bt = new Button(this); bt.setAllCaps(false); bt.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        Runnable show = () -> {
            String c = p.getString(key, "");
            bt.setText("Overlay:  " + (c.isEmpty() ? "Automatic (" + (auto != null ? auto.name : "none found") + ")" : c.equals("none") ? "None" : c));
        };
        show.run();
        bt.setOnClickListener(v -> {
            int k = Math.max(0, names.indexOf(p.getString(key, "")));
            String n = names.get((k + 1) % names.size());
            if (n.isEmpty()) p.edit().remove(key).apply(); else p.edit().putString(key, n).apply();
            buildGame(game);
        });
        col.addView(bt, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        text("Overlays in the overlay folder and the app's own; the automatic choice also looks next to the ROM.", DIM);
        String c = p.getString(key, "");
        String ov = c.isEmpty() ? (auto != null ? auto.name : null) : c.equals("none") ? null : c;
        if (ov != null) {
            header("Position and size of " + ov);
            float[] a = Prefs.adjust(this, ov);
            String[] labels = { "Left / right (picture heights)", "Up / down (picture heights)", "Size" };
            float[][] range = { { -0.05f, 0.05f, 0f }, { -0.05f, 0.05f, 0f }, { 0.9f, 1.1f, 1f } };
            for (int i = 0; i < 3; i++) {
                final int idx = i;
                TextView t = new TextView(this); t.setTextColor(Color.WHITE); t.setTextSize(16); t.setPadding(dp(8), dp(8), 0, 0);
                SeekBar sb = new SeekBar(this); sb.setMax(100); sb.setKeyProgressIncrement(1);
                sb.setProgress(Math.round((a[i] - range[i][0]) / (range[i][1] - range[i][0]) * 100));
                Runnable lab = () -> t.setText(String.format(Locale.ROOT, idx == 2 ? "%s:  x%.3f" : "%s:  %+.3f", labels[idx], Prefs.adjust(this, ov)[idx]));
                lab.run();
                sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar s, int pr, boolean user) {
                        float[] cur = Prefs.adjust(SettingsActivity.this, ov);
                        cur[idx] = range[idx][0] + (range[idx][1] - range[idx][0]) * pr / 100f;
                        if (Math.abs(cur[idx] - range[idx][2]) < 0.0011f) cur[idx] = range[idx][2];
                        p.edit().putString(Prefs.adjustOf(ov), cur[0] + "," + cur[1] + "," + cur[2]).apply();
                        lab.run();
                    }
                    public void onStartTrackingTouch(SeekBar s) { }
                    public void onStopTrackingTouch(SeekBar s) { }
                });
                col.addView(t); col.addView(sb);
            }
            button("Reset position and size", v -> { p.edit().remove(Prefs.adjustOf(ov)).apply(); buildGame(game); });
        }
        button("Back", v -> build());
    }

    @Override
    public void onBackPressed() {                        // (from a game's page: back to the settings; from them: out)
        if (inGame) build(); else super.onBackPressed();
    }
    private boolean inGame;

    private static String folder(String uri) {
        if (uri == null) return "";
        try { return "  [" + DocumentsContract.getTreeDocumentId(Uri.parse(uri)) + "]"; } catch (RuntimeException e) { return "  [set]"; }
    }

    private void pick(String action, int req) {
        Intent i = new Intent(action);
        if (action.equals(Intent.ACTION_OPEN_DOCUMENT)) { i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*"); }
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try { startActivityForResult(i, req); }
        catch (RuntimeException e) { Toast.makeText(this, "No file picker on this device", Toast.LENGTH_LONG).show(); }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        if (req == REQ_ROM_TREE || req == REQ_OVERLAY_TREE) {
            try { getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (SecurityException e) { Toast.makeText(this, "The folder could not be kept: " + e.getMessage(), Toast.LENGTH_LONG).show(); return; }
            Prefs.get(this).edit().putString(req == REQ_ROM_TREE ? Prefs.ROM_TREE : Prefs.OVERLAY_TREE, u.toString()).apply();
        } else if (req == REQ_SPEECH) {
            try (InputStream is = getContentResolver().openInputStream(u); FileOutputStream o = new FileOutputStream(new File(getFilesDir(), "al2.bin"))) {
                byte[] d = RomSource.all(is);
                if (d.length != 2048) throw new IOException("not the SP0256-AL2 ROM (" + d.length + " bytes, it has 2048)");
                o.write(d);
            } catch (IOException e) {
                new File(getFilesDir(), "al2.bin").delete();
                Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            }
        } else if (req == REQ_PLAY) {
            Intent go = new Intent(this, LaunchActivity.class).setAction(Intent.ACTION_VIEW).setData(u);
            go.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(go);
            return;
        }
        build();
    }

    // ---------------------------------------------------------------- the rows
    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }
    private void title(String s, int size) {
        TextView t = new TextView(this); t.setText(s); t.setTextColor(GREEN); t.setTextSize(size); t.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        col.addView(t);
    }
    private void header(String s) {
        TextView t = new TextView(this); t.setText(s); t.setTextColor(GREEN); t.setTextSize(20); t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setPadding(0, dp(20), 0, dp(6)); col.addView(t);
    }
    private void text(String s, int color) {
        TextView t = new TextView(this); t.setText(s); t.setTextColor(color); t.setTextSize(14); t.setPadding(dp(8), 0, 0, dp(6));
        col.addView(t);
    }
    private void button(String s, View.OnClickListener l) {
        Button bt = new Button(this); bt.setText(s); bt.setAllCaps(false); bt.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        bt.setOnClickListener(l);
        col.addView(bt, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    }
    private void check(Prefs.Item it, SharedPreferences p) {
        CheckBox c = new CheckBox(this); c.setText(it.label); c.setTextColor(Color.WHITE); c.setTextSize(16);
        c.setChecked("1".equals(Prefs.value(p, it)));
        c.setOnCheckedChangeListener((v, on) -> p.edit().putString(it.key, on ? "1" : "0").apply());
        col.addView(c);
    }
    private void choice(Prefs.Item it, SharedPreferences p) {
        Button bt = new Button(this); bt.setAllCaps(false); bt.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        Runnable show = () -> {
            String v = Prefs.value(p, it); String n = v;
            for (int i = 0; i < it.values.length; i++) if (it.values[i].equals(v)) n = it.names[i];
            bt.setText(it.label + ":  " + n);
        };
        show.run();
        bt.setOnClickListener(v -> {
            String cur = Prefs.value(p, it); int k = 0;
            for (int i = 0; i < it.values.length; i++) if (it.values[i].equals(cur)) k = i;
            p.edit().putString(it.key, it.values[(k + 1) % it.values.length]).apply();
            show.run();
        });
        col.addView(bt, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    }
    private void slider(Prefs.Item it, SharedPreferences p) {
        TextView t = new TextView(this); t.setTextColor(Color.WHITE); t.setTextSize(16); t.setPadding(dp(8), dp(8), 0, 0);
        SeekBar sb = new SeekBar(this); sb.setMax(100); sb.setKeyProgressIncrement(2);
        float v; try { v = Float.parseFloat(Prefs.value(p, it)); } catch (NumberFormatException e) { v = Float.parseFloat(it.def); }
        sb.setProgress(Math.round((v - it.min) / (it.max - it.min) * 100));
        float def = Float.parseFloat(it.def);
        Runnable show = () -> {
            float x = it.min + (it.max - it.min) * sb.getProgress() / 100f;
            t.setText(String.format(Locale.ROOT, "%s:  %.3g%s", it.label, x, Math.abs(x - def) < (it.max - it.min) * 0.006 ? "  (default)" : ""));
        };
        show.run();
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar s, int pr, boolean user) {
                float x = it.min + (it.max - it.min) * pr / 100f;
                if (Math.abs(x - def) < (it.max - it.min) * 0.006) x = def;          // (the default exactly, near it)
                p.edit().putString(it.key, Float.toString(x)).apply(); show.run();
            }
            public void onStartTrackingTouch(SeekBar s) { }
            public void onStopTrackingTouch(SeekBar s) { }
        });
        col.addView(t); col.addView(sb);
    }
}
