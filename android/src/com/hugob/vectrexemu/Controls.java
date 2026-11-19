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
	final RectF[] btn = new RectF[4]; final int[] btnPtr = { -1, -1, -1, -1 };
	RectF stickZone = new RectF (); float scx, scy, kx, ky, R = 60; int stickPtr = -1, picPtr = -1; int tbtn; int tax, tay;
	static final int[] ID4 = { 1, 9, 0, 8 };                      /* (the Vectrex's 1-4: the RetroPad's Y X B A) */
	Controls (Main m)
	{
		super (m); this.m = m; for (int i = 0; i < 4; i++) btn[i] = new RectF ();
		edge.setStyle (Paint.Style.STROKE); edge.setStrokeWidth (m.dp (1.5f)); txt.setTextAlign (Paint.Align.CENTER); txt.setColor (Main.HI); txt.setTypeface (Typeface.DEFAULT_BOLD);
	}
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
		float r = Math.min (Math.min (zr.width () / 4.6f, zr.height () / 4.4f), m.dp (38) * tsize), gap = r * 0.35f, cx = zr.centerX (), cy = zr.centerY ();
		for (int i = 0; i < 4; i++) { float bx = cx + (i % 2 == 1 ? r + gap / 2 : -r - gap / 2), by = cy + (i < 2 ? -r - gap / 2 : r + gap / 2); btn[i].set (bx - r, by - r, bx + r, by + r); }
		if (stickPtr < 0) { scx = kx = zl.centerX (); scy = ky = zl.centerY (); }
		txt.setTextSize (Math.max (m.dp (14), r * 0.75f)); invalidate ();
	}
	@Override protected void onSizeChanged (int w, int h, int ow, int oh) { relayout (); }
	@Override protected void onDraw (Canvas c)
	{
		if (!show) return;
		fill.setColor (0x990f1c17); edge.setColor (Main.EDGE2); c.drawCircle (scx, scy, R, fill); c.drawCircle (scx, scy, R, edge);
		fill.setColor (0xff16302a); edge.setColor (Main.G); c.drawCircle (kx, ky, R * 0.43f, fill); c.drawCircle (kx, ky, R * 0.43f, edge);
		for (int i = 0; i < 4; i++) { fill.setColor ((tbtn >> ID4[i] & 1) != 0 ? 0xe624443a : 0x990f1c17); c.drawOval (btn[i], fill); c.drawOval (btn[i], edge);
			c.drawText (String.valueOf (i + 1), btn[i].centerX (), btn[i].centerY () - (txt.descent () + txt.ascent ()) / 2, txt); }
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
	void down (int id, float x, float y)
	{
		String what = "nothing";
		if (shown ()) {
			int hit = -1; for (int i = 0; i < 4; i++) { RectF r = new RectF (btn[i]); r.inset (-m.dp (8), -m.dp (8)); if (r.contains (x, y)) hit = i; }
			if (hit >= 0 && btnPtr[hit] < 0) { btnPtr[hit] = id; tbtn |= 1 << ID4[hit]; send (); performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY); what = "button " + (hit + 1); invalidate (); }
			else if (stickPtr < 0 && stickZone.contains (x, y) && !inPic (x, y)) { stickPtr = id; scx = kx = x; scy = ky = y; what = "stick"; invalidate (); } }
		if (what.equals ("nothing") && picPtr < 0 && inPic (x, y)) { picPtr = id; pointer (true, x, y); what = "picture"; }
		Main.log (String.format (java.util.Locale.ROOT, "touch down %.0f,%.0f: %s", x, y, what));
	}
	void move (int id, float x, float y)
	{
		if (id == picPtr) pointer (true, x, y);
		if (id == stickPtr) {
			float dx = x - scx, dy = y - scy, d = (float) Math.hypot (dx, dy); if (d > R) { dx *= R / d; dy *= R / d; }
			kx = scx + dx; ky = scy + dy; float ax = dx / R, ay = dy / R; int b = tbtn & ~(0xf << 4);
			if (ay < -0.4f) b |= 1 << 4; if (ay > 0.4f) b |= 1 << 5; if (ax < -0.4f) b |= 1 << 6; if (ax > 0.4f) b |= 1 << 7;
			tbtn = b; tax = (int) (ax * 32767); tay = (int) (ay * 32767); send (); invalidate (); }
	}
	void up (int id, float x, float y)
	{
		if (id == picPtr) { picPtr = -1; pointer (false, x, y); }
		if (id == stickPtr) { stickPtr = -1; tbtn &= ~(0xf << 4); tax = tay = 0; send (); relayout (); }
		for (int i = 0; i < 4; i++) if (btnPtr[i] == id) { btnPtr[i] = -1; tbtn &= ~(1 << ID4[i]); send (); invalidate (); }
	}
	void send () { VX.setTouchPad (tbtn, tax, tay); }
}
