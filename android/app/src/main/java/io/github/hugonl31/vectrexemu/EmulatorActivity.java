package io.github.hugonl31.vectrexemu;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.SparseArray;
import android.util.SparseIntArray;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/** The game: full screen, the picture drawn by the native side (engine.cpp) into a SurfaceView, the
 *  controllers read here. Runs in the ":emu" process, which ends with the game.
 *    Back, or Select + Start      quit (straight back to the front end)
 *    Start                        the in-game menu, the game paused (Start or B: back to the game)
 *    Select + L1                  the RESET button                                 keyboard: Ctrl+R
 *    Select + R1, or Menu         the in-game menu, the game going on behind it    keyboard: F1
 *    (pause alone: the menu's Pause, or P on a keyboard, as the desktop) */
public class EmulatorActivity extends Activity implements SurfaceHolder.Callback {
    static { System.loadLibrary("vectrexemu"); }
    static native void nativeStart(String[] keys, String[] values, byte[] bios, byte[] cart, String sav,
                                   byte[] art, int artW, int artH, float artSheet, String artName, byte[] onWav, byte[] offWav);
    static native void nativePowerOff();
    static native void nativeSurface(Surface s);
    static native void nativeInput(int player, int jx, int jy, int buttons);
    static native void nativeReset();
    static native void nativePause(boolean p);
    static native void nativeUserPause(boolean p);
    static native void nativeSet(String key, String value);
    static native String nativeStats();
    static native void nativeStop();
    static native String nativeRenderer();

    static final String TAG = "VectrexEMU";
    private boolean started, quitting, offEffect, userPaused;
    private boolean menuPaused;                         // (the menu opened with Start paused the game: closing it goes on)
    private final Handler ui = new Handler(Looper.getMainLooper());
    private String game, overlayName;
    private Saves.Home savHome; private File savApp;

    // ---------------------------------------------------------------- the players' controllers
    private static final class Pad { float ax, ay, hx, hy; int dpad, buttons; boolean half; }   // dpad: 1 up 2 down 4 left 8 right
    private final Pad[] pads = { new Pad(), new Pad() };
    private final SparseIntArray player = new SparseIntArray();     // device id -> player
    private final SparseArray<Controls.Map> maps = new SparseArray<>();
    private boolean selectHeld, startHeld, comboUsed;

    // ---------------------------------------------------------------- the screen's parts
    private FrameLayout root;
    private TextView osd;
    private ScrollView menu;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        String o = Prefs.value(this, Prefs.ORIENT);         // (Settings > Screen: a device on its side)
        setRequestedOrientation(o.equals("portrait") ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                : o.equals("reverse_portrait") ? ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        String err = getIntent().getStringExtra(LaunchActivity.X_ERROR);
        if (err != null) { message(err + "\n\nBack: return"); return; }
        try {
            start();
        } catch (IOException e) {
            message("Cannot start: " + e.getMessage() + "\n\nBack: return");
            return;
        }
        root = new FrameLayout(this);
        SurfaceView sv = new SurfaceView(this);
        sv.getHolder().addCallback(this);
        root.addView(sv);
        osd = new TextView(this);                            // the on-screen display (Settings > On-screen display)
        osd.setTextColor(Rows.GREEN); osd.setTypeface(Typeface.MONOSPACE); osd.setTextSize(13); osd.setPadding(24, 8, 24, 12);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.START);
        root.addView(osd, lp);
        showOsd("1".equals(Prefs.value(this, Prefs.OSD)));
        setContentView(root);
        immersive();
        // (focused, the picture gets the keys; no focus highlight: with a controller or keyboard Android draws a
        //  translucent grey over the focused view - over the whole picture)
        sv.setDefaultFocusHighlightEnabled(false); root.setDefaultFocusHighlightEnabled(false);
        sv.setFocusable(true); sv.requestFocus();
    }

    private void start() throws IOException {
        byte[] bios = asset("bios/bios.bin");
        String romFile = getIntent().getStringExtra(LaunchActivity.X_ROM_FILE);
        String romName = getIntent().getStringExtra(LaunchActivity.X_ROM_NAME);
        byte[] cart = romFile != null ? RomSource.all(new FileInputStream(romFile)) : new byte[0];
        game = romFile != null ? RomSource.stem(romName) : "Mine Storm";
        // the flash cartridges' saved state: next to the ROM as on the desktop (Saves), the core's copy in the app
        savApp = Saves.app(this, romFile != null ? romName : "Mine Storm.bin");
        String sh = getIntent().getStringExtra(LaunchActivity.X_SAV_HOME);
        savHome = sh != null ? Saves.Home.decode(sh) : null;
        Saves.before(this, savHome, savApp, game);
        // the overlay
        RomSource.Rom where = new RomSource.Rom();
        String tree = getIntent().getStringExtra(LaunchActivity.X_TREE), dir = getIntent().getStringExtra(LaunchActivity.X_DIR);
        if (tree != null) { where.tree = Uri.parse(tree); where.parentDoc = getIntent().getStringExtra(LaunchActivity.X_PARENT); }
        if (dir != null) where.dir = new File(dir);
        Overlays.Art art = null;
        String choice = Prefs.get(this).getString(Prefs.overlayFor(game), "");    // (Settings > Games: per game)
        if (!"0".equals(Prefs.value(this, "art_on")) && !choice.equals("none")) {
            List<Overlays.Cand> cs = Overlays.candidates(this, romFile != null ? where : null);
            Overlays.Cand c = null;
            if (!choice.isEmpty()) for (Overlays.Cand k : cs) if (k.name.equals(choice)) c = k;
            if (c == null) c = Overlays.find(cs, game);
            if (c != null) art = Overlays.load(this, c);
            Log.i(TAG, "overlay for " + game + ": " + (art != null ? art.name + " " + art.w + "x" + art.h : "none") + " (" + cs.size() + " looked at)");
        }
        overlayName = art != null ? art.name : null;
        float[] adj = Prefs.adjust(this, overlayName);
        File al2 = new File(getFilesDir(), "al2.bin");
        String[][] conf = Prefs.forNative(this, "speech_rom", al2.exists() ? al2.getPath() : null,
                "art_dx", Float.toString(adj[0]), "art_dy", Float.toString(adj[1]), "art_scale", Float.toString(adj[2]));
        offEffect = "1".equals(Prefs.value(this, Prefs.OFF_EFFECT));
        nativeStart(conf[0], conf[1], bios, cart, savApp.getPath(), art != null ? art.rgba : null, art != null ? art.w : 0, art != null ? art.h : 0,
                    art != null ? art.sheet : 0f, art != null ? art.name : "", asset("sounds/on.wav"), asset("sounds/off.wav"));
        started = true;
    }

    private byte[] asset(String name) throws IOException {
        try (InputStream is = getAssets().open(name)) { return RomSource.all(is); }
    }

    private void message(String text) {
        TextView t = new TextView(this);
        t.setText(text); t.setTextColor(Rows.GREEN); t.setBackgroundColor(Color.BLACK);
        t.setTextSize(18); t.setGravity(Gravity.CENTER); t.setPadding(64, 64, 64, 64);
        setContentView(t);
        immersive();
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
        if (Build.VERSION.SDK_INT >= 28)
            getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
    }

    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) immersive(); }
    @Override protected void onPause() { super.onPause(); if (started) nativePause(true); }
    @Override protected void onResume() { super.onResume(); if (started) nativePause(false); }

    @Override public void surfaceCreated(SurfaceHolder h) { }
    @Override public void surfaceChanged(SurfaceHolder h, int fmt, int w, int hh) { if (started) nativeSurface(h.getSurface()); }
    @Override public void surfaceDestroyed(SurfaceHolder h) { if (started) nativeSurface(null); }

    /** back to the front end: the core saves (flash cartridges), the save goes next to the ROM, the process
     *  ends; with Switch off when quitting, first half a second of the tube going dark and the switch's click */
    private void quit() {
        if (quitting) return;
        quitting = true;
        Runnable leave = () -> {
            if (started) { nativeStop(); Saves.after(this, savHome, savApp); }
            finishAndRemoveTask();
            overridePendingTransition(0, 0);
            ui.postDelayed(() -> System.exit(0), 250);
        };
        if (started && offEffect) { nativePowerOff(); ui.postDelayed(leave, 600); }
        else leave.run();
    }

    @Override public void onBackPressed() { if (menu != null) closeMenu(); else quit(); }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (!quitting) { quitting = true; if (started) { nativeStop(); Saves.after(this, savHome, savApp); } System.exit(0); }
    }

    // ---------------------------------------------------------------- pause, RESET, the on-screen display
    private void pause(boolean p) {
        userPaused = p;
        if (started) nativeUserPause(p);
        osdText();
    }
    private final Runnable osdTick = new Runnable() {
        public void run() { osdText(); ui.postDelayed(this, 500); }
    };
    private boolean osdOn;
    private void showOsd(boolean on) {
        osdOn = on;
        ui.removeCallbacks(osdTick);
        if (on) ui.post(osdTick); else osdText();
    }
    private void osdText() {
        if (osd == null) return;
        String s = osdOn && started ? nativeStats() : "";
        if (userPaused) s = (s.isEmpty() ? "" : s + "   ") + "PAUSED";
        osd.setText(s);
        osd.setVisibility(s.isEmpty() ? View.GONE : View.VISIBLE);
    }

    // ---------------------------------------------------------------- the in-game menu
    private void openMenu(boolean pausing) {
        if (menu != null || !started) return;
        if (pausing && !userPaused) { pause(true); menuPaused = true; }
        for (int p = 0; p < 2; p++) { Pad s = pads[p]; s.buttons = 0; s.dpad = 0; s.ax = s.ay = s.hx = s.hy = 0; s.half = false; send(p); }
        menu = new ScrollView(this);
        menu.setBackgroundColor(Color.argb(225, 0, 0, 0));
        menu.setDefaultFocusHighlightEnabled(false);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(32, 24, 32, 24);
        menu.addView(col);
        Rows r = new Rows(this, col, (k, v) -> {
            if (k.equals(Prefs.OSD)) showOsd("1".equals(v));
            else nativeSet(k, v);
        }, 0.85f);
        r.title("Vectrex_EMU", 22);
        r.text(game + "   (B or Back: back to the game)", Rows.DIM);
        View first = r.button("Back to the game", v -> closeMenu());
        if (menuPaused) r.button("Stay paused", v -> { menuPaused = false; closeMenu(); });
        else r.button(userPaused ? "Continue (P)" : "Pause (P)", v -> { pause(!userPaused); closeMenu(); });
        r.button("RESET (Select + L1)", v -> { nativeReset(); closeMenu(); });
        r.button("Quit (Select + Start)", v -> quit());
        r.text(menuPaused ? "Every change shows at once and is kept. (Select + R1: the menu with the game going on behind it.)"
                          : "The game goes on behind the menu: every change shows at once and is kept.", Rows.DIM);
        for (Object o : Prefs.ITEMS) {
            if (o instanceof String) { if (!o.equals("Vectrex")) r.header((String) o); continue; }
            Prefs.Item it = (Prefs.Item) o;
            if (it.live) r.item(it);
        }
        r.button("New random scratches", v -> {
            int seed = 2 + new java.util.Random().nextInt(99998);
            Prefs.get(this).edit().putInt(Prefs.SEED, seed).apply();
            nativeSet(Prefs.SEED, Integer.toString(seed));
        });
        if (overlayName != null) {                           // (this overlay's position and size, kept per overlay)
            r.header("Position and size of " + overlayName);
            String[] keys = { "art_dx", "art_dy", "art_scale" };
            String[] labels = { "Left / right", "Up / down", "Size" };
            float[][] range = { { -0.05f, 0.05f, 0f }, { -0.05f, 0.05f, 0f }, { 0.9f, 1.1f, 1f } };
            for (int i = 0; i < 3; i++) adjustRow(r, col, keys[i], labels[i], range[i], i);
        }
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(Math.min(getResources().getDisplayMetrics().widthPixels / 2, r.dp(460)),
                ViewGroup.LayoutParams.MATCH_PARENT, Gravity.END);
        root.addView(menu, lp);
        first.requestFocus();
    }
    private void adjustRow(Rows r, LinearLayout col, String key, String label, float[] range, int idx) {
        TextView t = new TextView(this); t.setTextColor(Color.WHITE); t.setTextSize(14); t.setPadding(r.dp(8), r.dp(8), 0, 0);
        android.widget.SeekBar sb = new android.widget.SeekBar(this); sb.setMax(100); sb.setKeyProgressIncrement(1);
        float[] a = Prefs.adjust(this, overlayName);
        sb.setProgress(Math.round((a[idx] - range[0]) / (range[1] - range[0]) * 100));
        Runnable lab = () -> t.setText(String.format(java.util.Locale.ROOT, idx == 2 ? "%s:  x%.3f" : "%s:  %+.3f", label, Prefs.adjust(this, overlayName)[idx]));
        lab.run();
        sb.setOnSeekBarChangeListener(new android.widget.SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(android.widget.SeekBar s, int pr, boolean user) {
                if (!user) return;
                float[] cur = Prefs.adjust(EmulatorActivity.this, overlayName);
                cur[idx] = range[0] + (range[1] - range[0]) * pr / 100f;
                if (Math.abs(cur[idx] - range[2]) < 0.0011f) cur[idx] = range[2];
                Prefs.get(EmulatorActivity.this).edit().putString(Prefs.adjustOf(overlayName), cur[0] + "," + cur[1] + "," + cur[2]).apply();
                nativeSet(key, Float.toString(cur[idx]));
                lab.run();
            }
            public void onStartTrackingTouch(android.widget.SeekBar s) { }
            public void onStopTrackingTouch(android.widget.SeekBar s) { }
        });
        col.addView(t); col.addView(sb);
    }
    private void closeMenu() {
        if (menu == null) return;
        root.removeView(menu); menu = null;
        if (menuPaused) { menuPaused = false; pause(false); }
        root.getChildAt(0).requestFocus();
        immersive();
    }

    // ---------------------------------------------------------------- input
    private Controls.Map mapOf(InputDevice d, int id) {
        Controls.Map m = maps.get(id);
        if (m == null) {
            String name = Controls.nameOf(d);
            m = Controls.get(this, name);
            maps.put(id, m);
            if (Controls.isPad(d) && !d.isVirtual()) Controls.remember(this, name);
        }
        return m;
    }
    private int playerOf(InputDevice d, int id) {
        int k = player.indexOfKey(id);
        if (k >= 0) return player.valueAt(k);
        Controls.Map m = mapOf(d, id);
        int p;
        if (m.player.equals("1")) p = 0;
        else if (m.player.equals("2")) p = 1;
        else {                                           // (the first controller player 1, the second player 2;
            p = 0;                                       //  keyboards and remotes player 1)
            if (Controls.isPad(d)) {
                int n = 0;
                for (int i = 0; i < player.size(); i++) if (isPadId(player.keyAt(i))) n++;
                p = n >= 1 ? 1 : 0;
            }
        }
        player.put(id, p);
        Log.i(TAG, "input device " + (d != null ? d.getName() : id) + ": player " + (p + 1));
        return p;
    }
    private static boolean isPadId(int id) { return Controls.isPad(InputDevice.getDevice(id)); }

    private void send(int p) {
        Pad s = pads[p];
        float x = s.ax, y = s.ay;
        if (x == 0) x = s.hx != 0 ? Math.signum(s.hx) : (s.dpad & 4) != 0 ? -1 : (s.dpad & 8) != 0 ? 1 : 0;
        if (y == 0) y = s.hy != 0 ? Math.signum(s.hy) : (s.dpad & 1) != 0 ? -1 : (s.dpad & 2) != 0 ? 1 : 0;
        if (s.half) { x *= 0.5f; y *= 0.5f; }                               // (the half-stick key, as the desktop)
        int jx = Math.round(x * 127), jy = Math.round(-y * 127);            // (the Vectrex: positive = up)
        if (started) nativeInput(p, jx, jy, s.buttons);
    }
    private static float stick(float v, float dz) {
        float a = Math.abs(v);
        if (a <= dz) return 0;
        return Math.signum(v) * Math.min(1f, (a - dz) / (1f - dz));
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        if (menu != null) return super.dispatchGenericMotionEvent(ev);
        if ((ev.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && ev.getAction() == MotionEvent.ACTION_MOVE) {
            Controls.Map m = mapOf(ev.getDevice(), ev.getDeviceId());
            int p = playerOf(ev.getDevice(), ev.getDeviceId());
            Pad s = pads[p];
            if (m.stick.equals("right")) { s.ax = stick(ev.getAxisValue(MotionEvent.AXIS_Z), m.dz); s.ay = stick(ev.getAxisValue(MotionEvent.AXIS_RZ), m.dz); }
            else if (m.stick.equals("left")) { s.ax = stick(ev.getAxisValue(MotionEvent.AXIS_X), m.dz); s.ay = stick(ev.getAxisValue(MotionEvent.AXIS_Y), m.dz); }
            else { s.ax = s.ay = 0; }
            s.hx = ev.getAxisValue(MotionEvent.AXIS_HAT_X); s.hy = ev.getAxisValue(MotionEvent.AXIS_HAT_Y);
            send(p);
            return true;
        }
        return super.dispatchGenericMotionEvent(ev);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        int code = ev.getKeyCode();
        boolean down = ev.getAction() == KeyEvent.ACTION_DOWN, first = down && ev.getRepeatCount() == 0;
        if (ev.getAction() == KeyEvent.ACTION_MULTIPLE) return super.dispatchKeyEvent(ev);
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_VOLUME_MUTE
                || code == KeyEvent.KEYCODE_HOME || code == KeyEvent.KEYCODE_POWER) return super.dispatchKeyEvent(ev);
        // the menu: its own keys (B / Back / Menu close it), the rest moves through it
        if (menu != null) {
            boolean comboMenu = selectHeld && code == KeyEvent.KEYCODE_BUTTON_R1;
            if (code == KeyEvent.KEYCODE_BUTTON_SELECT) { selectHeld = down; return true; }
            if (code == KeyEvent.KEYCODE_BUTTON_START) {             // (closes it when let go, as it opened it)
                if (first) startHeld = true;
                else if (!down && startHeld) { startHeld = false; closeMenu(); }
                return true;
            }
            if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_BUTTON_B || code == KeyEvent.KEYCODE_ESCAPE
                    || code == KeyEvent.KEYCODE_MENU || code == KeyEvent.KEYCODE_F1 || comboMenu) {
                if (first) closeMenu();
                return true;
            }
            return super.dispatchKeyEvent(ev);
        }
        // the emulator's own keys
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE) { if (first) quit(); return true; }
        if (code == KeyEvent.KEYCODE_MENU || code == KeyEvent.KEYCODE_F1) { if (first) openMenu(false); return true; }
        if (code == KeyEvent.KEYCODE_P && !ev.isCtrlPressed()) { if (first) pause(!userPaused); return true; }
        if (code == KeyEvent.KEYCODE_R && ev.isCtrlPressed()) { if (first) nativeReset(); return true; }
        if (code == KeyEvent.KEYCODE_BUTTON_SELECT) {
            selectHeld = down;
            if (first && startHeld) { comboUsed = true; quit(); }       // (Select + Start, either one first)
            return true;
        }
        if (code == KeyEvent.KEYCODE_BUTTON_START) {
            if (down) { startHeld = true; if (selectHeld) { comboUsed = true; quit(); } }
            else { boolean open = startHeld && !comboUsed && !selectHeld; startHeld = false; comboUsed = false; if (open) openMenu(true); }
            return true;
        }
        if (selectHeld && first && code == KeyEvent.KEYCODE_BUTTON_L1) { comboUsed = true; nativeReset(); return true; }
        if (selectHeld && first && code == KeyEvent.KEYCODE_BUTTON_R1) { comboUsed = true; openMenu(false); return true; }
        // the game's: the controller's mapping
        Controls.Map m = mapOf(ev.getDevice(), ev.getDeviceId());
        int bit = 0, dir = 0; boolean half = false;
        for (int i = 0; i < 4; i++) if (code == m.keys[i]) bit = 1 << i;
        if (code == m.keys[4] || (m.keys[4] == KeyEvent.KEYCODE_SHIFT_LEFT && code == KeyEvent.KEYCODE_SHIFT_RIGHT)) half = true;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: if (bit == 0) bit = 8; break;   // (a TV remote: fire)
            case KeyEvent.KEYCODE_DPAD_UP: dir = 1; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: dir = 2; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: dir = 4; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: dir = 8; break;
            default: break;
        }
        if (bit == 0 && dir == 0 && !half) return true;      // (every other key: the game's, ignored)
        int p = playerOf(ev.getDevice(), ev.getDeviceId());
        Pad s = pads[p];
        if (bit != 0) s.buttons = down ? s.buttons | bit : s.buttons & ~bit;
        if (dir != 0) s.dpad = down ? s.dpad | dir : s.dpad & ~dir;
        if (half) s.half = down;
        send(p);
        return true;
    }
}
