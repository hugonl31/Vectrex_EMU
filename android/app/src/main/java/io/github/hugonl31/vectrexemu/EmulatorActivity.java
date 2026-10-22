package io.github.hugonl31.vectrexemu;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.SparseIntArray;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/** The game: full screen, the picture drawn by the native side (engine.cpp) into a SurfaceView, the
 *  controllers read here. Back (or Select + Start) quits straight back to the front end. Runs in the
 *  ":emu" process, which ends with the game. */
public class EmulatorActivity extends Activity implements SurfaceHolder.Callback {
    static { System.loadLibrary("vectrexemu"); }
    static native void nativeStart(String[] keys, String[] values, byte[] bios, byte[] cart, String sav,
                                   byte[] art, int artW, int artH, float artSheet, String artName, byte[] onWav, byte[] offWav);
    static native void nativePowerOff();
    static native void nativeSurface(Surface s);
    static native void nativeInput(int player, int jx, int jy, int buttons);
    static native void nativeReset();
    static native void nativePause(boolean p);
    static native void nativeStop();
    static native String nativeRenderer();

    static final String TAG = "VectrexEMU";
    private boolean started, quitting, offEffect;

    // ---------------------------------------------------------------- the players' controllers
    private static final class Pad { float ax, ay, hx, hy; int dpad, buttons; }   // dpad: 1 up 2 down 4 left 8 right
    private final Pad[] pads = { new Pad(), new Pad() };
    private final SparseIntArray player = new SparseIntArray();     // device id -> player
    private int[] buttonKeys;                                        // keycodes of the Vectrex buttons 1-4
    private float deadzone = 0.2f;
    private boolean selectHeld, startHeld;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        String o = Prefs.value(this, Prefs.ORIENT);         // (Settings > Screen: a device on its side)
        setRequestedOrientation(o.equals("portrait") ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                : o.equals("reverse_portrait") ? ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        String layout = Prefs.value(this, Prefs.BUTTONS);
        buttonKeys = new int[4];
        for (int i = 0; i < 4; i++) {
            char c = i < layout.length() ? layout.charAt(i) : 'x';
            buttonKeys[i] = c == 'a' ? KeyEvent.KEYCODE_BUTTON_A : c == 'b' ? KeyEvent.KEYCODE_BUTTON_B
                          : c == 'y' ? KeyEvent.KEYCODE_BUTTON_Y : KeyEvent.KEYCODE_BUTTON_X;
        }
        try { deadzone = Float.parseFloat(Prefs.value(this, Prefs.DEADZONE)); } catch (NumberFormatException e) { /* default */ }

        String err = getIntent().getStringExtra(LaunchActivity.X_ERROR);
        if (err != null) { message(err + "\n\nBack: return"); return; }
        try {
            start();
        } catch (IOException e) {
            message("Cannot start: " + e.getMessage() + "\n\nBack: return");
            return;
        }
        SurfaceView sv = new SurfaceView(this);
        sv.getHolder().addCallback(this);
        setContentView(sv);
        immersive();
        sv.setFocusable(true); sv.requestFocus();
    }

    private void start() throws IOException {
        byte[] bios = asset("bios/bios.bin");
        String romFile = getIntent().getStringExtra(LaunchActivity.X_ROM_FILE);
        String romName = getIntent().getStringExtra(LaunchActivity.X_ROM_NAME);
        byte[] cart = romFile != null ? RomSource.all(new FileInputStream(romFile)) : new byte[0];
        String game = romFile != null ? RomSource.stem(romName) : "Mine Storm";
        // the flash cartridges' saved state (high scores): the app's own storage, by the game's name
        File saves = new File(getFilesDir(), "saves"); saves.mkdirs();
        String sav = new File(saves, game.replaceAll("[/\\\\:*?\"<>|]", "_") + ".sav").getPath();
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
        float[] adj = Prefs.adjust(this, art != null ? art.name : null);
        File al2 = new File(getFilesDir(), "al2.bin");
        String[][] conf = Prefs.forNative(this, "speech_rom", al2.exists() ? al2.getPath() : null,
                "art_dx", Float.toString(adj[0]), "art_dy", Float.toString(adj[1]), "art_scale", Float.toString(adj[2]));
        offEffect = "1".equals(Prefs.value(this, Prefs.OFF_EFFECT));
        nativeStart(conf[0], conf[1], bios, cart, sav, art != null ? art.rgba : null, art != null ? art.w : 0, art != null ? art.h : 0,
                    art != null ? art.sheet : 0f, art != null ? art.name : "", asset("sounds/on.wav"), asset("sounds/off.wav"));
        started = true;
    }

    private byte[] asset(String name) throws IOException {
        try (InputStream is = getAssets().open(name)) { return RomSource.all(is); }
    }

    private void message(String text) {
        TextView t = new TextView(this);
        t.setText(text); t.setTextColor(Color.rgb(124, 255, 176)); t.setBackgroundColor(Color.BLACK);
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

    /** back to the front end: the core saves (flash cartridges), the process ends; with Switch off when
     *  quitting, first half a second of the tube going dark and the switch's click */
    private void quit() {
        if (quitting) return;
        quitting = true;
        Handler h = new Handler(Looper.getMainLooper());
        Runnable leave = () -> {
            if (started) nativeStop();
            finishAndRemoveTask();
            overridePendingTransition(0, 0);
            h.postDelayed(() -> System.exit(0), 250);
        };
        if (started && offEffect) { nativePowerOff(); h.postDelayed(leave, 600); }
        else leave.run();
    }

    @Override public void onBackPressed() { quit(); }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (!quitting) { quitting = true; if (started) nativeStop(); System.exit(0); }
    }

    // ---------------------------------------------------------------- input
    private int playerOf(InputEvent e) {
        int id = e.getDeviceId();
        int k = player.indexOfKey(id);
        if (k >= 0) return player.valueAt(k);
        InputDevice d = e.getDevice();
        boolean pad = d != null && (d.getSources() & (InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK)) != 0;
        int p = 0;                                       // (the first controller player 1, the second player 2;
        if (pad) {                                       //  keyboards and remotes player 1)
            int pads_ = 0;
            for (int i = 0; i < player.size(); i++) if (player.valueAt(i) >= 0 && isPad(player.keyAt(i))) pads_++;
            p = pads_ >= 1 ? 1 : 0;
        }
        player.put(id, p);
        Log.i(TAG, "input device " + (d != null ? d.getName() : id) + ": player " + (p + 1));
        return p;
    }
    private static boolean isPad(int id) {
        InputDevice d = InputDevice.getDevice(id);
        return d != null && (d.getSources() & (InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK)) != 0;
    }
    private interface InputEvent { int getDeviceId(); InputDevice getDevice(); }

    private void send(int p) {
        Pad s = pads[p];
        float x = stick(s.ax), y = stick(s.ay);
        if (x == 0) x = s.hx != 0 ? Math.signum(s.hx) : (s.dpad & 4) != 0 ? -1 : (s.dpad & 8) != 0 ? 1 : 0;
        if (y == 0) y = s.hy != 0 ? Math.signum(s.hy) : (s.dpad & 1) != 0 ? -1 : (s.dpad & 2) != 0 ? 1 : 0;
        int jx = Math.round(x * 127), jy = Math.round(-y * 127);            // (the Vectrex: positive = up)
        if (started) nativeInput(p, jx, jy, s.buttons);
    }
    private float stick(float v) {
        float a = Math.abs(v);
        if (a <= deadzone) return 0;
        return Math.signum(v) * Math.min(1f, (a - deadzone) / (1f - deadzone));
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        if ((ev.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && ev.getAction() == MotionEvent.ACTION_MOVE) {
            int p = playerOf(new InputEvent() {
                public int getDeviceId() { return ev.getDeviceId(); }
                public InputDevice getDevice() { return ev.getDevice(); }
            });
            Pad s = pads[p];
            s.ax = ev.getAxisValue(MotionEvent.AXIS_X); s.ay = ev.getAxisValue(MotionEvent.AXIS_Y);
            s.hx = ev.getAxisValue(MotionEvent.AXIS_HAT_X); s.hy = ev.getAxisValue(MotionEvent.AXIS_HAT_Y);
            send(p);
            return true;
        }
        return super.dispatchGenericMotionEvent(ev);
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent ev) {
        int code = ev.getKeyCode();
        boolean down = ev.getAction() == KeyEvent.ACTION_DOWN;
        if (ev.getAction() == KeyEvent.ACTION_MULTIPLE) return super.dispatchKeyEvent(ev);
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_ESCAPE) { if (down) quit(); return true; }
        if (code == KeyEvent.KEYCODE_BUTTON_SELECT) selectHeld = down;
        if (code == KeyEvent.KEYCODE_BUTTON_START) startHeld = down;
        if (selectHeld && startHeld) { quit(); return true; }
        if (code == KeyEvent.KEYCODE_BUTTON_SELECT || code == KeyEvent.KEYCODE_BUTTON_START) return true;
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_VOLUME_MUTE
                || code == KeyEvent.KEYCODE_HOME || code == KeyEvent.KEYCODE_POWER) return super.dispatchKeyEvent(ev);
        int bit = 0, dir = 0;
        for (int i = 0; i < 4; i++) if (code == buttonKeys[i]) bit = 1 << i;
        switch (code) {                                  // (keyboards as the desktop: A W S D = buttons 1-4)
            case KeyEvent.KEYCODE_A: bit = 1; break;
            case KeyEvent.KEYCODE_W: bit = 2; break;
            case KeyEvent.KEYCODE_S: bit = 4; break;
            case KeyEvent.KEYCODE_D: bit = 8; break;
            case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: bit = 8; break;   // (a TV remote: fire)
            case KeyEvent.KEYCODE_DPAD_UP: dir = 1; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: dir = 2; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: dir = 4; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: dir = 8; break;
            default: break;
        }
        if (bit == 0 && dir == 0) return true;           // (every other key: the game's, ignored)
        int p = playerOf(new InputEvent() {
            public int getDeviceId() { return ev.getDeviceId(); }
            public InputDevice getDevice() { return ev.getDevice(); }
        });
        Pad s = pads[p];
        if (bit != 0) s.buttons = down ? s.buttons | bit : s.buttons & ~bit;
        if (dir != 0) s.dpad = down ? s.dpad | dir : s.dpad & ~dir;
        send(p);
        return true;
    }
}
