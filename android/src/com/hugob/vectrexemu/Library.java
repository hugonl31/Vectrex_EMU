/* Library.java - the games, as the iOS app's library: the looks (Cartridges, Boxes, Overlays - each its own pictures, a
 * game without one shows its name), 1-5 per row or Titles only (one setting for every look), the filters (All, Recent,
 * Games, UVMC2, PiTrex, Tests), the search; a controller or the keys move the highlight, L / R a page, button 4 plays,
 * button 1 goes back (2 + 3 held with button 4: a UVMC2 game's calibration; touch and hold one: Play / Calibration).
 * Online, only the host changes the game - it starts for both.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.LruCache;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.AbsListView;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

final class Library extends LinearLayout implements Screens.Closing {
	static final class Game { String name, path, kind; }
	static List<Game> scan (Main m)
	{
		List<Game> a = new ArrayList<> ();
		for (File r : m.romRoots) walk (r, r, a, 0);
		File px = new File (m.sysDir, "vectrex_emu/pitrex/pitrex.img"); if (px.exists ()) { Game g = new Game (); g.name = "PiTrex"; g.path = px.getAbsolutePath (); g.kind = "PiTrex"; a.add (g);
			try { File th = new File (m.sysDir, "vectrex_emu/thumbs"); new File (th, "carts").mkdirs (); new File (th, "boxes").mkdirs (); VX.pitrexTiles (th.getAbsolutePath ()); } catch (Throwable t) { } }   /* (its cartridge and box) */
		/* (the PiTrex itself first, as in the PC's list - the owner, 2026-11-22; then by name) */
		Collections.sort (a, (x, y) -> { boolean px_ = x.kind.equals ("PiTrex"), py_ = y.kind.equals ("PiTrex"); return px_ != py_ ? (px_ ? -1 : 1) : x.name.compareToIgnoreCase (y.name); });
		ArrayList<Game> u = new ArrayList<> (); ArrayList<String> seen = new ArrayList<> ();   /* (the same game in two folders: once) */
		for (Game g : a) { String k = g.name.toLowerCase (Locale.ROOT) + "|" + g.kind; if (!seen.contains (k)) { seen.add (k); u.add (g); } }
		return u;
	}
	static void walk (File root, File d, List<Game> a, int depth)
	{
		File[] fs = d.listFiles (); if (fs == null || depth > 4) return;
		for (File f : fs) { if (f.isDirectory ()) { walk (root, f, a, depth + 1); continue; }
			String n = f.getName (), lo = n.toLowerCase (Locale.ROOT); int dot = lo.lastIndexOf ('.'); if (dot < 0) continue; String ext = lo.substring (dot + 1);
			if (!ext.equals ("bin") && !ext.equals ("vec") && !ext.equals ("gam") && !ext.equals ("um2") && !ext.equals ("v4e")) continue;
			Game g = new Game (); g.name = n.substring (0, dot); g.path = f.getAbsolutePath ();
			String rel = f.getAbsolutePath ().substring (root.getAbsolutePath ().length () + 1);
			g.kind = ext.equals ("um2") ? "UVMC2" : rel.startsWith ("Tests/") ? "Tests" : "Games"; a.add (g); }
	}
	static void show (Main m) { if (!m.stack.isEmpty () || !m.started) return; Main.log ("ui library opened"); m.push (new Library (m)); }

	final Main m; final GridView grid; final EditText search; final LinearLayout chips; final TextView size; final TextView[] looks = new TextView[3];
	List<Game> all, shown = new ArrayList<> (); String filter = "All"; int sel = -1; boolean ready;   /* (the highlight: a controller or the keys) */
	final LruCache<String, Bitmap> cache = new LruCache<String, Bitmap> (48 << 20) { @Override protected int sizeOf (String k, Bitmap b) { return b.getByteCount (); } };
	int perRow () { return Math.max (0, Math.min (5, m.prefs.getInt ("lib.perrow", 3))); }
	String look () { return m.prefs.getString ("lib.look", "boxes"); }
	float aspect () { String l = look (); return l.equals ("carts") ? 0.96f : l.equals ("boxes") ? 1.39f : 207f / 162; }
	Library (Main m)
	{
		super (m); this.m = m; setOrientation (VERTICAL); setBackgroundColor (Main.BG); setClickable (true); all = scan (m);
		Main.log ("library: " + all.size () + " games");
		LinearLayout head = new LinearLayout (m); head.setOrientation (VERTICAL); head.setBackgroundColor (Main.BAR);
		head.setPadding ((int) m.dp (16) + m.insL, (int) m.dp (8) + m.insTop, (int) m.dp (16) + m.insR, (int) m.dp (10));
		LinearLayout row = new LinearLayout (m); row.setGravity (Gravity.CENTER_VERTICAL);
		row.addView (m.text ("Games", 24, Main.HI, true), new LayoutParams (0, -2, 1));
		TextView close = Screens.button (m, "✕", false); close.setContentDescription ("Close"); close.setOnClickListener (v -> m.pop ());
		row.addView (close, new LayoutParams ((int) m.dp (48), (int) m.dp (44))); head.addView (row);
		LinearLayout lk = new LinearLayout (m); String[] ln = { "Cartridges", "Boxes", "Overlays" }, lv = { "carts", "boxes", "overlays" };
		for (int i = 0; i < 3; i++) { final String v = lv[i]; TextView b = Screens.button (m, ln[i], v.equals (look ())); b.setTextSize (13); looks[i] = b;
			b.setOnClickListener (x -> { m.prefs.edit ().putString ("lib.look", v).apply (); Main.log ("library look " + v); paintLooks (); cache.evictAll (); reload (); });
			LayoutParams lp = new LayoutParams (0, (int) m.dp (38), 1); lp.rightMargin = (int) m.dp (i < 2 ? 6 : 0); lk.addView (b, lp); }
		LayoutParams lkp = new LayoutParams (-1, -2); lkp.topMargin = (int) m.dp (8); head.addView (lk, lkp);
		LinearLayout srow = new LinearLayout (m);
		search = new EditText (m); search.setHint ("Search " + all.size () + " games"); search.setSingleLine (true); search.setTextColor (Main.FG); search.setHintTextColor (Main.DIM); search.setTextSize (15);
		search.setBackground (Main.round (Main.BODY, Main.EDGE, m.dp (10))); search.setPadding ((int) m.dp (12), 0, (int) m.dp (12), 0); search.setImeOptions (EditorInfo.IME_ACTION_DONE);
		search.addTextChangedListener (new TextWatcher () { public void beforeTextChanged (CharSequence s, int a, int b, int c) { } public void onTextChanged (CharSequence s, int a, int b, int c) { } public void afterTextChanged (Editable e) { apply (); } });
		srow.addView (search, new LayoutParams (0, (int) m.dp (40), 1));
		size = Screens.button (m, "", false); size.setTextSize (12); size.setOnClickListener (v -> sizeMenu ());
		LayoutParams sp = new LayoutParams (-2, (int) m.dp (40)); sp.leftMargin = (int) m.dp (8); srow.addView (size, sp);
		{ TextView get = Screens.button (m, "GET GAMES", false); get.setTextSize (12);   /* (the download page over the library - as iOS: the games looked for again when it closes) */
			get.setOnClickListener (v -> { Screens.Panel g = Getter.show (m); g.onClose = () -> { all = scan (m); Main.log ("library: " + all.size () + " games (again)"); search.setHint ("Search " + all.size () + " games"); cache.evictAll (); apply (); }; });
			LayoutParams gp = new LayoutParams (-2, (int) m.dp (40)); gp.leftMargin = (int) m.dp (8); srow.addView (get, gp); }
		LayoutParams srp = new LayoutParams (-1, -2); srp.topMargin = (int) m.dp (8); head.addView (srow, srp);
		HorizontalScrollView hs = new HorizontalScrollView (m); hs.setHorizontalScrollBarEnabled (false); chips = new LinearLayout (m);
		for (String f : new String[] { "All", "Recent", "Games", "UVMC2", "PiTrex", "Tests" }) { TextView c = Screens.button (m, f, false); c.setTextSize (13); c.setTag (f);
			c.setOnClickListener (v -> { filter = f; apply (); }); LayoutParams cp = new LayoutParams (-2, (int) m.dp (34)); cp.rightMargin = (int) m.dp (6); chips.addView (c, cp); }
		hs.addView (chips); LayoutParams hp = new LayoutParams (-1, -2); hp.topMargin = (int) m.dp (8); head.addView (hs, hp);
		addView (head, new LayoutParams (-1, -2));
		grid = new GridView (m); grid.setBackgroundColor (Main.BG); grid.setVerticalSpacing ((int) m.dp (16)); grid.setHorizontalSpacing ((int) m.dp (12));
		grid.setPadding ((int) m.dp (16) + m.insL, (int) m.dp (16), (int) m.dp (16) + m.insR, (int) m.dp (24) + m.insBot); grid.setClipToPadding (false);
		grid.setStretchMode (GridView.STRETCH_COLUMN_WIDTH); grid.setSelector (new android.graphics.drawable.ColorDrawable (0)); grid.setFocusable (false);
		grid.setOnItemClickListener ((par, v, pos, id) -> play (pos));
		grid.setOnItemLongClickListener ((par, v, pos, id) -> {   /* (as iOS's menu: Play on every game; a UVMC2 game its calibration screen too - buttons 2 + 3 held at its start) */
			if (pos < 0 || pos >= shown.size ()) return false;
			final Game g = shown.get (pos); v.performHapticFeedback (android.view.HapticFeedbackConstants.LONG_PRESS);
			final Runnable cal = () -> { Main.log ("library: " + g.name + " with buttons 2 + 3 held (calibration)"); m.ctl.holdFor ((1 << 9) | (1 << 0), 30000); VX.load (g.path); remember (m, g.path); m.pop (); };
			Screens.prompt (m, g.name, null, null, g.path.toLowerCase (Locale.ROOT).endsWith (".um2")
				? new Object[][] { { "Play", "", (Runnable) () -> play (pos) }, { "Calibration (2 + 3 held)", "", cal } } : new Object[][] { { "Play", "", (Runnable) () -> play (pos) } }, null);
			return true; });
		grid.setAdapter (adapter); addView (grid, new LayoutParams (-1, 0, 1));
		paintLooks (); sizeLabel (); reload (); apply (); ready = true;
		/* (the library itself holds the focus, not the search field - opened by touch the field took it, and a controller's
		 *  keys went into it; the field takes keys once it is tapped) */
		setFocusable (true); setFocusableInTouchMode (true); setDescendantFocusability (FOCUS_BEFORE_DESCENDANTS); if (android.os.Build.VERSION.SDK_INT >= 26) setDefaultFocusHighlightEnabled (false);
		post (() -> { requestFocus (); int cur = current (); if (cur >= 0) { sel = cur; adapter.notifyDataSetChanged (); grid.setSelection (Math.max (0, cur - Math.max (1, perRow ()))); } });   /* (opened at the game playing, selected) */
	}
	@Override public void closed () { cache.evictAll (); }
	/* the keys and the pad (Input.java hands a controller's here as the arrows, Page Up / Down, Enter): the highlight -
	 * up / down / left / right (titles only: left / right a page), a page, play; the search field keeps its own keys */
	int cols () { return Math.max (1, perRow ()); }
	@Override public boolean dispatchKeyEvent (KeyEvent e)
	{
		if (search.hasFocus () && e.getKeyCode () != KeyEvent.KEYCODE_DPAD_DOWN) return super.dispatchKeyEvent (e);
		final int c = e.getKeyCode (); if (e.getAction () != KeyEvent.ACTION_DOWN) return c == KeyEvent.KEYCODE_DPAD_UP || c == KeyEvent.KEYCODE_DPAD_DOWN || c == KeyEvent.KEYCODE_DPAD_LEFT || c == KeyEvent.KEYCODE_DPAD_RIGHT
			|| c == KeyEvent.KEYCODE_PAGE_UP || c == KeyEvent.KEYCODE_PAGE_DOWN || c == KeyEvent.KEYCODE_DPAD_CENTER || c == KeyEvent.KEYCODE_ENTER || super.dispatchKeyEvent (e);
		switch (c) {
		case KeyEvent.KEYCODE_DPAD_UP: move (0); return true; case KeyEvent.KEYCODE_DPAD_DOWN: if (search.hasFocus ()) requestFocus (); move (1); return true;
		case KeyEvent.KEYCODE_DPAD_LEFT: move (2); return true; case KeyEvent.KEYCODE_DPAD_RIGHT: move (3); return true;
		case KeyEvent.KEYCODE_PAGE_UP: page (-1); return true; case KeyEvent.KEYCODE_PAGE_DOWN: page (1); return true;
		case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: if (sel >= 0) play (sel); return true;
		}
		return super.dispatchKeyEvent (e);
	}
	/* (a controller's keys while the search field is typed in: before the on-screen keyboard can take them) */
	@Override public boolean dispatchKeyEventPreIme (KeyEvent e)
	{
		if (search.hasFocus () && (Input.isPad (e.getDevice ()) || KeyEvent.isGamepadButton (e.getKeyCode ())) && Input.key (m, e)) return true;
		return super.dispatchKeyEventPreIme (e);
	}
	/* a controller's (Input.screenKey): always the highlight - the search field lets go of the focus and its keyboard */
	boolean padKey (KeyEvent e, int b)
	{
		if (search.hasFocus ()) { ((android.view.inputmethod.InputMethodManager) m.getSystemService (android.content.Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow (search.getWindowToken (), 0);
			Main.log ("library: the controller takes over from the search field"); }
		if (!isFocused ()) requestFocus ();
		if (e.getAction () != KeyEvent.ACTION_DOWN) return true;
		switch (b) {
		case Input.UP: move (0); break; case Input.DOWN: move (1); break; case Input.LEFT: move (2); break; case Input.RIGHT: move (3); break;
		case Input.L: page (-1); break; case Input.R: page (1); break;
		case Input.A: if (e.getRepeatCount () == 0 && sel >= 0) play (sel); break;
		}
		return true;
	}
	int pageItems () { View v = grid.getChildAt (0); int ih = v != null ? v.getHeight () + grid.getVerticalSpacing () : (int) m.dp (200); return Math.max (1, grid.getHeight () / Math.max (1, ih)) * cols (); }
	void move (int d)
	{
		if (shown.isEmpty ()) return; final int n = shown.size (), k = cols ();
		if (sel < 0) { int cur = current (); sel = cur >= 0 ? cur : Math.max (0, grid.getFirstVisiblePosition ()); showSel (); return; }
		if (perRow () == 0 && (d == 2 || d == 3)) { page (d == 2 ? -1 : 1); return; }
		int s2 = sel + (d == 0 ? -k : d == 1 ? k : d == 2 ? -1 : 1);
		if (s2 < 0 || s2 >= n) { if (d == 1 && sel / k < (n - 1) / k) s2 = n - 1; else return; }
		sel = s2; showSel ();
	}
	void page (int dir) { if (shown.isEmpty ()) return; if (sel < 0) sel = 0; sel = Math.max (0, Math.min (shown.size () - 1, sel + dir * pageItems ())); showSel (); }
	void showSel ()
	{
		adapter.notifyDataSetChanged ();
		int first = grid.getFirstVisiblePosition (), last = grid.getLastVisiblePosition ();
		if (sel < first || sel > last || (last - first + 1 > 0 && (sel - first) / cols () >= Math.max (1, (last - first + 1) / cols ()) - 0)) grid.smoothScrollToPositionFromTop (sel, grid.getHeight () / 3, 120);
		performHapticFeedback (android.view.HapticFeedbackConstants.CLOCK_TICK);
	}
	int current () { String g = VX.game (); for (int i = 0; i < shown.size (); i++) if (shown.get (i).path.equals (g)) return i; return -1; }
	void paintLooks () { String[] lv = { "carts", "boxes", "overlays" }; for (int i = 0; i < 3; i++) { boolean on = lv[i].equals (look ()); looks[i].setTextColor (on ? Main.HI2 : Main.FG2); looks[i].setBackground (Screens.states (m, on ? Main.SEL : Main.BODY, Main.EDGE2, m.dp (10))); } }
	void sizeLabel () { int k = perRow (); size.setText (k > 0 ? k + " PER ROW ▾" : "TITLES ONLY ▾"); }
	void sizeMenu ()
	{
		String[] v = { "1", "2", "3", "4", "5", "0" }, l = { "1 per row", "2 per row", "3 per row", "4 per row", "5 per row", "Titles only" };
		Screens.pick (m, "Games per row", v, l, String.valueOf (perRow ()), s -> { m.prefs.edit ().putInt ("lib.perrow", Integer.parseInt (s)).apply (); Main.log ("library: " + s + " per row"); sizeLabel (); reload (); });
	}
	void reload ()
	{
		int k = perRow (); grid.setNumColumns (k == 0 ? 1 : k); grid.setVerticalSpacing ((int) m.dp (k == 0 ? 4 : 16)); adapter.notifyDataSetChanged ();
	}
	void apply ()
	{
		final String keep = sel >= 0 && sel < shown.size () ? shown.get (sel).path : null;   /* (the game highlighted) */
		String q = search.getText ().toString ().toLowerCase (Locale.ROOT); List<Game> r = new ArrayList<> ();
		if (filter.equals ("Recent")) { for (String p : m.prefs.getString ("recent", "").split ("\n")) for (Game g : all) if (g.path.equals (p)) r.add (g); }
		else for (Game g : all) if ((filter.equals ("All") && !g.kind.equals ("Tests")) || g.kind.equals (filter)) r.add (g);
		if (!q.isEmpty ()) { List<Game> f = new ArrayList<> (); for (Game g : r) if (g.name.toLowerCase (Locale.ROOT).contains (q)) f.add (g); r = f; }
		/* (the highlight in the new list: the same game while it is in it, else the first - past the list's end button 4 and
		 *  the stick did nothing) */
		int ns = -1; if (keep != null) for (int i = 0; i < r.size (); i++) if (r.get (i).path.equals (keep)) { ns = i; break; }
		if (ns < 0 && ready && !r.isEmpty ()) ns = 0;
		shown = r; sel = ns; adapter.notifyDataSetChanged ();
		if (ready && sel >= 0) { final int s0 = sel; grid.post (() -> grid.setSelection (Math.max (0, s0 - cols ()))); }
		for (int i = 0; i < chips.getChildCount (); i++) { TextView c = (TextView) chips.getChildAt (i); boolean on = c.getTag ().equals (filter);
			c.setTextColor (on ? Main.HI2 : Main.FG2); c.setBackground (Screens.states (m, on ? Main.SEL : 0, Main.EDGE2, m.dp (17))); }
	}
	/* the PiTrex's pictogram of the PC's simple list: a raspberry, in lines (10 x 6.4 units) */
	android.graphics.drawable.Drawable raspberry (int col)
	{
		final float k = m.dp (16) / 6.4f; int w = (int) Math.ceil (10 * k), h = (int) Math.ceil (6.4f * k);
		Bitmap b = Bitmap.createBitmap (w, h, Bitmap.Config.ARGB_8888); android.graphics.Canvas c = new android.graphics.Canvas (b);
		android.graphics.Paint p = new android.graphics.Paint (android.graphics.Paint.ANTI_ALIAS_FLAG); p.setStyle (android.graphics.Paint.Style.STROKE); p.setColor (col); p.setStrokeWidth (Math.max (2f, k * 0.42f)); p.setStrokeCap (android.graphics.Paint.Cap.ROUND);
		c.translate (0, 0.5f * k); c.drawOval (2.4f * k, 1.2f * k, 7.6f * k, 6.0f * k, p);
		final float[][] L = { { 5, 1.2f, 2.8f, -0.2f }, { 5, 1.2f, 7.2f, -0.2f }, { 2.8f, -0.2f, 4.2f, 0.6f }, { 7.2f, -0.2f, 5.8f, 0.6f }, { 3.6f, 3.4f, 6.4f, 3.4f }, { 5, 1.6f, 5, 5.6f } };
		for (float[] l : L) c.drawLine (l[0] * k, l[1] * k, l[2] * k, l[3] * k, p);
		android.graphics.drawable.BitmapDrawable d = new android.graphics.drawable.BitmapDrawable (getResources (), b); d.setBounds (0, 0, w, h); return d;
	}
	/* (n's picture in the look chosen, made at about px pixels wide - the tile's width on the screen, in a few steps, so
	 *  another number a row or a turn of the screen finds most made already; never kept at its full size) */
	Bitmap tile (String n, int px)
	{
		final int tw = VX.gensize (px);
		String key = look () + "/" + tw + "/" + n; Bitmap b = cache.get (key); if (b != null) return b;
		File th = new File (m.sysDir, "vectrex_emu/thumbs"); String base = n.split (" \\(")[0];
		String[] dirs = look ().equals ("boxes") ? new String[] { "boxes" } : look ().equals ("overlays") ? new String[] { "overlays", "" } : new String[] { "carts" };
		final String art = look ().equals ("overlays") ? null : shipped (n);   /* (the box art's name for it: the full artwork set's pictures) */
		final String[] names = art != null ? new String[] { n, base, art } : new String[] { n, base };
		for (String d : dirs) for (String nm : names) for (String x : EXT) { File f = new File (new File (th, d), nm + x);
			if (f.exists ()) { try { b = bitmapOf (VX.genfit (f.getAbsolutePath (), tw), tw); } catch (Throwable t) { Main.log ("library: genfit " + t); }
				if (b == null) { BitmapFactory.Options o = new BitmapFactory.Options (); o.inSampleSize = tw <= 420 ? 2 : 1; b = BitmapFactory.decodeFile (f.getAbsolutePath (), o); }
				if (b != null) { cache.put (key, b); return b; } } }
		if (look ().equals ("carts")) for (String nm : names) for (String x : EXT) {   /* (no cartridge picture: the player's own label on our cartridge - genart.c) */
			File f = new File (new File (th, "labels"), nm + x);
			if (f.exists ()) try { b = bitmapOf (VX.genlabel (f.getAbsolutePath (), tw), tw); if (b != null) { cache.put (key, b); return b; } } catch (Throwable t) { Main.log ("library: genlabel " + t); } }
		if (art != null) {                                 /* (none of the player's own: the one that comes with the app - assets/shipped) */
			byte[] pic = asset ("shipped/" + (look ().equals ("boxes") ? "boxes/" : "labels/") + art + ".jpg");
			if (pic != null) try { b = bitmapOf (look ().equals ("boxes") ? VX.genfitb (pic, tw) : VX.genlabelb (pic, tw), tw);
				if (b != null) { cache.put (key, b); return b; } } catch (Throwable t) { Main.log ("library: shipped " + t); } }
		if (!look ().equals ("overlays")) {               /* (none of its own: a cartridge / box drawn by the core's genart.c - as the iPhone, the PC, RetroArch) */
			try { b = bitmapOf (VX.genart (look ().equals ("boxes") ? 1 : 0, n, tw), tw); if (b != null) { cache.put (key, b); return b; } } catch (Throwable t) { Main.log ("library: genart " + t); } }
		return null;
	}
	static final String[] EXT = { ".png", ".jpg" };     /* (the player's own pictures: PNG or JPEG) */
	/* (the pictures that come with the app: assets/shipped - labels/<name>.jpg, boxes/<name>.jpg - and its index.txt,
	 *  "<key>\t<name>" a line (tools/make_shipped_art.py); the key as genart.c's vx_genart_key: the name without its (...) and
	 *  [...] parts, lower case, only a-z and 0-9) */
	static java.util.Map<String, String> ship;
	static String key (String n) { return n.replaceAll ("\\(.*?\\)|\\[.*?\\]", "").toLowerCase (java.util.Locale.ROOT).replaceAll ("[^a-z0-9]", ""); }
	String shipped (String n)
	{
		if (ship == null) { java.util.Map<String, String> mp = new java.util.HashMap<> (); byte[] ix = asset ("shipped/index.txt");
			if (ix != null) for (String ln : new String (ix, java.nio.charset.StandardCharsets.UTF_8).split ("\n")) { int t = ln.indexOf ('\t'); if (ln.startsWith ("#") || t < 0) continue; mp.put (ln.substring (0, t), ln.substring (t + 1).trim ()); }
			ship = mp; }
		return ship.get (key (n));
	}
	byte[] asset (String p)
	{
		try (java.io.InputStream in = m.getAssets ().open (p)) { java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream (); byte[] buf = new byte[65536]; int k; while ((k = in.read (buf)) > 0) o.write (buf, 0, k); return o.toByteArray (); }
		catch (Exception e) { return null; }
	}
	/* (genart.c's { w, h, ARGB... } as a Bitmap at most tw wide) */
	static Bitmap bitmapOf (int[] g, int tw)
	{
		if (g == null || g.length < 3 || g[0] <= 0 || g[1] <= 0) return null;
		Bitmap full = Bitmap.createBitmap (g, 2, g[0], g[0], g[1], Bitmap.Config.ARGB_8888);
		if (g[0] <= tw) return full;
		Bitmap b = Bitmap.createScaledBitmap (full, tw, Math.max (1, Math.round (g[1] * (float) tw / g[0])), true); if (b != full) full.recycle (); return b;
	}
	void play (int pos)
	{
		if (pos < 0 || pos >= shown.size ()) return; Game g = shown.get (pos); Main.log ("library: play " + g.name);
		if (Online.pick (m, g.path)) { m.pop (); return; }
		VX.load (g.path); remember (m, g.path); m.pop ();
	}
	static void remember (Main m, String path)
	{
		m.prefs.edit ().putString ("lastGame", path).apply ();
		StringBuilder s = new StringBuilder (path); int n = 1;
		for (String p : m.prefs.getString ("recent", "").split ("\n")) if (!p.isEmpty () && !p.equals (path) && n < 12) { s.append ('\n').append (p); n++; }
		m.prefs.edit ().putString ("recent", s.toString ()).apply ();
	}
	final BaseAdapter adapter = new BaseAdapter () {
		public int getCount () { return shown.size (); }
		public Object getItem (int i) { return shown.get (i); }
		public long getItemId (int i) { return i; }
		public View getView (int i, View cv, ViewGroup par) {
			final boolean titles = perRow () == 0; Game g = shown.get (i); boolean now = g.path.equals (VX.game ());
			LinearLayout c; ImageView img; TextView name, ph, kind; FrameLayout box;
			if (cv instanceof LinearLayout && cv.getTag () != null && cv.getTag ().equals (titles)) { c = (LinearLayout) cv; }
			else { c = new LinearLayout (m); c.setTag (titles);
				if (titles) { c.setOrientation (HORIZONTAL); c.setGravity (Gravity.CENTER_VERTICAL); c.setPadding ((int) m.dp (14), 0, (int) m.dp (14), 0); c.setMinimumHeight ((int) m.dp (46));
					name = m.text ("", 15, Main.FG, false); name.setSingleLine (true); name.setTag ("n"); c.addView (name, new LayoutParams (0, -2, 1));
					kind = m.text ("", 11, Main.DIM, true); kind.setTag ("k"); c.addView (kind); }
				else { c.setOrientation (VERTICAL); box = new FrameLayout (m); box.setTag ("b");
					img = new ImageView (m); img.setTag ("i"); img.setColorFilter (0xffcccccc, android.graphics.PorterDuff.Mode.MULTIPLY);   /* (the pictures at 80 % brightness, as the PC's library - the owner, 2026-11-23) */ box.addView (img, new FrameLayout.LayoutParams (-1, -1));
					ph = m.text ("", 15, Main.G, true); ph.setGravity (Gravity.CENTER); ph.setTag ("p"); ph.setPadding ((int) m.dp (6), 0, (int) m.dp (6), 0); box.addView (ph, new FrameLayout.LayoutParams (-1, -1));
					c.addView (box, new LayoutParams (-1, 100));
					name = m.text ("", 12, Main.FG, false); name.setMaxLines (2); name.setTag ("n"); name.setPadding (0, (int) m.dp (4), 0, 0); c.addView (name); } }
			name = c.findViewWithTag ("n"); name.setText (g.name); name.setTextColor (now ? Main.HI : Main.FG); name.setTypeface (null, now ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL);
			final boolean on = i == sel; if (on) { name.setTextColor (Main.HI2); name.setTypeface (null, android.graphics.Typeface.BOLD); }
			if (titles) { kind = c.findViewWithTag ("k"); kind.setText (g.kind.equals ("Games") ? "" : g.kind.toUpperCase (Locale.ROOT));
				/* (as the PC's list: UVMC2 games in their own colour, the PiTrex with its raspberry) */
				if (g.kind.equals ("UVMC2")) name.setTextColor (on || now ? 0xff8ac8dc : 0xff66a4b8);
				if (g.kind.equals ("PiTrex")) { int col = on ? Main.HI2 : Main.HI; name.setTextColor (col); name.setCompoundDrawablePadding ((int) m.dp (8)); name.setCompoundDrawables (raspberry (col), null, null, null); }
				else name.setCompoundDrawables (null, null, null, null);
				c.setBackground (on ? Screens.focus (m, m.dp (8)) : now ? Main.round (Main.PANEL, Main.EDGE, m.dp (8)) : null); return c; }
			box = c.findViewWithTag ("b"); img = c.findViewWithTag ("i"); ph = c.findViewWithTag ("p");
			int cols = Math.max (1, perRow ()); int w = (par.getWidth () - par.getPaddingLeft () - par.getPaddingRight () - (int) m.dp (12) * (cols - 1)) / cols;
			LayoutParams bl = (LayoutParams) box.getLayoutParams (); int hh = (int) (Math.max (w, 50) * aspect ()); if (bl.height != hh) { bl.height = hh; box.setLayoutParams (bl); }
			Bitmap b = tile (g.name, Math.max (w, 50)); img.setImageBitmap (b); ph.setText (b == null ? g.name.toUpperCase (Locale.ROOT) : "");
			img.setScaleType (look ().equals ("overlays") ? ImageView.ScaleType.FIT_XY : ImageView.ScaleType.FIT_CENTER);
			box.setBackground (b == null ? Main.round (Main.PANEL, on ? Main.HI2 : now ? Main.HI : Main.EDGE, m.dp (8)) : on || now ? Main.round (0, on ? Main.HI2 : Main.HI, m.dp (8)) : null);
			box.setForeground (on ? Screens.ring (m, m.dp (8)) : null);
			return c; }
	};
}
