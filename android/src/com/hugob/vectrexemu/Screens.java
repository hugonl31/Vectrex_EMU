/* Screens.java - the screens over the game, as the iOS app's (ios/App/VXUI.m): the game menu (save states, the games,
 * RESET, the overlay, the room light, all settings, online, the Vectrex's hardware), the settings (the app's own and every
 * core setting in its submenu, each value with its help), a setting's values, the controller's buttons. Plain views,
 * the PC app's colours; every row reachable with a controller (the highlight), button 4 picks, button 3 goes back.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.app.AlertDialog;
import java.io.File;

final class Screens {
	interface Closing { void closed (); }
	/* a screen: a header (its title, a close button) and a scrolled column; the content no wider than a tablet's sheet */
	static class Panel extends LinearLayout implements Closing {
		final LinearLayout col; final ScrollView sc; Runnable onClose; final Main m;
		Panel (Main m, String title) {
			super (m); this.m = m; setOrientation (VERTICAL); setBackgroundColor (Main.BG); setClickable (true);
			LinearLayout head = new LinearLayout (m); head.setOrientation (HORIZONTAL); head.setGravity (Gravity.CENTER_VERTICAL); head.setBackgroundColor (Main.BAR);
			head.setPadding ((int) m.dp (16) + m.insL, (int) m.dp (8) + m.insTop, (int) m.dp (8) + m.insR, (int) m.dp (8));
			TextView t = m.text (title, 22, Main.HI, true); head.addView (t, new LinearLayout.LayoutParams (0, -2, 1));
			TextView x = button (m, "✕", false); x.setContentDescription ("Close"); x.setOnClickListener (v -> m.pop ()); head.addView (x, new LinearLayout.LayoutParams ((int) m.dp (48), (int) m.dp (44)));
			addView (head, new LinearLayout.LayoutParams (-1, -2));
			sc = new ScrollView (m); sc.setFillViewport (true);
			FrameLayout holder = new FrameLayout (m);
			col = new LinearLayout (m); col.setOrientation (VERTICAL); col.setPadding ((int) m.dp (16) + m.insL, (int) m.dp (8), (int) m.dp (16) + m.insR, (int) m.dp (24) + m.insBot);
			holder.addView (col, new FrameLayout.LayoutParams ((int) Math.min (m.root.getWidth (), m.dp (640)), -2, Gravity.CENTER_HORIZONTAL));
			sc.addView (holder); addView (sc, new LinearLayout.LayoutParams (-1, 0, 1));
		}
		@Override public void closed () { if (onClose != null) onClose.run (); }
		TextView header (String s) { TextView t = m.text (s.toUpperCase (), 12, Main.DIM, true); t.setPadding ((int) m.dp (4), (int) m.dp (18), 0, (int) m.dp (6)); col.addView (t); return t; }
		TextView foot (String s) { TextView t = m.text (s, 12, Main.DIM, false); t.setPadding ((int) m.dp (4), (int) m.dp (6), 0, (int) m.dp (4)); col.addView (t); return t; }
		LinearLayout row (String title, String detail, Runnable go) { LinearLayout r = Screens.row (m, title, detail, go); LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (-1, -2); lp.bottomMargin = (int) m.dp (6); col.addView (r, lp); return r; }
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
	static LinearLayout row (Main m, String title, String detail, Runnable go)
	{
		LinearLayout r = new LinearLayout (m); r.setOrientation (LinearLayout.VERTICAL); r.setPadding ((int) m.dp (14), (int) m.dp (11), (int) m.dp (14), (int) m.dp (11));
		r.setBackground (states (m, Main.PANEL, Main.EDGE, m.dp (10)));
		TextView t = m.text (title, 16, go != null ? Main.HI : Main.FG, go != null); t.setTag ("t"); r.addView (t);
		if (detail != null && !detail.isEmpty ()) { TextView d = m.text (detail, 13, Main.DIM, false); d.setTag ("d"); r.addView (d); }
		if (go != null) { r.setFocusable (true); r.setClickable (true); r.setOnClickListener (v -> { Main.log ("ui " + title); go.run (); }); }
		return r;
	}
	static TextView button (Main m, String s, boolean on)
	{
		TextView b = m.text (s, 15, on ? Main.HI2 : Main.HI, true); b.setGravity (Gravity.CENTER); b.setPadding ((int) m.dp (10), (int) m.dp (8), (int) m.dp (10), (int) m.dp (8));
		b.setBackground (states (m, on ? Main.SEL : Main.BODY, Main.EDGE2, m.dp (10))); b.setFocusable (true); b.setClickable (true); return b;
	}
	static void back (Main m) { m.pop (); }

	/* ---------------------------------------------------------------- the game menu */
	static void gameMenu (Main m)
	{
		if (!m.stack.isEmpty () || !m.started) return; Main.log ("ui game menu opened");
		String g = new File (VX.game ()).getName (); int dot = g.lastIndexOf ('.'); if (dot > 0) g = g.substring (0, dot);
		Panel p = new Panel (m, g.isEmpty () ? "Vectrex" : g);
		TextView resume = button (m, "Resume", true); resume.setOnClickListener (v -> m.pop ());
		p.col.addView (resume, new LinearLayout.LayoutParams (-1, (int) m.dp (50))); p.foot ("Paused while this menu is open");
		p.header ("Save states");
		final boolean can = VX.stateSupported () != 0 && !Online.playing;
		LinearLayout slots = new LinearLayout (m); slots.setOrientation (LinearLayout.HORIZONTAL);
		for (int s = 1; s <= 3; s++) { final int k = s; LinearLayout c = new LinearLayout (m); c.setOrientation (LinearLayout.VERTICAL);
			TextView sv = button (m, "Save " + s, false), ld = button (m, "Load " + s, false);
			sv.setOnClickListener (v -> { int ok = VX.state (true, k); m.pop (); m.toast (ok != 0 ? "Saved in slot " + k : "Not saved", 2); });
			ld.setOnClickListener (v -> { int ok = VX.state (false, k); m.pop (); m.toast (ok != 0 ? "Slot " + k + " loaded" : "Nothing loaded", 2); });
			sv.setEnabled (can); ld.setEnabled (can && VX.stateExists (s) != 0); if (!sv.isEnabled ()) sv.setAlpha (0.4f); if (!ld.isEnabled ()) ld.setAlpha (0.4f);
			c.addView (sv, new LinearLayout.LayoutParams (-1, (int) m.dp (52))); LinearLayout.LayoutParams l2 = new LinearLayout.LayoutParams (-1, (int) m.dp (52)); l2.topMargin = (int) m.dp (6); c.addView (ld, l2);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, -2, 1); lp.leftMargin = lp.rightMargin = (int) m.dp (4); slots.addView (c, lp); }
		p.col.addView (slots);
		if (!can) p.foot (Online.playing ? "No save states during an online game." : "This game has no save states (UVMC2 and PiTrex games keep their own).");
		p.header ("Play");
		p.row ("Games", "The library: pick a game", () -> { m.pop (); Library.show (m); });
		p.row ("RESET", Online.playing ? "Resets both players' game" : "The Vectrex's RESET button", () -> { VX.act (1, 0); m.pop (); });
		final int ov = VX.ofind ("vectrex_emu_overlay");
		p.row (ov >= 0 && VX.opt (ov, 5).equals ("on") ? "Overlay: on" : "Overlay: off", "The game's overlay", () -> { m.overlayToggle (); m.pop (); });
		final int room = VX.ofind ("vectrex_emu_room");
		if (room >= 0) p.row ("Room light", VX.opt (room, 5), () -> { m.pop (); choice (m, room); });
		p.row ("All settings", "The picture, the overlay, the sound, the controls...", () -> { m.pop (); settings (m); });
		p.row ("Online: play with a friend", Online.playing ? "Playing online now" : "Host or join - phones and PCs together", () -> { m.pop (); Online.show (m); });
		p.header ("Vectrex hardware");
		p.row ("Eject / insert cartridge", "Mine Storm without a cartridge", () -> { VX.act (2, 0); m.pop (); });
		final int p2 = VX.ofind ("vectrex_emu_port2");
		if (p2 >= 0) p.row ("Port 2: " + VX.opt (p2, 5), "The controller port's device", () -> choice (m, p2));
		p.row ("Power off / on", "As the volume knob's switch", () -> { VX.act (3, 0); m.pop (); });
		m.push (p); resume.requestFocus ();
	}

	/* ---------------------------------------------------------------- the settings */
	static void settings (Main m)
	{
		Main.log ("ui settings opened"); Panel p = new Panel (m, "Settings");
		p.header ("App");
		String ts = m.prefs.getString ("touch.show", "auto");
		p.row ("Touch controls", ts.equals ("auto") ? "While no controller is connected" : ts.equals ("always") ? "Always" : "Never", () -> pick (m, "Touch controls", new String[] { "auto", "always", "never" },
			new String[] { "While no controller is connected", "Always", "Never" }, ts, v -> { m.prefs.edit ().putString ("touch.show", v).apply (); m.layout (); reopenSettings (m); }));
		p.row ("Touch controls: size", pct (m.prefs.getFloat ("touch.size", 1f)), () -> pickF (m, "Touch controls: size", "touch.size", new float[] { 0.75f, 0.9f, 1f, 1.15f, 1.3f }, 1f));
		p.row ("Touch controls: opacity", pct (m.prefs.getFloat ("touch.alpha", 1f)), () -> pickF (m, "Touch controls: opacity", "touch.alpha", new float[] { 0.25f, 0.5f, 0.75f, 1f }, 1f));
		p.row ("Left-handed", m.prefs.getBoolean ("touch.lefty", false) ? "On: the stick on the right" : "Off", () -> { m.prefs.edit ().putBoolean ("touch.lefty", !m.prefs.getBoolean ("touch.lefty", false)).apply (); m.layout (); reopenSettings (m); });
		p.row ("Controller buttons", "The Vectrex's buttons 1-4 on your controller", () -> controls (m));
		p.row ("Top bar (upright)", m.barAuto () ? "Hides itself: tap the top to show it" : "Always shown", () -> { m.prefs.edit ().putString ("app.topbar", m.barAuto () ? "fixed" : "auto").apply (); m.bar.setAlpha (1); m.layout (); reopenSettings (m); });
		if (m.hdrCan) p.row ("HDR brightness", pct (m.prefs.getFloat ("app.hdr", 1f)) + " of the screen's headroom (" + String.format (java.util.Locale.ROOT, "%.1fx", m.hdrRoom) + ")" + (VX.hdrActive () != 0 ? "" : " - not active"),
			() -> pickF (m, "HDR brightness", "app.hdr", new float[] { 0f, 0.25f, 0.5f, 0.75f, 1f }, 1f));
		else p.row ("HDR", "This screen shows no HDR", null);
		if (Pack.token (m) != null) p.row ("Downloads", "The games, box art, cartridges, UVMC2, PiTrex", () -> { m.pop (); Pack.choose (m, false); });
		{ int n = 0; for (int i = 0; i < VX.nopts (); i++) if (!VX.opt (i, 5).equals (VX.opt (i, 6))) n++; final int changed = n;
		  LinearLayout r = p.row ("Reset all settings", n > 0 ? n + " changed - all back to their defaults (a long press on one: that one)" : "All at their defaults", () -> {
			if (changed == 0) { m.toast ("All settings are at their defaults", 2); return; }
			pick (m, "Reset all settings?", new String[] { "no", "yes" }, new String[] { "Cancel", "Reset: every setting back to its default" }, "no", v -> {
				if (!v.equals ("yes")) { reopenSettings (m); return; } int k = 0;
				for (int i = 0; i < VX.nopts (); i++) if (!VX.opt (i, 5).equals (VX.opt (i, 6))) { VX.oset (i, VX.opt (i, 6)); k++; }
				Main.log ("settings: reset all (" + k + " changed)"); m.toast ("Settings back to their defaults", 2); reopenSettings (m); }); });
		  if (n > 0) ((TextView) r.findViewWithTag ("t")).setTextColor (Main.CHG); }
		p.row ("About", "Vectrex_EMU " + m.version () + " - files: " + m.sysDir, null);
		/* the core's settings, by its submenus */
		final int n = VX.nopts (), nc = VX.ncats ();
		for (int c = -1; c < nc; c++) {
			String ck = c >= 0 ? VX.cat (c, 0) : ""; boolean any = false;
			for (int i = 0; i < n; i++) { String k = VX.opt (i, 0); if (!VX.opt (i, 2).equals (ck) || k.equals ("vectrex_emu_deck_look")) continue;
				if (c < 0 && nc > 0 && !ck.isEmpty ()) continue;
				if (!any) { any = true; p.header (c >= 0 ? VX.cat (c, 1) : "General"); }
				final int ix = i; String v = VX.opt (i, 5); boolean chg = !v.equals (VX.opt (i, 6));
				LinearLayout r = p.row (VX.opt (i, 1), v, () -> choice (m, ix)); if (chg) ((TextView) r.findViewWithTag ("d")).setTextColor (Main.CHG);
				r.setOnLongClickListener (x -> { if (VX.opt (ix, 5).equals (VX.opt (ix, 6))) return true;   /* (a long press: back to the default) */
					VX.oset (ix, VX.opt (ix, 6)); Main.log ("settings: " + VX.opt (ix, 0) + " back to the default"); x.performHapticFeedback (android.view.HapticFeedbackConstants.LONG_PRESS);
					m.toast (VX.opt (ix, 1) + ": " + VX.opt (ix, 6) + " (the default)", 2); reopenSettings (m); return true; }); }
		}
		m.push (p);
	}
	static String pct (float f) { return Math.round (f * 100) + " %"; }
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
	static void pickF (Main m, String title, String key, float[] vals, float def)
	{
		String[] v = new String[vals.length], l = new String[vals.length]; float cur = m.prefs.getFloat (key, def);
		for (int i = 0; i < vals.length; i++) { v[i] = String.valueOf (vals[i]); l[i] = pct (vals[i]); }
		pick (m, title, v, l, String.valueOf (cur), s -> { m.prefs.edit ().putFloat (key, Float.parseFloat (s)).apply (); m.layout (); m.hdrChanged (); reopenSettings (m); });
	}
	/* a core setting's values, its help and its default */
	static void choice (Main m, int i)
	{
		Panel p = new Panel (m, VX.opt (i, 1)); String cur = VX.opt (i, 5); View first = null;
		final boolean fromSettings = m.top () instanceof Panel;
		if (!cur.equals (VX.opt (i, 6))) p.row ("Back to the default: " + VX.opt (i, 6), null, () -> { VX.oset (i, VX.opt (i, 6)); m.pop (); if (fromSettings && m.top () instanceof Panel) reopenSettings (m); });
		for (String v : VX.opt (i, 4).split ("\\|")) {
			LinearLayout r = p.row ((v.equals (cur) ? "✓  " : "     ") + v, null, () -> { VX.oset (i, v); m.pop (); if (fromSettings && m.top () instanceof Panel) reopenSettings (m); });
			if (v.equals (cur) || first == null) first = r; }
		p.foot (VX.opt (i, 3) + "\nDefault: " + VX.opt (i, 6));
		m.push (p); if (first != null) first.requestFocus ();
	}
	/* the Vectrex's buttons on the controller */
	static void controls (Main m)
	{
		Panel p = new Panel (m, "Controller buttons");
		p.foot ("The Vectrex controller's four buttons in a row: 1 2 3 4. On a modern pad's diamond: 1 left, 2 top, 3 bottom, 4 right. Over a menu, button 3 goes back and button 4 picks.");
		for (int v = 0; v < 4; v++) { final int k = v;
			p.row ("Vectrex button " + (v + 1), Input.PHYS[Input.phys (v)], () -> {
				String[] vals = new String[8]; for (int j = 0; j < 8; j++) vals[j] = String.valueOf (j);
				pick (m, "Vectrex button " + (k + 1), vals, Input.PHYS, String.valueOf (Input.phys (k)), s -> { m.prefs.edit ().putInt ("map." + (k + 1), Integer.parseInt (s)).apply (); m.pop (); controls (m); }); }); }
		p.row ("As the Vectrex controller (default)", "1 X, 2 Y, 3 A, 4 B", () -> { for (int j = 1; j <= 4; j++) m.prefs.edit ().remove ("map." + j).apply (); m.pop (); controls (m); });
		m.push (p);
	}
	/* a line of text typed in (chat, a code, an address) */
	interface Typed { void go (String s); }
	static void ask (Main m, String title, String hint, boolean caps, Typed go)
	{
		EditText e = new EditText (m); e.setHint (hint); e.setSingleLine (true);
		if (caps) e.setInputType (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
		new AlertDialog.Builder (m, android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle (title).setView (e)
			.setPositiveButton ("OK", (d, w) -> { String s = e.getText ().toString ().trim (); if (!s.isEmpty ()) go.go (s); }).setNegativeButton ("Cancel", null).show ();
	}
}
