/* DeckView.java - the control deck below the picture, as the iOS app's (ios/App/VXUI.m VXDeck): rounded panels of one
 * width whatever they show - the figures (FPS, Hz, the emulation rate coloured; CPU and GPU bars), port 2's device
 * (controller, VecVox, VecVoice, 3D Imager, light pen - online: your port), the cartridge (OUT), RESET - and the volume
 * as a knob (full) or a bar (slim); a long press on the volume switches the Vectrex off and on. Online the figures give
 * way to the players by port and the round trip; a player's row tapped takes that port. Full 58 dp, slim 40 dp; pop-up:
 * the slim deck over the picture's foot for a while after its tab is tapped. Upright only (sideways: the picture).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import org.json.JSONArray;
import org.json.JSONObject;
import java.util.Locale;

final class DeckView extends View {
	final Main m; int mode = 1;                                  /* (1 full, 2 slim) */
	final Paint pan = new Paint (Paint.ANTI_ALIAS_FLAG), edge = new Paint (Paint.ANTI_ALIAS_FLAG), txt = new Paint (Paint.ANTI_ALIAS_FLAG), ln = new Paint (Paint.ANTI_ALIAS_FLAG);
	final RectF rs = new RectF (), rd = new RectF (), rc = new RectF (), rr = new RectF (), rv = new RectF ();
	String fps = "--", hz = "--", emu = "--"; int cpu = -1, gpu = -1, port2, cartOut, power = 1, paused, vol = 100;
	JSONObject st = new JSONObject (); long stT; boolean online; int role, myPort;
	float d;                                                     /* (dp) */
	DeckView (Main m)
	{
		super (m); this.m = m; d = m.dp (1);
		edge.setStyle (Paint.Style.STROKE); edge.setStrokeWidth (d); ln.setStyle (Paint.Style.STROKE); ln.setStrokeCap (Paint.Cap.ROUND); ln.setStrokeJoin (Paint.Join.ROUND);
		txt.setTypeface (Typeface.create (Typeface.MONOSPACE, Typeface.BOLD)); txt.setTextAlign (Paint.Align.CENTER);
		setContentDescription ("Control deck");
	}
	/* each half second: the core's states, the figures, online */
	void tick ()
	{
		if (getVisibility () != VISIBLE) return;
		try { String[] o = VX.osd ().split ("\\|"); fps = o[0]; hz = o[1]; emu = o[2]; cpu = Integer.parseInt (o[3]); gpu = Integer.parseInt (o[4]); } catch (Exception e) { }
		int[] s = VX.deckState (); port2 = s[0]; cartOut = s[1]; power = s[2]; paused = s[3]; vol = s[4];
		long now = System.currentTimeMillis (); if (now - stT > 450) { stT = now; st = Online.status (); }
		int ph = st.optInt ("phase"); role = st.optInt ("role"); boolean hosting = role == 1 && ph >= 1 && ph <= 3; online = Online.playing || hosting;
		JSONArray pt = st.optJSONArray ("ports"); myPort = pt != null && pt.length () == 2 ? pt.optInt (role == 1 ? 0 : 1) : 0; if (myPort == 0) myPort = role == 1 ? 1 : 2;
		invalidate ();
	}
	/* the panels' places: full - the figures a third, three buttons alike, the knob 50 dp; slim - the figures, three 58 dp buttons, the volume bar the rest */
	void place ()
	{
		final float W = getWidth (), H = getHeight (), mg = 8 * d, gap = (mode == 1 ? 6 : 5) * d, ph = (mode == 1 ? 46 : 30) * d, y = (H - ph) / 2;
		if (mode == 1) { float sw = (float) Math.floor ((W - 2 * mg) * 0.33f), kw = 50 * d, bw = (float) Math.floor ((W - 2 * mg - sw - kw - 4 * gap) / 3);
			rs.set (mg, y, mg + sw, y + ph); float x = mg + sw + gap;
			rd.set (x, y, x + bw, y + ph); x += bw + gap; rc.set (x, y, x + bw, y + ph); x += bw + gap; rr.set (x, y, x + bw, y + ph); x += bw + gap; rv.set (x, y, W - mg, y + ph); }
		else { float sw = Math.min (170 * d, (float) Math.floor ((W - 2 * mg) * 0.29f)), bw = 58 * d;
			rs.set (mg, y, mg + sw, y + ph); float x = mg + sw + gap;
			rd.set (x, y, x + bw, y + ph); x += bw + gap; rc.set (x, y, x + bw, y + ph); x += bw + gap; rr.set (x, y, x + bw, y + ph); x += bw + gap; rv.set (x, y, W - mg, y + ph); }
	}
	void panel (Canvas c, RectF r, boolean on)
	{
		pan.setColor (on ? Main.SEL : Main.PANEL); c.drawRoundRect (r, 10 * d, 10 * d, pan);
		edge.setColor (on ? Main.EDGE2 : Main.EDGE); c.drawRoundRect (r, 10 * d, 10 * d, edge);
	}
	void text (Canvas c, String s, float x, float y, float sp, int col, Paint.Align a)
	{
		txt.setTextSize (sp * d); txt.setColor (col); txt.setTextAlign (a); c.drawText (s, x, y - (txt.descent () + txt.ascent ()) / 2, txt);
	}
	/* a word sized to its panel */
	void word (Canvas c, String s, RectF r, float y, float sp, int col)
	{
		txt.setTextSize (sp * d); float w = txt.measureText (s), max = r.width () - 6 * d; if (w > max) txt.setTextSize (sp * d * max / w);
		txt.setColor (col); txt.setTextAlign (Paint.Align.CENTER); c.drawText (s, r.centerX (), y - (txt.descent () + txt.ascent ()) / 2, txt);
	}
	@Override protected void onDraw (Canvas c)
	{
		place (); final boolean full = mode == 1;
		/* the figures, or online the players */
		panel (c, rs, false);
		if (online) {
			String me = st.optString ("me", "").toUpperCase (Locale.ROOT), peer = st.optString ("peer", "").toUpperCase (Locale.ROOT);
			if (me.isEmpty ()) me = "YOU"; if (peer.isEmpty ()) peer = role == 1 ? "PLAYER 2" : "PLAYER 1";
			int ph = st.optInt ("phase"); boolean hosting = role == 1 && ph >= 1 && ph <= 3;
			JSONArray pt = st.optJSONArray ("ports"); int hp = pt != null ? pt.optInt (0, 1) : 1, jp = pt != null ? pt.optInt (1, 2) : 2;
			String host = role == 1 ? me : peer, join = role == 1 ? peer : me, a1 = hosting ? me : hp == 1 ? host : jp == 1 ? join : null, a2 = hosting ? null : hp == 2 ? host : jp == 2 ? join : null;
			float x0 = rs.left + 9 * d, yc = rs.centerY ();
			text (c, "1 " + (a1 != null ? a1 : "FREE"), x0, yc - 7 * d, 10, myPort == 1 ? Main.HI2 : a1 != null ? Main.HI : Main.DIM, Paint.Align.LEFT);
			text (c, "2 " + (a2 != null ? a2 : "FREE"), x0, yc + 7 * d, 10, myPort == 2 ? Main.HI2 : a2 != null ? Main.HI : Main.DIM, Paint.Align.LEFT);
			String r1, r2; int rc1 = Main.DIM;
			if (hosting) { r1 = "WAITING"; r2 = ""; rc1 = Main.CHG; }
			else if (st.optInt ("lost") != 0) { r1 = "NO"; r2 = "CONNECTION"; rc1 = Main.CHG; }
			else { r1 = st.optInt ("relay") != 0 ? "RELAY" : ""; int rtt = st.optInt ("rtt"); r2 = rtt > 0 ? rtt + " MS" : ""; }
			text (c, r1, rs.right - 9 * d, yc - 7 * d, 9, rc1, Paint.Align.RIGHT); text (c, r2, rs.right - 9 * d, yc + 7 * d, 9, rc1, Paint.Align.RIGHT);
		} else if (paused != 0 && power != 0) word (c, "PAUSED", rs, rs.centerY (), 13, Main.CHG);
		else {
			int pct = 100; try { pct = Integer.parseInt (emu.replaceAll ("[^0-9]", "")); } catch (Exception e) { }
			int ec = !Character.isDigit (emu.isEmpty () ? 'x' : emu.charAt (0)) || pct >= 97 ? Main.G : pct >= 80 ? Main.CHG : 0xffed5454;
			String f = power != 0 ? fps + "  " + hz + "  " : "-- INT  -- FPS  ", e = power != 0 ? emu : "--";   /* (the core's figures carry their units: "50 INT", "60 FPS") */
			float y0 = full ? rs.top + 14 * d : rs.centerY (), sp = full ? 12 : 11;
			txt.setTextSize (sp * d); float w1 = txt.measureText (f), w2 = txt.measureText (e), k = Math.min (1f, (rs.width () - 12 * d) / (w1 + w2));
			txt.setTextSize (sp * d * k); w1 = txt.measureText (f); w2 = txt.measureText (e); float x = rs.centerX () - (w1 + w2) / 2;
			txt.setColor (power != 0 ? Main.HI : 0xff4a6658); txt.setTextAlign (Paint.Align.LEFT); float by = y0 - (txt.descent () + txt.ascent ()) / 2;
			c.drawText (f, x, by, txt); txt.setColor (power != 0 ? ec : 0xff4a6658); c.drawText (e, x + w1, by, txt);
			if (full) {                                          /* (the CPU and GPU bars) */
				float by2 = rs.top + 32 * d, lw = 22 * d, bw = (rs.width () - 16 * d - 2 * lw - 16 * d) / 2, x1 = rs.left + 8 * d;
				text (c, "CPU", x1, by2, 8, Main.DIM, Paint.Align.LEFT); bar (c, x1 + lw, by2, bw, power != 0 ? cpu : 0);
				float x2 = x1 + lw + bw + 8 * d; text (c, "GPU", x2, by2, 8, Main.DIM, Paint.Align.LEFT); bar (c, x2 + lw, by2, bw, power != 0 ? gpu : 0);
			}
		}
		/* port 2's device - online: your badge */
		panel (c, rd, online);
		String[][] W = { { "P2", "VOX", "VOICE", "3D", "PEN" }, { "P2", "VECVOX", "VECVOICE", "3D IMAGER", "LIGHT PEN" } };
		int kk = port2 >= 0 && port2 <= 4 ? port2 : 0;
		String dw = online ? (full ? "ONLINE P" + myPort : "P" + myPort) : W[full ? 1 : 0][kk];
		iconWord (c, rd, online ? 5 : kk, dw, online ? Main.HI2 : Main.HI);
		/* the cartridge */
		panel (c, rc, false); iconWord (c, rc, 6, cartOut != 0 ? "OUT" : "CART", cartOut != 0 ? 0xff4a6658 : Main.HI);
		/* RESET: full - icon and word; slim - the word */
		panel (c, rr, false); if (full) iconWord (c, rr, 7, "RESET", Main.HI); else word (c, "RESET", rr, rr.centerY (), 11, Main.HI);
		/* the volume: the knob, or the bar */
		panel (c, rv, false);
		if (full) knob (c); else volBar (c);
	}
	void bar (Canvas c, float x, float y, float w, int pct)
	{
		pan.setColor (0xff16302a); RectF t = new RectF (x, y - 2 * d, x + w, y + 2 * d); c.drawRoundRect (t, 2 * d, 2 * d, pan);
		pan.setColor (Main.G); t.right = x + w * Math.max (0, Math.min (100, pct)) / 100f; if (t.width () > 0) c.drawRoundRect (t, 2 * d, 2 * d, pan);
	}
	/* a panel's icon and word: full - the icon above the word; slim - beside it */
	void iconWord (Canvas c, RectF r, int icon, String w, int col)
	{
		if (mode == 1) { icon (c, icon, r.centerX (), r.top + 15 * d, 15 * d, col); word (c, w, r, r.top + 33 * d, 9, col); }
		else { txt.setTextSize (9 * d); float tw = Math.min (txt.measureText (w), r.width () - 30 * d), iw = 16 * d, x = r.centerX () - (iw + 4 * d + tw) / 2;
			icon (c, icon, x + iw / 2, r.centerY (), 13 * d, col);
			txt.setColor (col); txt.setTextAlign (Paint.Align.LEFT); float sc = tw < txt.measureText (w) ? tw / txt.measureText (w) : 1; txt.setTextSize (9 * d * sc);
			c.drawText (w, x + iw + 4 * d, r.centerY () - (txt.descent () + txt.ascent ()) / 2, txt); }
	}
	/* the icons, drawn (0 controller, 1 VecVox, 2 VecVoice, 3 3D Imager, 4 light pen, 5 online, 6 cartridge, 7 RESET) */
	void icon (Canvas c, int k, float cx, float cy, float s, int col)
	{
		ln.setColor (col); ln.setStrokeWidth (s * 0.11f); pan.setColor (col); float h = s / 2;
		Path p = new Path ();
		switch (k) {
		case 0: { RectF b = new RectF (cx - h, cy - h * 0.55f, cx + h, cy + h * 0.55f); c.drawRoundRect (b, h * 0.45f, h * 0.45f, ln);
			c.drawLine (cx - h * 0.62f, cy, cx - h * 0.22f, cy, ln); c.drawLine (cx - h * 0.42f, cy - h * 0.2f, cx - h * 0.42f, cy + h * 0.2f, ln);
			c.drawCircle (cx + h * 0.35f, cy - h * 0.1f, s * 0.06f, pan); c.drawCircle (cx + h * 0.58f, cy + h * 0.12f, s * 0.06f, pan); break; }
		case 1: { c.drawCircle (cx - h * 0.35f, cy - h * 0.25f, h * 0.28f, ln);   /* (a head with sound waves: VecVox, the elaborate voice) */
			p.moveTo (cx - h * 0.85f, cy + h * 0.75f); p.quadTo (cx - h * 0.35f, cy + h * 0.05f, cx + h * 0.15f, cy + h * 0.75f); c.drawPath (p, ln);
			RectF a1 = new RectF (cx + h * 0.05f, cy - h * 0.6f, cx + h * 0.55f, cy + h * 0.1f), a2 = new RectF (cx + h * 0.05f, cy - h * 0.9f, cx + h * 0.95f, cy + h * 0.4f);
			c.drawArc (a1, -50, 100, false, ln); c.drawArc (a2, -50, 100, false, ln); break; }
		case 2: { float q = s / 24f, ox = cx - 12 * q, oy = cy - 12 * q;          /* (VecVoice's robot, as the iOS app's) */
			c.drawRoundRect (new RectF (ox + 5 * q, oy + 7 * q, ox + 19 * q, oy + 17 * q), 2.5f * q, 2.5f * q, ln);
			c.drawLine (ox + 12 * q, oy + 7 * q, ox + 12 * q, oy + 4 * q, ln); c.drawCircle (ox + 12 * q, oy + 3.2f * q, 1.4f * q, pan);
			c.drawCircle (ox + 9.5f * q, oy + 11.3f * q, 1.3f * q, pan); c.drawCircle (ox + 14.5f * q, oy + 11.3f * q, 1.3f * q, pan);
			c.drawLine (ox + 9.5f * q, oy + 14.6f * q, ox + 14.5f * q, oy + 14.6f * q, ln);
			c.drawLine (ox + 8 * q, oy + 17 * q, ox + 8 * q, oy + 21 * q, ln); c.drawLine (ox + 16 * q, oy + 17 * q, ox + 16 * q, oy + 21 * q, ln); break; }
		case 3: { c.drawCircle (cx - h * 0.5f, cy + h * 0.1f, h * 0.38f, ln); c.drawCircle (cx + h * 0.5f, cy + h * 0.1f, h * 0.38f, ln);   /* (eyeglasses: the 3D Imager) */
			p.moveTo (cx - h * 0.14f, cy); p.quadTo (cx, cy - h * 0.18f, cx + h * 0.14f, cy); c.drawPath (p, ln);
			c.drawLine (cx - h * 0.88f, cy, cx - h * 1.0f, cy - h * 0.45f, ln); c.drawLine (cx + h * 0.88f, cy, cx + h * 1.0f, cy - h * 0.45f, ln); break; }
		case 4: { c.drawLine (cx - h * 0.55f, cy + h * 0.75f, cx + h * 0.6f, cy - h * 0.7f, ln);   /* (the light pen) */
			c.drawLine (cx - h * 0.4f, cy + h * 0.85f, cx - h * 0.8f, cy + h * 0.95f, ln); c.drawCircle (cx + h * 0.65f, cy - h * 0.75f, s * 0.07f, pan); break; }
		case 5: { c.drawCircle (cx, cy, h * 0.9f, ln); c.drawOval (new RectF (cx - h * 0.38f, cy - h * 0.9f, cx + h * 0.38f, cy + h * 0.9f), ln);   /* (online: a globe) */
			c.drawLine (cx - h * 0.9f, cy, cx + h * 0.9f, cy, ln); break; }
		case 6: { RectF b = new RectF (cx - h * 0.75f, cy - h * 0.85f, cx + h * 0.75f, cy + h * 0.85f); c.drawRoundRect (b, h * 0.12f, h * 0.12f, ln);   /* (the cartridge) */
			c.drawRect (new RectF (cx - h * 0.45f, cy - h * 0.5f, cx + h * 0.45f, cy + h * 0.15f), ln);
			c.drawLine (cx - h * 0.45f, cy + h * 0.5f, cx + h * 0.45f, cy + h * 0.5f, ln); break; }
		case 7: { RectF a = new RectF (cx - h * 0.75f, cy - h * 0.75f, cx + h * 0.75f, cy + h * 0.75f); c.drawArc (a, -60, 300, false, ln);   /* (RESET: a turning arrow) */
			double t = Math.toRadians (-60); float ex = cx + (float) Math.cos (t) * h * 0.75f, ey = cy + (float) Math.sin (t) * h * 0.75f;
			p.moveTo (ex - h * 0.42f, ey - h * 0.05f); p.lineTo (ex, ey); p.lineTo (ex + h * 0.05f, ey + h * 0.42f); c.drawPath (p, ln); break; }
		case 8: { p.moveTo (cx - h * 0.8f, cy - h * 0.3f); p.lineTo (cx - h * 0.4f, cy - h * 0.3f); p.lineTo (cx + h * 0.1f, cy - h * 0.75f); p.lineTo (cx + h * 0.1f, cy + h * 0.75f);   /* (a speaker) */
			p.lineTo (cx - h * 0.4f, cy + h * 0.3f); p.lineTo (cx - h * 0.8f, cy + h * 0.3f); p.close (); c.drawPath (p, ln);
			c.drawArc (new RectF (cx - h * 0.1f, cy - h * 0.45f, cx + h * 0.6f, cy + h * 0.45f), -50, 100, false, ln); break; }
		case 10: { float g = h * 0.12f, q = (s - g) / 2 - h * 0.1f;                  /* (GAMES: a grid) */
			for (int i = 0; i < 4; i++) { float x0 = cx - h * 0.9f + (i % 2) * (q + g), y0 = cy - h * 0.9f + (i / 2) * (q + g); c.drawRoundRect (new RectF (x0, y0, x0 + q, y0 + q), q * 0.22f, q * 0.22f, ln); } break; }
		case 11: case 12: {                                         /* (SAVE: an arrow into the tray; LOAD: out of it) */
			p.moveTo (cx - h * 0.85f, cy + h * 0.15f); p.lineTo (cx - h * 0.85f, cy + h * 0.85f); p.lineTo (cx + h * 0.85f, cy + h * 0.85f); p.lineTo (cx + h * 0.85f, cy + h * 0.15f); c.drawPath (p, ln);
			c.drawLine (cx, cy - h * 0.95f, cx, cy + h * 0.45f, ln); Path a = new Path ();
			if (k == 11) { a.moveTo (cx - h * 0.4f, cy + h * 0.05f); a.lineTo (cx, cy + h * 0.45f); a.lineTo (cx + h * 0.4f, cy + h * 0.05f); }
			else { a.moveTo (cx - h * 0.4f, cy - h * 0.55f); a.lineTo (cx, cy - h * 0.95f); a.lineTo (cx + h * 0.4f, cy - h * 0.55f); }
			c.drawPath (a, ln); break; }
		case 13: { c.drawRoundRect (new RectF (cx - h * 0.95f, cy - h * 0.35f, cx + h * 0.45f, cy + h * 0.75f), h * 0.15f, h * 0.15f, ln);   /* (OVERLAY: two sheets) */
			p.moveTo (cx - h * 0.55f, cy - h * 0.35f); p.lineTo (cx - h * 0.55f, cy - h * 0.75f); p.lineTo (cx + h * 0.95f, cy - h * 0.75f); p.lineTo (cx + h * 0.95f, cy + h * 0.35f); p.lineTo (cx + h * 0.45f, cy + h * 0.35f); c.drawPath (p, ln); break; }
		case 14: { RectF b = new RectF (cx - h * 0.6f, cy - h * 0.95f, cx + h * 0.6f, cy + h * 0.25f); c.drawArc (b, 135, 270, false, ln);   /* (ROOM: a light bulb) */
			c.drawLine (cx - h * 0.42f, cy + h * 0.12f, cx - h * 0.3f, cy + h * 0.45f, ln); c.drawLine (cx + h * 0.42f, cy + h * 0.12f, cx + h * 0.3f, cy + h * 0.45f, ln);
			c.drawLine (cx - h * 0.3f, cy + h * 0.55f, cx + h * 0.3f, cy + h * 0.55f, ln); c.drawLine (cx - h * 0.2f, cy + h * 0.8f, cx + h * 0.2f, cy + h * 0.8f, ln); break; }
		case 15: { for (int i = -1; i <= 1; i++) c.drawLine (cx - h * 0.8f, cy + i * h * 0.55f, cx + h * 0.8f, cy + i * h * 0.55f, ln); break; }   /* (MENU: three lines) */
		case 9: { c.drawArc (new RectF (cx - h * 0.75f, cy - h * 0.7f, cx + h * 0.75f, cy + h * 0.8f), -50, 280, false, ln); c.drawLine (cx, cy - h * 0.85f, cx, cy, ln); break; }   /* (power) */
		}
	}
	/* the knob: its track, the arc of the volume, the value in the middle (OFF when off) */
	void knob (Canvas c)
	{
		float r = 18 * d, cx = rv.centerX (), cy = rv.centerY (); RectF a = new RectF (cx - r, cy - r, cx + r, cy + r);
		ln.setStrokeWidth (4 * d); ln.setColor (0xff16302a); c.drawArc (a, 135, 270, false, ln);
		if (power != 0) { ln.setColor (Main.G); c.drawArc (a, 135, 270 * Math.max (0, Math.min (100, vol)) / 100f, false, ln); }
		ln.setStrokeWidth (1.5f * d); ln.setColor (Main.EDGE2); c.drawCircle (cx, cy, r * 0.62f, ln);
		text (c, power != 0 ? String.valueOf (vol) : "OFF", cx, cy, 9, power != 0 ? Main.HI : Main.CHG, Paint.Align.CENTER);
	}
	void volBar (Canvas c)
	{
		final boolean off = power == 0; icon (c, off ? 9 : 8, rv.left + 17 * d, rv.centerY (), 14 * d, off ? Main.CHG : Main.HI);
		float x = rv.left + 32 * d, w = Math.max (10 * d, rv.width () - 32 * d - 42 * d), y = rv.centerY ();
		pan.setColor (0xff16302a); c.drawRoundRect (new RectF (x, y - 2.5f * d, x + w, y + 2.5f * d), 2.5f * d, 2.5f * d, pan);
		if (!off) { pan.setColor (Main.G); c.drawRoundRect (new RectF (x, y - 2.5f * d, x + w * Math.max (0, Math.min (100, vol)) / 100f, y + 2.5f * d), 2.5f * d, 2.5f * d, pan); }
		text (c, off ? "OFF" : String.valueOf (vol), rv.right - 9 * d, y, 11, off ? Main.CHG : Main.HI, Paint.Align.RIGHT);
	}
	/* touches: a panel tapped; the volume dragged (the knob turned, the bar set); a long press on the volume: power */
	float downX, downY, lastAng; boolean volDrag; long downT; final Runnable longPress = () -> { if (volDrag) { volDrag = false; VX.act (3, 0); performHapticFeedback (HapticFeedbackConstants.LONG_PRESS); Main.log ("deck: power"); } };
	@Override public boolean onTouchEvent (MotionEvent e)
	{
		final float x = e.getX (), y = e.getY ();
		switch (e.getActionMasked ()) {
		case MotionEvent.ACTION_DOWN:
			downX = x; downY = y; downT = System.currentTimeMillis (); m.deckPoked ();
			if (rv.contains (x, y)) { volDrag = true; lastAng = ang (x, y); if (mode != 1) setBar (x); postDelayed (longPress, 600); }
			return true;
		case MotionEvent.ACTION_MOVE:
			if (volDrag) { if (Math.hypot (x - downX, y - downY) > 8 * d) removeCallbacks (longPress);
				if (mode != 1) setBar (x); else { float a = ang (x, y), da = a - lastAng; if (da > 180) da -= 360; if (da < -180) da += 360; lastAng = a; if (power != 0) { VX.act (4, da / 270.0); vol = Math.max (0, Math.min (100, vol + Math.round (da / 2.7f))); invalidate (); } } }
			return true;
		case MotionEvent.ACTION_UP:
			removeCallbacks (longPress);
			if (volDrag) { volDrag = false; Main.log ("deck: volume " + vol + " %"); return true; }
			if (Math.hypot (x - downX, y - downY) < 12 * d) tap (x, y);
			return true;
		case MotionEvent.ACTION_CANCEL: removeCallbacks (longPress); volDrag = false; return true;
		}
		return false;
	}
	float ang (float x, float y) { return (float) Math.toDegrees (Math.atan2 (y - rv.centerY (), x - rv.centerX ())); }
	void setBar (float x)
	{
		if (power == 0) return; float x0 = rv.left + 32 * d, w = Math.max (10 * d, rv.width () - 32 * d - 42 * d);
		int want = Math.round (100 * Math.max (0, Math.min (1, (x - x0) / w))); if (want != vol) { VX.act (4, (want - vol) / 100.0); vol = want; invalidate (); }
	}
	void tap (float x, float y)
	{
		performHapticFeedback (HapticFeedbackConstants.VIRTUAL_KEY);
		if (rs.contains (x, y)) { if (online && Online.playing) { int k = y < rs.centerY () ? 1 : 2; VX.net (9, k); m.toast ("ONLINE: YOU TAKE PORT " + k, 2); Main.log ("deck: take port " + k); } return; }
		if (rd.contains (x, y)) {
			if (online) { String code = st.optString ("code", ""); if (role == 1 && st.optInt ("phase") < 3 && !code.isEmpty ()) { m.toast ("ONLINE: HOSTING - CODE " + code, 3); return; } Online.show (m); return; }
			int i = VX.ofind ("vectrex_emu_port2"); if (i >= 0 && m.stack.isEmpty ()) Screens.choice (m, i); return; }
		if (rc.contains (x, y)) { Main.log ("deck: cartridge - the games"); Library.show (m); return; }
		if (rr.contains (x, y)) { Main.log ("deck: RESET"); VX.act (1, 0); }
	}
}
