/* Controls.java - the touch controls over the game, as the iOS app's: a stick where the thumb lands (left), buttons 1-4
 * as the Vectrex controller (right) - below the picture upright, beside it sideways; a finger on the picture is the
 * core's pointer (its control deck, the light pen). Settings > Controls: shown always / never / while no controller,
 * their size and opacity, left-handed.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;

final class Controls extends View {
	final Main m; final Paint fill = new Paint (Paint.ANTI_ALIAS_FLAG), edge = new Paint (Paint.ANTI_ALIAS_FLAG), txt = new Paint (Paint.ANTI_ALIAS_FLAG);
	final RectF[] btn = new RectF[4];
	RectF stickZone = new RectF (); float scx, scy, kx, ky, R = 60; int stickPtr = -1, picPtr = -1; int tbtn; int tax, tay;
	final RectF tog = new RectF (); int padDir;                   /* (the stick / D-pad switch beside it; the D-pad's arms pressed) */
	boolean dpad () { return m.prefs.getBoolean ("touch.dpad", false); }
	static final int[] ID4 = { 1, 9, 0, 8 };                      /* (the Vectrex's 1-4: the RetroPad's Y X B A) */
	static final int BMASK = 1 << 1 | 1 << 9 | 1 | 1 << 8;        /* (ID4's bits in tbtn) */
	/* the right side (the owner, 2026-11-26): a finger presses one button, or two neighbours from the gap between them,
	 * and slides from button to button; or, per game, an 8-way stick whose directions press the buttons (a diagonal two) */
	final RectF tog2 = new RectF (); RectF padZone = new RectF (); float bcx, bcy, BR;   /* (its switch; the buttons' zone, their square's centre and half side) */
	final int[] fb = new int[32];                                 /* (each finger on the right side: the buttons it presses, bits 0-3; -1 none) */
	int rpPtr = -1, rpDir, togPtr = -1; boolean togLong; String lastStem = "";
	static String stem () { String g = VX.game (); return g == null || g.isEmpty () ? "" : Online.stem (g); }
	boolean rstick () { return m.prefs.getBoolean ("touch.rstick." + stem (), false); }
	static String rmap (Main m, String st) { String s = m.prefs.getString ("touch.rmap." + st, "3421"); return s != null && s.matches ("[1-4]{4}") ? s : "3421"; }   /* (up, right, down, left) */
	Controls (Main m)
	{
		super (m); this.m = m; for (int i = 0; i < 4; i++) btn[i] = new RectF (); java.util.Arrays.fill (fb, -1);
		edge.setStyle (Paint.Style.STROKE); edge.setStrokeWidth (m.dp (1.5f)); txt.setTextAlign (Paint.Align.CENTER); txt.setColor (Main.HI); txt.setTypeface (Typeface.DEFAULT_BOLD);
		post (watch);
	}
	final Runnable watch = new Runnable () { public void run () {   /* (another game: its own right side) */
		final String s = stem (); if (!s.equals (lastStem)) { lastStem = s; freeRight (); invalidate (); } postDelayed (this, 500); } };
	void freeRight () { java.util.Arrays.fill (fb, -1); rpPtr = -1; rpDir = 0; btns (); }
	void recolour () { txt.setColor (Main.HI); invalidate (); }   /* (the colour scheme changed) */
	void relayout ()
	{
		final float tsize = m.prefs.getFloat ("touch.size", 1f); setAlpha (m.prefs.getFloat ("touch.alpha", 1f));
		R = m.dp (60) * tsize; final int W = getWidth (), H = getHeight (); if (W <= 0) { post (this::relayout); return; }
		final float[] p = m.pic; final float px = p[0], py = p[1], pw = p[2], ph = p[3];
		RectF zl, zr;
		final float mg = m.dp (10);                                  /* (a margin: the buttons kept on the screen) */
		if (W > H) { zl = new RectF (m.insL + mg, m.insTop + mg, Math.max (px, m.insL + R * 2.2f), H - m.insBot - mg); zr = new RectF (Math.min (px + pw, W - m.insR - R * 2.2f), m.insTop + mg, W - m.insR - mg, H - m.insBot - mg); }
		else { final int dm = m.deckMode (); float top = py + ph + (dm == 1 ? m.dp (58) : dm == 2 ? m.dp (40) : 0), bot = H - m.insBot - mg, need = Math.max (m.dp (120), m.dp (140) * tsize);
			if (bot - top < need) { bot = py + ph - m.dp (4); top = bot - need; }
			zl = new RectF (m.insL, top, W / 2f, bot); zr = new RectF (W / 2f, top, W - m.insR, bot); }
		if (m.prefs.getBoolean ("touch.lefty", false)) { RectF t = zl; zl = zr; zr = t; }
		stickZone = zl;
		{ final boolean left = zl.centerX () < W / 2f; final float tw = m.dp (54), th = m.dp (26), x0 = left ? zl.left + m.dp (12) : zl.right - m.dp (12) - tw;
		  tog.set (x0, zl.bottom - m.dp (8) - th, x0 + tw, zl.bottom - m.dp (8)); }   /* (bottom, outer side) */
		final boolean zrl = zr.centerX () < W / 2f;
		{ final float tw = m.dp (62), th = m.dp (26), x0 = zrl ? zr.left + m.dp (12) : zr.right - m.dp (12) - tw;
		  tog2.set (x0, zr.bottom - m.dp (8) - th, x0 + tw, zr.bottom - m.dp (8)); }   /* (the right side's switch: bottom, outer side) */
		float r = Math.min (Math.min (zr.width () / 4.6f, zr.height () / 4.4f), m.dp (38) * tsize), gap = r * 0.35f, cx = zr.centerX (), cy = zr.centerY ();
		{ final float bx = cx + (zrl ? -1 : 1) * (r + gap / 2), by = cy + r + gap / 2, mm = r + m.dp (3), dy = Math.max (0, Math.max (tog2.top - by, by - tog2.bottom));
		  if (dy < mm) { final float dx = zrl ? bx - tog2.right : tog2.left - bx, sh = Math.min ((float) Math.sqrt (mm * mm - dy * dy) - dx, (zr.width () - 4 * r - gap) / 2 - m.dp (2));
		    if (sh > 0) cx += zrl ? sh : -sh; } }   /* (the buttons clear of the switch: moved inward as far as needed - upright, little room) */
		for (int i = 0; i < 4; i++) { float bx = cx + (i % 2 == 1 ? r + gap / 2 : -r - gap / 2), by = cy + (i < 2 ? -r - gap / 2 : r + gap / 2); btn[i].set (bx - r, by - r, bx + r, by + r); }
		bcx = cx; bcy = cy; BR = 2 * r + gap / 2; padZone = zr;
		if (dpad ()) { scx = kx = zl.centerX (); scy = ky = zl.centerY () + m.dp (10); }   /* (the D-pad: in one place, always) */
		else if (stickPtr < 0) { scx = kx = zl.centerX (); scy = ky = zl.centerY (); }
		txt.setTextSize (Math.max (m.dp (14), r * 0.75f)); invalidate ();
	}
	@Override protected void onSizeChanged (int w, int h, int ow, int oh) { relayout (); }
	@Override protected void onDraw (Canvas c)
	{
		if (!show) return;
		if (dpad ()) cross (c, scx, scy, R, padDir);               /* (the D-pad: a cross, the arms pressed lit) */
		else { fill.setColor (0x990f1c17); edge.setColor (Main.EDGE2);
			c.drawCircle (scx, scy, R, fill); c.drawCircle (scx, scy, R, edge);
			fill.setColor (Main.pal (0xff16302a)); edge.setColor (Main.G); c.drawCircle (kx, ky, R * 0.43f, fill); c.drawCircle (kx, ky, R * 0.43f, edge); }
		pill (c, tog, dpad () ? "STICK" : "D-PAD");                /* (the switch) */
		final boolean rs = rstick (); pill (c, tog2, rs ? "BUTTONS" : "STICK");
		if (rs) {                                                /* (the right stick: the left D-pad's look, each arm's button at its end) */
			cross (c, bcx, bcy, BR, rpDir); final String mp = rmap (m, stem ()); final float o = BR * 0.68f, dy = -(txt.descent () + txt.ascent ()) / 2;
			c.drawText (mp.substring (0, 1), bcx, bcy - o + dy, txt); c.drawText (mp.substring (1, 2), bcx + o, bcy + dy, txt);
			c.drawText (mp.substring (2, 3), bcx, bcy + o + dy, txt); c.drawText (mp.substring (3, 4), bcx - o, bcy + dy, txt);
			return; }
		edge.setColor (Main.G);
		for (int i = 0; i < 4; i++) { fill.setColor ((tbtn >> ID4[i] & 1) != 0 ? 0xe624443a : 0x990f1c17); c.drawOval (btn[i], fill); c.drawOval (btn[i], edge);
			c.drawText (String.valueOf (i + 1), btn[i].centerX (), btn[i].centerY () - (txt.descent () + txt.ascent ()) / 2, txt); }
	}
	void cross (Canvas c, float x, float y, float R, int dir)    /* (a D-pad: the arms in dir lit) */
	{
		fill.setColor (0x990f1c17); edge.setColor (Main.EDGE2);
		final float w = R * 0.33f; android.graphics.Path p = new android.graphics.Path ();
		p.moveTo (x - w, y - R); p.lineTo (x + w, y - R); p.lineTo (x + w, y - w); p.lineTo (x + R, y - w); p.lineTo (x + R, y + w); p.lineTo (x + w, y + w);
		p.lineTo (x + w, y + R); p.lineTo (x - w, y + R); p.lineTo (x - w, y + w); p.lineTo (x - R, y + w); p.lineTo (x - R, y - w); p.lineTo (x - w, y - w); p.close ();
		c.drawPath (p, fill); c.drawPath (p, edge);
		fill.setColor (Main.pal (0xff24443a)); final float in = w * 1.1f, r2 = R - m.dp (4), h = w - m.dp (4), cr = m.dp (4);
		if ((dir & 1) != 0) c.drawRoundRect (new RectF (x - h, y - r2, x + h, y - in), cr, cr, fill);
		if ((dir & 2) != 0) c.drawRoundRect (new RectF (x - h, y + in, x + h, y + r2), cr, cr, fill);
		if ((dir & 4) != 0) c.drawRoundRect (new RectF (x - r2, y - h, x - in, y + h), cr, cr, fill);
		if ((dir & 8) != 0) c.drawRoundRect (new RectF (x + in, y - h, x + r2, y + h), cr, cr, fill);
	}
	void pill (Canvas c, RectF t, String s)                      /* (a small switch) */
	{
		fill.setColor (0x990f1c17); edge.setColor (Main.EDGE2); c.drawRoundRect (t, t.height () / 2, t.height () / 2, fill); c.drawRoundRect (t, t.height () / 2, t.height () / 2, edge);
		final float ts = txt.getTextSize (); txt.setTextSize (m.dp (9)); txt.setColor (Main.G);
		c.drawText (s, t.centerX (), t.centerY () - (txt.descent () + txt.ascent ()) / 2, txt); txt.setTextSize (ts); txt.setColor (Main.HI);
	}
	/* (the whole screen's touches come here: the controls when shown, else only the picture as the pointer) */
	boolean show; boolean shown () { return show; }   /* (the view always there - the picture's touches; the controls drawn when shown) */
	@Override public boolean onTouchEvent (MotionEvent e)
	{
		final int a = e.getActionMasked (), ix = e.getActionIndex ();
		if (m.barAuto () && a == MotionEvent.ACTION_DOWN && e.getY () < m.insTop + m.dp (70)) m.showBar ();
		if (a == MotionEvent.ACTION_DOWN && m.popTap (e.getX (), e.getY ())) return true;   /* (the pop-up deck: the tap opens it, nothing else) */
		switch (a) {
		case MotionEvent.ACTION_DOWN: case MotionEvent.ACTION_POINTER_DOWN: down (e.getPointerId (ix), e.getX (ix), e.getY (ix)); break;
		case MotionEvent.ACTION_MOVE: for (int i = 0; i < e.getPointerCount (); i++) move (e.getPointerId (i), e.getX (i), e.getY (i)); break;
		case MotionEvent.ACTION_UP: case MotionEvent.ACTION_POINTER_UP: up (e.getPointerId (ix), e.getX (ix), e.getY (ix)); break;
		case MotionEvent.ACTION_CANCEL: for (int i = 0; i < e.getPointerCount (); i++) up (e.getPointerId (i), e.getX (i), e.getY (i)); break;
		}
		return true;
	}
	boolean inPic (float x, float y) { float[] p = m.pic; return p[2] > 0 && x >= p[0] && x < p[0] + p[2] && y >= p[1] && y < p[1] + p[3]; }
	void pointer (boolean on, float x, float y)
	{
		float[] p = m.pic; if (p[2] <= 0) return;
		float px = Math.max (-1, Math.min (1, (x - p[0]) / p[2] * 2 - 1)), py = Math.max (-1, Math.min (1, (y - p[1]) / p[3] * 2 - 1)); VX.setPointer (on, px, py);
	}
	boolean onControl (float x, float y)   /* (a touch for a button or the stick - the pop-up deck leaves it) */
	{
		for (int i = 0; i < 4; i++) { RectF r = new RectF (btn[i]); r.inset (-m.dp (8), -m.dp (8)); if (r.contains (x, y)) return true; }
		return tog.contains (x, y) || tog2.contains (x, y) || (stickZone.contains (x, y) || rstick () && padZone.contains (x, y)) && !inPic (x, y);
	}
	/* the buttons under a finger (bits 0-3): in the gap between two neighbours (1-2, 3-4, 1-3, 2-4: the rectangle between
	 * their facing edges, as wide as they are) both, else the nearest one whose rect grown 8 dp holds it */
	int hits (float x, float y)
	{
		for (int k = 0; k < 4; k++) { final int i = k < 2 ? k * 2 : k - 2, j = k < 2 ? i + 1 : i + 2; final RectF a = btn[i], b = btn[j];
			if (k < 2 ? x > a.right && x < b.left && y >= a.top && y <= a.bottom : y > a.bottom && y < b.top && x >= a.left && x <= a.right) return 1 << i | 1 << j; }
		int best = -1; float bd = 0; final float g = m.dp (8);
		for (int i = 0; i < 4; i++) { final RectF r = btn[i]; if (x < r.left - g || x > r.right + g || y < r.top - g || y > r.bottom + g) continue;
			final float d = (float) Math.hypot (x - r.centerX (), y - r.centerY ()); if (best < 0 || d < bd) { best = i; bd = d; } }
		return best < 0 ? 0 : 1 << best;
	}
	boolean onOval (float x, float y) { for (RectF r : btn) if (Math.hypot (x - r.centerX (), y - r.centerY ()) <= r.width () / 2) return true; return false; }
	static String names (int b) { String s = ""; for (int i = 0; i < 4; i++) if ((b >> i & 1) != 0) s += (s.isEmpty () ? "" : "+") + (i + 1); return s.isEmpty () ? "none" : s; }
	void btns () { int b = 0; for (int f : fb) if (f > 0) for (int i = 0; i < 4; i++) if ((f >> i & 1) != 0) b |= 1 << ID4[i]; tbtn = tbtn & ~BMASK | b; send (); }   /* (every finger's buttons) */
	/* the 8 ways: up / down / left / right clearly; a diagonal only within 15 degrees of the exact corner; nothing in the
	 * middle (bits: 1 up, 2 down, 4 left, 8 right) */
	static int dir8 (float dx, float dy, float R)
	{
		int dir = 0; if (Math.hypot (dx, dy) > R * 0.22f) { double a = Math.toDegrees (Math.atan2 (dy, dx)); if (a < 0) a += 360; final double md = a % 90;
			if (md > 30 && md < 60) dir = (dy < 0 ? 1 : 2) | (dx < 0 ? 4 : 8);
			else { final int q = (int) Math.floor ((a + 45) / 90) % 4; dir = q == 0 ? 8 : q == 1 ? 2 : q == 2 ? 4 : 1; } }
		return dir;
	}
	final Runnable togHold = this::togHeld;
	void togHeld () { togLong = true; performHapticFeedback (HapticFeedbackConstants.LONG_PRESS); Main.log ("touch: the right switch held - the right stick's map"); mapPanel (m, null); }
	void flip ()                                                 /* (the right switch: buttons / a stick, for this game) */
	{
		final String st = stem (); final boolean on = !rstick (); m.prefs.edit ().putBoolean ("touch.rstick." + st, on).apply ();
		freeRight (); performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY); invalidate ();
		Main.log ("touch: the right side " + (on ? "a stick, map " + rmap (m, st) : "buttons") + " (" + st + ")");
		if (on && !m.prefs.contains ("touch.rmap." + st)) mapPanel (m, null);   /* (the first time: which button each way) */
	}
	/* the right stick's map for this game: which button each direction presses; after: once it closed (Settings again) */
	static void mapPanel (Main m, Runnable after)
	{
		final String st = stem (); if (st.isEmpty ()) { m.toast ("Start a game first", 2.5); return; }
		final char[] mp = rmap (m, st).toCharArray (); final View under = m.top ();
		Screens.Panel p = new Screens.Panel (m, "Right stick"); Main.log ("ui right stick map: " + st + " " + new String (mp));
		final String[] dirs = { "Up", "Right", "Down", "Left" }, vals = { "1", "2", "3", "4" }, labels = { "Button 1", "Button 2", "Button 3", "Button 4" };
		for (int k = 0; k < 4; k++) { final int d = k; final View[] row = new View[1];
			row[0] = p.row (dirs[k], "Button " + mp[k], () -> Screens.pick (m, dirs[d], vals, labels, String.valueOf (mp[d]), v -> {
				mp[d] = v.charAt (0); m.prefs.edit ().putString ("touch.rmap." + st, new String (mp)).apply (); Main.log ("right stick: " + dirs[d] + " button " + v + " (" + st + " " + new String (mp) + ")");
				final View t = row[0].findViewWithTag ("d"); if (t instanceof android.widget.TextView) ((android.widget.TextView) t).setText ("Button " + v); m.ctl.invalidate (); })); }
		p.row ("Done", null, m::pop);
		p.foot ("Which button each direction presses in this game.");
		p.onClose = () -> { m.prefs.edit ().putString ("touch.rmap." + st, new String (mp)).apply (); m.ctl.invalidate (); if (after != null) m.root.post (() -> { if (m.top () == under) after.run (); }); };
		m.push (p);
	}
	static String mapText (Main m)   /* (Settings: this game's map) */
	{
		final String st = stem (); if (st.isEmpty ()) return "No game running"; final String s = rmap (m, st);
		return "Up " + s.charAt (0) + ", right " + s.charAt (1) + ", down " + s.charAt (2) + ", left " + s.charAt (3) + (m.prefs.getBoolean ("touch.rstick." + st, false) ? " (in use)" : "");
	}
	void down (int id, float x, float y)
	{
		String what = "nothing";
		if (shown ()) {
			final boolean rs = rstick (); final int hit = rs ? 0 : hits (x, y); final boolean ok = id >= 0 && id < fb.length;
			if (togPtr < 0 && tog2.contains (x, y) && (rs || !onOval (x, y))) { togPtr = id; togLong = false; postDelayed (togHold, 500); what = "the right switch"; }   /* (a tap turns it, held: the map) */
			else if (ok && hit != 0) { fb[id] = hit; btns (); performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY); what = "buttons " + names (hit); invalidate (); }
			else if (ok && rs && rpPtr < 0 && padZone.contains (x, y) && !inPic (x, y)) { rpPtr = id; fb[id] = 0; what = "the right stick"; }
			else if (tog.contains (x, y)) {               /* (the stick / D-pad switch) */
				final boolean d = !dpad (); m.prefs.edit ().putBoolean ("touch.dpad", d).apply (); padDir = 0;
				performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY); relayout (); what = d ? "switched to the D-pad" : "switched to the stick"; }
			else if (stickPtr < 0 && stickZone.contains (x, y) && !inPic (x, y)) { stickPtr = id;
				if (dpad ()) { move (id, x, y); what = "D-pad"; } else { scx = kx = x; scy = ky = y; what = "stick"; } invalidate (); } }
		if (what.equals ("nothing") && picPtr < 0 && inPic (x, y)) { picPtr = id; pointer (true, x, y); what = "picture"; }
		Main.log (String.format (java.util.Locale.ROOT, "touch down %.0f,%.0f: %s", x, y, what));
		if (id == rpPtr && what.equals ("the right stick")) move (id, x, y);   /* (its first direction: logged after the touch) */
	}
	void move (int id, float x, float y)
	{
		if (id == picPtr) pointer (true, x, y);
		if (id >= 0 && id < fb.length && fb[id] >= 0) {           /* (a right-side finger: its buttons again - a slide, the stick's way) */
			int h;
			if (id == rpPtr) { final int dir = dir8 (x - bcx, y - bcy, BR); final String mp = rmap (m, stem ()); h = 0;
				if ((dir & 1) != 0) h |= 1 << mp.charAt (0) - '1'; if ((dir & 8) != 0) h |= 1 << mp.charAt (1) - '1';
				if ((dir & 2) != 0) h |= 1 << mp.charAt (2) - '1'; if ((dir & 4) != 0) h |= 1 << mp.charAt (3) - '1';
				if (dir != rpDir) { rpDir = dir; invalidate (); } }
			else h = hits (x, y);
			if (h != fb[id]) { fb[id] = h; btns (); if (h != 0) performHapticFeedback (HapticFeedbackConstants.CLOCK_TICK); invalidate ();
				Main.log (String.format (java.util.Locale.ROOT, "touch move %.0f,%.0f: buttons %s%s", x, y, names (h), id == rpPtr ? " (the right stick)" : "")); } }
		if (id == stickPtr) {
			float dx = x - scx, dy = y - scy, d = (float) Math.hypot (dx, dy), ax, ay;
			if (dpad ()) {                                       /* (up / down / left / right clearly; a diagonal only within 15 degrees of
			                                                        *  the exact corner; nothing in the middle) */
				final int dir = dir8 (dx, dy, R);
				if (dir != padDir) { padDir = dir; if (dir != 0) performHapticFeedback (HapticFeedbackConstants.CLOCK_TICK); }
				ax = (dir & 8) != 0 ? 1 : (dir & 4) != 0 ? -1 : 0; ay = (dir & 2) != 0 ? 1 : (dir & 1) != 0 ? -1 : 0;
			} else { if (d > R) { dx *= R / d; dy *= R / d; } kx = scx + dx; ky = scy + dy; ax = dx / R; ay = dy / R; }
			int b = tbtn & ~(0xf << 4);
			if (ay < -0.4f) b |= 1 << 4; if (ay > 0.4f) b |= 1 << 5; if (ax < -0.4f) b |= 1 << 6; if (ax > 0.4f) b |= 1 << 7;
			tbtn = b; tax = (int) (ax * 32767); tay = (int) (ay * 32767); send (); invalidate (); }
	}
	void up (int id, float x, float y)
	{
		if (id == picPtr) { picPtr = -1; pointer (false, x, y); }
		if (id == stickPtr) { stickPtr = -1; padDir = 0; tbtn &= ~(0xf << 4); tax = tay = 0; send (); relayout (); }
		if (id == togPtr) { togPtr = -1; removeCallbacks (togHold); if (!togLong) flip (); }
		if (id >= 0 && id < fb.length && fb[id] >= 0) { fb[id] = -1; if (id == rpPtr) { rpPtr = -1; rpDir = 0; } btns (); invalidate (); }
	}
	int held;                                                   /* (buttons held for the player a while: a UVMC2 game's calibration) */
	final Runnable letGo = () -> { held = 0; send (); android.util.Log.i ("VectrexEMU", "buttons let go"); };
	void holdFor (int mask, long ms) { removeCallbacks (letGo); held = mask; send (); android.util.Log.i ("VectrexEMU", "buttons held " + mask + " for " + ms + " ms"); postDelayed (letGo, ms); }   /* (again within the time: the full time again) */
	void send () { VX.setTouchPad (tbtn | held, tax, tay); }
}
