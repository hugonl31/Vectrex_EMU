package io.github.hugonl31.vectrexemu;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/** What a front end (HyperSpin, ...) starts. It finds the game (RomSource), writes it to the app's cache
 *  for the emulator, and starts EmulatorActivity in its own process - a new one every time: an emulator
 *  still running (a game started while another plays) is ended first. No window of its own. */
public class LaunchActivity extends Activity {
    static final String TAG = "VectrexEMU";
    static final String X_ROM_FILE = "vx.rom_file", X_ROM_NAME = "vx.rom_name", X_ERROR = "vx.error",
                        X_TREE = "vx.tree", X_PARENT = "vx.parent", X_DIR = "vx.dir";

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Intent in = getIntent();
        Log.i(TAG, "launch: " + in + (in.getExtras() != null ? " extras " + in.getExtras().keySet() : ""));
        Intent go = new Intent(this, EmulatorActivity.class);
        go.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        try {
            RomSource.Rom rom = RomSource.resolve(this, in);
            if (rom != null) {
                File f = new File(getCacheDir(), "launch.rom");
                try (FileOutputStream o = new FileOutputStream(f)) { o.write(rom.data); }
                go.putExtra(X_ROM_FILE, f.getPath());
                go.putExtra(X_ROM_NAME, rom.name);
                if (rom.tree != null) { go.putExtra(X_TREE, rom.tree.toString()); go.putExtra(X_PARENT, rom.parentDoc); }
                if (rom.dir != null) go.putExtra(X_DIR, rom.dir.getPath());
                Log.i(TAG, "game: " + rom.name + " (" + rom.data.length + " bytes) from " + rom.source);
                Prefs.played(this, RomSource.stem(rom.name));
            }
        } catch (RomSource.Failure e) {
            Log.e(TAG, e.getMessage());
            go.putExtra(X_ERROR, e.getMessage());
        } catch (IOException e) {
            go.putExtra(X_ERROR, "Cannot keep the game: " + e.getMessage());
        }
        if (!go.hasExtra(X_ROM_FILE) && !go.hasExtra(X_ERROR)) Prefs.played(this, "Mine Storm");
        endEmulator(this);
        startActivity(go);
        finish();
        overridePendingTransition(0, 0);
    }

    /** an emulator process still running: ended, and waited for (at most a second) */
    static void endEmulator(Context c) {
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null || am.getRunningAppProcesses() == null) return;
        String emu = c.getPackageName() + ":emu";
        int pid = -1;
        for (ActivityManager.RunningAppProcessInfo p : am.getRunningAppProcesses()) if (emu.equals(p.processName)) pid = p.pid;
        if (pid < 0) return;
        Process.killProcess(pid);
        for (int i = 0; i < 50 && new File("/proc/" + pid).exists(); i++) SystemClock.sleep(20);
    }
}
