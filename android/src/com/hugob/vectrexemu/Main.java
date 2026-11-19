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
	SharedPreferences prefs; FrameLayout root, screens; SurfaceView sv; Controls ctl; LinearLayout bar, toasts; TextView menuBtn;
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
			if (c != null) { c.hide (WindowInsets.Type.systemBars ()); c.setSystemBarsBehavior (android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); } return; }
		getWindow ().getDecorView ().setSystemUiVisibility (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
			| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
	}
	@Override public void onWindowFocusChanged (boolean f) { super.onWindowFocusChanged (f); if (f) immersive (); }
	void insets (WindowInsets ins)
	{
		int t = 0, bo = 0, l = 0, r = 0;
		if (Build.VERSION.SDK_INT >= 28 && ins.getDisplayCutout () != null) { t = ins.getDisplayCutout ().getSafeInsetTop (); bo = ins.getDisplayCutout ().getSafeInsetBottom (); l = ins.getDisplayCutout ().getSafeInsetLeft (); r = ins.getDisplayCutout ().getSafeInsetRight (); }
		if (t != insTop || bo != insBot || l != insL || r != insR) { insTop = t; insBot = bo; insL = l; insR = r; log ("safe insets " + t + " " + l + " " + bo + " " + r); layout (); }
	}

	/* ---------------------------------------------------------------- HDR: the screen's headroom; Settings > App */
	boolean hdrCan; float hdrRoom = 1;
	void hdrSetup ()
	{
		Display d = getWindowManager ().getDefaultDisplay ();
		try {
			hdrCan = Build.VERSION.SDK_INT >= 26 && d.isHdr ();
			if (hdrCan && d.getHdrCapabilities () != null) hdrRoom = Math.max (1f, d.getHdrCapabilities ().getDesiredMaxLuminance () / 203f);
			if (Build.VERSION.SDK_INT >= 34 && d.isHdrSdrRatioAvailable ()) hdrRoom = Math.max (hdrRoom, d.getHdrSdrRatio ());
			if (hdrCan && Build.VERSION.SDK_INT >= 26) getWindow ().setColorMode (ActivityInfo.COLOR_MODE_HDR);
		} catch (Throwable t) { log ("HDR: " + t); }
		float best = d.getRefreshRate (); try { for (Display.Mode m : d.getSupportedModes ()) best = Math.max (best, m.getRefreshRate ()); } catch (Throwable t) { }
		maxHz = best;
		log ("screen: " + d.getRefreshRate () + " Hz (up to " + best + "), HDR " + (hdrCan ? "yes, headroom " + hdrRoom : "no") + ", " + getResources ().getDisplayMetrics ().widthPixels + " x " + getResources ().getDisplayMetrics ().heightPixels + " px, density " + getResources ().getDisplayMetrics ().density);
		VX.setHdr (hdrCan, hdrRoom, prefs.getFloat ("app.hdr", 1f));
	}
	float maxHz = 60;
	void hdrChanged () { VX.setHdr (hdrCan, hdrRoom, prefs.getFloat ("app.hdr", 1f)); }

	/* ---------------------------------------------------------------- the content: the games, the BIOS, the cards */
	/* (where they are: the app's own folder (the downloads, adb), or RetroArch's folders - as the RetroArch core: BIOS/
	 *  vectrex_emu and ROMs/vectrex on a card, RetroArch/system - with the all-files access the player gives once) */
	List<File> volumes ()
	{
		List<File> v = new ArrayList<> (); v.add (Environment.getExternalStorageDirectory ());
		File[] s = new File ("/storage").listFiles (); if (s != null) for (File f : s) if (!f.getName ().equals ("emulated") && !f.getName ().equals ("self") && f.isDirectory ()) v.add (f);
		return v;
	}
	File ext () { File e = getExternalFilesDir (null); return e != null ? e : files; }
	boolean allFiles () { return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager (); }
	void findContent ()
	{
		romRoots.clear (); sysDir = null; savesDir = null; File e = ext ();
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
		if (!bios) { status ("No Vectrex files found.\n\nPut them in " + new File (ext (), "system/vectrex_emu") + " and the games in " + new File (ext (), "roms") + ", or in RetroArch's folders (BIOS/vectrex_emu, ROMs/vectrex) with access to all files."); return; }
		startCore ();
	}
	@Override protected void onResume ()
	{
		super.onResume (); VX.setRunning (true); immersive ();
		if (!started && welcome.getParent () != null && !Pack.busy) h.postDelayed (this::checkContent, 300);   /* (back from the all-files setting) */
		if (Online.playing && stack.isEmpty ()) VX.net (4, 0);
	}
	@Override protected void onPause () { super.onPause (); VX.setRunning (false); if (Online.playing) VX.net (4, 1); log ("app in the background"); }
	@Override protected void onDestroy () { super.onDestroy (); Adhoc.stop (this); log ("the app ends"); }
	@Override public void onRequestPermissionsResult (int code, String[] perms, int[] res) { if (code == 7) Adhoc.onPermission (this, res.length > 0 && res[0] == android.content.pm.PackageManager.PERMISSION_GRANTED); }

	String defaults ()
	{
		/* (this app's own, as the iOS app's: the deck below the picture, the picture upright as the screen stands, the keys
		 *  as on the PC, the screen's own rate) */
		int hz = maxHz >= 143 ? 144 : maxHz >= 119 ? 120 : maxHz >= 89 ? 90 : 60;
		return "vectrex_emu_deck=full\nvectrex_emu_rotate=off\nvectrex_emu_rotate_stick=off\nvectrex_emu_keys=on\nvectrex_emu_btn_eject=R\nvectrex_emu_rate=" + hz + " Hz\n";
	}
	void startCore ()
	{
		if (started) return; started = true; findContent ();
		String g = prefs.getString ("lastGame", ""); if (g.isEmpty () || !new File (g).exists ()) g = firstGame ();
		log ("starting with " + g); if (maxHz > 61) { WindowManager.LayoutParams a = getWindow ().getAttributes (); a.preferredRefreshRate = maxHz; getWindow ().setAttributes (a); }
		VX.start (sysDir.getAbsolutePath (), savesDir.getAbsolutePath (), g, defaults ());
		welcome.animate ().alpha (0).setDuration (500).withEndAction (() -> root.removeView (welcome));
		layout (); Live.start (this);
		h.postDelayed (() -> testIntent (getIntent ()), 3000);
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
		if (i.getStringExtra ("vx.adhoc") != null) h.postDelayed (() -> { log ("test: ad hoc host");   /* (as "Make a network and host") */
			Adhoc.start (this, (ok, msg) -> { log ("test: ad hoc " + (ok ? "up: " + Adhoc.ssidOn + " / " + Adhoc.pass (this) : msg)); if (ok) { Online.hostRom = VX.game (); Online.hostMode = 2;
				VX.netHost (2, Online.clean (Online.name (this)), Online.key (this), VX.game (), Online.skip ()); } }); }, 1500);
		if (jo != null) h.postDelayed (() -> { log ("test: join " + jo); VX.netJoin (jo, Online.clean (Online.name (this)), Online.key (this)); }, g != null ? 1500 : 0);
		i.removeExtra ("vx.game"); i.removeExtra ("vx.host"); i.removeExtra ("vx.join"); i.removeExtra ("vx.opts"); i.removeExtra ("vx.snap"); i.removeExtra ("vx.adhoc");
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
	boolean barAuto () { return prefs.getString ("app.topbar", "fixed").equals ("auto"); }
	void layout ()
	{
		int W = root.getWidth (), H = root.getHeight (); if (W <= 0 || H <= 0) { root.post (this::layout); return; }
		boolean land = W > H; if (land != landscape) { landscape = land; VX.setLandscape (land); log ("layout: " + W + " x " + H + " px, " + (land ? "landscape" : "portrait")); }
		boolean touch = started && touchShown (); boolean tablet = Math.min (W, H) / getResources ().getDisplayMetrics ().density >= 600;
		int row = land || barAuto () ? 0 : (int) dp (56), keep = land || tablet || !touch ? 0 : (int) dp (150);
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
	void tick ()
	{
		h.postDelayed (this::tick, 50);
		if (!started) return;
		int st = VX.status (); if (st < 0 && welcome.getParent () == null) { started = false; root.addView (welcome); welcome.setAlpha (1); status ("The game could not be started. See files/applog.txt"); return; }
		float[] p = VX.picRect (); if (p[2] != pic[2] || p[3] != pic[3] || p[0] != pic[0] || p[1] != pic[1]) { pic = p; ctl.relayout (); }
		String m = VX.takeMessage (); if (m != null && !m.isEmpty ()) toast (m, 2.5);
		if (VX.takeBrowser () && stack.isEmpty ()) Library.show (this);
		Online.tick (this);
		Input.poll (this);
		if (barAuto () && bar.getAlpha () > 0 && System.currentTimeMillis () > barUntil) bar.animate ().alpha (0).setDuration (400);
	}
	long barUntil;
	void showBar () { if (barAuto ()) { bar.animate ().alpha (1).setDuration (150); barUntil = System.currentTimeMillis () + 3000; } }

	/* ---------------------------------------------------------------- the screens over the game */
	void hold (boolean on) { VX.setHold (on); if (Online.playing) VX.net (4, on ? 1 : 0); }
	void push (View v)
	{
		if (stack.isEmpty ()) hold (true);
		stack.add (v); screens.addView (v, new FrameLayout.LayoutParams (-1, -1)); v.setAlpha (0); v.animate ().alpha (1).setDuration (150);
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
	@Override public void onBackPressed () { if (!stack.isEmpty ()) pop (); else if (started) Screens.gameMenu (this); else super.onBackPressed (); }

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
		String[][] it = { { "GAMES", "games" }, { "SAVE", "save" }, { "LOAD", "load" }, { "OVERLAY", "overlay" }, { "ROOM", "room" }, { "MENU", "menu" } };
		for (String[] x : it) {
			TextView t = text (x[0], 11, HI, true); t.setGravity (Gravity.CENTER); t.setBackground (round (PANEL, EDGE, dp (10)));
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, -1, 1); lp.leftMargin = lp.rightMargin = (int) dp (3); b.addView (t, lp);
			final String a = x[1]; t.setOnClickListener (v -> { v.performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY); quick (a); }); }
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
	void toast (String s, double secs)
	{
		log ("toast: " + s);
		TextView t = text ("  " + s + "  ", 14, HI, true); t.setGravity (Gravity.CENTER); t.setMaxLines (2); t.setPadding ((int) dp (12), (int) dp (7), (int) dp (12), (int) dp (7));
		t.setBackground (round (0xf20b1511, EDGE2, dp (10)));
		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-2, -2); lp.bottomMargin = (int) dp (6); lp.leftMargin = lp.rightMargin = (int) dp (20);
		toasts.addView (t, lp); while (toasts.getChildCount () > 4) toasts.removeViewAt (0);
		t.animate ().alpha (0).setStartDelay ((long) (Math.max (secs, 2) * 1000)).setDuration (500).withEndAction (() -> toasts.removeView (t));
	}

	/* ---------------------------------------------------------------- small helpers for the screens */
	TextView text (String s, float sp, int col, boolean bold)
	{
		TextView t = new TextView (this); t.setText (s); t.setTextSize (sp); t.setTextColor (col); if (bold) t.setTypeface (Typeface.DEFAULT_BOLD); return t;
	}
	static GradientDrawable round (int fill, int edge, float r) { GradientDrawable g = new GradientDrawable (); g.setColor (fill); g.setCornerRadius (r); if (edge != 0) g.setStroke (2, edge); return g; }
}

