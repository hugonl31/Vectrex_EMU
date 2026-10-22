package io.github.hugonl31.vectrexemu;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;
import java.util.Locale;

/** The launcher's entry: the folders the app may use (granted once: the Storage Access Framework, no broad
 *  storage permission), the speech ROM, every setting, the games' overlays, the controllers, and how to set
 *  the app up in a front end. Plain views, all reachable with a D-pad or a TV remote. */
public class SettingsActivity extends Activity {
    static final int REQ_ROM_TREE = 1, REQ_OVERLAY_TREE = 2, REQ_SPEECH = 3, REQ_PLAY = 4;
    private Rows r;
    private String page = "";                       // ("": the settings; else a game's or a controller's page)

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        build();
    }

    private LinearLayout screen() {
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.BLACK);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        sv.addView(col);
        sv.setDefaultFocusHighlightEnabled(false); col.setDefaultFocusHighlightEnabled(false);   // (no grey over the page)
        setContentView(sv);
        r = new Rows(this, col, null, 1f);
        int pad = r.dp(24);
        col.setPadding(pad, pad, pad, pad);
        return col;
    }

    private void build() {
        page = ""; capture = null;
        screen();
        Prefs.Store p = Prefs.get(this);
        r.title("Vectrex_EMU", 28);
        r.text("A Vectrex emulator that doesn't try to be perfect. It tries to be a Vectrex. (Hugo Bokhorst; GPL v3)", Rows.DIM);
        r.button("Play Mine Storm (no cartridge)", v -> startActivity(new Intent(this, LaunchActivity.class).setAction("io.github.hugonl31.vectrexemu.PLAY")));
        r.button("Play a ROM...", v -> pick(Intent.ACTION_OPEN_DOCUMENT, REQ_PLAY));

        r.header("Folders");
        r.button("Choose ROM folder" + folder(p.getString(Prefs.ROM_TREE, null)), v -> pick(Intent.ACTION_OPEN_DOCUMENT_TREE, REQ_ROM_TREE));
        r.text("Front ends that pass a file path (HyperSpin) need the folder with your ROMs granted here, once. The flash"
                + " cartridges' saves (Vectorblade) go next to the ROMs, as on the desktop.", Rows.DIM);
        r.button("Choose overlay folder" + folder(p.getString(Prefs.OVERLAY_TREE, null)), v -> pick(Intent.ACTION_OPEN_DOCUMENT_TREE, REQ_OVERLAY_TREE));
        r.text("MiSTer .art overlays (overlays.zip of the MiSTer Vectrex core, unpacked) or PNGs named like the ROMs."
                + " An \"overlays\" folder next to the ROMs is found too.", Rows.DIM);
        File al2 = new File(getFilesDir(), "al2.bin");
        r.button("Choose speech ROM (al2.bin)" + (al2.exists() ? "  [installed]" : ""), v -> pick(Intent.ACTION_OPEN_DOCUMENT, REQ_SPEECH));
        r.text("The SP0256-AL2 ROM for the VecVoice / VecVox: a free download (spatula-city.org/~im14u2c/sp0256-al2/).", Rows.DIM);

        r.header("Controllers");
        r.text("Each controller's buttons, stick, dead zone and player; keyboards and remotes share one.", Rows.DIM);
        for (String d : Controls.devices(this)) r.button(d, v -> buildController(d));

        List<String> games = Prefs.recent(this);
        if (!games.isEmpty()) {
            r.header("Games");
            r.text("Each game's overlay - automatic, another one, or none - and its position and size.", Rows.DIM);
            for (String g : games) {
                String ch = p.getString(Prefs.overlayFor(g), "");
                r.button(g + (ch.isEmpty() ? "" : ch.equals("none") ? "   [no overlay]" : "   [" + ch + "]"), v -> buildGame(g));
            }
        }

        r.text("In a game, Select + R1 (or Menu, F1) opens these settings over the picture: every change shows at once.", Rows.GREEN);
        for (Object o : Prefs.ITEMS) {
            if (o instanceof String) { r.header((String) o); continue; }
            r.item((Prefs.Item) o);
        }
        r.button("New random scratches", v -> {
            Prefs.get(this).edit().putInt(Prefs.SEED, 2 + new java.util.Random().nextInt(99998)).apply();
            Toast.makeText(this, "New scratches on every overlay, from the next game on", Toast.LENGTH_SHORT).show();
        });
        r.button("Reset all settings to their defaults", v -> {
            Prefs.Store.Editor e = Prefs.get(this).edit();
            for (Object o : Prefs.ITEMS) if (o instanceof Prefs.Item) e.remove(((Prefs.Item) o).key);
            e.remove(Prefs.SEED);
            e.apply(); build();
        });
        r.text("(The folders, the controllers and each game's overlay settings are kept.)", Rows.DIM);

        r.header("In a game");
        r.text("Back, or Select + Start: quit, back to the front end\nStart: pause\nSelect + L1: the RESET button\n"
                + "Select + R1, or Menu: the settings, live\nKeyboard: Esc quit, P pause, Ctrl+R RESET, F1 the settings", Rows.GREEN);
        r.header("Front ends (HyperSpin)");
        r.text("Start the game with an intent to\n  package  " + getPackageName() + "\n  activity  " + LaunchActivity.class.getName()
                + "\n  action  android.intent.action.VIEW\n  data  the ROM: a file path, file:// or content:// URI\n"
                + "(or the path in an extra named ROM, rom, path or file). Zipped ROMs work.", Rows.GREEN);
    }

    // ---------------------------------------------------------------- a game's overlay
    /** one game: its overlay (automatic, another, none) and the chosen overlay's position and size */
    private void buildGame(String game) {
        page = "game"; capture = null;
        LinearLayout col = screen();
        Prefs.Store p = Prefs.get(this);
        r.title(game, 24);
        List<Overlays.Cand> cs = Overlays.candidates(this, null);
        Overlays.Cand auto = Overlays.find(cs, game);
        List<String> names = new java.util.ArrayList<>();
        names.add(""); names.add("none");
        for (Overlays.Cand c : cs) if (!names.contains(c.name)) names.add(c.name);
        String key = Prefs.overlayFor(game);
        String cur = p.getString(key, "");
        r.button("Overlay:  " + (cur.isEmpty() ? "Automatic (" + (auto != null ? auto.name : "none found") + ")" : cur.equals("none") ? "None" : cur), v -> {
            int k = Math.max(0, names.indexOf(Prefs.get(this).getString(key, "")));
            String n = names.get((k + 1) % names.size());
            if (n.isEmpty()) Prefs.get(this).edit().remove(key).apply(); else Prefs.get(this).edit().putString(key, n).apply();
            buildGame(game);
        });
        r.text("Overlays in the overlay folder and the app's own; the automatic choice also looks next to the ROM.", Rows.DIM);
        String ov = cur.isEmpty() ? (auto != null ? auto.name : null) : cur.equals("none") ? null : cur;
        if (ov != null) {
            r.header("Position and size of " + ov);
            String[] labels = { "Left / right (picture heights)", "Up / down (picture heights)", "Size" };
            float[][] range = { { -0.05f, 0.05f, 0f }, { -0.05f, 0.05f, 0f }, { 0.9f, 1.1f, 1f } };
            for (int i = 0; i < 3; i++) {
                final int idx = i;
                TextView t = new TextView(this); t.setTextColor(Color.WHITE); t.setTextSize(16); t.setPadding(r.dp(8), r.dp(8), 0, 0);
                SeekBar sb = new SeekBar(this); sb.setMax(100); sb.setKeyProgressIncrement(1);
                float[] a = Prefs.adjust(this, ov);
                sb.setProgress(Math.round((a[i] - range[i][0]) / (range[i][1] - range[i][0]) * 100));
                Runnable lab = () -> t.setText(String.format(Locale.ROOT, idx == 2 ? "%s:  x%.3f" : "%s:  %+.3f", labels[idx], Prefs.adjust(this, ov)[idx]));
                lab.run();
                sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar s, int pr, boolean user) {
                        if (!user) return;
                        float[] c = Prefs.adjust(SettingsActivity.this, ov);
                        c[idx] = range[idx][0] + (range[idx][1] - range[idx][0]) * pr / 100f;
                        if (Math.abs(c[idx] - range[idx][2]) < 0.0011f) c[idx] = range[idx][2];
                        Prefs.get(SettingsActivity.this).edit().putString(Prefs.adjustOf(ov), c[0] + "," + c[1] + "," + c[2]).apply();
                        lab.run();
                    }
                    public void onStartTrackingTouch(SeekBar s) { }
                    public void onStopTrackingTouch(SeekBar s) { }
                });
                col.addView(t); col.addView(sb);
            }
            r.button("Reset position and size", v -> { Prefs.get(this).edit().remove(Prefs.adjustOf(ov)).apply(); buildGame(game); });
        }
        r.button("Back", v -> build());
    }

    // ---------------------------------------------------------------- a controller's mapping
    private String capture;                 // (a button being set: "device|input")
    private Button captureButton;

    private void buildController(String device) {
        page = "controller"; capture = null;
        screen();
        boolean kb = Controls.KEYBOARD.equals(device);
        Controls.Map m = Controls.get(this, device);
        r.title(device, 24);
        r.text(kb ? "Keyboards and TV remotes. The arrow keys (the D-pad) are the stick." : "Press a row, then the button on this controller.", Rows.DIM);
        for (int i = 0; i < 5; i++) {
            final String input = Controls.BUTTONS[i];
            final String label = Controls.BUTTON_NAMES[i];
            Button[] holder = new Button[1];
            holder[0] = r.button(label + ":  " + Controls.keyName(m.keys[i]), v -> {
                capture = device + "|" + input; captureButton = holder[0];
                holder[0].setText(label + ":  press a button now (Back: none)");
            });
        }
        if (!kb) {
            r.button("Stick:  " + (m.stick.equals("left") ? "the left stick" : m.stick.equals("right") ? "the right stick" : "the D-pad only"), v -> {
                String n = m.stick.equals("left") ? "right" : m.stick.equals("right") ? "dpad" : "left";
                Prefs.get(this).edit().putString(Prefs.mapOf(device, "stick"), n).apply(); buildController(device);
            });
            TextView t = new TextView(this); t.setTextColor(Color.WHITE); t.setTextSize(16); t.setPadding(r.dp(8), r.dp(8), 0, 0);
            SeekBar sb = new SeekBar(this); sb.setMax(50); sb.setKeyProgressIncrement(1);
            sb.setProgress(Math.round(m.dz * 100));
            t.setText(String.format(Locale.ROOT, "Dead zone:  %.2f", m.dz));
            sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar s, int pr, boolean user) {
                    if (!user) return;
                    Prefs.get(SettingsActivity.this).edit().putString(Prefs.mapOf(device, "dz"), Float.toString(pr / 100f)).apply();
                    t.setText(String.format(Locale.ROOT, "Dead zone:  %.2f", pr / 100f));
                }
                public void onStartTrackingTouch(SeekBar s) { }
                public void onStopTrackingTouch(SeekBar s) { }
            });
            addRow(t); addRow(sb);
        }
        r.button("Player:  " + (m.player.equals("auto") ? "automatic (the first controller used: 1, the next: 2)" : m.player), v -> {
            String n = m.player.equals("auto") ? "1" : m.player.equals("1") ? "2" : "auto";
            Prefs.get(this).edit().putString(Prefs.mapOf(device, "player"), n).apply(); buildController(device);
        });
        r.button("Reset this controller", v -> { Controls.reset(this, device); buildController(device); });
        r.button("Back", v -> build());
    }
    private void addRow(android.view.View v) { r.col.addView(v); }

    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        if (capture != null && ev.getAction() == KeyEvent.ACTION_DOWN && ev.getRepeatCount() == 0) {
            int code = ev.getKeyCode();
            String[] f = capture.split("\\|");
            InputDevice d = ev.getDevice();
            boolean back = code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE;
            if (!back && !Controls.nameOf(d).equals(f[0])) return true;     // (another controller's button: not this one's)
            Prefs.get(this).edit().putString(Prefs.mapOf(f[0], f[1]), back ? "0" : Integer.toString(code)).apply();
            capture = null;
            buildController(f[0]);
            return true;
        }
        if (capture != null) return true;
        return super.dispatchKeyEvent(ev);
    }

    @Override
    public void onBackPressed() {                        // (from a page: back to the settings; from them: out)
        if (!page.isEmpty()) build(); else super.onBackPressed();
    }

    // ---------------------------------------------------------------- folders and files
    private static String folder(String uri) {
        if (uri == null) return "";
        try { return "  [" + DocumentsContract.getTreeDocumentId(Uri.parse(uri)) + "]"; } catch (RuntimeException e) { return "  [set]"; }
    }

    private void pick(String action, int req) {
        Intent i = new Intent(action);
        if (action.equals(Intent.ACTION_OPEN_DOCUMENT)) { i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*"); }
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        if (req == REQ_ROM_TREE) i.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);      // (the saves next to the ROMs)
        try { startActivityForResult(i, req); }
        catch (RuntimeException e) { Toast.makeText(this, "No file picker on this device", Toast.LENGTH_LONG).show(); }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        if (req == REQ_ROM_TREE || req == REQ_OVERLAY_TREE) {
            try {
                if (req == REQ_ROM_TREE) {
                    try { getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION); }
                    catch (SecurityException e) { getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
                } else getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) { Toast.makeText(this, "The folder could not be kept: " + e.getMessage(), Toast.LENGTH_LONG).show(); return; }
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
}
