/* Adjust.java - the one-slider panel over the running game, as the iOS app's VXAdjust: a number setting (4 or more
 * numbers among its values, off counted as 0) moved with a slider while the game goes on and shows it; its other values
 * (auto and the like) as buttons beside it; < > to the previous / next number setting of its group; a double tap on the
 * slider or Default: its default; the value orange when changed; the list button: all settings, at this one; Done.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.view.GestureDetector;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import java.util.ArrayList;

final class Adjust {
	static LinearLayout panel; static int opt = -1;

	/* (a value's number, as iOS's VNum: off is 0, an x before it left out - "1.5x", "x2", "100 %"; NaN: a word) */
	static double num (String s)
	{
		String t = s.toLowerCase (java.util.Locale.ROOT).trim (); if (t.startsWith ("off")) return 0;
		if (t.startsWith ("x")) t = t.substring (1);
		java.util.regex.Matcher mt = java.util.regex.Pattern.compile ("^[-+]?(\\d+\\.?\\d*|\\.\\d+)").matcher (t);
		return mt.find () ? Double.parseDouble (mt.group ()) : Double.NaN;
	}
	/* (the numbers, sorted - null when not a slider: fewer than 4, or the screen's rates) */
	static ArrayList<String> nums (int i, ArrayList<String> extra)
	{
		ArrayList<String> n = new ArrayList<> ();
		for (String v : VX.opt (i, 4).split ("\\|")) { if (v.contains (" Hz")) return null; if (!Double.isNaN (num (v))) n.add (v); else if (extra != null) extra.add (v); }
		if (n.size () < 4) return null;
		n.sort ((a, b) -> Double.compare (num (a), num (b))); return n;
	}
	static boolean slider (int i) { return nums (i, null) != null; }

	static void close (Main m) { if (panel != null) { m.root.removeView (panel); panel = null; opt = -1; Main.log ("adjust closed"); } }
	/* (the panel's frame: the name at the top between < >, the help line, the slider, the value and the buttons at the foot) */
	static LinearLayout frame (Main m, String cat, String name, String help, View prev, View next)
	{
		LinearLayout p = new LinearLayout (m); p.setOrientation (LinearLayout.VERTICAL); p.setPadding ((int) m.dp (14), (int) m.dp (12), (int) m.dp (14), (int) m.dp (14));
		p.setBackground (Main.round ((Main.BAR & 0x00ffffff) | 0xf2000000, Main.EDGE2, m.dp (22))); p.setClickable (true);
		LinearLayout top = new LinearLayout (m); top.setGravity (Gravity.CENTER_VERTICAL); top.setBaselineAligned (false);
		LinearLayout mid = new LinearLayout (m); mid.setOrientation (LinearLayout.VERTICAL); mid.setGravity (Gravity.CENTER_HORIZONTAL);
		TextView tc = m.text (cat.toUpperCase (java.util.Locale.ROOT), 11, Main.DIM, true), tn = m.text (name, 17, Main.HI, true);
		tn.setTypeface (android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD);
		tc.setGravity (Gravity.CENTER); tn.setGravity (Gravity.CENTER); mid.addView (tc); mid.addView (tn);
		if (prev != null) top.addView (prev, new LinearLayout.LayoutParams ((int) m.dp (44), (int) m.dp (44)));
		top.addView (mid, new LinearLayout.LayoutParams (0, -2, 1));
		if (next != null) top.addView (next, new LinearLayout.LayoutParams ((int) m.dp (44), (int) m.dp (44)));
		p.addView (top);
		TextView info = m.text (help, 12, Main.DIM, false); info.setGravity (Gravity.CENTER); info.setMaxLines (2);
		LinearLayout.LayoutParams il = new LinearLayout.LayoutParams (-1, -2); il.topMargin = (int) m.dp (8); p.addView (info, il);
		return p;
	}
	static LinearLayout foot (Main m, LinearLayout p, TextView val, Runnable def, Runnable done, Runnable all)
	{
		LinearLayout bot = new LinearLayout (m); bot.setGravity (Gravity.CENTER_VERTICAL); bot.setBaselineAligned (false);
		val.setTypeface (android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD); bot.addView (val, new LinearLayout.LayoutParams (0, -2, 1));
		TextView al = Screens.button (m, "☰", false), df = Screens.button (m, "Default", false), dn = Screens.button (m, "Done", true); al.setContentDescription ("All settings");
		for (TextView b : new TextView[] { al, df, dn }) { LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (b == al ? (int) m.dp (44) : b == dn ? (int) m.dp (80) : -2, (int) m.dp (40)); lp.leftMargin = (int) m.dp (8); bot.addView (b, lp); }
		df.setOnClickListener (v -> def.run ()); dn.setOnClickListener (v -> done.run ()); al.setOnClickListener (v -> all.run ());
		LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams (-1, -2); bl.topMargin = (int) m.dp (10); p.addView (bot, bl);
		return bot;
	}
	static void place (Main m, LinearLayout p)
	{
		FrameLayout.LayoutParams fl = new FrameLayout.LayoutParams (Math.min ((int) m.dp (560), m.root.getWidth () - (int) m.dp (20)), -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
		fl.bottomMargin = m.insBot + (int) m.dp (8);
		panel = p; m.root.addView (p, fl); p.setAlpha (0); p.animate ().alpha (1).setDuration (200);
	}
	/* (a double tap on the slider: the default - the slider moves on the taps as usual first) */
	static void doubleTap (Main m, SeekBar sb, Runnable def)
	{
		final GestureDetector g = new GestureDetector (m, new GestureDetector.SimpleOnGestureListener () {
			@Override public boolean onDoubleTap (MotionEvent e) { sb.performHapticFeedback (HapticFeedbackConstants.LONG_PRESS); sb.post (def); return true; } });
		sb.setOnTouchListener ((v, e) -> { g.onTouchEvent (e); return false; });
	}
	static void show (Main m, int i)
	{
		close (m); m.popAll (); opt = i;
		final ArrayList<String> extra = new ArrayList<> (); final ArrayList<String> n = nums (i, extra); if (n == null) return;
		Main.log ("adjust " + VX.opt (i, 0));
		String cat = ""; for (int c = 0; c < VX.ncats (); c++) if (VX.cat (c, 0).equals (VX.opt (i, 2))) cat = VX.cat (c, 1);
		TextView prev = Screens.button (m, "‹", false), next = Screens.button (m, "›", false); prev.setContentDescription ("Previous setting"); next.setContentDescription ("Next setting");
		LinearLayout p = frame (m, cat, VX.opt (i, 1), VX.opt (i, 3), prev, next);
		final TextView val = m.text (VX.opt (i, 5), 20, Main.HI, true);
		final ArrayList<TextView> exb = new ArrayList<> ();
		final Runnable paint = () -> { val.setTextColor (VX.opt (i, 5).equals (VX.opt (i, 6)) ? Main.HI : Main.CHG);   /* (changed: orange) */
			for (TextView b : exb) { final boolean on = VX.opt (i, 5).equals (b.getText ().toString ()); b.setTextColor (on ? Main.HI2 : Main.HI); b.setBackground (Screens.states (m, on ? Main.SEL : Main.BODY, Main.EDGE2, m.dp (10))); } };   /* (auto and the like: lit while chosen) */
		paint.run ();
		final SeekBar sb = Screens.seekBar (m); sb.setMax (n.size () - 1);
		{ int at = n.indexOf (VX.opt (i, 5));
		  if (at < 0) { at = n.indexOf (VX.opt (i, 6)); if (at < 0) at = n.indexOf ("1x"); if (at < 0) at = (n.size () - 1) / 2; sb.setAlpha (0.4f); }   /* (a word - auto: the thumb at the default's place, else 1x's, else the middle) */
		  sb.setProgress (at); }
		sb.setOnSeekBarChangeListener (new SeekBar.OnSeekBarChangeListener () {
			public void onProgressChanged (SeekBar s, int v, boolean user) { if (!user) return; String nv = n.get (v); if (nv.equals (VX.opt (i, 5))) return;
				VX.oset (i, nv); val.setText (nv); paint.run (); s.setAlpha (1); s.performHapticFeedback (HapticFeedbackConstants.CLOCK_TICK); }
			public void onStartTrackingTouch (SeekBar s) {}
			public void onStopTrackingTouch (SeekBar s) { Main.log ("adjust " + VX.opt (i, 0) + " = " + VX.opt (i, 5)); } });
		final Runnable def = () -> { Main.log ("adjust " + VX.opt (i, 0) + ": the default"); VX.oset (i, VX.opt (i, 6)); show (m, i); };
		doubleTap (m, sb, def);
		LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams (-1, (int) m.dp (44)); sl.topMargin = (int) m.dp (6); p.addView (sb, sl);
		if (!extra.isEmpty ()) {                                   /* (auto and the like: buttons) */
			LinearLayout ex = new LinearLayout (m);
			for (String e : extra) { TextView b = Screens.button (m, e, VX.opt (i, 5).equals (e)); b.setTextSize (13); exb.add (b);
				b.setOnClickListener (v -> { VX.oset (i, e); show (m, i); });
				LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, (int) m.dp (36), 1); lp.leftMargin = lp.rightMargin = (int) m.dp (4); ex.addView (b, lp); }
			LinearLayout.LayoutParams el = new LinearLayout.LayoutParams (-1, -2); el.topMargin = (int) m.dp (6); p.addView (ex, el); }
		foot (m, p, val, def, () -> close (m), () -> { close (m); Screens.settings (m, i); });   /* (the list: all settings, scrolled to this one) */
		prev.setOnClickListener (v -> step (m, i, -1)); next.setOnClickListener (v -> step (m, i, 1));
		place (m, p);
	}
	/* HDR brightness: a slider over the running game - the picture changes as it moves (the owner, 2026-11-22; it was a list of five) */
	static void showHdr (Main m)
	{
		close (m); m.popAll (); opt = -1; Main.log ("adjust hdr");
		LinearLayout p = frame (m, "App", "HDR brightness", "The brightest lines and dots above the screen's white.", null, null);
		final float f0 = m.prefs.getFloat ("app.hdr", 1f);
		final java.util.function.Function<Float, String> say = f -> f > 0.01f ? String.format (java.util.Locale.ROOT, "up to %.1fx white", 1 + (m.hdrRoom - 1) * f) : "Off";
		final TextView val = m.text (say.apply (f0), 20, f0 < 0.995f ? Main.CHG : Main.HI, true);
		final SeekBar sb = Screens.seekBar (m); sb.setMax (100); sb.setProgress (Math.round (f0 * 100));
		sb.setOnSeekBarChangeListener (new SeekBar.OnSeekBarChangeListener () {
			public void onProgressChanged (SeekBar s, int v, boolean user) { if (!user) return; float f = v / 100f; m.prefs.edit ().putFloat ("app.hdr", f).apply (); m.hdrChanged (); val.setText (say.apply (f)); val.setTextColor (f < 0.995f ? Main.CHG : Main.HI); }
			public void onStartTrackingTouch (SeekBar s) {}
			public void onStopTrackingTouch (SeekBar s) { Main.log ("HDR " + s.getProgress () + " %"); } });
		final Runnable def = () -> { m.prefs.edit ().putFloat ("app.hdr", 1f).apply (); m.hdrChanged (); showHdr (m); };
		doubleTap (m, sb, def);
		LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams (-1, (int) m.dp (44)); sl.topMargin = (int) m.dp (6); p.addView (sb, sl);
		foot (m, p, val, def, () -> close (m), () -> { close (m); Screens.settings (m); });
		place (m, p);
	}
	/* (the next / previous number setting of the same group) */
	static void step (Main m, int i, int d)
	{
		final int n = VX.nopts ();
		for (int k = 1; k < n; k++) { int j = ((i + d * k) % n + n) % n; if (VX.opt (j, 2).equals (VX.opt (i, 2)) && slider (j)) { show (m, j); return; } }
	}
}
