/* Screens.java - the screens over the game, as the iOS app's (ios/App/VXUI.m): the game menu (save states, the games,
 * RESET, the overlay, the room light, all settings, online, the Vectrex's hardware), the settings (the app's own and every
 * core setting in its group, in the iOS app's order, each value with its help), a setting's values, the controller's buttons. Plain views,
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
		final int ov = VX.ofind ("vectrex_emu_overlay"); final boolean ovOn = ov >= 0 && VX.opt (ov, 5).equals ("on");
		final int room = VX.ofind ("vectrex_emu_room");
		LinearLayout acts = new LinearLayout (m); acts.setOrientation (LinearLayout.HORIZONTAL);
		String[] an = { "Games", "RESET", ovOn ? "Overlay on" : "Overlay off", "Room light" };
		Runnable[] ag = { () -> { m.pop (); Library.show (m); }, () -> { VX.act (1, 0); m.pop (); }, () -> { m.overlayToggle (); m.pop (); },
			() -> { if (room < 0) return; m.pop (); choice (m, room); } };
		for (int k = 0; k < 4; k++) { final Runnable go = ag[k]; final String nm = an[k]; TextView b = button (m, nm, k == 2 && ovOn); b.setTextSize (13); b.setMaxLines (2);
			b.setOnClickListener (v -> { Main.log ("ui " + nm); go.run (); });
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams (0, (int) m.dp (64), 1); lp.leftMargin = lp.rightMargin = (int) m.dp (4); acts.addView (b, lp); }
		LinearLayout.LayoutParams alp = new LinearLayout.LayoutParams (-1, -2); alp.topMargin = (int) m.dp (14); alp.bottomMargin = (int) m.dp (8); p.col.addView (acts, alp);
		p.row ("All settings", "The picture, the overlay, the sound, the controls...", () -> { m.pop (); settings (m); });
		p.row ("Online: play with a friend", Online.playing ? "Playing online now" : "Host or join - phones and PCs together", () -> { m.pop (); Online.show (m); });
		p.header ("Vectrex hardware");
		p.row ("Eject / insert cartridge", "Mine Storm without a cartridge", () -> { VX.act (2, 0); m.pop (); });
		final int p2 = VX.ofind ("vectrex_emu_port2");
		if (p2 >= 0) p.row ("Port 2: " + VX.opt (p2, 5), "The controller port's device", () -> choice (m, p2));
		m.push (p); resume.requestFocus ();
	}

	/* ---------------------------------------------------------------- the settings */
	static void settings (Main m)
	{
		Main.log ("ui settings opened"); Panel p = new Panel (m, "Settings");
		p.header ("App");
		p.row ("Clock and battery", m.prefs.getBoolean ("app.statusbar", false) ? "On" : "Off", () -> { m.prefs.edit ().putBoolean ("app.statusbar", !m.prefs.getBoolean ("app.statusbar", false)).apply ();
			Main.log ("clock and battery " + (m.prefs.getBoolean ("app.statusbar", false) ? "on" : "off")); m.immersive (); m.root.postDelayed (() -> reopenSettings (m), 350); });   /* (built again once the status bar's room is known) */
		{ final int bs = m.barSel (); final String[] BL = { "Auto", "Shown", "Auto-hide" };
		  p.row ("Top bar", BL[bs], () -> pick (m, "Top bar", new String[] { "0", "1", "2" }, BL, "" + bs, v -> { final int k = Integer.parseInt (v);
			m.prefs.edit ().putString ("app.bar", k == 1 ? "always" : k == 2 ? "hide" : "auto").apply (); Main.log ("top bar " + BL[k]); m.showBar (); m.bar.setAlpha (1); m.layout (); reopenSettings (m); })); }
		{ final int sel = m.deckSel (); final boolean pop = m.deckPop (); final String[] DL = { "Auto", "Off", "Full", "Slim" };
		  p.row ("Control deck", DL[sel], () -> pick (m, "Control deck", new String[] { "0", "1", "2", "3" }, DL, "" + sel, v -> {
			m.deckSet (Integer.parseInt (v), m.deckPop ()); m.layout (); reopenSettings (m); }));
		  if (sel >= 2) p.row ("Deck pop-up", pop ? "On" : "Off", () -> { m.deckSet (m.deckSel (), !m.deckPop ()); m.layout (); reopenSettings (m); });
		  else if (sel == 0) p.row ("Deck pop-up", (pop ? "On" : "Off") + " (Auto)", null); }
		{ final int si = VX.ofind ("vectrex_emu_skip_intro");
		  if (si >= 0) p.row ("Skip intro", VX.opt (si, 5).equals ("on") ? "On" : "Off", () -> { VX.oset (si, VX.opt (si, 5).equals ("on") ? "off" : "on"); reopenSettings (m); }); }
		p.row ("Downloads", Pack.token (m) != null ? "games, boxes, UVMC2, PiTrex" : "Not in this build (no server key)", () -> { if (Pack.token (m) == null) { m.toast ("This build has no server key: no downloads", 2.5); return; } m.pop (); Pack.choose (m, false); });
		if (m.hdrCan) p.row ("HDR brightness", m.prefs.getFloat ("app.hdr", 1f) > 0.01f ? "The brightest dots up to " + String.format (java.util.Locale.ROOT, "%.1fx", 1 + (m.hdrRoom - 1) * m.prefs.getFloat ("app.hdr", 1f)) + " white" : "Off: as an ordinary screen",
			() -> pickF (m, "HDR brightness", "app.hdr", new float[] { 0f, 0.25f, 0.5f, 0.75f, 1f }, 1f));
		else p.row ("HDR brightness", m.hdrScreen ? "Not on this Android version (HDR needs Android 14)" : "Not on this screen (it shows no HDR)", null);
		{ final int md0 = m.prefs.getInt ("app.hdrmode", 0), md = md0 == 1 ? 0 : md0; final boolean h = m.hdrCan;   /* (the look - iOS alike; without HDR: Bright or Vectrex) */
			p.row (h ? "HDR look" : "Look", md == 2 ? "Vectrex: as a real Vectrex - dots and slow lines stand out. Wants a bright screen" : "Bright: all lines pushed up to white - bright vectors on a dim screen",
				() -> pick (m, h ? "HDR look" : "Look", h ? new String[] { "2", "0" } : new String[] { "0", "2" }, h ? new String[] { "Vectrex", "Bright" } : new String[] { "Bright", "Vectrex" }, String.valueOf (md), s -> {
					m.prefs.edit ().putInt ("app.hdrmode", Integer.parseInt (s)).apply (); VX.setHdrLook (Integer.parseInt (s)); m.trailLast = -1; m.trailTick (); Main.log ("HDR look " + s); reopenSettings (m); })); }
		p.row ("Controls", "controllers, touch, keyboard", () -> { m.pop (); controls (m); });
		p.row ("About and help", "how to play, online, credits", () -> about (m));
		{ int n = 0; for (int i = 0; i < VX.nopts (); i++) if (!VX.opt (i, 5).equals (VX.opt (i, 6))) n++; final int changed = n;
		  LinearLayout r = p.row ("Reset all settings", n > 0 ? n + " changed - all back to their defaults (a long press on one: that one)" : "All at their defaults", () -> {
			if (changed == 0) { m.toast ("All settings are at their defaults", 2); return; }
			pick (m, "Reset all settings?", new String[] { "no", "yes" }, new String[] { "Cancel", "Reset: every setting back to its default" }, "no", v -> {
				if (!v.equals ("yes")) { reopenSettings (m); return; } int k = 0;
				for (int i = 0; i < VX.nopts (); i++) if (!VX.opt (i, 5).equals (VX.opt (i, 6))) { VX.oset (i, VX.opt (i, 6)); k++; }
				m.prefs.edit ().remove ("app.deckpop").remove ("app.topbar").remove ("app.bar").apply (); m.showBar (); m.layout ();   /* (the deck's pop-up and the top bar too: Auto) */
				Main.log ("settings: reset all (" + k + " changed)"); m.toast ("Settings back to their defaults", 2); reopenSettings (m); }); });
		  if (n > 0) ((TextView) r.findViewWithTag ("t")).setTextColor (Main.CHG); }
		p.foot ("Auto: what fits this screen with the picture at its full width. Top bar auto-hide: tap the top of the screen to bring it back. Deck pop-up: hidden until you tap the picture's bottom. Skip intro: games start straight away.");
		/* the core's settings by its groups, in the iOS app's order (VXSettings): the most used first, in each group the
		 * usual ones on top; settings that mean nothing here left out */
		final int n = VX.nopts (), nc = VX.ncats ();
		java.util.List<String> order = java.util.Arrays.asList ("video", "overlay", "case", "tube", "controls", "audio", "system", "unit", "circuit");
		java.util.List<String> top = java.util.Arrays.asList ("vectrex_emu_profile", "vectrex_emu_deck", "vectrex_emu_rotate", "vectrex_emu_rate", "vectrex_emu_size",
			"vectrex_emu_overlay", "vectrex_emu_ov_glow", "vectrex_emu_scratches", "vectrex_emu_room", "vectrex_emu_ceiling",
			"vectrex_emu_glow", "vectrex_emu_afterglow", "vectrex_emu_brightness", "vectrex_emu_keys", "vectrex_emu_port2");
		java.util.List<String> hide = java.util.Arrays.asList ("vectrex_emu_shape", "vectrex_emu_btn_browser", "vectrex_emu_deck_look", "vectrex_emu_deck", "vectrex_emu_skip_intro");   /* (the deck, skip intro: under App) */
		java.util.ArrayList<Integer> cs = new java.util.ArrayList<> ();
		for (String k : order) for (int c = 0; c < nc; c++) if (VX.cat (c, 0).equals (k)) cs.add (c);
		for (int c = 0; c < nc; c++) if (!cs.contains (c)) cs.add (c);
		if (nc == 0) cs.add (-1);
		for (int c : cs) {
			String ck = c >= 0 ? VX.cat (c, 0) : "";
			java.util.ArrayList<Integer> rows = new java.util.ArrayList<> ();
			for (int i = 0; i < n; i++) if (VX.opt (i, 2).equals (ck) && !hide.contains (VX.opt (i, 0))) rows.add (i);
			rows.sort ((x, y) -> { int a = top.indexOf (VX.opt (x, 0)), b = top.indexOf (VX.opt (y, 0)); if (a < 0) a = 999; if (b < 0) b = 999; return Integer.compare (a, b); });
			if (rows.isEmpty ()) continue;
			p.header (c >= 0 ? VX.cat (c, 1) : "General");
			for (int i : rows) {
				final int ix = i; String v = VX.opt (i, 5); boolean chg = !v.equals (VX.opt (i, 6));
				LinearLayout r = p.row (VX.opt (i, 1), v, () -> choice (m, ix)); if (chg) ((TextView) r.findViewWithTag ("d")).setTextColor (Main.CHG);
				r.setOnLongClickListener (x -> { if (VX.opt (ix, 5).equals (VX.opt (ix, 6))) return true;   /* (a long press: back to the default) */
					VX.oset (ix, VX.opt (ix, 6)); Main.log ("settings: " + VX.opt (ix, 0) + " back to the default"); x.performHapticFeedback (android.view.HapticFeedbackConstants.LONG_PRESS);
					m.toast (VX.opt (ix, 1) + ": " + VX.opt (ix, 6) + " (the default)", 2); reopenSettings (m); return true; }); }
			if (c >= 0 && !VX.cat (c, 2).isEmpty ()) p.foot (VX.cat (c, 2));
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
		if (Adjust.slider (i)) { Adjust.show (m, i); return; }   /* (a number: the slider over the running game, as the iOS app) */
		Panel p = new Panel (m, VX.opt (i, 1)); String cur = VX.opt (i, 5); View first = null;
		final boolean fromSettings = m.top () instanceof Panel;
		if (!cur.equals (VX.opt (i, 6))) p.row ("Back to the default: " + VX.opt (i, 6), null, () -> { VX.oset (i, VX.opt (i, 6)); m.pop (); if (fromSettings && m.top () instanceof Panel) reopenSettings (m); });
		for (String v : VX.opt (i, 4).split ("\\|")) {
			LinearLayout r = p.row ((v.equals (cur) ? "✓  " : "     ") + v, null, () -> { VX.oset (i, v); m.pop (); if (fromSettings && m.top () instanceof Panel) reopenSettings (m); });
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
		if (np == 0) p.row (kb ? "Keyboard" : "Nothing connected", kb ? "the PC keys" : "touch", null);
		p.foot ("Player 1 is the first controller, player 2 the second.");
		p.header ("Vectrex buttons on the controller");
		for (int v = 0; v < 4; v++) { final int k = v;
			p.row ("Button " + (v + 1), Input.PHYS[Input.phys (v)], () -> {
				String[] vals = new String[8]; for (int j = 0; j < 8; j++) vals[j] = String.valueOf (j);
				pick (m, "Vectrex button " + (k + 1), vals, Input.PHYS, String.valueOf (Input.phys (k)), s -> { m.prefs.edit ().putInt ("map." + (k + 1), Integer.parseInt (s)).apply (); m.pop (); controls (m); }); }); }
		p.row ("As the Vectrex controller (default)", Input.nintendo ? "1 Y, 2 X, 3 B, 4 A (this pad's labels)" : "1 X, 2 Y, 3 A, 4 B", () -> { for (int j = 1; j <= 4; j++) m.prefs.edit ().remove ("map." + j).apply (); m.pop (); controls (m); });
		p.foot ("Tap a button to choose. The Vectrex controller's four buttons in a row; on a modern pad's diamond by default 1 left, 2 top, 3 bottom, 4 right.");
		p.header ("Touch controls");
		String ts = m.prefs.getString ("touch.show", "auto");
		p.row ("Show", ts.equals ("auto") ? "Automatic" : ts.equals ("always") ? "Always" : "Never", () -> pick (m, "Show", new String[] { "auto", "always", "never" },
			new String[] { "Automatic", "Always", "Never" }, ts, v -> { m.prefs.edit ().putString ("touch.show", v).apply (); m.layout (); m.pop (); controls (m); }));
		p.row ("Size", pct (m.prefs.getFloat ("touch.size", 1f)), () -> pickC (m, "Size", "touch.size", new float[] { 0.7f, 0.85f, 1f, 1.2f, 1.4f }, 1f));
		p.row ("Opacity", pct (m.prefs.getFloat ("touch.alpha", 1f)), () -> pickC (m, "Opacity", "touch.alpha", new float[] { 0.2f, 0.4f, 0.6f, 0.8f, 1f }, 1f));
		p.row ("Directions", m.prefs.getBoolean ("touch.dpad", false) ? "D-pad" : "Stick", () -> { m.prefs.edit ().putBoolean ("touch.dpad", !m.prefs.getBoolean ("touch.dpad", false)).apply (); m.layout (); m.pop (); controls (m); });   /* (the stick, or a D-pad: clear turns) */
		p.row ("Left-handed (the stick on the right)", m.prefs.getBoolean ("touch.lefty", false) ? "On" : "Off", () -> { m.prefs.edit ().putBoolean ("touch.lefty", !m.prefs.getBoolean ("touch.lefty", false)).apply (); m.layout (); m.pop (); controls (m); });
		p.foot ("Automatic: shown only while no controller or keyboard is connected. The stick appears where the thumb lands; the D-pad gives clear up, down, left, right (the small switch beside it changes it too).");
		p.header ("Keyboard");
		final int kk = VX.ofind ("vectrex_emu_keys");
		if (kk >= 0) p.row ("Keyboard as on the PC", VX.opt (kk, 5).equals ("on") ? "On" : "Off", () -> { VX.oset (kk, VX.opt (kk, 5).equals ("on") ? "off" : "on"); m.pop (); controls (m); });
		p.foot ("As on the PC: the arrows the stick (Shift: half), A W S D buttons 1-4, P pause, L the games, O overlay, Ctrl+R RESET, Ctrl+E eject, Page Up / Down volume, M the menu.");
		m.push (p);
	}
	static void pickC (Main m, String title, String key, float[] vals, float def)
	{
		String[] v = new String[vals.length], l = new String[vals.length]; float cur = m.prefs.getFloat (key, def);
		for (int i = 0; i < vals.length; i++) { v[i] = String.valueOf (vals[i]); l[i] = pct (vals[i]); }
		pick (m, title, v, l, String.valueOf (cur), s -> { m.prefs.edit ().putFloat (key, Float.parseFloat (s)).apply (); m.layout (); m.pop (); controls (m); });
	}
	/* Settings > About and help, as the iOS app's (VXAbout.m) */
	static void about (Main m)
	{
		Panel p = new Panel (m, "About and help"); Main.log ("settings: about and help");
		String[][] parts = {
			{ "Vectrex_EMU", "Version " + m.version () + ". The Vectrex as it looks on a real tube: the beam, the phosphor's afterglow, the glow, the overlays, the glass and the case." },
			{ "Playing", "MENU (top bar) opens the game menu: save and load, games, RESET, overlay, room light, all settings. With a controller: Select + X; with a keyboard: M.\nThe knob below the picture: the volume; hold it to switch the Vectrex off and on.\nTouch a setting and hold: back to its default." },
			{ "Controls", "The touch controls show while no controller or keyboard is connected (Settings > Controls: always, never, their size and side). Controller buttons 1-4 are chosen there too." },
			{ "Online", "Game menu > Online: host your game in the game room (anyone can join), with a code (only who you tell), or on this Wi-Fi (no internet needed); join with a code or from the lists. Phones and PCs play together - both need the same game and the same version of Vectrex_EMU. A menu open pauses both players." },
			{ "Your own games", "Copy ROMs (.bin .vec .gam .um2) with a file manager to " + new File (m.ext (), "roms") + "." },
			{ "Credits", "Vectrex_EMU by Hugo Bokhorst, started from vecx, the open-source Vectrex emulator. Overlays: the libretro overlay-borders collection and their creators (the full list: overlays/CREDITS.md in the app's files). Last Human and the UVMC2 games with their makers' permission.\nVectrex is a trademark of its owners; this app is not affiliated with them." },
			{ "Licence", "Free software: GNU General Public License version 3. The source code: github.com/hugonl31/Vectrex_EMU" } };
		for (String[] x : parts) { p.header (x[0]); TextView b = m.text (x[1], 15, Main.FG, false); b.setPadding ((int) m.dp (4), 0, (int) m.dp (4), (int) m.dp (6)); p.col.addView (b); }
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
