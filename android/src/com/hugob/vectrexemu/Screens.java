/* Screens.java - the screens over the game, as the iOS app's (ios/App/VXUI.m): the game menu (save states, the games,
 * RESET, the overlay, the room light, all settings, online, the Vectrex's hardware), the settings (the app's own and every
 * core setting in its group, in the iOS app's order, each value with its help), a setting's values, the controller's buttons. Plain views,
 * the PC app's colours; every row reachable with a controller (the highlight), button 4 picks, button 1 goes back.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.text.InputType;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.io.File;

final class Screens {
	interface Closing { void closed (); }
	/* a screen: a header (its title, a close button) and a scrolled column; the content no wider than a tablet's sheet.
	 * SHEET: from below, the paused game above it, a tap there closes it (the game menu - the iOS app's sheet); BARE: no
	 * header (the first start's Ready to play) */
	static final int FULL = 0, SHEET = 1, BARE = 2;
	static class Panel extends LinearLayout implements Closing {
		final LinearLayout col; final ScrollView sc; Runnable onClose; final Main m;
		boolean live;                                  /* (the game goes on under it - the chat line, as iOS's: no pause for either player) */
		boolean settings;                              /* (the settings list: a value chosen in a list over it builds it again) */
		Runnable refresh;                              /* (a value chosen over it: what it shows again) */
		Panel (Main m, String title) { this (m, title, FULL); }
		Panel (Main m, String title, int kind)
		{
			super (m); this.m = m; setOrientation (VERTICAL); setClickable (true);
			setBackgroundColor (kind == SHEET ? 0x8c000000 : Main.BG);
			if (kind == SHEET) { View gap = new View (m); gap.setOnClickListener (v -> m.pop ()); addView (gap, new LinearLayout.LayoutParams (-1, 0, 1)); }
			if (kind == FULL) {
				LinearLayout head = new LinearLayout (m); head.setOrientation (HORIZONTAL); head.setGravity (Gravity.CENTER_VERTICAL); head.setBackgroundColor (Main.BAR); head.setBaselineAligned (false);
				head.setPadding ((int) m.dp (16) + m.insL, (int) m.dp (8) + m.insTop, (int) m.dp (8) + m.insR, (int) m.dp (8));
				TextView t = m.text (title, 22, Main.HI, true); head.addView (t, new LinearLayout.LayoutParams (0, -2, 1));
				TextView x = button (m, "✕", false); x.setContentDescription ("Close"); x.setOnClickListener (v -> m.pop ()); head.addView (x, new LinearLayout.LayoutParams ((int) m.dp (48), (int) m.dp (44)));
				addView (head, new LinearLayout.LayoutParams (-1, -2)); }
			sc = new ScrollView (m); sc.setFillViewport (kind != SHEET);
			final int maxW = (int) m.dp (640);
			FrameLayout holder = new FrameLayout (m) {   /* (the column: the screen's width, at most a tablet's sheet - measured each time, so a
				                                           turn from landscape to portrait narrows it; it kept the width it opened with and cut the text) */
				@Override protected void onMeasure (int w, int hm) {
					int ww = MeasureSpec.getSize (w); col.getLayoutParams ().width = Math.min (ww, maxW); super.onMeasure (w, hm); } };
			col = new LinearLayout (m); col.setOrientation (VERTICAL);
			col.setPadding ((int) m.dp (16) + m.insL, (int) m.dp (kind == SHEET ? 4 : kind == BARE ? 24 : 8) + (kind == BARE ? m.insTop : 0), (int) m.dp (16) + m.insR, (int) m.dp (24) + m.insBot);
			holder.addView (col, new FrameLayout.LayoutParams (Math.min (m.root.getWidth (), maxW), -2, Gravity.CENTER_HORIZONTAL));
			sc.addView (holder);
			if (kind == SHEET) {                       /* (the sheet: rounded at the top, its grabber, as tall as its content) */
				LinearLayout box = new LinearLayout (m); box.setOrientation (VERTICAL); box.setClickable (true);
				GradientDrawable bg = new GradientDrawable (); bg.setColor (Main.BAR); final float r = m.dp (22); bg.setCornerRadii (new float[] { r, r, r, r, 0, 0, 0, 0 }); box.setBackground (bg);
				View grab = new View (m); grab.setBackground (Main.round (Main.EDGE2, 0, m.dp (3)));
				LinearLayout.LayoutParams gl = new LinearLayout.LayoutParams ((int) m.dp (36), (int) m.dp (5)); gl.gravity = Gravity.CENTER_HORIZONTAL; gl.topMargin = (int) m.dp (6); gl.bottomMargin = (int) m.dp (14);
				box.addView (grab, gl); box.addView (sc, new LinearLayout.LayoutParams (-1, -2));
				addView (box, new LinearLayout.LayoutParams (-1, -2)); }
			else addView (sc, new LinearLayout.LayoutParams (-1, 0, 1));
		}
		@Override public void closed () { if (onClose != null) onClose.run (); }
		TextView header (String s) { TextView t = m.text (s.toUpperCase (), 12, Main.DIM, true); t.setPadding ((int) m.dp (4), (int) m.dp (18), 0, (int) m.dp (6)); col.addView (t); return t; }
		TextView foot (String s) { TextView t = m.text (s, 12, Main.DIM, false); t.setPadding ((int) m.dp (4), (int) m.dp (6), 0, (int) m.dp (4)); col.addView (t); return t; }
		TextView body (String s) { TextView t = m.text (s, 15, Main.FG2, false); t.setPadding ((int) m.dp (4), (int) m.dp (2), 0, (int) m.dp (12)); col.addView (t); return t; }   /* (a prompt's text, as the iOS app's) */
		EditText field (String hint, boolean caps)   /* (a line to type, as the iOS app's prompt) */
		{
			EditText e = new EditText (m); e.setHint (hint); e.setSingleLine (true); e.setTextColor (Main.FG); e.setHintTextColor (Main.DIM); e.setTextSize (17);
			if (caps) e.setInputType (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
			e.setBackground (Main.round (Main.PANEL, Main.EDGE2, m.dp (12))); e.setPadding ((int) m.dp (14), 0, (int) m.dp (14), 0);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-1, (int) m.dp (50)); lp.bottomMargin = (int) m.dp (14); col.addView (e, lp); return e;
		}
		/* (a view in the column, h high - -2 its own -, top dp above it) */
		View add (View v, int h, int top) { LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-1, h > 0 ? (int) m.dp (h) : h); lp.topMargin = (int) m.dp (top); col.addView (v, lp); return v; }
		LinearLayout put (LinearLayout r) { LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-1, -2); lp.bottomMargin = (int) m.dp (6); col.addView (r, lp); return r; }
		LinearLayout row (String title, String detail, Runnable go) { return row (title, detail, go, null); }
		LinearLayout toggle (String title, String note, boolean on, Runnable go) { return put (Screens.toggleRow (m, title, note, on, go)); }
		LinearLayout row (String title, String detail, Runnable go, Boolean tog) { return put (Screens.row (m, title, detail, go, tog)); }
	}
	static StateListDrawable states (Main m, int fill, int edge, float r)
	{
		StateListDrawable s = new StateListDrawable ();
		s.addState (new int[] { android.R.attr.state_pressed }, Main.round (Main.SEL, Main.EDGE2, r));
		s.addState (new int[] { android.R.attr.state_focused }, focus (m, r));
		s.addState (new int[] {}, Main.round (fill, edge, r)); return s;
	}
	static GradientDrawable ring (Main m, float r) { GradientDrawable g = new GradientDrawable (); g.setColor (0); g.setCornerRadius (r); g.setStroke ((int) m.dp (3), Main.HI2); return g; }
	static GradientDrawable focus (Main m, float r) { GradientDrawable g = new GradientDrawable (); g.setColor (Main.SEL); g.setCornerRadius (r); g.setStroke ((int) m.dp (2.5f), Main.HI2); return g; }
	static LinearLayout row (Main m, String title, String detail, Runnable go) { return row (m, title, detail, go, null); }
	/* tog: a switch at the row's right (its tap turns it) - null: an app row whose detail is On / Off. A core setting says it
	 * itself: only one of exactly on and off is a switch; one with more values (auto, strong, a button...) shows its value
	 * and opens its choice - its "on" or "off" showed a switch that opened a menu (the owner, 2026-11-25) */
	static LinearLayout row (Main m, String title, String detail, Runnable go, Boolean togArg)
	{
		LinearLayout r = new LinearLayout (m); r.setOrientation (LinearLayout.VERTICAL); r.setPadding ((int) m.dp (14), (int) m.dp (11), (int) m.dp (14), (int) m.dp (11));
		r.setBackground (states (m, Main.PANEL, Main.EDGE, m.dp (10))); r.setBaselineAligned (false);
		TextView t = m.text (title, 16, go != null ? Main.HI : Main.FG, go != null); t.setTag ("t"); r.addView (t);
		final boolean tog = go != null && detail != null && (detail.equalsIgnoreCase ("on") || detail.equalsIgnoreCase ("off")) && (togArg == null || togArg);
		if (tog) {                                 /* (an on / off row: a switch at its right, as the iOS app's - the owner, 2026-11-22; the row's tap turns it) */
			r.setOrientation (LinearLayout.HORIZONTAL); r.setGravity (Gravity.CENTER_VERTICAL);
			t.setLayoutParams (new LinearLayout.LayoutParams (0, -2, 1f));
			TextView d = m.text (detail, 13, Main.DIM, false); d.setTag ("d"); d.setVisibility (View.GONE); r.addView (d);
			r.addView (new Toggle (m, detail.equalsIgnoreCase ("on")), new LinearLayout.LayoutParams ((int) m.dp (46), (int) m.dp (26))); }
		else if (detail != null && !detail.isEmpty ()) { TextView d = m.text (detail, 13, Main.DIM, false); d.setTag ("d"); r.addView (d); }
		if (go != null) { r.setFocusable (true); r.setClickable (true); r.setOnClickListener (v -> { Main.log ("ui " + title); go.run (); }); }
		return r;
	}
	/* (a row's title and its small note below it, the column at the row's left) */
	static LinearLayout texts (Main m, LinearLayout r, String title, String note, int col)
	{
		LinearLayout txt = new LinearLayout (m); txt.setOrientation (LinearLayout.VERTICAL);
		TextView t = m.text (title, 16, col, true); t.setTag ("t"); txt.addView (t);
		if (note != null && !note.isEmpty ()) { TextView d = m.text (note, 13, Main.DIM, false); d.setTag ("d"); txt.addView (d); }
		LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams (0, -2, 1f); tl.rightMargin = (int) m.dp (10); r.addView (txt, tl); return txt;
	}
	static LinearLayout hrow (Main m)
	{
		LinearLayout r = new LinearLayout (m); r.setOrientation (LinearLayout.HORIZONTAL); r.setGravity (Gravity.CENTER_VERTICAL); r.setBaselineAligned (false);
		r.setPadding ((int) m.dp (14), (int) m.dp (11), (int) m.dp (14), (int) m.dp (11)); r.setBackground (states (m, Main.PANEL, Main.EDGE, m.dp (10))); return r;
	}
	/* a setting of two choices as a switch (the owner, 2026-11-25: no menu to open for two choices): its name, a small
	 * note below saying what it does now, the switch at the right; a tap turns it */
	static LinearLayout toggleRow (Main m, String title, String note, boolean on, Runnable go)
	{
		LinearLayout r = hrow (m); texts (m, r, title, note, Main.HI);
		r.addView (new Toggle (m, on), new LinearLayout.LayoutParams ((int) m.dp (46), (int) m.dp (26)));
		r.setFocusable (true); r.setClickable (true); r.setOnClickListener (v -> { Main.log ("ui " + title); go.run (); });
		return r;
	}
	/* (a switch that can not be turned now - Deck pop-up with the deck on Auto or off: shown, greyed, as iOS) */
	static void greyed (LinearLayout r)
	{
		r.setClickable (false); r.setFocusable (false); r.setOnClickListener (null);
		View t = r.findViewWithTag ("t"); if (t instanceof TextView) ((TextView) t).setTextColor (Main.DIM);
		for (int i = 0; i < r.getChildCount (); i++) if (r.getChildAt (i) instanceof Toggle) r.getChildAt (i).setAlpha (0.4f);
	}
	/* the switch of an on / off row: a pill, its knob right and the pill green when on */
	static class Toggle extends View {
		final boolean on; final android.graphics.Paint pt = new android.graphics.Paint (android.graphics.Paint.ANTI_ALIAS_FLAG);
		Toggle (Main m, boolean on) { super (m); this.on = on; }
		@Override protected void onDraw (android.graphics.Canvas c) {
			float w = getWidth (), h = getHeight (), r = h / 2f;
			pt.setColor (on ? Main.G : Main.EDGE2); c.drawRoundRect (0, 0, w, h, r, r, pt);
			pt.setColor (on ? Main.HI2 : Main.FG2); c.drawCircle (on ? w - r : r, r, r - h * 0.12f, pt); }
	}
	/* a green tick at a row's right: the download page's items that are all here (the owner, 2026-11-26) */
	static final int TICK = 0xff3ccf5a;
	static class Tick extends View {
		final android.graphics.Paint pt = new android.graphics.Paint (android.graphics.Paint.ANTI_ALIAS_FLAG);
		final android.graphics.Path pa = new android.graphics.Path ();
		Tick (Main m) { super (m); pt.setColor (TICK); pt.setStyle (android.graphics.Paint.Style.STROKE); pt.setStrokeCap (android.graphics.Paint.Cap.ROUND); pt.setStrokeJoin (android.graphics.Paint.Join.ROUND); }
		@Override protected void onDraw (android.graphics.Canvas c) {
			float w = getWidth (), h = getHeight (); pt.setStrokeWidth (h * 0.14f);
			pa.reset (); pa.moveTo (w * 0.14f, h * 0.54f); pa.lineTo (w * 0.40f, h * 0.80f); pa.lineTo (w * 0.88f, h * 0.22f); c.drawPath (pa, pt); }
	}
	/* a row of the download page with a tick at its right (hidden until the item is all here) */
	static Tick withTick (Main m, LinearLayout r)
	{
		LinearLayout txt = new LinearLayout (m); txt.setOrientation (LinearLayout.VERTICAL);
		while (r.getChildCount () > 0) { View v = r.getChildAt (0); r.removeViewAt (0); txt.addView (v); }
		r.setOrientation (LinearLayout.HORIZONTAL); r.setGravity (Gravity.CENTER_VERTICAL);
		LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams (0, -2, 1f); tl.rightMargin = (int) m.dp (10); r.addView (txt, tl);
		Tick k = new Tick (m); r.addView (k, new LinearLayout.LayoutParams ((int) m.dp (26), (int) m.dp (26))); return k;
	}
	/* choices side by side inside a row, as iOS's segmented control: a tap picks one; with a controller the row's left /
	 * right steps them, button 4 the next */
	interface Sel { void go (int k); }
	static class Seg extends LinearLayout {
		final TextView[] t; int cur; final Sel go; final Main m;
		Seg (Main m, String[] items, int sel, Sel go)
		{
			super (m); this.m = m; this.go = go; cur = sel; t = new TextView[items.length]; setOrientation (HORIZONTAL); setBaselineAligned (false);
			setBackground (Main.round (Main.BODY, Main.EDGE2, m.dp (9))); final int p = (int) m.dp (2); setPadding (p, p, p, p);
			for (int i = 0; i < items.length; i++) { final int k = i; TextView x = m.text (items[i], 13, Main.FG2, true); x.setGravity (Gravity.CENTER); x.setSingleLine (true);
				x.setPadding ((int) m.dp (10), (int) m.dp (6), (int) m.dp (10), (int) m.dp (6)); x.setOnClickListener (v -> pick (k)); t[i] = x;
				addView (x, new LinearLayout.LayoutParams (0, -1, 1)); }
			paint ();
		}
		@Override protected void onMeasure (int w, int h)   /* (the row's full width - a narrow screen: each word its own width, smaller to fit, never cut) */
		{
			float sp = 13, pad = 10;
			if (MeasureSpec.getMode (w) == MeasureSpec.EXACTLY) {
				final Paint p = new Paint (); p.setTypeface (Typeface.DEFAULT_BOLD); p.setTextSize (m.dp (13) * getResources ().getConfiguration ().fontScale);
				float tw = 0; for (TextView x : t) tw += p.measureText (x.getText ().toString ());
				final float avail = MeasureSpec.getSize (w) - getPaddingLeft () - getPaddingRight ();
				if (tw + t.length * m.dp (20) > avail) { pad = 4; sp = Math.max (8, 13 * Math.min (1, (avail - t.length * m.dp (8) - m.dp (2)) / tw)); }
				for (TextView x : t) { LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) x.getLayoutParams (); lp.width = 0; lp.weight = p.measureText (x.getText ().toString ()) + m.dp (2 * pad); } }
			else for (TextView x : t) { LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) x.getLayoutParams (); lp.width = -2; lp.weight = 0; }   /* (beside the title: each word its own width) */
			for (TextView x : t) { if (Math.abs (x.getTextSize () - m.dp (sp) * getResources ().getConfiguration ().fontScale) > 0.5f) x.setTextSize (sp);
				final int pp = (int) m.dp (pad); if (x.getPaddingLeft () != pp) x.setPadding (pp, x.getPaddingTop (), pp, x.getPaddingBottom ()); }
			super.onMeasure (w, h);
		}
		void paint () { for (int i = 0; i < t.length; i++) { boolean on = i == cur; t[i].setTextColor (on ? Main.HI2 : Main.FG2); t[i].setBackground (on ? Main.round (Main.SEL, Main.EDGE2, m.dp (7)) : null); } }
		void pick (int k) { if (k < 0 || k >= t.length || k == cur) return; cur = k; paint (); performHapticFeedback (HapticFeedbackConstants.CLOCK_TICK); go.go (k); }
	}
	/* (a row: its title - and note - at the left, a control at the right; when both do not fit side by side the control goes
	 *  below the title, the row's full width) */
	static LinearLayout ctlRow (Main m, String title, String note, int col, View ctl)
	{
		final LinearLayout[] txt = new LinearLayout[1];
		LinearLayout r = new LinearLayout (m) {
			@Override protected void onMeasure (int w, int h) {
				final int avail = MeasureSpec.getSize (w) - getPaddingLeft () - getPaddingRight ();
				ctl.measure (MeasureSpec.makeMeasureSpec (0, MeasureSpec.UNSPECIFIED), MeasureSpec.makeMeasureSpec (0, MeasureSpec.UNSPECIFIED));
				View t = txt[0].findViewWithTag ("t"); t.measure (MeasureSpec.makeMeasureSpec (0, MeasureSpec.UNSPECIFIED), MeasureSpec.makeMeasureSpec (0, MeasureSpec.UNSPECIFIED));
				final int cw = ctl instanceof SeekBar ? (int) m.dp (170) : ctl.getMeasuredWidth ();
				final boolean below = MeasureSpec.getMode (w) != MeasureSpec.UNSPECIFIED && cw + Math.min (t.getMeasuredWidth (), (int) m.dp (110)) + (int) m.dp (10) > avail;
				LinearLayout.LayoutParams tl = (LinearLayout.LayoutParams) txt[0].getLayoutParams (), cl = (LinearLayout.LayoutParams) ctl.getLayoutParams ();
				if (below) { setOrientation (VERTICAL); setGravity (Gravity.START); tl.width = -1; tl.weight = 0; tl.rightMargin = 0; cl.topMargin = (int) m.dp (8); cl.gravity = Gravity.START; cl.width = -1; }   /* (the control: the row's width) */
				else { setOrientation (HORIZONTAL); setGravity (Gravity.CENTER_VERTICAL); tl.width = 0; tl.weight = 1; tl.rightMargin = (int) m.dp (10); cl.topMargin = 0; cl.gravity = Gravity.CENTER_VERTICAL; cl.width = ctl instanceof SeekBar ? (int) m.dp (170) : -2; }
				super.onMeasure (w, h); } };
		r.setOrientation (LinearLayout.HORIZONTAL); r.setGravity (Gravity.CENTER_VERTICAL); r.setBaselineAligned (false);
		r.setPadding ((int) m.dp (14), (int) m.dp (11), (int) m.dp (14), (int) m.dp (11)); r.setBackground (states (m, Main.PANEL, Main.EDGE, m.dp (10)));
		txt[0] = texts (m, r, title, note, col); r.addView (ctl, new LinearLayout.LayoutParams (-2, -2));
		return r;
	}
	/* a setting of a few choices as segments inside its row (Top bar, Control deck, Show, Directions - as iOS) */
	static LinearLayout segRow (Panel p, String title, String note, String[] items, int sel, Sel go)
	{
		final Main m = p.m; final Seg s = new Seg (m, items, sel, k -> { Main.log ("ui " + title + ": " + items[k]); go.go (k); });
		LinearLayout r = ctlRow (m, title, note, Main.HI, s);
		r.setFocusable (true); r.setClickable (true); r.setOnClickListener (v -> s.pick ((s.cur + 1) % items.length));
		r.setOnKeyListener ((v, code, e) -> { if (code != KeyEvent.KEYCODE_DPAD_LEFT && code != KeyEvent.KEYCODE_DPAD_RIGHT) return false;
			if (e.getAction () == KeyEvent.ACTION_DOWN) s.pick (s.cur + (code == KeyEvent.KEYCODE_DPAD_LEFT ? -1 : 1)); return true; });
		return p.put (r);
	}
	/* a value moved continuously (the touch controls' size and opacity, as iOS's slider in the row) */
	interface Slid { void go (float v); }
	static SeekBar seekBar (Main m)
	{
		SeekBar sb = new SeekBar (m); sb.setProgressTintList (android.content.res.ColorStateList.valueOf (Main.G)); sb.setThumbTintList (android.content.res.ColorStateList.valueOf (Main.HI));
		sb.setProgressBackgroundTintList (android.content.res.ColorStateList.valueOf (Main.EDGE2)); return sb;
	}
	static LinearLayout sliderRow (Panel p, String title, float min, float max, float cur, Slid go)
	{
		final Main m = p.m; final SeekBar sb = seekBar (m); sb.setMax (1000); sb.setFocusable (false);
		sb.setProgress (Math.round ((cur - min) / (max - min) * 1000));
		sb.setOnSeekBarChangeListener (new SeekBar.OnSeekBarChangeListener () {
			public void onProgressChanged (SeekBar s, int v, boolean user) { go.go (min + (max - min) * v / 1000f); }
			public void onStartTrackingTouch (SeekBar s) {}
			public void onStopTrackingTouch (SeekBar s) { Main.log ("ui " + title + " " + String.format (java.util.Locale.ROOT, "%.2f", min + (max - min) * s.getProgress () / 1000f)); } });
		sb.setLayoutParams (new LinearLayout.LayoutParams ((int) m.dp (170), (int) m.dp (36)));
		LinearLayout r = ctlRow (m, title, null, Main.HI, sb); ((LinearLayout.LayoutParams) sb.getLayoutParams ()).width = (int) m.dp (170);
		r.setFocusable (true); r.setOnKeyListener ((v, code, e) -> { if (code != KeyEvent.KEYCODE_DPAD_LEFT && code != KeyEvent.KEYCODE_DPAD_RIGHT) return false;
			if (e.getAction () == KeyEvent.ACTION_DOWN) { sb.setProgress (Math.max (0, Math.min (1000, sb.getProgress () + (code == KeyEvent.KEYCODE_DPAD_LEFT ? -50 : 50)))); go.go (min + (max - min) * sb.getProgress () / 1000f); } return true; });
		return p.put (r);
	}
	/* a pull-down at the row's right (Max brightness, as iOS's): the choice and a chevron, its menu on a tap */
	static LinearLayout pullRow (Panel p, String title, String note, String[] items, int sel, int col, Sel go)
	{
		final Main m = p.m; final TextView b = m.text (items[sel] + "  ▾", 15, col, true); b.setPadding ((int) m.dp (8), (int) m.dp (6), (int) m.dp (2), (int) m.dp (6));
		LinearLayout r = ctlRow (m, title, note, Main.HI, b);
		final Runnable open = () -> drop (m, b, items, sel, k -> { Main.log ("ui " + title + ": " + items[k]); go.go (k); });
		b.setOnClickListener (v -> open.run ()); r.setFocusable (true); r.setClickable (true); r.setOnClickListener (v -> open.run ());
		return p.put (r);
	}
	/* (the pull-down's menu: the app's own little list by its button) */
	static void drop (Main m, View anchor, String[] items, int sel, Sel go)
	{
		LinearLayout l = new LinearLayout (m); l.setOrientation (LinearLayout.VERTICAL); l.setBackground (Main.round (Main.BODY, Main.EDGE2, m.dp (12)));
		final int pd = (int) m.dp (4); l.setPadding (pd, pd, pd, pd);
		final PopupWindow pw = new PopupWindow (l, -2, -2, true); pw.setBackgroundDrawable (new ColorDrawable (0)); pw.setOutsideTouchable (true); pw.setElevation (m.dp (8));
		View first = null;
		for (int i = 0; i < items.length; i++) { final int k = i;
			TextView t = m.text ((i == sel ? "✓  " : "     ") + items[i], 16, i == sel ? Main.HI2 : Main.FG, i == sel);
			t.setPadding ((int) m.dp (14), (int) m.dp (11), (int) m.dp (22), (int) m.dp (11)); t.setBackground (states (m, 0, 0, m.dp (8)));
			t.setFocusable (true); t.setClickable (true); t.setOnClickListener (v -> { pw.dismiss (); if (k != sel) go.go (k); });
			l.addView (t, new LinearLayout.LayoutParams (-1, -2)); if (i == sel) first = t; }
		/* (below the button where it fits, else above it; its right edge at the button's) */
		l.measure (View.MeasureSpec.makeMeasureSpec (0, View.MeasureSpec.UNSPECIFIED), View.MeasureSpec.makeMeasureSpec (0, View.MeasureSpec.UNSPECIFIED));
		final int[] at = new int[2]; anchor.getLocationInWindow (at); final int w = l.getMeasuredWidth (), h = l.getMeasuredHeight (), gap = (int) m.dp (2);
		final int x = Math.max (0, at[0] + anchor.getWidth () - w), y = at[1] + anchor.getHeight () + gap + h <= m.root.getHeight () ? at[1] + anchor.getHeight () + gap : Math.max (0, at[1] - gap - h);
		pw.showAtLocation (anchor, Gravity.TOP | Gravity.START, x, y); if (first != null) first.requestFocus ();
	}
	static TextView button (Main m, String s, boolean on)
	{
		TextView b = m.text (s, 15, on ? Main.HI2 : Main.HI, true); b.setGravity (Gravity.CENTER); b.setPadding ((int) m.dp (10), (int) m.dp (8), (int) m.dp (10), (int) m.dp (8));
		b.setBackground (states (m, on ? Main.SEL : Main.BODY, Main.EDGE2, m.dp (10))); b.setFocusable (true); b.setClickable (true); return b;
	}
	/* a button of the game menu: its icon above its word, as the iOS app's (and the quick bar's) - the deck's icons, and two
	 * of its own (20 the settings' sliders, 21 a download) */
	static class IconBtn extends View {
		final Main m; final int ic; final String w; final boolean on; final Paint tp = new Paint (Paint.ANTI_ALIAS_FLAG), ln = new Paint (Paint.ANTI_ALIAS_FLAG);
		IconBtn (Main m, String w, int ic, boolean on)
		{
			super (m); this.m = m; this.w = w; this.ic = ic; this.on = on; setContentDescription (w); setFocusable (true); setClickable (true);
			setBackground (states (m, on ? Main.SEL : Main.PANEL, on ? Main.EDGE2 : Main.EDGE, m.dp (12)));
			ln.setStyle (Paint.Style.STROKE); ln.setStrokeCap (Paint.Cap.ROUND);
		}
		@Override public void setEnabled (boolean e) { super.setEnabled (e); setAlpha (e ? 1 : 0.4f); setFocusable (e); }
		@Override protected void onDraw (Canvas c)
		{
			final float d = m.dp (1), cx = getWidth () / 2f, cy = getHeight () / 2f - 7 * d, s = 15 * d, h = s / 2; final int col = on ? Main.HI2 : Main.HI;
			if (ic < 20) { if (m.deck != null) m.deck.icon (c, ic, cx, cy, s, col); }
			else { ln.setColor (col); ln.setStrokeWidth (s * 0.11f); tp.setColor (col);
				if (ic == 20) { final float[] kx = { -0.35f, 0.4f, -0.1f };      /* (the settings: three sliders) */
					for (int i = -1; i <= 1; i++) { c.drawLine (cx - h * 0.9f, cy + i * h * 0.6f, cx + h * 0.9f, cy + i * h * 0.6f, ln); c.drawCircle (cx + kx[i + 1] * h, cy + i * h * 0.6f, s * 0.1f, tp); } }
				else { c.drawCircle (cx, cy, h * 0.92f, ln); c.drawLine (cx, cy - h * 0.45f, cx, cy + h * 0.4f, ln);   /* (a download: a circle with its arrow down) */
					c.drawLine (cx - h * 0.35f, cy + h * 0.05f, cx, cy + h * 0.4f, ln); c.drawLine (cx + h * 0.35f, cy + h * 0.05f, cx, cy + h * 0.4f, ln); } }
			tp.setColor (col); tp.setTypeface (Typeface.DEFAULT_BOLD); tp.setTextAlign (Paint.Align.CENTER); tp.setTextSize (10.5f * d);
			final float tw = tp.measureText (w), room = getWidth () - 8 * d; if (tw > room) tp.setTextSize (10.5f * d * room / tw);
			c.drawText (w, cx, cy + h + 15 * d, tp);
		}
	}
	static void back (Main m) { m.pop (); }

	/* the app's own prompt, as the iOS app's (vxa_prompt): a title, a header, the text, the choices as rows (a title, a small
	 * note, what it does - null: a line only, greyed), a foot; a choice closes it first */
	static Panel prompt (Main m, String title, String head, String body, Object[][] choices, String foot)
	{
		Panel p = new Panel (m, title); if (head != null) p.header (head); if (body != null) p.body (body); View first = null;
		for (Object[] c : choices) { final Runnable go = (Runnable) c[2];
			LinearLayout r = p.row ((String) c[0], (String) c[1], go == null ? null : () -> { m.pop (); go.run (); });
			if (go == null) ((TextView) r.findViewWithTag ("t")).setTextColor (Main.DIM); else if (first == null) first = r; }
		if (foot != null) p.foot (foot);
		m.push (p); if (first != null) first.requestFocus (); return p;
	}
	static void note (Main m, String title, String text) { prompt (m, title, null, text, new Object[][] { { "OK", "", (Runnable) () -> { } } }, null); }

	/* ---------------------------------------------------------------- the game menu */
	/* (as the iOS app's sheet: the game's name and "Paused" at the left, a small Resume at the right; the most used first -
	 *  save / load, the games and RESET, the picture's two quick ones, all settings, online, the downloads; the Vectrex's
	 *  own hardware - eject, port 2 - small at the foot) */
	static void gameMenu (Main m)
	{
		if (!m.stack.isEmpty () || !m.started) return; Main.log ("ui game menu opened");
		String g = new File (VX.game ()).getName (); int dot = g.lastIndexOf ('.'); if (dot > 0) g = g.substring (0, dot);
		final Panel p = new Panel (m, g.isEmpty () ? "Vectrex" : g, SHEET);
		LinearLayout head = new LinearLayout (m); head.setOrientation (LinearLayout.HORIZONTAL); head.setGravity (Gravity.CENTER_VERTICAL); head.setBaselineAligned (false);
		LinearLayout tt = new LinearLayout (m); tt.setOrientation (LinearLayout.VERTICAL);
		TextView t = m.text (g.isEmpty () ? "Vectrex" : g, 18, Main.HI, true); t.setTypeface (Typeface.MONOSPACE, Typeface.BOLD); tt.addView (t);
		TextView sub = m.text ("Paused while this menu is open", 12, Main.DIM, false); sub.setPadding (0, (int) m.dp (2), 0, 0); tt.addView (sub);
		head.addView (tt, new LinearLayout.LayoutParams (0, -2, 1));
		TextView resume = button (m, "Resume", true); resume.setOnClickListener (v -> m.pop ());
		LinearLayout.LayoutParams rl = new LinearLayout.LayoutParams ((int) m.dp (100), (int) m.dp (44)); rl.leftMargin = (int) m.dp (12); head.addView (resume, rl);
		p.add (head, -2, 0);
		p.add (m.text ("Save states", 15, Main.FG, true), -2, 14);
		final boolean can = VX.stateSupported () != 0 && !Online.playing;
		LinearLayout slots = new LinearLayout (m); slots.setOrientation (LinearLayout.HORIZONTAL);
		for (int s = 1; s <= 3; s++) { final int k = s; LinearLayout c = new LinearLayout (m); c.setOrientation (LinearLayout.VERTICAL);
			IconBtn sv = new IconBtn (m, "Save " + s, 11, false), ld = new IconBtn (m, "Load " + s, 12, false);
			sv.setOnClickListener (v -> { Main.log ("ui save " + k); int ok = VX.state (true, k); m.pop (); m.toast (ok != 0 ? "Saved in slot " + k : "Not saved", 2); });
			ld.setOnClickListener (v -> { Main.log ("ui load " + k); int ok = VX.state (false, k); m.pop (); m.toast (ok != 0 ? "Slot " + k + " loaded" : "Nothing loaded", 2); });
			sv.setEnabled (can); ld.setEnabled (can && VX.stateExists (s) != 0);
			c.addView (sv, new LinearLayout.LayoutParams (-1, (int) m.dp (55))); LinearLayout.LayoutParams l2 = new LinearLayout.LayoutParams (-1, (int) m.dp (55)); l2.topMargin = (int) m.dp (6); c.addView (ld, l2);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, -2, 1); if (s > 1) lp.leftMargin = (int) m.dp (8); slots.addView (c, lp); }
		p.add (slots, -2, 14);
		if (!can) p.add (m.text (Online.playing ? "No save states during an online game." : "This game has no save states (UVMC2 and PiTrex games keep their own).", 12, Main.DIM, false), -2, 8);
		final int ov = VX.ofind ("vectrex_emu_overlay"); final boolean ovOn = ov >= 0 && VX.opt (ov, 5).equals ("on");
		final int room = VX.ofind ("vectrex_emu_room");
		LinearLayout acts = new LinearLayout (m); acts.setOrientation (LinearLayout.HORIZONTAL);
		String[] an = { "Games", "RESET", ovOn ? "Overlay on" : "Overlay off", "Room light" }; int[] ai = { 10, 7, 13, 14 };
		Runnable[] ag = { () -> { m.pop (); Library.show (m); }, () -> { VX.act (1, 0); m.pop (); }, () -> { m.overlayToggle (); m.pop (); },
			() -> { if (room < 0) return; m.pop (); choice (m, room); } };
		for (int k = 0; k < 4; k++) { final Runnable go = ag[k]; final String nm = an[k]; IconBtn b = new IconBtn (m, nm, ai[k], k == 2 && ovOn);
			b.setOnClickListener (v -> { Main.log ("ui " + nm); go.run (); });
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, (int) m.dp (64), 1); if (k > 0) lp.leftMargin = (int) m.dp (8); acts.addView (b, lp); }
		p.add (acts, -2, 14);
		final Object[][] big = { { "All settings", 20, (Runnable) () -> { m.pop (); settings (m); } }, { "Online: play with a friend", 5, (Runnable) () -> { m.pop (); Online.show (m); } },
			{ "Get games & extras", 21, (Runnable) () -> { m.pop (); Getter.show (m); } } };
		for (Object[] b : big) { final String nm = (String) b[0]; final Runnable go = (Runnable) b[2]; IconBtn x = new IconBtn (m, nm, (Integer) b[1], false);
			x.setOnClickListener (v -> { Main.log ("ui " + nm); go.run (); }); p.add (x, 56, 14); }
		p.add (m.text ("VECTREX HARDWARE", 11, Main.DIM, true), -2, 24);
		LinearLayout hw = new LinearLayout (m); hw.setOrientation (LinearLayout.HORIZONTAL);
		final int p2 = VX.ofind ("vectrex_emu_port2");
		final TextView ej = button (m, "Eject / insert cartridge", false), pb = button (m, p2 >= 0 ? "Port 2: " + VX.opt (p2, 5) : "Port 2", false);
		ej.setOnClickListener (v -> { Main.log ("ui eject"); VX.act (2, 0); m.pop (); });
		pb.setOnClickListener (v -> { if (p2 >= 0) choice (m, p2); });
		p.refresh = () -> { if (p2 >= 0) pb.setText ("Port 2: " + VX.opt (p2, 5)); };   /* (a device chosen: the button says it) */
		for (TextView b : new TextView[] { ej, pb }) { b.setTextSize (13); b.setTypeface (Typeface.DEFAULT); b.setTextColor (Main.FG2); b.setMaxLines (2);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, (int) m.dp (40), 1); if (b == pb) lp.leftMargin = (int) m.dp (8); hw.addView (b, lp); }
		p.add (hw, -2, 14);
		m.push (p); resume.requestFocus ();
	}

	/* ---------------------------------------------------------------- the settings */
	static void settings (Main m) { settings (m, -1); }
	/* focus: the core setting to show (the slider's list button - as iOS: the list scrolled to it, its row lit a moment) */
	static void settings (Main m, int focus)
	{
		Main.log ("ui settings opened"); Panel p = new Panel (m, "Settings"); p.settings = true; p.refresh = () -> reopenSettings (m);
		p.header ("App");
		p.row ("Clock and battery", m.prefs.getBoolean ("app.statusbar", false) ? "On" : "Off", () -> { m.prefs.edit ().putBoolean ("app.statusbar", !m.prefs.getBoolean ("app.statusbar", false)).apply ();
			Main.log ("clock and battery " + (m.prefs.getBoolean ("app.statusbar", false) ? "on" : "off")); m.immersive (); m.root.postDelayed (() -> reopenSettings (m), 350); });   /* (built again once the status bar's room is known) */
		{ final String[] BL = { "Auto", "Shown", "Auto-hide" };   /* (the choices inside the row, as iOS's segments) */
		  segRow (p, "Top bar", null, BL, m.barSel (), k -> { m.prefs.edit ().putString ("app.bar", k == 1 ? "always" : k == 2 ? "hide" : "auto").apply ();
			Main.log ("top bar " + BL[k]); m.showBar (); m.bar.setAlpha (1); m.layout (); }); }
		{ final int sel = m.deckSel (); final boolean pop = m.deckPop ();
		  segRow (p, "Control deck", null, new String[] { "Auto", "Off", "Full", "Slim" }, sel, k -> { m.deckSet (k, m.deckPop ()); m.layout (); reopenSettings (m); });
		  LinearLayout dp = p.toggle ("Deck pop-up", null, pop, () -> { m.deckSet (m.deckSel (), !m.deckPop ()); m.layout (); reopenSettings (m); });
		  if (sel < 2) greyed (dp); }                  /* (Auto: its own choice, shown; off: nothing to pop up) */
		{ final int si = VX.ofind ("vectrex_emu_skip_intro");
		  if (si >= 0) p.row ("Skip intro", VX.opt (si, 5).equals ("on") ? "On" : "Off", () -> { VX.oset (si, VX.opt (si, 5).equals ("on") ? "off" : "on"); reopenSettings (m); }); }
		if (Pack.token (m) != null) p.row ("Downloads", "test build: games, boxes, UVMC2 (VecDoom), PiTrex", () -> { m.pop (); Pack.choose (m, false); });   /* (the private test build: its own pack too) */
		p.row ("Get games & extras", "the original games, free homebrew, the PiTrex, the speech ROM - or your own files", () -> { m.pop (); Getter.show (m); });
		if (m.hdrCan) p.row ("HDR brightness", m.prefs.getFloat ("app.hdr", 1f) > 0.01f ? "The brightest dots up to " + String.format (java.util.Locale.ROOT, "%.1fx", 1 + (m.hdrRoom - 1) * m.prefs.getFloat ("app.hdr", 1f)) + " white" : "Off: as an ordinary screen",
			() -> Adjust.showHdr (m));   /* (a slider over the running game) */
		else p.row ("HDR brightness", m.hdrScreen ? "Not on this Android version (HDR needs Android 14)" : "Not on this screen (it shows no HDR)", null);
		{ final int mb = Math.max (0, Math.min (2, m.maxBright ())); final String[] ML = { "Off", "Gameplay", "Whole app" };
		  final String[] MD = { "Your own brightness", "Screen at maximum while playing, yours in menus", "Screen at maximum, menus too" };
		  pullRow (p, "Max brightness", MD[mb], ML, mb, mb != 0 ? Main.HI2 : Main.FG2, k -> {
			m.prefs.edit ().putInt ("app.maxbright", k).apply (); Main.log ("max brightness " + ML[k]); m.brightTick (); reopenSettings (m); }); }
		{ final int lk = m.prefs.getInt ("app.look", 1);   /* (the look - iOS alike) */
			p.toggle ("Bright look", lk == 1 ? "Brighter, wider lines - for a small screen" : "Off: as a real Vectrex", lk == 1, () -> { final int s = 1 - lk;
					m.prefs.edit ().putInt ("app.look", s).apply (); VX.setBright (s); Main.log ("look " + s); reopenSettings (m); }); }
		p.row ("Brighter afterglow", m.trailBoost () ? "On" : "Off", () -> { m.prefs.edit ().putBoolean ("app.trailboost", !m.trailBoost ()).apply (); m.trailLast = -1; m.trailTick (); Main.log ("brighter afterglow " + m.trailBoost ()); reopenSettings (m); });
		p.row ("Controls", "controllers, touch, keyboard", () -> { m.pop (); controls (m); });
		p.row ("About and help", "how to play, online, credits", () -> about (m));
		{ int n = 0; for (int i = 0; i < VX.nopts (); i++) if (!VX.opt (i, 5).equals (VX.opt (i, 6))) n++; final int changed = n;
		  /* (one version on both apps: the title dimmed when nothing is changed - a tap then only says so; else the prompt) */
		  LinearLayout r = p.row ("Reset all settings", n > 0 ? n + " changed - all back to their defaults (a long press on one: that one)" : "All at their defaults", () -> {
			if (changed == 0) { m.toast ("Every setting is at its default", 2); return; }
			prompt (m, "Reset all settings?", null, "Every setting of the picture, the overlay, the sound and the controls back to its default.", new Object[][] {
				{ "Reset", "every setting to its default", (Runnable) () -> { int k = 0;
					for (int i = 0; i < VX.nopts (); i++) if (!VX.opt (i, 5).equals (VX.opt (i, 6))) { VX.oset (i, VX.opt (i, 6)); k++; }
					m.prefs.edit ().remove ("app.deckpop").remove ("app.topbar").remove ("app.bar").apply (); m.showBar (); m.layout ();   /* (the deck's pop-up and the top bar too: Auto) */
					Main.log ("settings: reset all (" + k + " changed)"); { int pi = VX.ofind ("vectrex_emu_palette"); if (pi >= 0) palCheck (m, pi); } m.toast ("Settings reset", 2); reopenSettings (m); } },
				{ "Cancel", "", (Runnable) () -> { } } }, null); });
		  ((TextView) r.findViewWithTag ("t")).setTextColor (n > 0 ? Main.CHG : Main.DIM); }
		p.foot ("Auto: what fits this screen with the picture at its full width. Top bar auto-hide: tap the top of the screen to bring it back. Deck pop-up: hidden until you tap the picture's bottom. Skip intro: games start straight away.");
		/* the core's settings by its groups, in the iOS app's order (VXSettings): the most used first, in each group the
		 * rows in the core's order - the desktop's Config tab's (2026-11-23); settings that mean nothing here left out */
		final int n = VX.nopts (), nc = VX.ncats ();
		java.util.List<String> order = java.util.Arrays.asList ("video", "overlay", "case", "tube", "controls", "audio", "system", "unit", "circuit");
		java.util.List<String> top = java.util.Arrays.asList ("vectrex_emu_profile");
		java.util.List<String> hide = java.util.Arrays.asList ("vectrex_emu_shape", "vectrex_emu_btn_browser", "vectrex_emu_deck_look", "vectrex_emu_deck", "vectrex_emu_skip_intro", "vectrex_emu_tube_look", "vectrex_emu_gamelist");   /* (the game list: RetroArch's own browser - the app has its library) */   /* (the deck, skip intro, the look: under App) */
		java.util.ArrayList<Integer> cs = new java.util.ArrayList<> ();
		for (String k : order) for (int c = 0; c < nc; c++) if (VX.cat (c, 0).equals (k)) cs.add (c);
		for (int c = 0; c < nc; c++) if (!cs.contains (c)) cs.add (c);
		if (nc == 0) cs.add (-1);
		View focusRow = null;
		for (int c : cs) {
			String ck = c >= 0 ? VX.cat (c, 0) : "";
			java.util.ArrayList<Integer> rows = new java.util.ArrayList<> ();
			for (int i = 0; i < n; i++) if (VX.opt (i, 2).equals (ck) && !hide.contains (VX.opt (i, 0))) rows.add (i);
			rows.sort ((x, y) -> { int a = top.indexOf (VX.opt (x, 0)), b = top.indexOf (VX.opt (y, 0)); if (a < 0) a = 999; if (b < 0) b = 999; return Integer.compare (a, b); });
			if (rows.isEmpty ()) continue;
			p.header (c >= 0 ? VX.cat (c, 1) : "General");
			for (int i : rows) {
				final int ix = i; String v = VX.opt (i, 5); boolean chg = !v.equals (VX.opt (i, 6));
				final String vals = VX.opt (i, 4); final boolean onoff = vals.equals ("on|off") || vals.equals ("off|on");   /* (on / off: the row's switch turns it) */
				final String[] two = vals.split ("\\|");
				LinearLayout r;
				if (!onoff && two.length == 2) {   /* (two other choices - Colours blue / green: a switch too, its note what it is now) */
					final String[] sw = TWO.get (VX.opt (i, 0)); final String onV = sw != null ? sw[1] : (two[0].equals (VX.opt (i, 6)) ? two[1] : two[0]);
					final String offV = onV.equals (two[0]) ? two[1] : two[0]; final boolean isOn = v.equals (onV);
					r = p.toggle (sw != null ? sw[0] : VX.opt (i, 1) + ": " + onV, sw != null ? (isOn ? sw[2] : sw[3]) : "Now: " + v,
						isOn, () -> { VX.oset (ix, isOn ? offV : onV); palCheck (m, ix); reopenSettings (m); });
					if (chg) ((TextView) r.findViewWithTag ("t")).setTextColor (Main.CHG); }
				else { r = p.row (VX.opt (i, 1), v, onoff ? (Runnable) () -> { VX.oset (ix, VX.opt (ix, 5).equals ("on") ? "off" : "on"); reopenSettings (m); } : (Runnable) () -> choice (m, ix), onoff);
					if (chg) ((TextView) r.findViewWithTag (onoff ? "t" : "d")).setTextColor (Main.CHG); }
				r.setOnLongClickListener (x -> { x.performHapticFeedback (HapticFeedbackConstants.LONG_PRESS); help (m, ix); return true; });   /* (a long press: what it does, and back to its default) */
				if (i == focus) focusRow = r; }
			if (c >= 0 && !VX.cat (c, 2).isEmpty ()) p.foot (VX.cat (c, 2));
		}
		m.push (p);
		if (focusRow != null) { final View fr = focusRow; p.postDelayed (() -> { p.sc.scrollTo (0, Math.max (0, fr.getTop () + fr.getHeight () / 2 - p.sc.getHeight () / 2));
			fr.setPressed (true); fr.postDelayed (() -> fr.setPressed (false), 600); }, 200); }
	}
	/* a setting touched and held, as the iOS app's menu: what it does, and back to its default (greyed when it is there) */
	static void help (Main m, int ix)
	{
		Main.log ("help " + VX.opt (ix, 0)); final String def = VX.opt (ix, 6); final boolean at = VX.opt (ix, 5).equals (def);
		prompt (m, VX.opt (ix, 1), null, VX.opt (ix, 3), new Object[][] { { "Back to the default: " + def, "", at ? null : (Runnable) () -> {
			VX.oset (ix, def); Main.log ("settings: " + VX.opt (ix, 0) + " back to the default"); palCheck (m, ix); reopenSettings (m); } } }, null);
	}
	static String pct (float f) { return Math.round (f * 100) + " %"; }
	/* the core's settings of two choices shown as a switch: its name, the value that is "on", the notes on / off */
	static final java.util.Map<String, String[]> TWO = new java.util.HashMap<> ();
	static { TWO.put ("vectrex_emu_palette", new String[] { "Green colours", "green", "The deck, the menus and the buttons in green", "Off: a deeper blue" }); }
	static void palCheck (Main m, int i) { if (VX.opt (i, 0).equals ("vectrex_emu_palette")) m.applyPalette (VX.opt (i, 5).equals ("green")); }   /* (the colour scheme: at once) */
	static void reopenSettings (Main m)                          /* (an app setting changed: the list shown again, scrolled where it was) */
	{
		View t = m.top (); int y = t instanceof Panel ? ((Panel) t).sc.getScrollY () : 0; m.pop (); settings (m);
		View n = m.top (); if (n instanceof Panel) n.post (() -> ((Panel) n).sc.scrollTo (0, y));
	}
	interface Pick { void go (String v); }
	static void pick (Main m, String title, String[] vals, String[] labels, String cur, Pick go)
	{
		Panel p = new Panel (m, title); View first = null;
		for (int i = 0; i < vals.length; i++) { final String v = vals[i];
			LinearLayout r = p.row ((v.equals (cur) ? "✓  " : "     ") + labels[i], null, () -> { m.pop (); go.go (v); });
			if (v.equals (cur) || first == null) first = r; }
		m.push (p); if (first != null) first.requestFocus ();
	}
	/* a core setting's values, its help and its default; the screen under it shows the value then (the settings, the game
	 * menu's Port 2) */
	static void choice (Main m, int i)
	{
		if (Adjust.slider (i)) { Adjust.show (m, i); return; }   /* (a number: the slider over the running game, as the iOS app) */
		Panel p = new Panel (m, VX.opt (i, 1)); String cur = VX.opt (i, 5); View first = null;
		final Runnable chosen = () -> { m.pop (); View t = m.top (); if (t instanceof Panel && ((Panel) t).refresh != null) ((Panel) t).refresh.run (); };
		if (!cur.equals (VX.opt (i, 6))) p.row ("Back to the default: " + VX.opt (i, 6), null, () -> { VX.oset (i, VX.opt (i, 6)); chosen.run (); });
		for (String v : VX.opt (i, 4).split ("\\|")) {
			LinearLayout r = p.row ((v.equals (cur) ? "✓  " : "     ") + v, null, () -> { VX.oset (i, v); chosen.run (); });
			if (v.equals (cur) || first == null) first = r; }
		p.foot (VX.opt (i, 3) + "\nDefault: " + VX.opt (i, 6));
		m.push (p); if (first != null) first.requestFocus ();
	}
	/* Settings > Controls, as the iOS app's: what is connected, the Vectrex's buttons on the controller, the touch
	 * controls, the keyboard */
	static void controls (Main m)
	{
		Main.log ("ui controls opened"); Panel p = new Panel (m, "Controls");
		final boolean kb = m.getResources ().getConfiguration ().keyboard == android.content.res.Configuration.KEYBOARD_QWERTY;
		p.header ("Connected"); int np = 0;
		for (int k = 0; k < Input.devs.size () && k < 2; k++) { android.view.InputDevice d = android.view.InputDevice.getDevice (Input.devs.get (k));
			if (d == null) continue; p.row (d.getName (), "player " + (k + 1), null); np++; }
		if (kb) p.row ("Keyboard", "the PC keys", null);   /* (a keyboard: its row too, beside the controllers - as iOS) */
		else if (np == 0) p.row ("Nothing connected", "touch", null);
		p.foot ("Player 1 is the first controller, player 2 the second.");
		p.header ("Vectrex buttons on the controller");
		for (int v = 0; v < 4; v++) { final int k = v;
			LinearLayout r = p.row ("Button " + (v + 1), Input.PHYS[Input.phys (v)], () -> {
				Object[][] ch = new Object[8][]; for (int j = 0; j < 8; j++) { final int s = j;
					ch[j] = new Object[] { Input.PHYS[j], "", (Runnable) () -> { m.prefs.edit ().putInt ("map." + (k + 1), s).apply (); Main.log ("controls: button " + (k + 1) + " on " + Input.PHYS[s]); m.pop (); controls (m); } }; }
				prompt (m, "Vectrex button " + (k + 1), "On the controller's", null, ch, null); });
			final String mk = "map." + (v + 1);   /* (moved from the default: orange, as a changed setting) */
			if (m.prefs.contains (mk) && m.prefs.getInt (mk, 0) != (Input.nintendo ? Input.DEF_N : Input.DEF)[v]) ((TextView) r.findViewWithTag ("d")).setTextColor (Main.CHG); }
		p.row ("As the Vectrex controller (default)", Input.nintendo ? "1 Y, 2 X, 3 B, 4 A (this pad's labels)" : "1 X, 2 Y, 3 A, 4 B", () -> { for (int j = 1; j <= 4; j++) m.prefs.edit ().remove ("map." + j).apply (); m.pop (); controls (m); });
		p.foot ("Tap a button to choose. The Vectrex controller's four buttons in a row; on a modern pad's diamond by default 1 left, 2 top, 3 bottom, 4 right.");
		p.header ("Touch controls");
		{ final String ts = m.prefs.getString ("touch.show", "auto"); final String[] SV = { "auto", "always", "never" };
		  segRow (p, "Show", null, new String[] { "Automatic", "Always", "Never" }, ts.equals ("always") ? 1 : ts.equals ("never") ? 2 : 0, k -> {
			m.prefs.edit ().putString ("touch.show", SV[k]).apply (); m.layout (); }); }
		sliderRow (p, "Size", 0.7f, 1.4f, m.prefs.getFloat ("touch.size", 1f), v -> { m.prefs.edit ().putFloat ("touch.size", v).apply (); m.layout (); });   /* (continuous, as iOS's) */
		sliderRow (p, "Opacity", 0.2f, 1f, m.prefs.getFloat ("touch.alpha", 1f), v -> { m.prefs.edit ().putFloat ("touch.alpha", v).apply (); m.layout (); });
		segRow (p, "Directions", null, new String[] { "Stick", "D-pad" }, m.prefs.getBoolean ("touch.dpad", false) ? 1 : 0, k -> {   /* (the stick, or a D-pad: clear turns) */
			m.prefs.edit ().putBoolean ("touch.dpad", k == 1).apply (); m.layout (); });
		p.row ("Left-handed (the stick on the right)", m.prefs.getBoolean ("touch.lefty", false) ? "On" : "Off", () -> { m.prefs.edit ().putBoolean ("touch.lefty", !m.prefs.getBoolean ("touch.lefty", false)).apply (); m.layout (); m.pop (); controls (m); });
		/* (the right stick's row - this game's - comes here) */
		p.foot ("Automatic: shown only while no controller or keyboard is connected. The stick appears where the thumb lands; the D-pad gives clear up, down, left, right (the small switch beside it changes it too).");
		p.header ("Keyboard");
		final int kk = VX.ofind ("vectrex_emu_keys");
		if (kk >= 0) p.row ("Keyboard as on the PC", VX.opt (kk, 5).equals ("on") ? "On" : "Off", () -> { VX.oset (kk, VX.opt (kk, 5).equals ("on") ? "off" : "on"); m.pop (); controls (m); });
		p.foot ("As on the PC: the arrows the stick (Shift: half), A W S D buttons 1-4, P pause, L the games, O overlay, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, M the menu.");
		m.push (p);
	}
	/* Settings > About and help, as the iOS app's (VXAbout.m) */
	static void about (Main m)
	{
		Panel p = new Panel (m, "About and help"); Main.log ("settings: about and help");
		String[][] parts = {
			{ "Vectrex_EMU", "Version " + m.version () + " (build " + m.versionCode () + "). The Vectrex as it looks on a real tube: the beam, the phosphor's afterglow, the glow, the overlays, the glass and the case." },
			{ "Playing", "MENU (top bar) opens the game menu: save and load, games, RESET, overlay, room light, all settings. With a controller: Select + X; with a keyboard: M.\nThe knob below the picture: the volume; hold it to switch the Vectrex off and on.\nTouch a setting and hold: what it does, and back to its default." },
			{ "Controls", "The touch controls show while no controller or keyboard is connected (Settings > Controls: always, never, their size and side). Controller buttons 1-4 are chosen there too. In a menu or the games: button 4 picks, button 1 goes back.\nA UVMC2 game's calibration: hold buttons 2 + 3 while button 4 starts it, or touch and hold the game: Calibration (2 + 3 held)." },
			{ "Online", "Game menu > Online: host your game in the game room (anyone can join), with a code (only who you tell), or on this Wi-Fi (no internet needed); join with a code or from the lists. Phones and PCs play together - both need the same game and the same version of Vectrex_EMU. A menu open pauses both players." },
			{ "Your own games", "Get games & extras > Add files... adds ROMs (.bin .vec .gam .um2). Or copy them with a file manager to " + new File (m.ext (), "roms") + "." },
			{ "Credits", "Vectrex_EMU by Hugo Bokhorst, started from vecx, the open-source Vectrex emulator. Overlays: the libretro overlay-borders collection and their creators (the full list: overlays/CREDITS.md in the app's files). Last Human and the UVMC2 games with their makers' permission. Title artwork and pictures: Simon Quincey (his Vectrex 2D Front Cover Box Art pack), used with permission. Box and label design, label art and reconstructions by Hugo Bokhorst.\nVectrex is a trademark of its owners; this app is not affiliated with them." },
			{ "Licence", "Free software: GNU General Public License version 3. The source code: github.com/hugonl31/Vectrex_EMU" } };
		for (String[] x : parts) { p.header (x[0]); TextView b = m.text (x[1], 15, Main.FG, false); b.setPadding ((int) m.dp (4), 0, (int) m.dp (4), (int) m.dp (6)); p.col.addView (b); }
		m.push (p);
	}
	/* a line of text typed in (chat, a code, an address) */
	interface Typed { void go (String s); }
	static Panel ask (Main m, String title, String hint, boolean caps, Typed go) { return ask (m, title, hint, caps, "OK", false, go); }
	/* a line typed: the app's own panel, as the iOS app's prompt (no system dialog - the owner, 2026-11-25); its button's word;
	 * live: the game goes on under it (the chat) */
	static Panel ask (Main m, String title, String hint, boolean caps, String button, boolean live, Typed go)
	{
		Panel p = new Panel (m, title); p.live = live; final EditText e = p.field (hint, caps);
		final Runnable ok = () -> { String s = e.getText ().toString ().trim (); hideKeys (m, e); m.pop (); if (!s.isEmpty ()) go.go (s); };
		e.setImeOptions (android.view.inputmethod.EditorInfo.IME_ACTION_DONE); e.setOnEditorActionListener ((v, a, k) -> { ok.run (); return true; });
		p.row (button, "", ok);
		m.push (p); e.requestFocus ();
		e.postDelayed (() -> { android.view.inputmethod.InputMethodManager im = (android.view.inputmethod.InputMethodManager) m.getSystemService (android.content.Context.INPUT_METHOD_SERVICE); if (im != null) im.showSoftInput (e, 0); }, 150);
		return p;
	}
	static void hideKeys (Main m, View v) { android.view.inputmethod.InputMethodManager im = (android.view.inputmethod.InputMethodManager) m.getSystemService (android.content.Context.INPUT_METHOD_SERVICE); if (im != null) im.hideSoftInputFromWindow (v.getWindowToken (), 0); }
}
