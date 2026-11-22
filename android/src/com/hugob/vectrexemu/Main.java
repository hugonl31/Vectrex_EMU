/* Main.java - Vectrex_EMU for Android: the game screen, as the iOS app's (ios/App/VXApp.m) - the picture (a SurfaceView
 * the core's thread draws on, jni/vxa.c), the touch controls (a stick where the thumb lands, buttons 1-4 as the Vectrex
 * controller) while no controller is connected, the quick bar upright / the menu button sideways, the core's control deck
 * under the picture (the core draws it; touched as its pointer), the screens over the game (Screens.java), online play.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.app.Activity;
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
	static final int BG = 0xff05080a, BAR = 0xff0a1210, PANEL = 0xff0b1511, BODY = 0xff0f1c17, EDGE = 0xff1d3329, EDGE2 = 0xff2f5544, SEL = 0xff24443a,
		G = 0xff66aa88, HI = 0xff88ccbb, HI2 = 0xffbbffee, FG = 0xffc8d6cf, FG2 = 0xffa9bdb3, DIM = 0xff7f978b, CHG = 0xffd9a35e;
	static Main me;
	SharedPreferences prefs; FrameLayout root, screens; SurfaceView sv; Controls ctl; LinearLayout bar, toasts; TextView menuBtn; DeckView deck; View popTab; long popUntil;
	View welcome; TextView wStatus; ProgressBar wProg;
	final Handler h = new Handler (Looper.getMainLooper ());
	File files, sysDir, savesDir; final List<File> romRoots = new ArrayList<> ();
	boolean started, landscape; int insTop, insBot, insL, insR;
	final ArrayList<View> stack = new ArrayList<> ();           /* (the screens over the game, the top one last) */

	float dp (float v) { return TypedValue.applyDimension (TypedValue.COMPLEX_UNIT_DIP, v, getResources ().getDisplayMetrics ()); }
	static void log (String s) { try { VX.log (s); } catch (Throwable t) { } }

	@Override protected void onCreate (Bundle b)
	{
		super.onCreate (b); me = this; prefs = getSharedPreferences ("vx", MODE_PRIVATE);
		files = getFilesDir (); VX.logStart (files.getAbsolutePath ());
		log ("app start: Vectrex_EMU " + version () + " on " + Build.MANUFACTURER + " " + Build.MODEL + ", Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")");
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
		menuBtn.setBackground (round (0xbf0b1511, EDGE2, dp (22))); menuBtn.setOnClickListener (v -> Screens.gameMenu (this)); menuBtn.setContentDescription ("Menu");
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
		log ("screen: " + d.getRefreshRate () + " Hz (up to " + best + "), HDR " + (hdrCan ? "yes, headroom " + hdrRoom : hdrScreen ? "screen yes, but not on this Android (before 14): off" : "no") + ", " + getResources ().getDisplayMetrics ().widthPixels + " x " + getResources ().getDisplayMetrics ().heightPixels + " px, density " + getResources ().getDisplayMetrics ().density);
		VX.setHdr (hdrCan && prefs.getFloat ("app.hdr", 1f) > 0.01f, hdrRoom, prefs.getFloat ("app.hdr", 1f));
		VX.setBright (prefs.getInt ("app.look", 1));   /* (Bright the default on phones and tablets) */
		VX.setHdrLook (0);   /* (2026-11-22: one picture - no look to choose) */   /* (Bright the default everywhere) */   /* (1, the whole picture lifted: gone) */   /* (the look: Vectrex the default with HDR, Bright without - as iOS) */
		trailTick ();
	}
	float maxHz = 60, curHz = 60; boolean hdrScreen;
	void hdrChanged ()
	{
		final float f = prefs.getFloat ("app.hdr", 1f); VX.setHdr (hdrCan && f > 0.01f, hdrRoom, f);
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
	@Override protected void onPause () { super.onPause (); VX.setRunning (false); if (Online.playing) VX.net (4, 2); log ("app in the background"); }   /* (online: the other
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
	boolean trailBoost () { return prefs.getBoolean ("app.trailboost", getResources ().getConfiguration ().smallestScreenWidthDp < 600); }
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
		if (started) return; started = true; findContent (); showBar ();   /* (the bar hiding itself: shown a while at the start) */
		String g = prefs.getString ("lastGame", ""); if (g.isEmpty () || !new File (g).exists ()) g = firstGame ();
		log ("starting with " + g);
		VX.start (sysDir.getAbsolutePath (), savesDir.getAbsolutePath (), g, defaults ());
		welcome.animate ().alpha (0).setDuration (500).withEndAction (() -> root.removeView (welcome));
		layout (); Live.start (this);
		h.postDelayed (this::autoOnce, 2000);
		h.postDelayed (() -> testIntent (getIntent ()), 3000);
		h.postDelayed (() -> Online.firstName (this), 2500);    /* (the player name: this device's, or asked for once) */
		if (!prefs.getBoolean ("app.welcomed", false)) h.postDelayed (() -> { prefs.edit ().putBoolean ("app.welcomed", true).apply ();
			toast ("Tap the cartridge on the deck for the games, ≡ or the menu button for the rest", 5); }, 1500);
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
		boolean show = dm != 3 || System.currentTimeMillis () < popUntil || !stack.isEmpty () || deck.paused != 0;   /* (the pop-up: a while after a touch, while paused) */
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
		if (!started) return;
		int st = VX.status (); if (st < 0 && welcome.getParent () == null) { started = false; root.addView (welcome); welcome.setAlpha (1); status ("The game could not be started. See files/applog.txt"); return; }
		float[] p = VX.picRect (); if (p[2] != pic[2] || p[3] != pic[3] || p[0] != pic[0] || p[1] != pic[1]) { pic = p; ctl.relayout (); }
		placeDeck (); { long now = System.currentTimeMillis (); if (now - deckT > 500) { deckT = now; deck.tick (); } }
		String m = VX.takeMessage (); if (m != null && !m.isEmpty ()) toast (m, 2.5);
		if (VX.takeBrowser () && stack.isEmpty ()) Library.show (this);
		Online.tick (this);
		Input.poll (this);
		if (barAuto () && !barHidden && System.currentTimeMillis () > barUntil) { barHidden = true; bar.animate ().alpha (0).setDuration (400); }
	}
	long barUntil; boolean barHidden;
	void showBar () { if (barAuto ()) { if (barHidden) log ("top bar shown"); barHidden = false; bar.animate ().cancel (); bar.animate ().alpha (1).setDuration (150); barUntil = System.currentTimeMillis () + 3000; }
		else { barHidden = false; bar.setAlpha (1); } }

	/* ---------------------------------------------------------------- the screens over the game */
	void hold (boolean on) { VX.setHold (on); if (Online.playing) VX.net (4, on ? 1 : 0); }
	void push (View v)
	{
		if (stack.isEmpty ()) hold (true);
		stack.add (v); screens.addView (v, new FrameLayout.LayoutParams (-1, -1)); screens.bringToFront (); v.setAlpha (0); v.animate ().alpha (1).setDuration (150);   /* (in front of the start screen too) */
		v.post (() -> { View f = v.findFocus (); if (f == null) v.requestFocus (); });
	}
	void pop ()
	{
		if (stack.isEmpty ()) return; View v = stack.remove (stack.size () - 1); screens.removeView (v);
		if (v instanceof Screens.Closing) ((Screens.Closing) v).closed ();
		if (stack.isEmpty ()) { hold (false); immersive (); } else stack.get (stack.size () - 1).requestFocus ();
	}
	void popAll () { while (!stack.isEmpty ()) pop (); }
	View top () { return stack.isEmpty () ? null : stack.get (stack.size () - 1); }

	/* ---------------------------------------------------------------- keys and controllers: Input.java; here what the activity gets */
	@Override public boolean dispatchKeyEvent (KeyEvent e) { if (Input.key (this, e)) return true; return super.dispatchKeyEvent (e); }
	@Override public boolean dispatchGenericMotionEvent (MotionEvent e) { if (Input.motion (this, e)) return true; return super.dispatchGenericMotionEvent (e); }
	@Override public void onBackPressed () { if (Adjust.panel != null) Adjust.close (this); else if (!stack.isEmpty ()) pop (); else if (started) Screens.gameMenu (this); else super.onBackPressed (); }

	/* ---------------------------------------------------------------- the start screen */
	View welcomeView ()
	{
		LinearLayout l = new LinearLayout (this); l.setOrientation (LinearLayout.VERTICAL); l.setGravity (Gravity.CENTER); l.setBackgroundColor (BG); l.setPadding ((int) dp (32), 0, (int) dp (32), 0);
		TextView t = text ("VECTREX_EMU", 30, HI, true); t.setLetterSpacing (0.08f); t.setGravity (Gravity.CENTER); l.addView (t);
		TextView s = text ("Vectrex arcade, faithfully", 14, DIM, false); s.setGravity (Gravity.CENTER); l.addView (s);
		wStatus = text ("", 14, FG2, false); wStatus.setGravity (Gravity.CENTER); wStatus.setPadding (0, (int) dp (28), 0, (int) dp (12)); l.addView (wStatus);
		wProg = new ProgressBar (this, null, android.R.attr.progressBarStyleHorizontal); wProg.setMax (1000); wProg.setVisibility (View.GONE);
		l.addView (wProg, new LinearLayout.LayoutParams (-1, (int) dp (8)));
		return l;
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

