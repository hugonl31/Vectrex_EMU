/* Main.java - Vectrex_EMU for Android: the game screen, as the iOS app's (ios/App/VXApp.m) - the picture (a SurfaceView
 * the core's thread draws on, jni/vxa.c), the touch controls (a stick where the thumb lands, buttons 1-4 as the Vectrex
 * controller) while no controller is connected, the quick bar upright / the menu button sideways, the core's control deck
 * under the picture (the core draws it; touched as its pointer), the screens over the game (Screens.java), online play.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ActivityInfo;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;

public class Main extends Activity implements SurfaceHolder.Callback {
	/* the theme: the PC app's colours */
	static int BG = 0xff05080a, BAR = 0xff0a1210, PANEL = 0xff0b1511, BODY = 0xff0f1c17, EDGE = 0xff1d3329, EDGE2 = 0xff2f5544, SEL = 0xff24443a,
		G = 0xff66aa88, HI = 0xff88ccbb, HI2 = 0xffbbffee, FG = 0xffc8d6cf, FG2 = 0xffa9bdb3, DIM = 0xff7f978b, CHG = 0xffd9a35e;
	/* The colour scheme (the owner, 2026-11-22): blue, the default, or green - the core's rule (browser.c vx_pal): a green (hue
	 * 100 - 190 degrees, some colour in it) goes to hue 216, 1.25 x as strong. The scheme of a start: set before the first view
	 * (app.palgreen: the core's Colours setting as it was when the app went to the background) */
	static boolean green = false;
	static int pal (int c)
	{
		if (green) return c;
		float[] h = new float[3]; android.graphics.Color.colorToHSV (c, h);
		if (h[0] > 100f && h[0] < 190f && h[1] > 0.12f) { h[0] = 216f; h[1] = Math.min (1f, h[1] * 1.25f); return android.graphics.Color.HSVToColor (android.graphics.Color.alpha (c), h); }
		return c;
	}
	static final int[] BASE = { BG, BAR, PANEL, BODY, EDGE, EDGE2, SEL, G, HI, HI2, FG, FG2, DIM };   /* (the theme's own colours: each scheme made from them) */
	static void palette (boolean g)
	{
		green = g; final int[] c = new int[BASE.length]; for (int i = 0; i < c.length; i++) c[i] = pal (BASE[i]);
		BG = c[0]; BAR = c[1]; PANEL = c[2]; BODY = c[3]; EDGE = c[4]; EDGE2 = c[5]; SEL = c[6]; G = c[7]; HI = c[8]; HI2 = c[9]; FG = c[10]; FG2 = c[11]; DIM = c[12];
	}
	/* the colour scheme changed (Settings: Green colours): everything in the new one at once - the owner, 2026-11-25: until the
	 * next start some of it kept the old one (the quick bar, the menu button, the pop-up tab were made with the colours of
	 * the app's start); the screen open is made again by the caller */
	void applyPalette (boolean g)
	{
		if (g == green) return;
		palette (g); prefs.edit ().putInt ("app.palgreen", g ? 1 : 0).apply (); log ("colours: " + (g ? "green" : "blue") + " - the app's own views made again");
		int i = root.indexOfChild (bar); root.removeView (bar); bar = quickBar (); root.addView (bar, i, new FrameLayout.LayoutParams (-1, (int) dp (44)));
		i = root.indexOfChild (popTab); final int pv = popTab.getVisibility (); root.removeView (popTab); popTab = popTabView (); popTab.setVisibility (pv);
		root.addView (popTab, i, new FrameLayout.LayoutParams ((int) dp (64), (int) dp (26)));
		menuBtn.setTextColor (HI); menuBtn.setBackground (round (pal (0xbf0b1511), EDGE2, dp (22)));
		deck.invalidate (); ctl.recolour (); layout (); placeDeck ();
	}
	static Main me; static boolean palSet;
	SharedPreferences prefs; FrameLayout root, screens; SurfaceView sv; Controls ctl; LinearLayout bar, toasts; TextView menuBtn; DeckView deck; View popTab; long popUntil;
	View welcome; TextView wStatus; ProgressBar wProg;
	final Handler h = new Handler (Looper.getMainLooper ());
	File files, sysDir, savesDir; final List<File> romRoots = new ArrayList<> ();
	boolean started, landscape; int insTop, insBot, insL, insR;
	final ArrayList<View> stack = new ArrayList<> ();           /* (the screens over the game, the top one last) */

	float dp (float v) { return TypedValue.applyDimension (TypedValue.COMPLEX_UNIT_DIP, v, getResources ().getDisplayMetrics ()); }
	static void log (String s) { try { VX.log (s); } catch (Throwable t) { } }
	static boolean crashHook;

	/* (2026-11-26, the owner's Nova crashed once switching games and its log just stopped: Android 11+ keeps why the app last
	 *  ended - a crash, a native crash with its tombstone, an ANR, killed for memory... Each start logs the reasons not seen
	 *  before (the log goes to the Linode); a native crash's or an ANR's trace as its readable strings - the signal, the abort
	 *  message, the backtrace's libraries and functions) */
	void lastExit ()
	{
		if (Build.VERSION.SDK_INT < 30) return;
		try {
			ActivityManager am = (ActivityManager) getSystemService (ACTIVITY_SERVICE);
			List<ApplicationExitInfo> l = am.getHistoricalProcessExitReasons (null, 0, 5);
			long seen = prefs.getLong ("app.exitseen", 0), newest = seen;
			final String[] R = { "unknown", "exit (itself)", "signalled", "low memory", "CRASH (Java)", "CRASH (native)", "ANR (not responding)",
				"start failed", "permission changed", "too many resources", "user asked", "user stopped", "a dependency died", "other", "frozen",
				"package state changed", "package updated" };
			for (int k = l.size () - 1; k >= 0; k--) {
				ApplicationExitInfo e = l.get (k);
				if (e.getTimestamp () <= seen) continue;
				newest = Math.max (newest, e.getTimestamp ());
				final int r = e.getReason ();
				log ("last exit: " + (r >= 0 && r < R.length ? R[r] : "reason " + r) + ", status " + e.getStatus () + ", "
					+ new java.text.SimpleDateFormat ("yyyy-MM-dd HH:mm:ss.SSS", java.util.Locale.ROOT).format (new java.util.Date (e.getTimestamp ()))
					+ ", " + (e.getDescription () != null ? e.getDescription () : "") + ", memory " + e.getPss () / 1024 + " MB (pss), " + e.getRss () / 1024 + " MB (rss)");
				if (r == ApplicationExitInfo.REASON_CRASH_NATIVE || r == ApplicationExitInfo.REASON_ANR) {
					try (java.io.InputStream in = e.getTraceInputStream ()) {
						if (in == null) { log ("last exit: no trace kept"); continue; }
						java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream (); byte[] buf = new byte[65536]; int n;
						while ((n = in.read (buf)) > 0 && bo.size () < 4 << 20) bo.write (buf, 0, n);
						final byte[] d = bo.toByteArray ();
						if (r == ApplicationExitInfo.REASON_ANR) {        /* (an ANR's trace is text: its main thread) */
							String t = new String (d, "UTF-8"); int i = t.indexOf ("\"main\""); t = t.substring (Math.max (0, i), Math.min (t.length (), Math.max (0, i) + 6000));
							for (String s : t.split ("\n")) if (!s.trim ().isEmpty ()) log ("last exit trace: " + s.trim ());
						} else {                                            /* (a native crash: the tombstone, a protobuf - its strings, in order) */
							StringBuilder sb = new StringBuilder (); int lines = 0; java.util.HashSet<String> once = new java.util.HashSet<> ();
							for (int i = 0; i <= d.length && lines < 160; i++) {
								final int c = i < d.length ? d[i] & 255 : 0;
								if (c >= 32 && c < 127) { sb.append ((char) c); continue; }
								if (sb.length () >= 6 && once.add (sb.toString ())) { log ("last exit trace: " + sb); lines++; }
								sb.setLength (0);
							}
						}
					} catch (Throwable t) { log ("last exit: trace not read (" + t + ")"); }
				}
			}
			prefs.edit ().putLong ("app.exitseen", newest).apply ();
		} catch (Throwable t) { log ("last exit: not read (" + t + ")"); }
	}

	@Override protected void onCreate (Bundle b)
	{
		super.onCreate (b); me = this; prefs = getSharedPreferences ("vx", MODE_PRIVATE);
		if (!palSet) { palSet = true; palette (prefs.getInt ("app.palgreen", 0) == 1); }
		files = getFilesDir (); VX.logStart (files.getAbsolutePath ());
		log ("app start: Vectrex_EMU " + version () + " on " + Build.MANUFACTURER + " " + Build.MODEL + ", Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")");
		lastExit ();                                            /* (how the app ended last time - a crash with its trace) */
		if (!crashHook) { crashHook = true; final Thread.UncaughtExceptionHandler old = Thread.getDefaultUncaughtExceptionHandler ();
			Thread.setDefaultUncaughtExceptionHandler ((th, ex) -> {   /* (a Java crash: its stack into the log first - sent at the next start) */
				try { java.io.StringWriter sw = new java.io.StringWriter (); ex.printStackTrace (new java.io.PrintWriter (sw));
					log ("CRASH (Java) on thread " + th.getName () + ": " + sw.toString ().replace ("\n", " | ")); } catch (Throwable t) { }
				if (old != null) old.uncaughtException (th, ex); }); }
		Window w = getWindow (); w.addFlags (WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
		{ WindowManager.LayoutParams a = w.getAttributes (); a.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES; w.setAttributes (a); }   /* (the whole screen: the picture placed clear of a camera hole by the safe insets) */
		w.setStatusBarColor (Color.BLACK); w.setNavigationBarColor (Color.BLACK);
		root = new FrameLayout (this); root.setBackgroundColor (Color.BLACK);
		sv = new SurfaceView (this); sv.getHolder ().addCallback (this); root.addView (sv, new FrameLayout.LayoutParams (-1, -1));
		ctl = new Controls (this); root.addView (ctl, new FrameLayout.LayoutParams (-1, -1));
		bar = quickBar (); root.addView (bar, new FrameLayout.LayoutParams (-1, (int) dp (44)));
		deck = new DeckView (this); deck.setVisibility (View.GONE); root.addView (deck, new FrameLayout.LayoutParams (-1, (int) dp (58)));
		popTab = popTabView (); popTab.setVisibility (View.GONE); root.addView (popTab, new FrameLayout.LayoutParams ((int) dp (64), (int) dp (26)));
		menuBtn = new TextView (this); menuBtn.setText ("≡"); menuBtn.setTextSize (24); menuBtn.setTextColor (HI); menuBtn.setGravity (Gravity.CENTER);
		menuBtn.setBackground (round (pal (0xbf0b1511), EDGE2, dp (22))); menuBtn.setOnClickListener (v -> Screens.gameMenu (this)); menuBtn.setContentDescription ("Menu");
		root.addView (menuBtn, new FrameLayout.LayoutParams ((int) dp (44), (int) dp (44)));
		toasts = new LinearLayout (this); toasts.setOrientation (LinearLayout.VERTICAL); toasts.setGravity (Gravity.CENTER_HORIZONTAL);
		FrameLayout.LayoutParams tl = new FrameLayout.LayoutParams (-1, -2, Gravity.TOP); root.addView (toasts, tl);
		screens = new FrameLayout (this); root.addView (screens, new FrameLayout.LayoutParams (-1, -1));
		welcome = welcomeView (); root.addView (welcome, new FrameLayout.LayoutParams (-1, -1));
		setContentView (root); immersive ();
		root.setOnApplyWindowInsetsListener ((v, ins) -> { insets (ins); return ins; });
		hdrSetup ();
		h.postDelayed (this::tick, 100);
		h.post (this::checkContent);
	}
	String version () { try { return getPackageManager ().getPackageInfo (getPackageName (), 0).versionName; } catch (Exception e) { return "?"; } }
	long versionCode () { try { return getPackageManager ().getPackageInfo (getPackageName (), 0).getLongVersionCode (); } catch (Exception e) { return 0; } }
	/* (the start screen's version line: the real one - the version, its build, the test build said) */
	String versionLine () { return version () + " (" + versionCode () + ")" + (Getter.testTok (this) != null ? " - test build" : ""); }
	void immersive ()
	{
		if (Build.VERSION.SDK_INT >= 30) { getWindow ().setDecorFitsSystemWindows (false); android.view.WindowInsetsController c = getWindow ().getInsetsController ();
			if (c != null) { boolean sb = prefs.getBoolean ("app.statusbar", false);   /* (Settings > Clock and battery: the status bar kept) */
				c.hide (sb ? WindowInsets.Type.navigationBars () : WindowInsets.Type.systemBars ()); if (sb) c.show (WindowInsets.Type.statusBars ());
				c.setSystemBarsBehavior (android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); } return; }
		getWindow ().getDecorView ().setSystemUiVisibility (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
			| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
	}
	@Override public void onWindowFocusChanged (boolean f) { super.onWindowFocusChanged (f); if (f) immersive (); }
	void insets (WindowInsets ins)
	{
		int t = 0, bo = 0, l = 0, r = 0;
		if (Build.VERSION.SDK_INT >= 30 && prefs.getBoolean ("app.statusbar", false)) t = ins.getInsets (WindowInsets.Type.statusBars ()).top;
		if (Build.VERSION.SDK_INT >= 28 && ins.getDisplayCutout () != null) { t = Math.max (t, ins.getDisplayCutout ().getSafeInsetTop ()); bo = ins.getDisplayCutout ().getSafeInsetBottom (); l = ins.getDisplayCutout ().getSafeInsetLeft (); r = ins.getDisplayCutout ().getSafeInsetRight (); }
		if (t != insTop || bo != insBot || l != insL || r != insR) { insTop = t; insBot = bo; insL = l; insR = r; log ("safe insets " + t + " " + l + " " + bo + " " + r); layout (); }
	}

	/* ---------------------------------------------------------------- HDR: the screen's headroom; Settings > App */
	boolean hdrCan; float hdrRoom = 1;
	void hdrSetup ()
	{
		Display d = getWindowManager ().getDefaultDisplay ();
		try {
			/* (HDR only where Android keeps ordinary pictures at their brightness beside it: Android 14 with the screen's
			 *  HDR / SDR ratio. Before - the Nova, Android 13 - the extended linear window showed the whole picture dim, HDR
			 *  brightness 0 % or not; and not when the player set it to 0 % - an ordinary window then, from the start) */
			hdrScreen = Build.VERSION.SDK_INT >= 26 && d.isHdr ();
			hdrCan = hdrScreen && Build.VERSION.SDK_INT >= 34 && d.isHdrSdrRatioAvailable ();
			if (hdrCan && d.getHdrCapabilities () != null) hdrRoom = Math.max (1f, d.getHdrCapabilities ().getDesiredMaxLuminance () / 203f);
			if (hdrCan) hdrRoom = Math.max (hdrRoom, d.getHdrSdrRatio ());
			if (hdrCan && prefs.getFloat ("app.hdr", 1f) > 0.01f) getWindow ().setColorMode (ActivityInfo.COLOR_MODE_HDR);
		} catch (Throwable t) { log ("HDR: " + t); }
		float best = d.getRefreshRate (); try { for (Display.Mode m : d.getSupportedModes ()) best = Math.max (best, m.getRefreshRate ()); } catch (Throwable t) { }
		maxHz = best;
		curHz = d.getRefreshRate ();
		{ int pw = getResources ().getDisplayMetrics ().widthPixels, ph = getResources ().getDisplayMetrics ().heightPixels;   /* (the tuning: the whole screen's sides) */
		  try { pw = d.getMode ().getPhysicalWidth (); ph = d.getMode ().getPhysicalHeight (); } catch (Throwable t) { }
		  final int[] R = { 144, 120, 90, 72 }; int top = 60; for (int r : R) if (best >= r - 1) { top = r; break; }   /* (the fastest of the core's rates it can run) */
		  VX.setTune (Math.max (pw, ph), Math.min (pw, ph), top); }
		log ("screen: " + d.getRefreshRate () + " Hz (up to " + best + "), HDR " + (hdrCan ? "yes, headroom " + hdrRoom : hdrScreen ? "screen yes, but not on this Android (before 14): off" : "no") + ", " + getResources ().getDisplayMetrics ().widthPixels + " x " + getResources ().getDisplayMetrics ().heightPixels + " px, density " + getResources ().getDisplayMetrics ().density);
		VX.setHdr (hdrCan && prefs.getFloat ("app.hdr", 1f) > 0.01f, hdrRoom, prefs.getFloat ("app.hdr", 1f));
		VX.setBright (prefs.getInt ("app.look", 1));   /* (Bright the default on phones and tablets) */
		VX.setHdrLook (0);   /* (2026-11-22: one picture - no look to choose) */   /* (Bright the default everywhere) */   /* (1, the whole picture lifted: gone) */   /* (the look: Vectrex the default with HDR, Bright without - as iOS) */
		trailTick ();
	}
	float maxHz = 60, curHz = 60; boolean hdrScreen;
	void hdrChanged ()
	{
		final float f = prefs.getFloat ("app.hdr", 1f); VX.setHdr (hdrCan && f > 0.01f, hdrNow > 0 ? hdrNow : hdrRoom, f);   /* (the headroom now, if read) */
		if (hdrCan && (f > 0.01f) != (VX.hdrActive () != 0)) toast (f > 0.01f ? "HDR: on at the next start of the app" : "HDR: off at the next start of the app", 3);   /* (the window's kind: chosen at the start) */
	}

	/* ---------------------------------------------------------------- the content: the games, the BIOS, the cards */
	/* (where they are: the app's own folder (the downloads, adb), or RetroArch's folders - as the RetroArch core: BIOS/
	 *  vectrex_emu and ROMs/vectrex on a card, RetroArch/system - with the all-files access the player gives once) */
	List<File> volumes ()
	{
		List<File> v = new ArrayList<> (); v.add (Environment.getExternalStorageDirectory ());
		File[] s = new File ("/storage").listFiles (); if (s != null) for (File f : s) if (!f.getName ().equals ("emulated") && !f.getName ().equals ("self") && f.isDirectory ()) v.add (f);
		/* (Android's own list of the volumes - /storage may not be listable (Android 13, the Nova's SD card: a fresh install found
		 *  nothing); the app's own folder on each volume, four levels up, as well) */
		try { if (Build.VERSION.SDK_INT >= 30) for (android.os.storage.StorageVolume sv : ((android.os.storage.StorageManager) getSystemService (Context.STORAGE_SERVICE)).getStorageVolumes ()) {
				File d = sv.getDirectory (); if (d != null && !v.contains (d)) v.add (d); } } catch (Throwable t) { log ("volumes: " + t); }
		File[] ex = getExternalFilesDirs (null); if (ex != null) for (File f : ex) { if (f == null) continue; File r = f.getParentFile (); for (int i = 0; i < 3 && r != null; i++) r = r.getParentFile (); if (r != null && !v.contains (r)) v.add (r); }
		return v;
	}
	File ext () { File e = getExternalFilesDir (null); return e != null ? e : files; }
	boolean allFiles () { return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager (); }
	void findContent ()
	{
		romRoots.clear (); sysDir = null; savesDir = null; File e = ext ();
		try { File td = new File (e, "roms/Tests"); td.mkdirs (); VX.testCart (new File (td, "Test Cart.bin").getAbsolutePath ()); } catch (Throwable t) { }   /* (the Test Cart: the picture compared between platforms - Library > Tests) */
		if (new File (e, "system/vectrex_emu/bios.bin").exists () || new File (e, "system/vectrex_emu").isDirectory ()) sysDir = new File (e, "system");
		if (new File (e, "roms").isDirectory ()) romRoots.add (new File (e, "roms"));
		if (allFiles ()) for (File v : volumes ()) {
			File b = new File (v, "BIOS/vectrex_emu"), ra = new File (v, "RetroArch/system/vectrex_emu");
			if (sysDir == null && new File (b, "bios.bin").exists ()) sysDir = b.getParentFile ();
			if (sysDir == null && new File (ra, "bios.bin").exists ()) sysDir = ra.getParentFile ();
			for (String r : new String[] { "ROMs/vectrex", "roms/vectrex", "RetroArch/roms/vectrex" }) { File f = new File (v, r); if (f.isDirectory () && !romRoots.contains (f)) romRoots.add (f); }
			File sv2 = new File (v, "RetroArch/saves/Vectrex_EMU"); if (savesDir == null && sv2.isDirectory ()) savesDir = sv2; }
		if (sysDir == null) sysDir = new File (e, "system");
		if (savesDir == null) savesDir = new File (e, "saves");
		sysDir.mkdirs (); savesDir.mkdirs ();
		log ("content: system " + sysDir + ", saves " + savesDir + ", games in " + romRoots + (allFiles () ? "" : " (no all-files access)"));
	}
	void checkContent ()
	{
		if (Base.busy) return;
		if (Base.needed (this)) { Base.busy = true; status ("Setting up..."); new Thread (() -> { Base.install (this); h.post (() -> { Base.busy = false; checkContent (); }); }, "base").start (); return; }   /* (the public build: its own files unpacked first, once) */
		findContent ();
		boolean bios = new File (sysDir, "vectrex_emu/bios.bin").exists () || new File (sysDir, "vectrex_emu").isDirectory ();
		if (Pack.token (this) != null) { Pack.check (this, bios); return; }   /* (the private test build: the parts from the project's server) */
		if (!bios && !allFiles () && Build.VERSION.SDK_INT >= 30 && !prefs.getBoolean ("asked.files", false)) {
			prefs.edit ().putBoolean ("asked.files", true).apply ();
			status ("Vectrex_EMU reads the games and the Vectrex's files from RetroArch's folders (BIOS/vectrex_emu, ROMs/vectrex) or from its own folder.\n\nAllow access to all files, then come back.");
			h.postDelayed (() -> { try { startActivity (new Intent (Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse ("package:" + getPackageName ()))); }
				catch (Exception x) { startActivity (new Intent (Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)); } }, 2500);
			return; }
		if (!bios) { log ("no Vectrex files: looked in " + volumes () + (allFiles () ? "" : " (no all-files access)")); status ("No Vectrex files found.\n\nPut them in " + new File (ext (), "system/vectrex_emu") + " and the games in " + new File (ext (), "roms") + ", or in RetroArch's folders (BIOS/vectrex_emu, ROMs/vectrex) with access to all files."); return; }
		startCore ();
	}
	@Override protected void onResume ()
	{
		super.onResume (); VX.setRunning (true); immersive ();
		if (!started && welcome.getParent () != null && !Pack.busy) h.postDelayed (this::checkContent, 300);   /* (back from the all-files setting) */
		if (Online.playing) VX.net (4, stack.isEmpty () ? 0 : 1);   /* (back: playing again - or still in a menu) */
	}
	@Override protected void onPause () { super.onPause (); try { prefs.edit ().putInt ("app.palgreen", VX.palette ()).apply (); } catch (Throwable t) { } VX.setRunning (false); if (Online.playing) VX.net (4, 2); log ("app in the background"); }   /* (online: the other
	                                                     side sees "in another app") */
	@Override protected void onDestroy () { super.onDestroy (); Adhoc.stop (this); log ("the app ends"); }
	@Override public void onRequestPermissionsResult (int code, String[] perms, int[] res) { if (code == 7) Adhoc.onPermission (this, res.length > 0 && res[0] == android.content.pm.PackageManager.PERMISSION_GRANTED); }

	/* the top bar and the full deck do not both fit (upright, the picture at its full width - 5 % less allowed): the bar hides
	 * itself, the deck pops up by default - as the iOS app (a phone fits; the Nova's 4:3, a square screen not) */
	boolean squarish ()
	{
		android.util.DisplayMetrics dm = new android.util.DisplayMetrics (); getWindowManager ().getDefaultDisplay ().getRealMetrics (dm);
		final float a = Math.max (dm.widthPixels, dm.heightPixels) / dm.density, b = Math.min (dm.widthPixels, dm.heightPixels) / dm.density;
		return b > 0 && a < b * 1.278f * 0.95f + 70 + 58 + 30;   /* (dp: the picture 1.278 x its width high; the bar's row, the deck, the safe edges) */
	}
	boolean popDef ()                                          /* (the slim deck does not fit with the bar either: it pops up by default - four stages, as iOS) */
	{
		android.util.DisplayMetrics dm = new android.util.DisplayMetrics (); getWindowManager ().getDefaultDisplay ().getRealMetrics (dm);
		final float a = Math.max (dm.widthPixels, dm.heightPixels) / dm.density, b = Math.min (dm.widthPixels, dm.heightPixels) / dm.density;
		return b > 0 && a < b * 1.278f * 0.95f + 70 + 40 + 30;
	}
	boolean tight ()                                           /* (the top bar alone does not fit either: it hides itself by default) */
	{
		android.util.DisplayMetrics dm = new android.util.DisplayMetrics (); getWindowManager ().getDefaultDisplay ().getRealMetrics (dm);
		final float a = Math.max (dm.widthPixels, dm.heightPixels) / dm.density, b = Math.min (dm.widthPixels, dm.heightPixels) / dm.density;
		return b > 0 && a < b * 1.278f * 0.95f + 70 + 30;
	}
	String defaults ()
	{
		/* (this app's own, as the iOS app's: the deck below the picture, the picture upright as the screen stands, the keys
		 *  as on the PC, the screen's own rate) */
		int hz = curHz >= 143 ? 144 : curHz >= 119 ? 120 : curHz >= 89 ? 90 : curHz >= 71 ? 72 : 60;   /* (the screen's rate now: the Nova "up to 120" ran at 60 - 120 Hz ran the emulator twice a refresh, 15.6 of 16.7 ms) */
		return "vectrex_emu_deck=auto" + "\nvectrex_emu_rotate=off\nvectrex_emu_rotate_stick=on\nvectrex_emu_keys=on\nvectrex_emu_btn_eject=R\nvectrex_emu_rate=" + hz + " Hz\n";
	}
	/* (once, 1.7.6: the deck and the top bar to Auto - an older install kept the screen's old default as its own value) */
	void autoOnce ()
	{
		if (prefs.getBoolean ("app.auto1", false)) return; if (VX.ofind ("vectrex_emu_deck") < 0) { h.postDelayed (this::autoOnce, 1000); return; }
		prefs.edit ().putBoolean ("app.auto1", true).remove ("app.deckpop").remove ("app.topbar").remove ("app.bar").apply ();
		final String v = VX.val ("vectrex_emu_deck"); if (v.equals ("pop-up") || v.equals ("slim") || v.equals ("full")) VX.oset (VX.ofind ("vectrex_emu_deck"), "auto");
		log ("deck and top bar: Auto (once)"); showBar (); layout ();
	}
	/* the trail for the look and the screen's brightness, every 2 s (the owner's Nova next to a real Vectrex, 2026-11-21: Vectrex on
	 * SDR 2x, on HDR 1.25x; Bright at full brightness a bit too bright - 0.75x, easing with the slider) */
	float trailLast = -1; final Runnable trailR = this::trailTick;
	/* (the afterglow 20 % brighter - the owner, 2026-11-22: on a phone's small picture the trail is lost; on by default on a
	 *  phone (its shorter side under 600 dp), off on a tablet, the setting below the Look) */
	boolean trailBoost () { return prefs.getBoolean ("app.trailboost", false); }   /* (off by default since the afterglow's own shape for the apps) */
	void trailTick ()
	{
		h.removeCallbacks (trailR);
		float b = 0.5f;
		try { b = android.provider.Settings.System.getInt (getContentResolver (), android.provider.Settings.System.SCREEN_BRIGHTNESS, 128) / 255f; } catch (Throwable t) { }
		b = Math.max (0f, Math.min (1f, b));
		final int md = 0;
		final float tm = trailBoost () ? 1.2f : 1f;   /* (2026-11-22: as the PC, the master - was scaled down with the screen's brightness) */  //   /* (SDR Vectrex 2.8: 40 % more - the owner's Nova, iPad and iPhone without HDR) */
		if (Math.abs (tm - trailLast) > 0.02f) { trailLast = tm; VX.setTrailMul (tm); log ("trail x" + tm + " (look " + md + ", brightness " + Math.round (b * 100) + " %)"); }
		h.postDelayed (trailR, 2000);
	}
	void startCore ()
	{
		if (started) return; started = true; findContent (); Pack.retire (this); showBar ();   /* (the bar hiding itself: shown a while at the start) */
		String g = prefs.getString ("lastGame", ""); if (g.isEmpty () || !new File (g).exists ()) g = firstGame ();
		log ("starting with " + g);
		VX.start (sysDir.getAbsolutePath (), savesDir.getAbsolutePath (), g, defaults ());
		tuneWait (System.currentTimeMillis ());           /* (the start screen stays while the first start tunes this device) */
		layout (); Live.start (this);
		h.postDelayed (this::autoOnce, 2000);
		h.postDelayed (() -> testIntent (getIntent ()), 3000);
		if (Pack.token (this) == null && !prefs.getBoolean ("getter.offered", false)) h.postDelayed (this::offerGetter, 2500);
	}
	void offerGetter ()   /* (the public build's first start: where the games come from - once the device test and such are closed) */
	{
		if (prefs.getBoolean ("getter.offered", false)) return;   /* (offered meanwhile - the first start's games before the device test: not twice, nor over Ready to play) */
		final int t = VX.tuning ();   /* (not over the device test - its question, the test itself or its result) */
		if (!stack.isEmpty () || welcome.getParent () != null || t == 1 || t == 3) { h.postDelayed (this::offerGetter, 1500); return; }
		prefs.edit ().putBoolean ("getter.offered", true).apply ();
		if (Getter.count (Getter.present (this), Getter.GCE) == 0) { log ("first start: get games & extras offered"); Getter.show (this); }
	}
	@Override protected void onActivityResult (int code, int res, Intent data)
	{
		super.onActivityResult (code, res, data);
		if (code == Getter.PICK && res == RESULT_OK) Getter.picked (this, data);
	}
	/* the tests' hooks (adb: am start -e vx.game <path> -e vx.name <name> -e vx.host 0|1|2 / -e vx.join <code or address>):
	 * the same steps a player takes, logged as such */
	@Override protected void onNewIntent (Intent i) { super.onNewIntent (i); setIntent (i); if (started) testIntent (i); }
	void testIntent (Intent i)
	{
		if (i == null) return; String g = i.getStringExtra ("vx.game"), n = i.getStringExtra ("vx.name"), ho = i.getStringExtra ("vx.host"), jo = i.getStringExtra ("vx.join");
		if (n != null) prefs.edit ().putString ("net.name", Online.clean (n)).apply ();
		String op = i.getStringExtra ("vx.opts"), sn = i.getStringExtra ("vx.snap");   /* (vx.opts "size=600 px;overlay=off", vx.snap "RUNS") */
		if (op != null) for (String kv : op.split (";")) { int q = kv.indexOf ('='); if (q > 0) { int k = VX.ofind ("vectrex_emu_" + kv.substring (0, q).trim ()); if (k >= 0) VX.oset (k, kv.substring (q + 1).trim ()); } }
		if (sn != null) { log ("test: snapshot at run " + sn); VX.snap (Integer.parseInt (sn.trim ()), new File (ext (), "snap.ppm").getAbsolutePath ()); }
		if (g != null) { log ("test: load " + g); VX.load (g); Library.remember (this, g); }
		if (ho != null) h.postDelayed (() -> { log ("test: host " + ho); int mode = Integer.parseInt (ho); Online.hostRom = VX.game (); Online.hostMode = mode;
			VX.netHost (mode, Online.clean (Online.name (this)), Online.key (this), VX.game (), Online.skip ()); }, g != null ? 1500 : 0);
		if ("settings".equals (i.getStringExtra ("vx.screen"))) h.postDelayed (() -> { log ("test: settings"); Screens.settings (this); }, 1200);
		if (i.getStringExtra ("vx.adhoc") != null) h.postDelayed (() -> { log ("test: ad hoc host");   /* (as "Make a network and host") */
			Adhoc.start (this, (ok, msg) -> { log ("test: ad hoc " + (ok ? "up: " + Adhoc.ssidOn + " / " + Adhoc.pass (this) : msg)); if (ok) { Online.hostRom = VX.game (); Online.hostMode = 2;
				VX.netHost (2, Online.clean (Online.name (this)), Online.key (this), VX.game (), Online.skip ()); } }); }, 1500);
		if (jo != null) h.postDelayed (() -> { log ("test: join " + jo); VX.netJoin (jo, Online.clean (Online.name (this)), Online.key (this)); }, g != null ? 1500 : 0);
		i.removeExtra ("vx.game"); i.removeExtra ("vx.host"); i.removeExtra ("vx.join"); i.removeExtra ("vx.opts"); i.removeExtra ("vx.snap"); i.removeExtra ("vx.adhoc"); i.removeExtra ("vx.screen");
	}
	String firstGame ()
	{
		for (File r : romRoots) { File m = new File (r, "Mine Storm.bin"); if (m.exists ()) return m.getAbsolutePath (); }
		for (Library.Game x : Library.scan (this)) if (x.kind.equals ("Games")) return x.path;
		return new File (romRoots.isEmpty () ? new File (ext (), "roms") : romRoots.get (0), "Mine Storm.bin").getAbsolutePath ();   /* (none: the core's built-in Mine Storm) */
	}

	/* ---------------------------------------------------------------- the surface */
	@Override public void surfaceCreated (SurfaceHolder hd) { log ("surface created"); }
	@Override public void surfaceChanged (SurfaceHolder hd, int f, int w, int hh) { log ("surface " + w + " x " + hh); VX.setSurface (hd.getSurface ()); layout (); }
	@Override public void surfaceDestroyed (SurfaceHolder hd) { log ("surface destroyed"); VX.setSurface (null); }

	/* ---------------------------------------------------------------- the layout: the room for the picture, the bar, the controls */
	boolean touchShown ()
	{
		String m = prefs.getString ("touch.show", "auto");
		return m.equals ("always") || (m.equals ("auto") && Input.pads () == 0);
	}
	/* the top bar (Settings > App): Auto (the default: shown where it fits, else hiding itself), Shown, Auto-hide - as iOS */
	int barSel () { String b = prefs.getString ("app.bar", null);
		if (b == null && prefs.contains ("app.topbar")) { b = prefs.getString ("app.topbar", "fixed").equals ("auto") ? "hide" : "always"; prefs.edit ().putString ("app.bar", b).remove ("app.topbar").apply (); }   /* (an older one carried over) */
		return "always".equals (b) ? 1 : "hide".equals (b) ? 2 : 0; }
	boolean barAuto () { final int s = barSel (); return s == 2 || (s == 0 && tight ()); }   /* (it hides itself) */
	void layout ()
	{
		int W = root.getWidth (), H = root.getHeight (); if (W <= 0 || H <= 0) { root.post (this::layout); return; }
		boolean land = W > H; if (land != landscape) { landscape = land; VX.setLandscape (land); log ("layout: " + W + " x " + H + " px, " + (land ? "landscape" : "portrait")); }
		{ int r = getWindowManager ().getDefaultDisplay ().getRotation ();   /* (a handheld's controls are made for its landscape grip: turned from there - the Nova's screen is upright by nature, landscape is its rotation 90) */
		  final boolean natPortrait = (r % 2 == 0) == (H > W); final int rel = (r - (natPortrait ? 1 : 0) + 4) % 4;
		  if (rel != Input.rot) { Input.rot = rel; log ("screen turned: rotation " + r * 90 + ", the controls turned " + rel * 90); } }
		boolean touch = started && touchShown (); boolean tablet = Math.min (W, H) / getResources ().getDisplayMetrics ().density >= 600;
		final int dm = deckMode (); final int dh = dm == 1 ? (int) dp (58) : dm == 2 ? (int) dp (40) : 0;   /* (the deck below the picture: its room) */
		int row = land || barAuto () ? 0 : (int) dp (56), keep = (land || tablet || !touch ? 0 : (int) dp (150)) + dh;
		int x = insL, y = insTop + row, w = W - insL - insR, hh = H - insTop - insBot - row - keep;
		VX.setRoom (x, y, w, hh, !land);
		FrameLayout.LayoutParams bl = (FrameLayout.LayoutParams) bar.getLayoutParams (); bl.leftMargin = insL + (int) dp (8); bl.rightMargin = insR + (int) dp (8); bl.topMargin = insTop + (int) dp (6); bar.setLayoutParams (bl);
		bar.setVisibility (started && !land ? View.VISIBLE : View.GONE);
		FrameLayout.LayoutParams ml = (FrameLayout.LayoutParams) menuBtn.getLayoutParams (); ml.gravity = Gravity.TOP | Gravity.RIGHT; ml.topMargin = insTop + (int) dp (8); ml.rightMargin = insR + (int) dp (12); menuBtn.setLayoutParams (ml);
		menuBtn.setVisibility (started && land ? View.VISIBLE : View.GONE);
		FrameLayout.LayoutParams tl = (FrameLayout.LayoutParams) toasts.getLayoutParams (); tl.topMargin = insTop + (int) dp (64); toasts.setLayoutParams (tl);
		if (ctl.show != touch) { ctl.show = touch; log ("touch controls " + (touch ? "shown" : "hidden")); } ctl.relayout ();
	}

	/* ---------------------------------------------------------------- each moment: the core's news, online, the picture's place */
	float[] pic = new float[4];
	/* the deck (Settings: Control deck): 1 full, 2 slim, 3 pop-up, 0 off - upright only, as the iOS app */
	int deckMode ()
	{
		if (!started || landscape) return 0; final int st = deckStyle ();
		return st != 0 && deckPop () ? 3 : st;
	}
	/* (Settings > App: the deck's look - 0 off, 1 full, 2 slim - and whether it pops up, a setting of its own; the old "pop-up": slim, popping up) */
	/* (Auto, the default: what fits this screen - full, slim, slim popping up; Off; Full; Slim - a full / slim one may pop up) */
	int deckSel () { String v = VX.val ("vectrex_emu_deck"); return v.equals ("off") ? 1 : v.equals ("full") ? 2 : v.equals ("slim") || v.equals ("pop-up") ? 3 : 0; }
	int deckStyle () { final int s = deckSel (); return s == 0 ? (squarish () ? 2 : 1) : s == 1 ? 0 : s == 2 ? 1 : 2; }
	boolean deckPop () { final int s = deckSel (); return s == 0 ? popDef () : VX.val ("vectrex_emu_deck").equals ("pop-up") || prefs.getBoolean ("app.deckpop", false); }
	void deckSet (int sel, boolean pop)
	{
		final String[] V = { "auto", "off", "full", "slim" }; if (sel < 0 || sel > 3) sel = 0;
		VX.oset (VX.ofind ("vectrex_emu_deck"), V[sel]); prefs.edit ().putBoolean ("app.deckpop", pop).apply (); log ("deck " + V[sel] + (sel >= 2 && pop ? ", pop-up" : ""));
	}
	int lastDeckMode = -1;
	void placeDeck ()
	{
		final int dm = deckMode (); if (dm != lastDeckMode) { lastDeckMode = dm; layout (); }
		if (dm == 0 || pic[2] <= 0) { deck.setVisibility (View.GONE); popTab.setVisibility (View.GONE); return; }
		final int look = dm == 3 ? deckStyle () : dm, h = (int) dp (look == 1 ? 58 : 40);
		final boolean below = dm != 3 || root.getHeight () - insBot - (pic[1] + pic[3]) >= h + dp (8);   /* (popping up: below the picture where there is room, else over its foot) */
		final int top = (int) (below ? pic[1] + pic[3] : pic[1] + pic[3] - h);
		FrameLayout.LayoutParams l = (FrameLayout.LayoutParams) deck.getLayoutParams ();
		if (l.topMargin != top || l.height != h || l.leftMargin != insL || l.rightMargin != insR) { l.topMargin = top; l.height = h; l.leftMargin = insL; l.rightMargin = insR; l.gravity = android.view.Gravity.TOP; deck.setLayoutParams (l); }
		deck.mode = look == 1 ? 1 : 2;
		boolean show = dm != 3 || System.currentTimeMillis () < popUntil || !stack.isEmpty () || deck.paused != 0 || deck.power == 0;   /* (the pop-up: a while after a touch, while paused, while switched off - its OFF in sight) */
		deck.setVisibility (show ? View.VISIBLE : View.GONE);
		popTab.setVisibility (View.GONE);   /* (no tab: a tap on the picture's foot opens the pop-up deck) */
		if (dm == 3) { FrameLayout.LayoutParams t = (FrameLayout.LayoutParams) popTab.getLayoutParams (); t.gravity = android.view.Gravity.TOP;
			t.leftMargin = (int) (pic[0] + pic[2] / 2 - dp (32)); t.topMargin = (int) (pic[1] + pic[3] - dp (14)); popTab.setLayoutParams (t); }
	}
	/* the pop-up deck opened: a tap on the picture's foot, where it shows (as the hidden top bar: a tap at the top) */
	boolean popTap (float x, float y)
	{
		if (deckMode () != 3 || deck.getVisibility () == View.VISIBLE || pic[2] <= 0) return false;
		if (x < pic[0] || x > pic[0] + pic[2] || y < pic[1] + pic[3] - dp (70) || y > pic[1] + pic[3] + dp (80)) return false;
		if (ctl.shown () && ctl.onControl (x, y)) return false;   /* (never a touch for the stick or a button: over the picture's foot when there is no room below) */
		popUntil = System.currentTimeMillis () + 4000; log ("deck: pop-up opened"); placeDeck (); return true;
	}
	void deckPoked () { if (deckMode () == 3) popUntil = System.currentTimeMillis () + 4000; }
	View popTabView ()                                         /* (the pop-up deck's tab under the picture) */
	{
		TextView t = text ("\u25B4", 12, DIM, true); t.setGravity (Gravity.CENTER); t.setBackground (round (PANEL, EDGE, dp (7)));
		t.setOnClickListener (v -> { popUntil = System.currentTimeMillis () + 4000; log ("deck: pop-up opened"); placeDeck (); }); t.setContentDescription ("Show the control deck");
		return t;
	}
	long deckT;
	void tick ()
	{
		h.postDelayed (this::tick, 50);
		brightTick ();                                   /* (Max brightness: the start screen too, Whole app) */
		if (!started) return;
		rateTick (); hdrTick ();
		int st = VX.status (); if (st < 0 && welcome.getParent () == null) { started = false; root.addView (welcome); welcome.setAlpha (1); status ("The game could not be started. See files/applog.txt"); return; }
		float[] p = VX.picRect (); if (p[2] != pic[2] || p[3] != pic[3] || p[0] != pic[0] || p[1] != pic[1]) { pic = p; ctl.relayout (); }
		placeDeck (); { long now = System.currentTimeMillis (); if (now - deckT > 500) { deckT = now; deck.tick (); } }
		String m = VX.takeMessage (); if (m != null && !m.isEmpty ()) toast (m, 2.5);
		if (VX.takeBrowser () && stack.isEmpty ()) Library.show (this);
		Online.tick (this);
		Input.poll (this);
		if (barAuto () && !barHidden && System.currentTimeMillis () > barUntil) { barHidden = true; bar.animate ().alpha (0).setDuration (400); }
	}
	/* Max brightness (Settings > App, as the iPhone app: Off, Gameplay, Whole app): the screen at its brightest, whatever the
	 * brightness slider or auto-brightness says - the window's own override, as a boarding pass. Gameplay: while a game plays,
	 * the user's own in the menus (the HDR slider is no menu); Whole app: the menus too. Never when the app is not in front
	 * (Android drops a window's override by itself) */
	int maxBright () { return prefs.getInt ("app.maxbright", 1); }   /* (0 off, 1 gameplay - the default, the owner 2026-11-25 - 2 whole app) */
	boolean brightUp;
	void brightTick ()
	{
		final int mb = maxBright (); final boolean want = mb == 2 || (mb == 1 && stack.isEmpty () && welcome.getParent () == null);
		if (want == brightUp) return; brightUp = want;
		WindowManager.LayoutParams lp = getWindow ().getAttributes (); lp.screenBrightness = want ? WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_FULL : WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE;
		getWindow ().setAttributes (lp); log ("full brightness: " + (want ? "up" : "yours back"));
	}
	long barUntil; boolean barHidden;
	void showBar () { if (barAuto ()) { if (barHidden) log ("top bar shown"); barHidden = false; bar.animate ().cancel (); bar.animate ().alpha (1).setDuration (150); barUntil = System.currentTimeMillis () + 3000; }
		else { barHidden = false; bar.setAlpha (1); } }

	/* ---------------------------------------------------------------- the screens over the game */
	void hold (boolean on) { VX.setHold (on); if (Online.playing) VX.net (4, on ? 1 : 0); }
	void push (View v)
	{
		if (stack.isEmpty () && !(v instanceof Screens.Panel && ((Screens.Panel) v).live)) hold (true);   /* (the chat line: the game goes on) */
		stack.add (v); brightTick (); screens.addView (v, new FrameLayout.LayoutParams (-1, -1)); screens.bringToFront (); v.setAlpha (0); v.animate ().alpha (1).setDuration (150);   /* (in front of the start screen too) */
		v.post (() -> { View f = v.findFocus (); if (f == null) v.requestFocus (); });
	}
	void pop ()
	{
		if (stack.isEmpty ()) return; View v = stack.remove (stack.size () - 1); screens.removeView (v);
		if (v instanceof Screens.Closing) ((Screens.Closing) v).closed ();
		if (stack.isEmpty ()) { hold (false); immersive (); if (started) Input.sendAll (); } else stack.get (stack.size () - 1).requestFocus ();   /* (buttons 2 + 3 held at a game's start: a UVMC2 calibration) */
	}
	void popAll () { while (!stack.isEmpty ()) pop (); }
	View top () { return stack.isEmpty () ? null : stack.get (stack.size () - 1); }

	/* ---------------------------------------------------------------- keys and controllers: Input.java; here what the activity gets */
	@Override public boolean dispatchKeyEvent (KeyEvent e) { if (Input.key (this, e)) return true; return super.dispatchKeyEvent (e); }
	@Override public boolean dispatchGenericMotionEvent (MotionEvent e) { if (Input.motion (this, e)) return true; return super.dispatchGenericMotionEvent (e); }
	@Override public void onBackPressed () { if (Adjust.panel != null) Adjust.close (this); else if (!stack.isEmpty ()) pop (); else if (started) Screens.gameMenu (this); else super.onBackPressed (); }

	/* ---------------------------------------------------------------- the start screen */
	/* (as the iOS app's: the vector logo, the name and its line in the middle; what happens, the progress and the version at the foot) */
	View welcomeView ()
	{
		FrameLayout f = new FrameLayout (this); f.setBackgroundColor (BG);
		LinearLayout mid = new LinearLayout (this); mid.setOrientation (LinearLayout.VERTICAL); mid.setGravity (Gravity.CENTER_HORIZONTAL);
		mid.addView (new Logo (this), new LinearLayout.LayoutParams ((int) dp (170), (int) dp (180)));
		TextView t = text ("Vectrex_EMU", 32, HI, true); t.setTypeface (Typeface.MONOSPACE, Typeface.BOLD); t.setGravity (Gravity.CENTER);
		LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams (-2, -2); tl.topMargin = (int) dp (10); mid.addView (t, tl);
		TextView s = text ("The Vectrex, as faithful as it gets", 14, DIM, false); s.setGravity (Gravity.CENTER);
		LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams (-2, -2); sl.topMargin = (int) dp (10); mid.addView (s, sl);
		FrameLayout.LayoutParams ml = new FrameLayout.LayoutParams (-2, -2, Gravity.CENTER); ml.bottomMargin = (int) dp (120); f.addView (mid, ml);   /* (60 dp above the middle) */
		LinearLayout bot = new LinearLayout (this); bot.setOrientation (LinearLayout.VERTICAL);
		wStatus = text ("", 14, FG2, false); wStatus.setGravity (Gravity.CENTER); bot.addView (wStatus);
		wProg = new ProgressBar (this, null, android.R.attr.progressBarStyleHorizontal); wProg.setMax (1000); wProg.setVisibility (View.GONE);
		wProg.setProgressTintList (android.content.res.ColorStateList.valueOf (G)); wProg.setProgressBackgroundTintList (android.content.res.ColorStateList.valueOf (pal (0xff16302a)));
		LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams (-1, (int) dp (8)); pl.topMargin = (int) dp (12); bot.addView (wProg, pl);
		TextView ver = text (versionLine (), 11, DIM, false); ver.setGravity (Gravity.CENTER);
		LinearLayout.LayoutParams vl = new LinearLayout.LayoutParams (-1, -2); vl.topMargin = (int) dp (12); bot.addView (ver, vl);
		FrameLayout.LayoutParams bl = new FrameLayout.LayoutParams (-1, -2, Gravity.BOTTOM); bl.leftMargin = bl.rightMargin = (int) dp (32); bl.bottomMargin = (int) dp (28); f.addView (bot, bl);
		return f;
	}
	/* the start screen's logo: a vector shape, as the console draws - two hexagons and the inner one's three diagonals, glowing */
	static class Logo extends View {
		final Paint p = new Paint (Paint.ANTI_ALIAS_FLAG); final Main m;
		Logo (Main m) { super (m); this.m = m; setLayerType (View.LAYER_TYPE_SOFTWARE, null); }   /* (the glow: a shadow layer, drawn in software) */
		@Override protected void onDraw (Canvas c)
		{
			final float d = m.dp (1); c.translate (getWidth () / 2f, getHeight () / 2f);
			final float[][] o = { { 0, -78 }, { 68, -38 }, { 68, 42 }, { 0, 82 }, { -68, 42 }, { -68, -38 } }, i = { { 0, -48 }, { 40, -24 }, { 40, 26 }, { 0, 50 }, { -40, 26 }, { -40, -24 } };
			android.graphics.Path path = new android.graphics.Path ();
			for (float[][] h : new float[][][] { o, i }) { path.moveTo (h[0][0] * d, h[0][1] * d); for (int k = 1; k < 6; k++) path.lineTo (h[k][0] * d, h[k][1] * d); path.close (); }
			for (int[] l : new int[][] { { 0, 3 }, { 5, 2 }, { 1, 4 } }) { path.moveTo (i[l[0]][0] * d, i[l[0]][1] * d); path.lineTo (i[l[1]][0] * d, i[l[1]][1] * d); }
			p.setStyle (Paint.Style.STROKE); p.setStrokeWidth (2.2f * d); p.setStrokeCap (Paint.Cap.ROUND); p.setStrokeJoin (Paint.Join.ROUND); p.setColor (G);
			p.setShadowLayer (10 * d, 0, 0, (G & 0x00ffffff) | 0xb3000000); c.drawPath (path, p);
		}
	}
	/* the first start's "Ready to play", once - as the iOS app's: what is in place, Play or Choose a game; then the rest
	 * (the player name) */
	void showReady (Runnable after)
	{
		if (prefs.getBoolean ("app.welcomed", false) || !stack.isEmpty ()) { after.run (); return; }
		prefs.edit ().putBoolean ("app.welcomed", true).apply (); log ("ready-to-play shown");
		final File sd = new File (sysDir, "vectrex_emu"); int games = 0;
		for (Library.Game g : Library.scan (this)) if (g.kind.equals ("Games") || g.kind.equals ("UVMC2")) games++;
		String pad = null; for (int id : Input.devs) { InputDevice d = InputDevice.getDevice (id); if (d != null) { pad = d.getName (); break; } }
		if (pad == null) for (int id : InputDevice.getDeviceIds ()) { InputDevice d = InputDevice.getDevice (id); if (Input.isPad (d)) { pad = d.getName (); break; } }
		final String[][] rows = {
			{ "Vectrex BIOS", new File (sd, "bios.bin").exists () ? "Ready" : "Built in" },
			{ "Games", games + ", with overlays" },
			{ "UVMC2 and Last Human", new File (sd, "uvmc2").exists () ? "Ready" : "-" },
			{ "PiTrex SD card", new File (sd, "pitrex/pitrex.img").exists () ? "Ready, calibrated" : "-" },
			{ "Speech ROM (VecVoice)", new File (sd, "sp0256-al2.bin").exists () || new File (sd, "al2.bin").exists () ? "Ready" : "-" },
			{ "Controls", pad != null ? pad + ": player 1" : "Touch - a controller or keyboard works too" } };
		Screens.Panel p = new Screens.Panel (this, "Ready to play", Screens.BARE);
		p.add (text ("SET UP - ONCE", 12, DIM, true), -2, 0);
		TextView t = text ("Ready to play", 26, HI, true); t.setTypeface (Typeface.MONOSPACE, Typeface.BOLD); p.add (t, -2, 14);
		p.add (text ("Everything is in place. The picture's quality is measured for this device at the first game; change anything later in Settings.", 14, FG2, false), -2, 14);
		LinearLayout list = new LinearLayout (this); list.setOrientation (LinearLayout.VERTICAL); list.setBackground (round (EDGE, 0, dp (14))); list.setClipToOutline (true);
		for (int k = 0; k < rows.length; k++) { LinearLayout h = new LinearLayout (this); h.setOrientation (LinearLayout.HORIZONTAL); h.setGravity (Gravity.CENTER_VERTICAL); h.setBaselineAligned (false);
			h.setBackgroundColor (PANEL); h.setPadding ((int) dp (16), (int) dp (13), (int) dp (16), (int) dp (13));
			h.addView (text (rows[k][0], 15, FG, false), new LinearLayout.LayoutParams (0, -2, 1));
			TextView b = text (rows[k][1], 13, HI, true); b.setGravity (Gravity.END); LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams (0, -2, 1); bl.leftMargin = (int) dp (10); h.addView (b, bl);
			LinearLayout.LayoutParams hl = new LinearLayout.LayoutParams (-1, -2); if (k > 0) hl.topMargin = 1; list.addView (h, hl); }
		p.add (list, -2, 16);
		TextView play = Screens.button (this, "Play", true), pick = Screens.button (this, "Choose a game", false);
		play.setOnClickListener (v -> { log ("ready: play"); pop (); }); pick.setOnClickListener (v -> { log ("ready: choose a game"); pop (); Library.show (this); });
		p.add (play, 54, 28); p.add (pick, 46, 16);
		p.onClose = after;
		push (p); play.requestFocus ();
	}
	/* the first start (and after an update that measures anew): the core tunes this device once - every screen rate at the
	 * largest picture, the overlay on - before the first game is shown; never in the middle of a game (2026-11-24) */
	/* the screen asked for the game's rate - as the iOS app (its preferred frame rate): the display mode with that refresh
	 * rate (or a whole multiple of it), at once when the setting changes; before, a chosen 120 Hz ran twice a refresh on a
	 * panel left at 60 until the system's own setting was changed (the owner's Nova, 2026-11-24). Android drops it by
	 * itself when the app leaves the front */
	int rateAsked = -1;
	void rateTick ()
	{
		final int r = VX.rate (); if (r <= 0 || r == rateAsked) return; rateAsked = r;
		try { final Display d = getWindowManager ().getDefaultDisplay (); final Display.Mode cur = d.getMode (); int best = 0, bk = 99;
			for (Display.Mode m : d.getSupportedModes ()) {
				if (m.getPhysicalWidth () != cur.getPhysicalWidth () || m.getPhysicalHeight () != cur.getPhysicalHeight ()) continue;
				for (int k = 1; k <= 4; k++) if (Math.abs (m.getRefreshRate () - r * k) < 1.0f && k < bk) { bk = k; best = m.getModeId (); } }
			final WindowManager.LayoutParams lp = getWindow ().getAttributes ();
			if (lp.preferredDisplayModeId != best) { lp.preferredDisplayModeId = best; getWindow ().setAttributes (lp); }
			log ("screen asked for " + r + " Hz: " + (best != 0 ? "mode " + best + (bk > 1 ? " (" + (r * bk) + " Hz)" : "") : "no such mode - the system's own"));
		} catch (Throwable t) { log ("screen rate: " + t); }
	}
	/* HDR (Android 14 and up): the headroom the screen gives now, read again every second - as the iOS app (at full
	 * brightness only a little is left above white) */
	long hdrT; float hdrNow = -1;
	void hdrTick ()
	{
		if (!hdrCan || Build.VERSION.SDK_INT < 34) return; final long now = System.currentTimeMillis (); if (now - hdrT < 1000) return; hdrT = now;
		try { final float r = Math.max (1f, getWindowManager ().getDefaultDisplay ().getHdrSdrRatio ()); if (Math.abs (r - hdrNow) < 0.05f) return; hdrNow = r;
			final float f = prefs.getFloat ("app.hdr", 1f); VX.setHdr (f > 0.01f, r, f); log ("HDR: headroom now " + r + "x"); }
		catch (Throwable t) { }
	}
	boolean tuneAsked, tuneRan; String tuneSaid = "";
	void tuneWait (long t0)
	{
		final int t = VX.tuning (), st = VX.status ();
		if (t == 3 && !tuneAsked && getterFirst ()) {   /* (the public build's first start: the games first, the device test after them -
			                                         the owner, 2026-11-25: the download page broke into the test) */
			tuneAsked = true; prefs.edit ().putBoolean ("getter.offered", true).apply (); log ("first start: get games & extras, then the device test");
			status ("First the games"); Screens.Panel g = Getter.show (this); g.onClose = this::testAfterDownloads; }
		else if (t == 3 && !tuneAsked) { tuneAsked = true; status ("A short test of this device first"); askTune (); }
		else if (t == 3 && stack.isEmpty ()) VX.tuneAnswer (-1);   /* (the question closed without an answer: later) */
		if (t == 1) { final String s = "Testing this device (once)...\n" + VX.tuneStatus (); if (!s.equals (tuneSaid)) { tuneSaid = s; status (s); } }
		if (st < 0 || t == 2 || (t == 0 && System.currentTimeMillis () - t0 > 60000)) {
			welcome.animate ().alpha (0).setDuration (500).withEndAction (() -> root.removeView (welcome));
			/* (the first start's Ready to play once the start screen is gone - not over the device test's question or its
			 *  result -, the player name after it: this device's, or asked for once) */
			final Runnable greet = () -> h.postDelayed (() -> showReady (() -> h.postDelayed (() -> Online.firstName (this), 800)), 600);
			if (t == 2 && tuneRan && !VX.tuneReport ().isEmpty ()) tuneReport (VX.tuneReport (), greet); else greet.run ();
			return; }
		h.postDelayed (() -> tuneWait (t0), 100);
	}
	boolean getterFirst () { return Pack.token (this) == null && !prefs.getBoolean ("getter.offered", false) && Getter.count (Getter.present (this), Getter.GCE) == 0; }
	void testAfterDownloads ()   /* (the download page closed: its downloads finished first - the test measures this device alone) */
	{
		if (Getter.anyBusy ()) { status ("Downloading...\nThe device test follows"); if (stack.isEmpty ()) { hold (true); } h.postDelayed (this::testAfterDownloads, 500); return; }
		status ("A short test of this device"); askTune ();
	}
	/* the question before the test - what it does, how long, that it never runs in a game */
	void askTune ()
	{
		Screens.Panel p = new Screens.Panel (this, "Device test");
		p.header ("Best quality at full speed");
		p.row ("Test now", "About 10 to 20 seconds, once", () -> { pop (); tuneRan = true; VX.tuneAnswer (1); });
		p.row ("Later", "Asked again at the next start", () -> { pop (); VX.tuneAnswer (-1); });
		p.foot ("A short test of this device's graphics finds the best picture quality it keeps at full speed, at each screen rate. It runs before you play, never during a game.");
		push (p);
	}
	/* the test's result: each screen rate - every effect on, or what this device runs without */
	void tuneReport (String r, Runnable after)
	{
		log ("device test: " + r.replace ('\n', ';'));
		if (r == null || r.isEmpty ()) return;
		Screens.Panel p = new Screens.Panel (this, "Device test done");
		p.header (r.contains ("effects off") ? "What this device runs at full speed" : "Every effect on at every rate");
		for (String line : r.split ("\n")) { final int c = line.indexOf (": "); p.row (c > 0 ? line.substring (0, c) : line, c > 0 ? line.substring (c + 2) : "", null); }
		p.foot ("Quality profile Auto uses these results; the settings let you choose another.");
		p.row ("OK", "", this::pop);
		p.onClose = after;                              /* (closed - OK or back: the greeting then) */
		push (p);
	}
	void status (String s) { wStatus.setText (s); log ("start screen: " + s.replace ('\n', ' ')); }

	/* ---------------------------------------------------------------- the quick bar (upright): the most used, one tap */
	LinearLayout quickBar ()
	{
		LinearLayout b = new LinearLayout (this); b.setOrientation (LinearLayout.HORIZONTAL);
		String[][] it = { { "GAMES", "games", "10" }, { "SAVE", "save", "11" }, { "LOAD", "load", "12" }, { "OVERLAY", "overlay", "13" }, { "ROOM", "room", "14" }, { "MENU", "menu", "15" } };
		for (String[] x : it) {                                    /* (as the iOS app's: the icon above the word) */
			final String w = x[0]; final int ic = Integer.parseInt (x[2]);
			View t = new View (this) { final Paint tp = new Paint (Paint.ANTI_ALIAS_FLAG);
				@Override protected void onDraw (Canvas c) { if (deck == null) return; float d = dp (1), cx = getWidth () / 2f;
					deck.icon (c, ic, cx, getHeight () * 0.36f, 15 * d, HI);
					tp.setColor (HI); tp.setTypeface (Typeface.DEFAULT_BOLD); tp.setTextAlign (Paint.Align.CENTER); tp.setTextSize (10 * d);
					float tw = tp.measureText (w), room = getWidth () - 6 * d; if (tw > room) tp.setTextSize (10 * d * room / tw);
					c.drawText (w, cx, getHeight () - 7 * d, tp); } };
			t.setBackground (round (PANEL, EDGE, dp (12))); t.setContentDescription (w); t.setFocusable (false);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, -1, 1); lp.leftMargin = lp.rightMargin = (int) dp (3); b.addView (t, lp);
			final String a = x[1]; t.setOnClickListener (v -> { if (barHidden) { showBar (); return; }   /* (the faded bar: a tap shows it, presses nothing) */
				v.performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY); showBar (); quick (a); }); }
		return b;
	}
	void quick (String a)
	{
		log ("ui bar " + a);
		switch (a) {
		case "games": Library.show (this); break;
		case "save": toast (Online.playing ? "No save states during an online game" : VX.state (true, 1) != 0 ? "Saved in slot 1" : "This game has no save states", 2); break;
		case "load": toast (Online.playing ? "No save states during an online game" : VX.stateExists (1) != 0 && VX.state (false, 1) != 0 ? "Slot 1 loaded" : "Nothing saved in slot 1", 2); break;
		case "overlay": overlayToggle (); break;
		case "room": { int i = VX.ofind ("vectrex_emu_room"); if (i >= 0) Screens.choice (this, i); break; }
		case "menu": Screens.gameMenu (this); break;
		}
	}
	void overlayToggle () { int i = VX.ofind ("vectrex_emu_overlay"); if (i >= 0) VX.oset (i, VX.opt (i, 5).equals ("on") ? "off" : "on"); }

	/* ---------------------------------------------------------------- messages */
	long toastNext; int toastN;
	/* (the other player paused: a line that stays until they are back - at the top of the toasts) */
	TextView ban;
	void banner (String s)
	{
		if (s == null) { if (ban != null) { toasts.removeView (ban); ban = null; } return; }
		if (ban == null) { ban = text ("  " + s + "  ", 15, HI, true); ban.setGravity (Gravity.CENTER); ban.setMaxLines (2); ban.setPadding ((int) dp (14), (int) dp (9), (int) dp (14), (int) dp (9));
			ban.setBackground (round (0xf20b1511, EDGE2, dp (10)));
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-2, -2); lp.bottomMargin = (int) dp (8); lp.leftMargin = lp.rightMargin = (int) dp (20); toasts.addView (ban, 0, lp); }
		else if (!ban.getText ().toString ().equals ("  " + s + "  ")) ban.setText ("  " + s + "  ");
	}
	void toast (String s, double secs)
	{
		log ("toast: " + s);
		TextView t = text ("  " + s + "  ", 14, HI, true); t.setGravity (Gravity.CENTER); t.setMaxLines (2); t.setPadding ((int) dp (12), (int) dp (7), (int) dp (12), (int) dp (7));
		t.setBackground (round (0xf20b1511, EDGE2, dp (10)));
		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-2, -2); lp.bottomMargin = (int) dp (6); lp.leftMargin = lp.rightMargin = (int) dp (20);
		toasts.bringToFront ();   /* (over any open screen: a chat line came while a menu was open, under it) */
		toasts.addView (t, lp); while (toasts.getChildCount () > 4) toasts.removeViewAt (ban != null && toasts.getChildAt (0) == ban ? 1 : 0);
		t.animate ().alpha (0).setStartDelay ((long) (Math.max (secs, 2) * 1000)).setDuration (500).withEndAction (() -> toasts.removeView (t));
	}

	/* ---------------------------------------------------------------- small helpers for the screens */
	TextView text (String s, float sp, int col, boolean bold)
	{
		TextView t = new TextView (this); t.setText (s); t.setTextSize (sp); t.setTextColor (col); if (bold) t.setTypeface (Typeface.DEFAULT_BOLD); return t;
	}
	static GradientDrawable round (int fill, int edge, float r) { GradientDrawable g = new GradientDrawable (); g.setColor (fill); g.setCornerRadius (r); if (edge != 0) g.setStroke (2, edge); return g; }
}

