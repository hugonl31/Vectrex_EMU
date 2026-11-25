/* Pack.java - the private test build's content and log, as the iOS app's: the parts (the Vectrex and the games needed,
 * box art, cartridges, UVMC2, PiTrex chosen) from the project's server - new and changed parts downloaded on the start
 * screen before the game (the owner: no playing while downloading), the progress shown -, and the app's log sent live.
 * Only in a build with the token (assets/token.txt, put in by the build from outside the repository); without it the app
 * uses the files already on the device.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class Pack {
	static final String HOST = "http://172.233.60.40:7746";
	static String tok; static boolean tokRead, busy; static JSONArray parts;
	static String token (Main m)
	{
		if (tokRead) return tok; tokRead = true;
		try (InputStream in = m.getAssets ().open ("token.txt")) { String t = new String (read (in), StandardCharsets.UTF_8).trim (); tok = t.isEmpty () ? null : t; } catch (Exception e) { tok = null; }
		return tok;
	}
	static byte[] read (InputStream in) throws java.io.IOException { ByteArrayOutputStream o = new ByteArrayOutputStream (); byte[] b = new byte[65536]; int n; while ((n = in.read (b)) > 0) o.write (b, 0, n); return o.toByteArray (); }
	static File partsFile (Main m) { return new File (m.files, "parts.json"); }
	static JSONObject installed (Main m) { try { return new JSONObject (new String (java.nio.file.Files.readAllBytes (partsFile (m).toPath ()), StandardCharsets.UTF_8)); } catch (Exception e) { return new JSONObject (); } }
	static void setInstalled (Main m, String id, String v) { try { JSONObject j = installed (m); j.put (id, v); try (FileOutputStream o = new FileOutputStream (partsFile (m))) { o.write (j.toString ().getBytes (StandardCharsets.UTF_8)); } } catch (Exception e) { } }
	/* at the start: the list from the server; the needed and the changed parts downloaded first */
	static void check (Main m, boolean have)
	{
		if (busy) return;   /* (once at a time: the start and coming back both asked - two downloads ran into the same files) */
		m.status ("Looking for games..."); busy = true;
		new Thread (() -> {
			JSONArray p = null; String err = "";
			try { HttpURLConnection c = (HttpURLConnection) new URL (HOST + "/iosextra/packjson").openConnection (); c.setConnectTimeout (8000); c.setReadTimeout (8000);
				c.setRequestProperty ("X-Vectrex-Token", tok); if (c.getResponseCode () == 200) p = new JSONObject (new String (read (c.getInputStream ()), StandardCharsets.UTF_8)).optJSONArray ("parts"); else err = "HTTP " + c.getResponseCode (); }
			catch (Exception e) { err = e.toString (); }
			final JSONArray pp = p; final String er = err;
			m.h.post (() -> {
				JSONObject inst = installed (m); Main.log ("parts: server " + (pp != null ? pp.length () : 0) + ", here " + inst.length () + (er.isEmpty () ? "" : " - " + er));
				if (pp == null || pp.length () == 0) { busy = false; if (have || inst.length () > 0) m.startCore (); else m.status ("The games could not be fetched: no connection to the project's server. Close the app and try again."); return; }
				parts = pp; List<String> need = new ArrayList<> ();
				for (int i = 0; i < pp.length (); i++) { JSONObject x = pp.optJSONObject (i); String v = inst.optString (x.optString ("id"), null);
					if ((v != null && !v.equals (x.optString ("version"))) || (v == null && x.optBoolean ("required"))) need.add (x.optString ("id")); }
				if (inst.length () == 0 && !have) { Main.log ("first start: the choice of parts"); m.status ("Choose what to download"); choose (m, true); }   /* (as the
					                                                   iPhone app: asked first, everything on - the screens now lie in front of the start screen) */
				else if (!need.isEmpty ()) fetch (m, need);
				else { busy = false; m.startCore (); } });
		}).start ();
	}
	/* files no longer shipped: a pack update only adds, so they stay on a phone - removed here at the start and after an
	 * update (as the PC's RETIRED_ROMS): a game only as it was shipped (its MD5 - a player's own copy stays), its pictures */
	static final String[][] RETIRED = { { "roms/UVMC2 Demo.um2", "a5a8fddb38477db65497449b040beb78" }, { "system/vectrex_emu/thumbs/carts/UVMC2 Demo.png", null } };
	static void retire (Main m)
	{
		for (String[] r : RETIRED) { File f = new File (m.ext (), r[0]); if (!f.isFile ()) continue;
			try { if (r[1] != null && !r[1].equalsIgnoreCase (VX.md5 (f.getAbsolutePath ()))) { Main.log ("retired: " + r[0] + " kept (not the shipped file)"); continue; }
				Main.log ("retired: " + r[0] + (f.delete () ? " removed" : " could not be removed")); }
			catch (Exception e) { Main.log ("retired: " + r[0] + " - " + e); } }
	}
	/* the choice of parts: the needed ones always (the first start), the rest chosen */
	static void choose (Main m, boolean first)
	{
		if (parts == null) { m.toast ("The list of parts is not here yet", 2); return; }
		Screens.Panel p = new Screens.Panel (m, first ? "Download the games" : "Downloads"); JSONObject inst = installed (m);
		final ArrayList<String> pick = new ArrayList<> ();
		p.header (first ? "Needed parts come always - choose the rest" : "Parts");
		final TextView[] sum = new TextView[1];
		final Runnable total = () -> { long sz = 0; for (int i = 0; i < parts.length (); i++) if (pick.contains (parts.optJSONObject (i).optString ("id"))) sz += parts.optJSONObject (i).optLong ("size");
			if (sum[0] != null) sum[0].setText (sz > 0 ? String.format (Locale.ROOT, "To download: %.0f MB%s", sz / 1e6, metered (m) ? " - you are on mobile data" : "") : "Nothing to download"); };
		for (int i = 0; i < parts.length (); i++) { JSONObject x = parts.optJSONObject (i); final String id = x.optString ("id"); final boolean req = x.optBoolean ("required");
			String v = inst.optString (id, null); boolean have = v != null, cur = have && v.equals (x.optString ("version"));
			if ((req && !cur) || (first && !cur)) pick.add (id);   /* (the first time: everything on - as the iPhone app) */
			final String detail = String.format (Locale.ROOT, "%.0f MB - %s", x.optLong ("size") / 1e6, req ? "needed" : cur ? "here" : have ? "a new version" : "optional");
			LinearLayout r = p.row ((pick.contains (id) ? "✓  " : "     ") + x.optString ("name"), detail, null);
			if (!req) { r.setFocusable (true); r.setClickable (true); r.setOnClickListener (vw -> { TextView t = r.findViewWithTag ("t");
				if (pick.contains (id)) { pick.remove (id); t.setText ("     " + x.optString ("name")); } else { pick.add (id); t.setText ("✓  " + x.optString ("name")); } total.run (); }); } }
		p.foot ("The Vectrex and the games are needed: without them nothing plays. The rest: whenever you like (Settings > Downloads).");
		final boolean[] went = { false };
		LinearLayout dl = p.row ("Download", "To download", () -> { went[0] = true; m.pop (); fetch (m, pick); }); sum[0] = dl.findViewWithTag ("d"); total.run ();
		if (first) p.onClose = () -> { if (went[0]) return; final ArrayList<String> req = new ArrayList<> ();   /* (closed at the first start: the needed parts only - nothing plays without them) */
			for (int i = 0; i < parts.length (); i++) if (parts.optJSONObject (i).optBoolean ("required")) req.add (parts.optJSONObject (i).optString ("id"));
			Main.log ("downloads: closed - the needed parts only"); m.h.post (() -> fetch (m, req)); };
		m.push (p);
	}
	static boolean metered (Main m)                 /* (mobile data: said beside the size) */
	{
		try { android.net.ConnectivityManager c = (android.net.ConnectivityManager) m.getSystemService (android.content.Context.CONNECTIVITY_SERVICE); return c != null && c.isActiveNetworkMetered (); }
		catch (Throwable t) { return false; }
	}
	/* the parts downloaded one by one on the start screen, the progress shown; the game after them */
	static void fetch (Main m, List<String> ids)
	{
		if (ids.isEmpty ()) { busy = false; m.startCore (); return; }
		busy = true; if (m.welcome.getParent () == null) { m.root.addView (m.welcome); m.welcome.setAlpha (1); m.hold (true); }
		m.wProg.setVisibility (View.VISIBLE); m.wProg.setProgress (0);
		long total = 0; for (int i = 0; i < parts.length (); i++) if (ids.contains (parts.optJSONObject (i).optString ("id"))) total += parts.optJSONObject (i).optLong ("size");
		final long all = total; Main.log ("parts: fetching " + ids + String.format (Locale.ROOT, " (%.1f MB)", all / 1e6));
		new Thread (() -> {
			long done = 0; boolean ok = true;
			for (String id : ids) { JSONObject x = null; for (int i = 0; i < parts.length (); i++) if (parts.optJSONObject (i).optString ("id").equals (id)) x = parts.optJSONObject (i); if (x == null) continue;
				final String name = x.optString ("name"); File tmp = new File (m.getCacheDir (), "part.tgz");
				try { HttpURLConnection c = (HttpURLConnection) new URL (HOST + "/iosextra/part/" + id).openConnection (); c.setConnectTimeout (15000); c.setReadTimeout (60000);
					c.setRequestProperty ("X-Vectrex-Token", tok); if (c.getResponseCode () != 200) throw new java.io.IOException ("HTTP " + c.getResponseCode ());
					try (InputStream in = c.getInputStream (); OutputStream o = new FileOutputStream (tmp)) { byte[] b = new byte[1 << 16]; int n; long got = 0, lastUi = 0;
						while ((n = in.read (b)) > 0) { o.write (b, 0, n); got += n; long now = System.currentTimeMillis ();
							if (now - lastUi > 200) { lastUi = now; final long a = done + got; m.h.post (() -> { m.wProg.setProgress ((int) (all > 0 ? 1000 * a / all : 0));
								m.status (String.format (Locale.ROOT, "%s\n%.1f of %.1f MB  -  %.0f %%", name, a / 1e6, all / 1e6, all > 0 ? 100.0 * a / all : 0)); }); } } }
					m.h.post (() -> m.status (name + "\nUnpacking..."));
					int files = VX.untgz (tmp.getAbsolutePath (), m.ext ().getAbsolutePath ()); tmp.delete ();
					if (files < 0) throw new java.io.IOException ("not whole");
					setInstalled (m, id, x.optString ("version")); done += x.optLong ("size"); Main.log ("part " + id + ": " + files + " files"); }
				catch (Exception e) { Main.log ("part " + id + ": failed - " + e); ok = false; break; } }
			final boolean fine = ok;
			m.h.post (() -> { m.wProg.setVisibility (View.GONE); busy = false; retire (m);
				if (!fine && installed (m).length () == 0) { m.status ("The games could not be fetched. Close the app and try again."); return; }
				if (!fine) m.status ("The download failed - on with the files here; try again later in Settings > Downloads.");
				if (!m.started) m.h.postDelayed (m::startCore, fine ? 0 : 3000);
				else { m.welcome.animate ().alpha (0).setDuration (400).withEndAction (() -> m.root.removeView (m.welcome)); m.hold (false); } });
		}).start ();
	}
}

/* the app's log, sent live to the project's server (the private test build: new apps start with detailed logs) */
final class Live {
	static String tok;   /* (the live log's key: the private build's token, or the public test build's log-only one - assets/logtoken.txt) */
	static void start (Main m)
	{
		tok = Pack.token (m);
		if (tok == null) try (InputStream in = m.getAssets ().open ("logtoken.txt")) { String t = new String (Pack.read (in), StandardCharsets.UTF_8).trim (); tok = t.isEmpty () ? null : t; } catch (Exception e) { tok = null; }
		if (tok == null) return;
		final String sess = String.format ("d%04x", new java.util.Random ().nextInt (0x10000));
		new Thread (() -> {
			try { File prev = new File (m.files, "applog_previous.txt"); if (prev.exists ()) post (sess + "-app-previous-log", java.nio.file.Files.readAllBytes (prev.toPath ())); } catch (Exception e) { }
			while (true) { try { Thread.sleep (2000); String s = VX.takeLog (); if (!s.isEmpty ()) post (sess + "-app-log", s.getBytes (StandardCharsets.UTF_8)); } catch (Exception e) { } }
		}, "live-log").start ();
	}
	static void post (String kind, byte[] d) throws java.io.IOException
	{
		HttpURLConnection c = (HttpURLConnection) new URL (Pack.HOST + "/ioslog").openConnection (); c.setDoOutput (true); c.setConnectTimeout (10000); c.setReadTimeout (15000);
		c.setRequestProperty ("X-Vectrex-Token", tok); c.setRequestProperty ("X-Vectrex-Kind", kind); c.setRequestProperty ("X-Vectrex-Ext", "txt");
		try (OutputStream o = c.getOutputStream ()) { o.write (d); } c.getResponseCode (); c.disconnect ();
	}
}
