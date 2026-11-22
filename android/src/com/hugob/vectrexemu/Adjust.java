/* Adjust.java - the one-slider panel over the running game, as the iOS app's VXAdjust: a number setting (4 or more
 * numbers among its values) moved with a slider while the game goes on and shows it; its other values (auto and the
 * like) as buttons beside it; < > to the previous / next number setting of its group; Default, All settings, Done.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import java.util.ArrayList;

final class Adjust {
	static LinearLayout panel; static int opt = -1;

	static double num (String s) { try { return Double.parseDouble (s.replaceAll ("[^0-9.+-].*$", "").trim ()); } catch (Exception e) { return Double.NaN; } }
	/* (the numbers, sorted - null when not a slider: fewer than 4, or the screen's rates) */
	static ArrayList<String> nums (int i, ArrayList<String> extra)
	{
		ArrayList<String> n = new ArrayList<> ();
		for (String v : VX.opt (i, 4).split ("\\|")) { if (v.contains (" Hz")) return null; if (!Double.isNaN (num (v)) && v.matches ("^[-+]?[0-9.].*")) n.add (v); else if (extra != null) extra.add (v); }
		if (n.size () < 4) return null;
		n.sort ((a, b) -> Double.compare (num (a), num (b))); return n;
	}
	static boolean slider (int i) { return nums (i, null) != null; }

	static void close (Main m) { if (panel != null) { m.root.removeView (panel); panel = null; opt = -1; Main.log ("adjust closed"); } }
	static void show (Main m, int i)
	{
		close (m); m.popAll (); opt = i;
		final ArrayList<String> extra = new ArrayList<> (); final ArrayList<String> n = nums (i, extra); if (n == null) return;
		Main.log ("adjust " + VX.opt (i, 0));
		LinearLayout p = new LinearLayout (m); p.setOrientation (LinearLayout.VERTICAL); p.setPadding ((int) m.dp (14), (int) m.dp (12), (int) m.dp (14), (int) m.dp (14));
		p.setBackground (Main.round ((Main.BAR & 0x00ffffff) | 0xf2000000, Main.EDGE2, m.dp (22))); p.setClickable (true);
		String cat = ""; for (int c = 0; c < VX.ncats (); c++) if (VX.cat (c, 0).equals (VX.opt (i, 2))) cat = VX.cat (c, 1);
		LinearLayout top = new LinearLayout (m); top.setGravity (Gravity.CENTER_VERTICAL);
		TextView prev = Screens.button (m, "‹", false), next = Screens.button (m, "›", false);
		LinearLayout mid = new LinearLayout (m); mid.setOrientation (LinearLayout.VERTICAL); mid.setGravity (Gravity.CENTER_HORIZONTAL);
		TextView tc = m.text (cat.toUpperCase (java.util.Locale.ROOT), 11, Main.DIM, true), tn = m.text (VX.opt (i, 1), 17, Main.HI, true);
		tc.setGravity (Gravity.CENTER); tn.setGravity (Gravity.CENTER); mid.addView (tc); mid.addView (tn);
		top.addView (prev, new LinearLayout.LayoutParams ((int) m.dp (44), (int) m.dp (44))); top.addView (mid, new LinearLayout.LayoutParams (0, -2, 1)); top.addView (next, new LinearLayout.LayoutParams ((int) m.dp (44), (int) m.dp (44)));
		p.addView (top);
		TextView info = m.text (VX.opt (i, 3), 12, Main.DIM, false); info.setGravity (Gravity.CENTER); info.setMaxLines (2);
		LinearLayout.LayoutParams il = new LinearLayout.LayoutParams (-1, -2); il.topMargin = (int) m.dp (6); p.addView (info, il);
		final TextView val = m.text (VX.opt (i, 5), 20, Main.HI, true);
		final SeekBar sb = new SeekBar (m); sb.setMax (n.size () - 1); { int at = n.indexOf (VX.opt (i, 5)); sb.setProgress (Math.max (0, at)); }
		sb.setOnSeekBarChangeListener (new SeekBar.OnSeekBarChangeListener () {
			public void onProgressChanged (SeekBar s, int v, boolean user) { if (!user) return; String nv = n.get (v); VX.oset (i, nv); val.setText (nv); }
			public void onStartTrackingTouch (SeekBar s) {}
			public void onStopTrackingTouch (SeekBar s) { Main.log ("adjust " + VX.opt (i, 0) + " = " + VX.opt (i, 5)); } });
		LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams (-1, (int) m.dp (44)); sl.topMargin = (int) m.dp (6); p.addView (sb, sl);
		if (!extra.isEmpty ()) {                                   /* (auto and the like: buttons) */
			LinearLayout ex = new LinearLayout (m);
			for (String e : extra) { TextView b = Screens.button (m, e, VX.opt (i, 5).equals (e)); b.setTextSize (13);
				b.setOnClickListener (v -> { VX.oset (i, e); show (m, i); });
				LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, (int) m.dp (38), 1); lp.leftMargin = lp.rightMargin = (int) m.dp (4); ex.addView (b, lp); }
			LinearLayout.LayoutParams el = new LinearLayout.LayoutParams (-1, -2); el.topMargin = (int) m.dp (6); p.addView (ex, el); }
		LinearLayout bot = new LinearLayout (m); bot.setGravity (Gravity.CENTER_VERTICAL);
		bot.addView (val, new LinearLayout.LayoutParams (0, -2, 1));
		TextView all = Screens.button (m, "☰", false), def = Screens.button (m, "Default", false), done = Screens.button (m, "Done", true);
		for (TextView b : new TextView[] { all, def, done }) { LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (b == all ? (int) m.dp (48) : -2, (int) m.dp (40)); lp.leftMargin = (int) m.dp (8); bot.addView (b, lp); }
		LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams (-1, -2); bl.topMargin = (int) m.dp (10); p.addView (bot, bl);
		def.setOnClickListener (v -> { VX.oset (i, VX.opt (i, 6)); show (m, i); });
		done.setOnClickListener (v -> close (m));
		all.setOnClickListener (v -> { close (m); Screens.settings (m); });
		prev.setOnClickListener (v -> step (m, i, -1)); next.setOnClickListener (v -> step (m, i, 1));
		FrameLayout.LayoutParams fl = new FrameLayout.LayoutParams (Math.min ((int) m.dp (560), m.root.getWidth () - (int) m.dp (20)), -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
		fl.bottomMargin = m.insBot + (int) m.dp (8);
		panel = p; m.root.addView (p, fl); p.setAlpha (0); p.animate ().alpha (1).setDuration (200);
	}
	/* HDR brightness: a slider over the running game - the picture changes as it moves (the owner, 2026-11-22; it was a list of five) */
	static void showHdr (Main m)
	{
		close (m); m.popAll (); opt = -1; Main.log ("adjust hdr");
		LinearLayout p = new LinearLayout (m); p.setOrientation (LinearLayout.VERTICAL); p.setPadding ((int) m.dp (14), (int) m.dp (12), (int) m.dp (14), (int) m.dp (14));
		p.setBackground (Main.round ((Main.BAR & 0x00ffffff) | 0xf2000000, Main.EDGE2, m.dp (22))); p.setClickable (true);
		TextView tc = m.text ("APP", 11, Main.DIM, true), tn = m.text ("HDR brightness", 17, Main.HI, true); tc.setGravity (Gravity.CENTER); tn.setGravity (Gravity.CENTER); p.addView (tc); p.addView (tn);
		final float f0 = m.prefs.getFloat ("app.hdr", 1f);
		final java.util.function.Function<Float, String> say = f -> f > 0.01f ? String.format (java.util.Locale.ROOT, "up to %.1fx white", 1 + (m.hdrRoom - 1) * f) : "Off";
		final TextView val = m.text (say.apply (f0), 20, Main.HI, true);
		final SeekBar sb = new SeekBar (m); sb.setMax (100); sb.setProgress (Math.round (f0 * 100));
		sb.setOnSeekBarChangeListener (new SeekBar.OnSeekBarChangeListener () {
			public void onProgressChanged (SeekBar s, int v, boolean user) { if (!user) return; float f = v / 100f; m.prefs.edit ().putFloat ("app.hdr", f).apply (); m.hdrChanged (); val.setText (say.apply (f)); }
			public void onStartTrackingTouch (SeekBar s) {}
			public void onStopTrackingTouch (SeekBar s) { Main.log ("HDR " + s.getProgress () + " %"); } });
		LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams (-1, (int) m.dp (44)); sl.topMargin = (int) m.dp (6); p.addView (sb, sl);
		LinearLayout bot = new LinearLayout (m); bot.setGravity (Gravity.CENTER_VERTICAL); bot.addView (val, new LinearLayout.LayoutParams (0, -2, 1));
		TextView all = Screens.button (m, "☰", false), def = Screens.button (m, "Default", false), done = Screens.button (m, "Done", true);
		for (TextView b : new TextView[] { all, def, done }) { LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (b == all ? (int) m.dp (48) : -2, (int) m.dp (40)); lp.leftMargin = (int) m.dp (8); bot.addView (b, lp); }
		LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams (-1, -2); bl.topMargin = (int) m.dp (10); p.addView (bot, bl);
		def.setOnClickListener (v -> { m.prefs.edit ().putFloat ("app.hdr", 1f).apply (); m.hdrChanged (); showHdr (m); });
		done.setOnClickListener (v -> close (m));
		all.setOnClickListener (v -> { close (m); Screens.settings (m); });
		FrameLayout.LayoutParams fl = new FrameLayout.LayoutParams (Math.min ((int) m.dp (560), m.root.getWidth () - (int) m.dp (20)), -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
		fl.bottomMargin = m.insBot + (int) m.dp (8);
		panel = p; m.root.addView (p, fl); p.setAlpha (0); p.animate ().alpha (1).setDuration (200);
	}
	/* (the next / previous number setting of the same group) */
	static void step (Main m, int i, int d)
	{
		final int n = VX.nopts ();
		for (int k = 1; k < n; k++) { int j = ((i + d * k) % n + n) % n; if (VX.opt (j, 2).equals (VX.opt (i, 2)) && slider (j)) { show (m, j); return; } }
	}
}
