/* Getter.java - Get games & extras, as the PC's (src/frontend/getter.py, the same items, sources and checks): the
 * original GCE games, the free homebrew games that have an overlay, the PiTrex's SD card files and the speech ROM. Each
 * item has a chain of public sources tried in turn - the project's own copies (Google Drive, archive.org), the other
 * archive.org sets, GitHub, the authors' pages, the Wayback Machine -; a download is checked before anything of it is
 * unpacked (the project's copies by their exact bytes, a set of games by the games' fingerprints, the PiTrex an SD card)
 * and named as the emulator lists it. Files already there are kept. Nothing of it comes with the app: the user downloads
 * it and takes the responsibility (the disclaimer, accepted once). Add files...: the user's own (a game, a zip of games,
 * the speech ROM, a PiTrex SD card zip), through Android's file picker. The public build's downloads - the private test
 * build has the project's own server (Pack).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.content.Intent;
import android.net.Uri;
import android.widget.LinearLayout;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;

final class Getter {
	static final String DISCLAIMER = "These files are not part of Vectrex_EMU and do not come with it. The maintainer of Vectrex_EMU on GitHub "
		+ "does not take responsibility for the sharing of these files. You, the user, download them yourself from "
		+ "the sites named, and you take the responsibility for doing so and for respecting their copyrights, which "
		+ "belong to their owners. (The original Vectrex games are free for non-commercial use by Smith Engineering's "
		+ "permission; the homebrew games, the overlays, the PiTrex files and the speech ROM belong to their authors.)";
	static final String[][] WHAT = {
		{ "GCE Vectrex games", "the project's copy (Google Drive, archive.org), archive.org" },
		{ "Free homebrew games", "the project's copy (Google Drive, archive.org), archive.org, GitHub (Malban, MiSTer)" },
		{ "PiTrex SD card files", "Malban's site, the project's copy (archive.org, Google Drive), the Wayback Machine" },
		{ "Speech ROM (al2.bin)", "the project's copy (Google Drive, archive.org), Joe Zbiciak's site, the Wayback Machine" },
		{ "Box art and labels (full set)", "this project's GitHub release (its own remade artwork)" } };

	/* ------------------------------------------------------------ the games' names (getter.py) */
	static final String[] GCE = { "3D Crazy Coaster", "3D Mine Storm", "3D Narrow Escape", "Armor Attack", "Bedlam", "Berzerk",
		"Blitz! Action Football", "Clean Sweep", "Cosmic Chasm", "Dark Tower (Proto)", "Fortress of Narzod",
		"Heads-Up Action Soccer", "HyperChase Auto Race", "Lunar Lander (Proto)", "Mail Plane (Proto)", "Mine Storm",
		"Mine Storm II", "Mine Storm III (Proto)", "Mr. Boston Clean Sweep", "Pitcher's Duel (Proto)", "Polar Rescue",
		"Pole Position", "Rip Off", "Rocket Sledge (Proto)", "Scramble", "Solar Quest", "Space Wars", "Spike",
		"Spinball", "Star Castle", "Star Ship", "Star Trek - The Motion Picture", "StarHawk", "Tour De France (Proto)",
		"Web Wars", "ZZAP (Proto)" };
	static final String[] HOMEBREW = { "Moon Lander", "Nebula Commander", "Omega Chase", "Patriots", "Spike Hoppin'", "Thrust", "Vectrexians",
		"Web Warp", "Telengard", "Vectorblade", "Neon Hawk (Demo)" };
	static final Map<String, String> KNOWN = new HashMap<> (), HB_KNOWN = new HashMap<> (), SUMS = new HashMap<> ();
	static {
		for (String n : GCE) KNOWN.put (key (n.replaceAll ("\\s*\\(Proto\\)", "")), n);
		String[][] al = { { "zzapscrollingspacegame", "ZZAP (Proto)" }, { "minestorm3", "Mine Storm III (Proto)" }, { "hyperchase", "HyperChase Auto Race" },
			{ "blitz", "Blitz! Action Football" }, { "blitzfootball", "Blitz! Action Football" }, { "headsup", "Heads-Up Action Soccer" },
			{ "headsupsoccer", "Heads-Up Action Soccer" }, { "startrek", "Star Trek - The Motion Picture" } };
		for (String[] a : al) KNOWN.put (a[0], a[1]);
		for (String n : HOMEBREW) HB_KNOWN.put (key (n.replaceAll ("\\s*\\(Demo\\)", "")), n);
		HB_KNOWN.put ("neonhawkdemo", "Neon Hawk (Demo)"); HB_KNOWN.put ("telengard256kflash", "Telengard"); HB_KNOWN.put ("vectrexthrust", "Thrust");
		String[] s = { "a81c9e4ca6c68e42", "3D Crazy Coaster", "e3b8594d71d33cc9", "3D Mine Storm", "8988075388910d62", "3D Narrow Escape",
			"eb76f860130f8cc8", "Armor Attack", "6efd76b38d603797", "Bedlam", "6e27b509020260e3", "Berzerk", "d4fd15ceed0d4eff", "Blitz! Action Football",
			"67f8513958c04e93", "Clean Sweep", "56a7f277bf09f944", "Cosmic Chasm", "fbfd58464d42986d", "Dark Tower (Proto)", "bc27411caba93e4d", "Fortress of Narzod",
			"0406c13952fb0bd6", "Heads-Up Action Soccer", "a246f1d94a5b062b", "HyperChase Auto Race", "4318d453257b3599", "Lunar Lander (Proto)",
			"05f56e708f752ca8", "Mail Plane (Proto)", "5f756a350f7c163f", "Mine Storm", "39dbec9632885095", "Mine Storm II", "b456780dfbb2a5ec", "Mine Storm III (Proto)",
			"bc244985be4108c1", "Moon Lander", "765af69f6a231590", "Mr. Boston Clean Sweep", "d30fade000f73db3", "Nebula Commander", "42427dc02a6a25c0", "Neon Hawk (Demo)",
			"c390f580f841b270", "Omega Chase", "228dc86174f12aca", "Patriots", "4de711490c7edfe1", "Pitcher's Duel (Proto)", "a501ef26d670d50b", "Polar Rescue",
			"b0c0b12cbea918d8", "Pole Position", "22f7a490f6f7aaa3", "Rip Off", "526b347bc02a47c8", "Rocket Sledge (Proto)", "38e38b5c60466146", "Scramble",
			"b76f14c824344c02", "Solar Quest", "70c26d3974219ef4", "Space Wars", "9a2f8d06286c4315", "Spike", "775d6e5fd438246a", "Spike Hoppin'",
			"2e8285061530ccad", "Spinball", "5444ef96ef9fd758", "Star Castle", "d182eac5a54ffc64", "Star Ship", "3a2ee61316fb316d", "Star Trek - The Motion Picture",
			"86c6e6039ecdf971", "StarHawk", "ffc1e68c7b7fc721", "Telengard", "a29a9974706b5cff", "Telengard", "451ba51be3f94b3a", "Thrust",
			"4f461b0b51c82433", "Tour De France (Proto)", "95d4704dd59fe584", "Vectorblade", "9b40748fb9e395f7", "Vectrexians", "8e984cbb6c85d9bc", "Web Warp",
			"922621cf8e0fa4d8", "Web Wars", "1db76a96327738be", "ZZAP (Proto)" };
		for (int i = 0; i < s.length; i += 2) SUMS.put (s[i], s[i + 1]);
	}
	static String key (String s) { return s.toLowerCase (Locale.ROOT).replaceAll ("[^a-z0-9]", ""); }
	static final Pattern BAD = Pattern.compile ("\\[(b|h|a|f|o|t|p)\\d*\\]");
	static String hbName (String stem)
	{
		if (BAD.matcher (stem.toLowerCase (Locale.ROOT)).find ()) return null;
		String t = stem.replaceAll ("(?i)^zzz-unk-", "").replaceAll ("\\s*[\\(\\[][^\\)\\]]*[\\)\\]]", "").replaceAll ("(?i)\\s+by\\s+.*$", "").trim ().replaceAll ("\\s+v?\\d+(\\.\\d+)+$", "");
		return HB_KNOWN.get (key (t));
	}
	static String gameName (String stem)
	{
		String low = stem.toLowerCase (Locale.ROOT);
		if (BAD.matcher (low).find () || low.contains ("(pd)")) return null;
		if (low.contains ("boston")) return "Mr. Boston Clean Sweep";
		String title = stem.replaceAll ("\\s*[\\(\\[][^\\)\\]]*[\\)\\]]", "").trim (); String k = key (title);
		if (KNOWN.containsKey (k)) return KNOWN.get (k);
		return title.isEmpty () ? null : title + (low.contains ("proto") ? " (Proto)" : "");
	}
	static String hex (byte[] b) { StringBuilder s = new StringBuilder (); for (byte x : b) s.append (String.format ("%02x", x & 255)); return s.toString (); }
	static String sha (String alg, byte[] d, int n) { try { MessageDigest m = MessageDigest.getInstance (alg); m.update (d, 0, n); return hex (m.digest ()); } catch (Exception e) { return ""; } }
	static int trimmed (byte[] d) { int n = d.length; while (n > 0 && d[n - 1] == (byte) 0xff) n--; while (n > 0 && d[n - 1] == 0) n--; return n; }
	static String fp (byte[] d) { return sha ("SHA-1", d, trimmed (d)).substring (0, 16); }   /* (a game's bytes without the padding at the end) */
	static boolean romExt (String n) { String l = n.toLowerCase (Locale.ROOT); return l.endsWith (".bin") || l.endsWith (".vec") || l.endsWith (".gam") || l.endsWith (".rom"); }
	static String stem (String n) { String b = n.substring (n.lastIndexOf ('/') + 1); int d = b.lastIndexOf ('.'); return d > 0 ? b.substring (0, d) : b; }

	/* ------------------------------------------------------------ what is there */
	static File roms (Main m) { return new File (m.ext (), "roms"); }
	static File pxDir (Main m) { return new File (m.sysDir, "vectrex_emu/pitrex"); }
	static File speechFile (Main m) { return new File (m.sysDir, "vectrex_emu/al2.bin"); }
	static List<File> romFiles (Main m)
	{
		List<File> out = new ArrayList<> (); List<File> roots = new ArrayList<> (m.romRoots); if (!roots.contains (roms (m))) roots.add (roms (m));
		for (File r : roots) walk (r, out, 0); return out;
	}
	static void walk (File d, List<File> out, int depth)
	{
		File[] fs = d.listFiles (); if (fs == null || depth > 4) return;
		for (File f : fs) if (f.isDirectory ()) walk (f, out, depth + 1); else if (romExt (f.getName ()) && f.length () <= (1 << 20)) out.add (f);
	}
	static byte[] readFile (File f) throws IOException { try (InputStream in = new FileInputStream (f)) { return Pack.read (in); } }
	static Set<String> present (Main m)
	{
		Set<String> have = new HashSet<> ();
		for (File f : romFiles (m)) { String s = stem (f.getName ());
			for (String n : new String[] { s, gameName (s), hbName (s) }) if (n != null && (Arrays.asList (GCE).contains (n) || Arrays.asList (HOMEBREW).contains (n))) have.add (n);
			try { String n = SUMS.get (fp (readFile (f))); if (n != null) have.add (n); } catch (Exception e) { } }
		return have;
	}
	static int count (Set<String> have, String[] of) { int n = 0; for (String s : of) if (have.contains (s)) n++; return n; }
	static String state (Main m, int k, Set<String> have)
	{
		switch (k) {
		case 0: return count (have, GCE) + " of the " + GCE.length + " original games";
		case 1: return count (have, HOMEBREW) + " of " + HOMEBREW.length + " free homebrew games";
		case 2: return new File (pxDir (m), "pitrex.img").exists () ? "found" : "not there";
		case 5: return new File (roms (m), "VecDoom.um2").exists () && new File (roms (m), "Tests/Photo Test.bin").exists () ? "found" : "not there";   /* (the test carts too: 2026-11-25) */
		case 4: { String[] l = new File (m.sysDir, "vectrex_emu/thumbs/labels").list (); int n = l == null ? 0 : l.length; return n >= 250 ? "found (" + n + " labels)" : n > 0 ? n + " labels" : "the 112 that come with the app"; }
		default: return speechFile (m).exists () || new File (m.sysDir, "vectrex_emu/sp0256-al2.bin").exists () ? "found" : "not there"; }
	}
	static boolean complete (Main m, int k, Set<String> have)
	{
		return k == 0 ? count (have, GCE) >= GCE.length : k == 1 ? count (have, HOMEBREW) >= HOMEBREW.length : state (m, k, have).startsWith ("found");
	}

	/* ------------------------------------------------------------ the sources: each a chain of addresses */
	static final String DRIVE = "https://drive.usercontent.google.com/download?export=download&confirm=t&id=";
	static final String DRIVE_GAMES = DRIVE + "1dcRdUxgMg2yX0ux-izPzcttY0SislQgF", DRIVE_SPEECH = DRIVE + "1L_fWaxPa4KLWnE7-RNCDqmI1clzSXxPP",
		DRIVE_PITREX = DRIVE + "17XNGNgabHEQ0eNc7zzuPhTPxmZ7G4Rri", DRIVE_HOMEBREW = DRIVE + "1N2lCNS8VSlBQeCqKp40nYoQVegHyhub7";
	static final String DRIVE_PITREX_NAME = "PiTrex_sd_Jul29_25";   /* (the version the project's copy of the PiTrex's SD card is) */
	static final String MIRROR = "https://archive.org/download/vectrex-emu-extras/", MIRROR_GAMES = MIRROR + "GCE_Vectrex_games.zip",
		MIRROR_HOMEBREW = MIRROR + "Vectrex_homebrew_free.zip", MIRROR_PITREX = MIRROR + "PiTrex_sd_Jul29_25.zip", MIRROR_SPEECH = MIRROR + "al2.bin";
	/* the full artwork set (the project's own remade box art and labels, 280 games - Simon Quincey's title artwork and
	 * pictures with his permission): from the project's GitHub release, the fastest source for a big file */
	static final String ART = "https://github.com/hugonl31/Vectrex_EMU/releases/download/artwork-1/Vectrex_EMU-artwork-full.zip";
	static final Map<String, Object[]> PINNED = new HashMap<> ();
	static {
		Object[] g = { 169168L, "deaa7cf0ca07c5a57b9dd65f57089dc87a17de946f8d1c5246e992db8fa9d92d" }, h = { 148368L, "c2b7987d991b8599f1563606897ccd6ba2124b0cab64372ace026546aa37aed8" },
			s = { 2048L, "6a4c0cd4aa9ae472ce28109b650680c76f76a53e6d6fd63af6b9bfde801a9e48" }, p = { 188168647L, "237b4f61b267c15a1d1d3c16e761f4e63d31da11b24a1d25bec1e35b4a8d81ab" };
		PINNED.put (DRIVE_GAMES, g); PINNED.put (MIRROR_GAMES, g); PINNED.put (DRIVE_HOMEBREW, h); PINNED.put (MIRROR_HOMEBREW, h);
		PINNED.put (DRIVE_SPEECH, s); PINNED.put (MIRROR_SPEECH, s); PINNED.put (DRIVE_PITREX, p); PINNED.put (MIRROR_PITREX, p);
		PINNED.put (ART, new Object[] { 96438203L, "375fb111813e7a11e69a96a5b9baa11d9f42baf521b2dc5067d833fc2448defe" });
	}
	static final String SPEECH_SHA = "6a4c0cd4aa9ae472ce28109b650680c76f76a53e6d6fd63af6b9bfde801a9e48";
	interface Step { List<String> urls () throws Exception; }
	static Step one (String u) { return () -> Collections.singletonList (u); }
	static String getText (String url, int timeout) throws IOException { HttpURLConnection c = open (url, timeout); try (InputStream in = c.getInputStream ()) { return new String (Pack.read (in), "UTF-8"); } }
	static String dec (String s) { try { return java.net.URLDecoder.decode (s.replace ("+", "%2B"), "UTF-8"); } catch (Exception e) { return s; } }   /* (a file's name in its address: as the file is called) */
	static String enc (String s) { try { return URLEncoder.encode (s, "UTF-8").replace ("+", "%20"); } catch (Exception e) { return s; } }
	static List<String> iaSearch (String q)
	{
		List<String> out = new ArrayList<> ();
		try { JSONArray d = new JSONObject (getText ("https://archive.org/advancedsearch.php?q=" + enc (q) + "&fl%5B%5D=identifier&sort%5B%5D=addeddate%20desc&rows=5&output=json", 30))
				.getJSONObject ("response").getJSONArray ("docs");
			for (int i = 0; i < d.length (); i++) out.add (d.getJSONObject (i).getString ("identifier")); } catch (Exception e) { }
		return out;
	}
	static String iaZip (String ident, String pick)
	{
		try { JSONArray f = new JSONObject (getText ("https://archive.org/metadata/" + ident, 30)).getJSONArray ("files"); String best = null; long bs = -1;
			for (int i = 0; i < f.length (); i++) { JSONObject x = f.getJSONObject (i); String n = x.optString ("name");
				if (!n.toLowerCase (Locale.ROOT).endsWith (".zip") || !"original".equals (x.optString ("source")) || n.toLowerCase (Locale.ROOT).matches (".*(extra|patch).*")) continue;
				if (pick != null && !n.toLowerCase (Locale.ROOT).contains (pick)) continue;
				long sz = x.optLong ("size", 0); if (sz > bs) { bs = sz; best = n; } }
			return best != null ? "https://archive.org/download/" + ident + "/" + enc (best) : null; } catch (Exception e) { return null; }
	}
	static List<String> wayback (String prefix, String pattern)
	{
		List<String[]> hits = new ArrayList<> ();
		try { for (String ln : getText ("https://web.archive.org/cdx/search/cdx?url=" + enc (prefix) + "&matchType=prefix&fl=original,timestamp,length&filter=statuscode:200", 60).split ("\n")) {
				String[] p = ln.trim ().split (" "); if (p.length == 3 && Pattern.compile (pattern, Pattern.CASE_INSENSITIVE).matcher (p[0]).find () && p[2].matches ("\\d+") && Long.parseLong (p[2]) > 1000) hits.add (new String[] { p[1], p[0] }); } }
		catch (Exception e) { }
		Collections.sort (hits, (a, b) -> b[0].compareTo (a[0])); List<String> out = new ArrayList<> ();
		for (String[] h : hits) out.add ("https://web.archive.org/web/" + h[0] + "id_/" + h[1]); return out;
	}
	static List<Step> chain (int k)
	{
		List<Step> c = new ArrayList<> (); final String mi = "https://raw.githubusercontent.com/Videodr0me/Vectrex_MiSTer/master/games/";
		switch (k) {
		case 0: c.add (one (DRIVE_GAMES)); c.add (one (MIRROR_GAMES)); c.add (() -> Collections.singletonList (iaZip ("vectrex-champion-collection", "collection")));
			c.add (() -> { List<String> o = new ArrayList<> (); for (String id : iaSearch ("title:(vectrex champion collection)")) o.add (iaZip (id, "collection")); return o; });
			c.add (one ("https://archive.org/download/vectrex-champion-collection/Vectrex%20Champion%20Collection.zip"));
			c.add (() -> Arrays.asList (iaZip ("gce-vectrex-games-complete", null), iaZip ("GCE_Vectrex_TOSEC_2012_04_23", null), iaZip ("vectrex_202604", null))); break;
		case 1: c.add (one (DRIVE_HOMEBREW)); c.add (one (mi + "Vectorblade/Vectorblade%20(v1.13).bin")); c.add (one (mi + "NeonHawk/Neon%20Hawk%20Demo%20(v1.0).bin"));
			c.add (one ("https://raw.githubusercontent.com/malbanGit/Telengard/master/Telengard_256kFlash.bin")); c.add (one (MIRROR_HOMEBREW));
			c.add (() -> Arrays.asList (iaZip ("gce-vectrex-games-complete", null), iaZip ("GCE_Vectrex_TOSEC_2012_04_23", null))); c.add (one (mi + "Telengard/Telengard%20(v1.03).bin")); break;
		case 2: c.add (() -> { List<String> o = new ArrayList<> (); try { String page = getText ("http://vide.malban.de/pitrex/pitrex-baremetal-download", 30); Set<Integer> ids = new java.util.TreeSet<> (Collections.reverseOrder ());
					/* (Malban's site is slow for 190 MB: when the newest file it offers is the version the project's own
					 *  copy has, that copy first - Google Drive, checked byte for byte; a newer version from his site) */
					Set<String> names = new HashSet<> (); Matcher nm = Pattern.compile ("PiTrex_sd_\\w+\\.zip", Pattern.CASE_INSENSITIVE).matcher (page); while (nm.find ()) names.add (nm.group ().toLowerCase (Locale.ROOT));
					if (!names.isEmpty () && names.equals (Collections.singleton (DRIVE_PITREX_NAME.toLowerCase (Locale.ROOT) + ".zip"))) o.add (DRIVE_PITREX);
					Matcher mm = Pattern.compile ("download_id=(\\d+)").matcher (page);
					while (mm.find ()) { String after = page.substring (mm.start (), Math.min (page.length (), mm.start () + 1500)); if (Pattern.compile ("PiTrex_sd_\\w+\\.zip", Pattern.CASE_INSENSITIVE).matcher (after).find ()) ids.add (Integer.parseInt (mm.group (1))); }
					for (int id : ids) o.add ("http://vide.malban.de/?sdm_process_download=1&download_id=" + id); } catch (Exception e) { } return o; });
			c.add (one ("http://vide.malban.de/?sdm_process_download=1&download_id=7074")); c.add (one ("http://vectrex.malban.de/pitrex/PiTrex_sd_Jul29_25.zip")); c.add (one (MIRROR_PITREX));
			c.add (() -> wayback ("vectrex.malban.de/pitrex/", "(pitrex_sd_|sd_drive_)[^/]*\\.zip$")); break;
		case 4: c.add (one (ART)); break;
		default: c.add (one (DRIVE_SPEECH)); c.add (one ("http://spatula-city.org/~im14u2c/sp0256-al2/al2.bin")); c.add (one (MIRROR_SPEECH));
			c.add (() -> wayback ("spatula-city.org/~im14u2c/sp0256-al2/al2.bin", "al2\\.bin$")); }
		return c;
	}

	/* ------------------------------------------------------------ fetched and checked */
	interface Progress { void at (long done, long total); }
	static HttpURLConnection open (String url, int timeout) throws IOException
	{
		for (int hop = 0; hop < 8; hop++) {   /* (redirects by hand: Android does not follow one from http to https) */
			HttpURLConnection c = (HttpURLConnection) new URL (url).openConnection (); c.setInstanceFollowRedirects (false);
			c.setConnectTimeout (20000); c.setReadTimeout (timeout * 1000); c.setRequestProperty ("User-Agent", "Vectrex_EMU");
			int code = c.getResponseCode ();
			if (code >= 300 && code < 400 && c.getHeaderField ("Location") != null) { url = new URL (new URL (url), c.getHeaderField ("Location")).toString (); c.disconnect (); continue; }
			if (code != 200) { c.disconnect (); throw new IOException ("HTTP " + code); }
			return c; }
		throw new IOException ("too many redirects");
	}
	static void fetch (String url, File to, Progress p) throws IOException
	{
		HttpURLConnection c = open (url, 90); long total = c.getContentLengthLong (), done = 0, last = 0;
		try (InputStream in = c.getInputStream (); OutputStream o = new FileOutputStream (to)) { byte[] b = new byte[1 << 16]; int n;
			while ((n = in.read (b)) > 0) { o.write (b, 0, n); done += n; long now = System.currentTimeMillis (); if (now - last > 250) { last = now; p.at (done, total); } } }
		p.at (done, total);
	}
	static String fileSha256 (File f) throws IOException
	{
		try (InputStream in = new FileInputStream (f)) { MessageDigest m = MessageDigest.getInstance ("SHA-256"); byte[] b = new byte[1 << 16]; int n; while ((n = in.read (b)) > 0) m.update (b, 0, n); return hex (m.digest ()); }
		catch (java.security.NoSuchAlgorithmException e) { throw new IOException (e); }
	}
	static boolean isZip (File f) { try (InputStream in = new FileInputStream (f)) { byte[] b = new byte[4]; return in.read (b) == 4 && b[0] == 'P' && b[1] == 'K'; } catch (Exception e) { return false; } }
	static boolean isZip (byte[] b) { return b.length > 4 && b[0] == 'P' && b[1] == 'K'; }
	/* the item's games (by their fingerprints) a zip has - zips in zips too */
	static void found (byte[] zip, int k, Set<String> out, int depth)
	{
		List<String> want = Arrays.asList (k == 0 ? GCE : HOMEBREW);
		try (ZipInputStream z = new ZipInputStream (new java.io.ByteArrayInputStream (zip))) { ZipEntry e;
			while ((e = z.getNextEntry ()) != null) { if (e.isDirectory ()) continue; String n = e.getName ();
				if (n.toLowerCase (Locale.ROOT).endsWith (".zip") && depth < 2) found (Pack.read (z), k, out, depth + 1);
				else if (romExt (n)) { String g = SUMS.get (fp (Pack.read (z))); if (g != null && want.contains (g)) out.add (g); } } }
		catch (Exception e) { }
	}
	static final String[] PITREX_KEYS = { "pitrex.img", "bootcode.bin", "start.elf", "config.txt" };
	/* a download is what it should be: the project's copy that very file, al2.bin its known bytes, a set enough of the
	 * games, a homebrew file a known game, the PiTrex an SD card */
	static boolean check (int k, String url, File f) throws IOException
	{
		Object[] pin = PINNED.get (url);
		if (pin != null) return f.length () == (Long) pin[0] && fileSha256 (f).equals (pin[1]);
		if (k == 3) return f.length () == 2048 && fileSha256 (f).equals (SPEECH_SHA);
		if (k == 2) { if (!isZip (f)) return false; Set<String> tops = new HashSet<> ();
			try (ZipFile z = new ZipFile (f)) { Enumeration<? extends ZipEntry> en = z.entries ();
				while (en.hasMoreElements ()) { String n = en.nextElement ().getName ().replace ('\\', '/'); while (n.endsWith ("/")) n = n.substring (0, n.length () - 1);
					if (n.split ("/").length <= 2) tops.add (n.substring (n.lastIndexOf ('/') + 1).toLowerCase (Locale.ROOT)); } }
			for (String key : PITREX_KEYS) if (!tops.contains (key)) return false; return true; }
		if (f.length () > 64L << 20) return false;
		byte[] d = readFile (f); Set<String> got = new HashSet<> ();
		if (!isZip (d)) { if (k == 0) return false; String g = SUMS.get (fp (d)); return g != null && Arrays.asList (HOMEBREW).contains (g); }
		found (d, k, got, 0); return got.size () >= (k == 0 ? 20 : 6);
	}

	/* ------------------------------------------------------------ unpacked and named */
	static Set<String> ownSums = new HashSet<> (), ownNames = new HashSet<> ();
	static void owned (Main m)
	{
		ownSums.clear (); ownNames.clear ();
		for (File f : romFiles (m)) { String s = stem (f.getName ()); for (String n : new String[] { s, gameName (s), hbName (s) }) if (n != null) ownNames.add (n);
			try { byte[] d = readFile (f); ownSums.add (sha ("SHA-1", d, d.length)); ownSums.add (sha ("SHA-1", d, trimmed (d))); } catch (Exception e) { } }
	}
	/* a game written only if the roms folder has it not yet (the same bytes under another name count): 1 if written */
	static int writeGame (Main m, String name, byte[] d) throws IOException
	{
		File out = new File (roms (m), name + ".bin"); if (out.exists ()) return 0;
		String a = sha ("SHA-1", d, d.length), b = sha ("SHA-1", d, trimmed (d));
		if (ownNames.contains (name) || ownSums.contains (a) || ownSums.contains (b)) return 0;
		ownNames.add (name); ownSums.add (a); ownSums.add (b); out.getParentFile ().mkdirs ();
		try (OutputStream o = new FileOutputStream (out)) { o.write (d); } return 1;
	}
	/* one file of a download as ours: games and homebrew into roms (named), the speech ROM into the system folder */
	static int place (Main m, int k, String path, byte[] d) throws IOException
	{
		if (k == 3) { if (d.length != 2048) return 0; File o = speechFile (m); if (o.exists ()) return 0; o.getParentFile ().mkdirs (); try (OutputStream w = new FileOutputStream (o)) { w.write (d); } return 1; }
		if (!romExt (path)) return 0; String n = k == 0 ? gameName (stem (path)) : hbName (stem (path));
		return n != null ? writeGame (m, n, d) : 0;
	}
	static int unpackBytes (Main m, int k, String name, byte[] d, int depth) throws IOException
	{
		if (!isZip (d)) return place (m, k, name, d);
		int added = 0;
		try (ZipInputStream z = new ZipInputStream (new java.io.ByteArrayInputStream (d))) { ZipEntry e;
			while ((e = z.getNextEntry ()) != null) { if (e.isDirectory ()) continue; String n = e.getName ().replace ('\\', '/'); byte[] b = Pack.read (z);
				added += n.toLowerCase (Locale.ROOT).endsWith (".zip") && depth < 2 ? unpackBytes (m, k, n, b, depth + 1) : place (m, k, n, b); } }
		return added;
	}
	/* the PiTrex's SD card: every file into its folder (one folder round it all taken off), files there kept */
	static int unpackPitrex (Main m, File f) throws IOException
	{
		File dir = pxDir (m); int added = 0;
		try (ZipFile z = new ZipFile (f)) { List<String> names = new ArrayList<> (); Enumeration<? extends ZipEntry> en = z.entries ();
			while (en.hasMoreElements ()) { ZipEntry e = en.nextElement (); if (!e.isDirectory ()) names.add (e.getName ().replace ('\\', '/')); }
			Set<String> tops = new HashSet<> (); boolean allIn = true; for (String n : names) { tops.add (n.split ("/", 2)[0]); if (!n.contains ("/")) allIn = false; }
			String strip = tops.size () == 1 && allIn ? tops.iterator ().next () + "/" : "";
			en = z.entries ();
			while (en.hasMoreElements ()) { ZipEntry e = en.nextElement (); if (e.isDirectory ()) continue; String n = e.getName ().replace ('\\', '/');
				if (!strip.isEmpty () && n.startsWith (strip)) n = n.substring (strip.length ());
				if (n.contains ("..")) continue; File out = new File (dir, n); if (out.exists ()) continue; out.getParentFile ().mkdirs ();
				try (InputStream in = z.getInputStream (e); OutputStream o = new FileOutputStream (out)) { byte[] b = new byte[1 << 16]; int r; while ((r = in.read (b)) > 0) o.write (b, 0, r); }
				added++; } }
		return added;
	}
	/* the artwork set: its system/vectrex_emu/thumbs/... into this app's system folder (where the games list reads them);
	 * a picture already there is kept - the player's own win */
	static int unpackArt (Main m, File f) throws IOException
	{
		int added = 0;
		try (ZipFile z = new ZipFile (f)) { Enumeration<? extends ZipEntry> en = z.entries ();
			while (en.hasMoreElements ()) { ZipEntry e = en.nextElement (); String n = e.getName ().replace ('\\', '/');
				if (e.isDirectory () || !n.startsWith ("system/") || n.contains ("..")) continue;
				File out = new File (m.sysDir, n.substring (7)); if (out.exists ()) continue; out.getParentFile ().mkdirs ();
				try (InputStream in = z.getInputStream (e); OutputStream o = new FileOutputStream (out)) { byte[] b = new byte[1 << 16]; int r; while ((r = in.read (b)) > 0) o.write (b, 0, r); }
				added++; } }
		return added;
	}
	/* an item through its chain: each address until one works (the games: until all of them are there); the files added */
	static int getItem (Main m, int k, Progress p, Say say) throws IOException
	{
		if (k == 5) return getTest (m, p);
		Set<String> tried = new LinkedHashSet<> (); int added = 0; boolean ok = false; File tmp = new File (m.getCacheDir (), "download" + k + ".tmp");   /* (each item its own: Download all runs them side by side - one shared file was overwritten halfway) */
		synchronized (Getter.class) { owned (m); }
		outer:
		for (Step s : chain (k)) {
			List<String> urls; try { urls = s.urls (); } catch (Exception e) { continue; }
			for (String u : urls) {
				if (u == null || !tried.add (u)) continue;
				say.say ("trying " + new URL (u).getHost () + "...");
				try { fetch (u, tmp, p); if (!check (k, u, tmp)) { Main.log ("get: " + u + " - not the file it should be"); continue; }
					synchronized (Getter.class) { added += k == 4 ? unpackArt (m, tmp) : k == 2 ? unpackPitrex (m, tmp) : unpackBytes (m, k, dec (u.substring (u.lastIndexOf ('/') + 1)), readFile (tmp), 0); }
					ok = true; Main.log ("get: " + u + " - " + added + " files");
					if (complete (m, k, present (m))) break outer; }
				catch (Exception e) { Main.log ("get: " + u + " - " + e); }
				finally { tmp.delete (); } } }
		if (!ok) throw new IOException ("none of its sources worked");
		return added;
	}
	interface Say { void say (String s); }

	/* ------------------------------------------------------------ the screen */
	static final String[] TITLE = { "GCE Vectrex games", "Homebrew games (free)", "PiTrex SD card", "Speech ROM (al2.bin)", "Box art and labels (full set)", "Test extras (test build only)" };
	static final String[] SIZE = { "0.2 MB", "1 MB", "190 MB", "2 KB", "96 MB", "25 MB" };
	static final String[] SITE = { "archive.org", "archive.org, GitHub", "vide.malban.de", "spatula-city.org", "this project's GitHub", "the test carts, VecDoom, Doom, Quake, Veccy Road - the project's server" };
	static final int N = TITLE.length;
	static final boolean[] busy = new boolean[N];
	/* the private test build (the owner, 2026-11-25: the app private until he is happy): the public build's page and one
	 * item more - the test extras from the project's server, behind the test token (assets/logtoken.txt). The release
	 * build (VX_RELEASE=1) has no token: the item is not there */
	static String testTok (Main m) { try (InputStream in = m.getAssets ().open ("logtoken.txt")) { String t = new String (Pack.read (in), "UTF-8").trim (); return t.isEmpty () ? null : t; } catch (Exception e) { return null; } }
	static int items (Main m) { return testTok (m) != null ? N : N - 1; }
	static int getTest (Main m, Progress p) throws IOException
	{
		String tok = testTok (m); if (tok == null) throw new IOException ("not in this build");
		File tmp = new File (m.getCacheDir (), "download5.tmp");
		try { HttpURLConnection c = (HttpURLConnection) new URL (Pack.HOST + "/iosextra/part/testx").openConnection (); c.setConnectTimeout (15000); c.setReadTimeout (60000);
			c.setRequestProperty ("X-Vectrex-Token", tok); if (c.getResponseCode () != 200) throw new IOException ("HTTP " + c.getResponseCode ());
			long total = c.getContentLengthLong (), done = 0, last = 0;
			try (InputStream in = c.getInputStream (); OutputStream o = new FileOutputStream (tmp)) { byte[] b = new byte[1 << 16]; int n;
				while ((n = in.read (b)) > 0) { o.write (b, 0, n); done += n; long now = System.currentTimeMillis (); if (now - last > 250) { last = now; p.at (done, total); } } }
			int files = VX.untgz (tmp.getAbsolutePath (), m.ext ().getAbsolutePath ()); Main.log ("get: test extras - " + files + " files");
			if (files < 0) throw new IOException ("not unpacked"); return files; }
		finally { tmp.delete (); }
	}
	static Screens.Panel show (Main m)
	{
		Main.log ("ui get games & extras"); m.findContent (); Set<String> have = present (m);
		Screens.Panel p = new Screens.Panel (m, "Get games & extras");
		p.header ("Download");
		final TextView[] det = new TextView[N];
		for (int i = 0; i < N; i++) if (!busy[i]) said[i] = null;
		for (int i = 0; i < items (m); i++) { final int k = i;   /* (a download going on: its line, orange - as iOS) */
			LinearLayout r = p.row (TITLE[k], said[k] != null ? said[k] : state (m, k, have) + "  -  " + SIZE[k] + ", " + SITE[k], () -> start (m, new int[] { k }));
			det[k] = r.findViewWithTag ("d"); if (busy[k]) det[k].setTextColor (Main.CHG); }
		p.row ("Download all", "What is not here yet", () -> { List<Integer> ks = new ArrayList<> (); Set<String> h = present (m);
			for (int i = 0; i < items (m); i++) if (!complete (m, i, h)) ks.add (i); if (ks.isEmpty ()) { m.toast ("Everything is here", 2); return; }
			int[] a = new int[ks.size ()]; for (int i = 0; i < a.length; i++) a[i] = ks.get (i); start (m, a); });
		p.foot ("Files already there are kept. The box art for 112 games comes with the app; the full set has 280. The PiTrex's SD card (190 MB) and the full box art (96 MB) are large: best on Wi-Fi.");
		p.header ("Your own files");
		p.row ("Add files...", "Games (.bin, .vec, a zip of them), UVMC2 games (.um2), the speech ROM, a PiTrex SD card zip", () -> pick (m));
		p.header ("Notice");
		p.foot (DISCLAIMER);
		rows = det; m.push (p); return p;
	}
	static boolean anyBusy () { for (boolean b : busy) if (b) return true; return false; }
	static TextView[] rows; static final String[] said = new String[N];   /* (a row's line while it downloads, and its result) */
	static void say (Main m, int k, String s) { m.h.post (() -> { said[k] = s; if (rows != null && rows[k] != null) { rows[k].setText (s); rows[k].setTextColor (busy[k] ? Main.CHG : Main.DIM); } }); }
	static void start (Main m, int[] ks)
	{
		if (m.prefs.getBoolean ("dl.agreed", false)) { run (m, ks); return; }
		StringBuilder s = new StringBuilder ("The files and where they come from:\n");
		for (String[] w : WHAT) s.append ("\n").append (w[0]).append (": ").append (w[1]);
		Screens.Panel p = new Screens.Panel (m, "Before you download");   /* (the app's own panel, as the iOS app's prompt) */
		p.header ("Notice"); p.body (DISCLAIMER);
		p.row ("I agree - download", "", () -> { m.prefs.edit ().putBoolean ("dl.agreed", true).apply (); Main.log ("downloads: the notice agreed to"); m.pop (); run (m, ks); });
		p.row ("Cancel", "", m::pop);
		p.foot (s.toString ()); m.push (p);
	}
	static void run (Main m, int[] ks)
	{
		for (int k : ks) { if (busy[k]) continue; busy[k] = true; final int kk = k; say (m, k, "starting...");
			new Thread (() -> {
				String res;
				try { int n = getItem (m, kk, (done, total) -> say (m, kk, total > 0 ? String.format (Locale.ROOT, "%.1f of %.1f MB  -  %.0f %%", done / 1e6, total / 1e6, 100.0 * done / total) : String.format (Locale.ROOT, "%.1f MB", done / 1e6)),
						s -> say (m, kk, s));
					res = state (m, kk, present (m)) + (n > 0 ? "  -  " + n + " files added" : "  -  nothing new"); }
				catch (Exception e) { res = "Could not be downloaded: " + e.getMessage () + ". Try again later, or get it by hand and use Add files."; Main.log ("get " + TITLE[kk] + ": " + e); }
				final String r = res; busy[kk] = false;
				say (m, kk, r); m.h.post (() -> m.toast (TITLE[kk] + ": " + r, 3)); }, "get-" + k).start (); }
	}

	/* ------------------------------------------------------------ Add files...: the user's own, through the file picker */
	static final int PICK = 41;
	static void pick (Main m)
	{
		Intent i = new Intent (Intent.ACTION_OPEN_DOCUMENT); i.addCategory (Intent.CATEGORY_OPENABLE); i.setType ("*/*"); i.putExtra (Intent.EXTRA_ALLOW_MULTIPLE, true);
		try { m.startActivityForResult (i, PICK); } catch (Exception e) { m.toast ("No file picker on this device", 2.5); }
	}
	static void picked (Main m, Intent data)
	{
		if (data == null) return; final List<Uri> uris = new ArrayList<> ();
		if (data.getClipData () != null) for (int i = 0; i < data.getClipData ().getItemCount (); i++) uris.add (data.getClipData ().getItemAt (i).getUri ());
		else if (data.getData () != null) uris.add (data.getData ());
		m.toast ("Adding " + uris.size () + (uris.size () == 1 ? " file..." : " files..."), 2);
		new Thread (() -> {
			int games = 0, other = 0; File tmp = new File (m.getCacheDir (), "import.tmp");
			synchronized (Getter.class) { owned (m); }
			for (Uri u : uris) {
				String name = "file"; try (android.database.Cursor c = m.getContentResolver ().query (u, null, null, null, null)) { if (c != null && c.moveToFirst ()) name = c.getString (c.getColumnIndexOrThrow (android.provider.OpenableColumns.DISPLAY_NAME)); } catch (Exception e) { }
				try (InputStream in = m.getContentResolver ().openInputStream (u); OutputStream o = new FileOutputStream (tmp)) { byte[] b = new byte[1 << 16]; int n; while ((n = in.read (b)) > 0) o.write (b, 0, n); }
				catch (Exception e) { Main.log ("add: " + name + " - " + e); continue; }
				try { synchronized (Getter.class) {
						if (isZip (tmp) && check (2, "", tmp)) { other += unpackPitrex (m, tmp); continue; }                 /* (a PiTrex SD card) */
						if (tmp.length () == 2048 && fileSha256 (tmp).equals (SPEECH_SHA)) { other += place (m, 3, "al2.bin", readFile (tmp)); continue; }
						if (tmp.length () > (64L << 20)) continue;
						games += importGames (m, name, readFile (tmp), 0); } }
				catch (Exception e) { Main.log ("add: " + name + " - " + e); }
				finally { tmp.delete (); } }
			final int g = games, o = other; Main.log ("add: " + g + " games, " + o + " other files");
			m.h.post (() -> m.toast (g + (g == 1 ? " game" : " games") + (o > 0 ? ", " + o + " other files" : "") + " added" + (g + o == 0 ? " (nothing new, or not a Vectrex file)" : ""), 3));
		}, "add-files").start ();
	}
	/* a game file (named ours when known, else as it was), or a zip of them, into roms/Imported */
	static int importGames (Main m, String name, byte[] d, int depth) throws IOException
	{
		String low = name.toLowerCase (Locale.ROOT);
		if (low.endsWith (".um2") || low.endsWith (".v4e")) {   /* (as it is: a UVMC2 game, a Vec4ever file - as the iOS app's) */
			File out = new File (roms (m), "Imported/" + new File (name).getName ()); if (out.exists ()) return 0;
			out.getParentFile ().mkdirs (); try (OutputStream o = new FileOutputStream (out)) { o.write (d); } return 1; }
		if (isZip (d)) { int n = 0;
			try (ZipInputStream z = new ZipInputStream (new java.io.ByteArrayInputStream (d))) { ZipEntry e;
				while ((e = z.getNextEntry ()) != null) if (!e.isDirectory ()) { String en = e.getName ().replace ('\\', '/'); byte[] b = Pack.read (z);
					String el = en.toLowerCase (Locale.ROOT);
					if (el.endsWith (".zip") && depth < 2) n += importGames (m, en, b, depth + 1); else if (romExt (en) || el.endsWith (".um2") || el.endsWith (".v4e")) n += importGames (m, en, b, depth + 1); } }
			return n; }
		return romExt (name) ? importOne (m, name, d) : 0;
	}
	static int importOne (Main m, String path, byte[] d) throws IOException
	{
		if (d.length == 0 || d.length > (1 << 20)) return 0;
		String known = SUMS.get (fp (d)), s = stem (path), n = known != null ? known : gameName (s) != null && KNOWN.containsValue (gameName (s)) ? gameName (s) : hbName (s) != null ? hbName (s) : s;
		String a = sha ("SHA-1", d, d.length); if (ownSums.contains (a)) return 0;
		File out = new File (roms (m), "Imported/" + n.replaceAll ("[\\\\/:*?\"<>|]", "_") + ".bin"); if (out.exists ()) return 0;
		out.getParentFile ().mkdirs (); try (OutputStream o = new FileOutputStream (out)) { o.write (d); } ownSums.add (a); return 1;
	}
}

/* the files that come with the app (the public build): the Vectrex's BIOS, the overlays, the UVMC2's folder and the
 * included games (assets/base.zip - libretro/package_android.sh's package) unpacked into the app's folder once a version */
final class Base {
	static boolean busy;
	static boolean needed (Main m)
	{
		if (Pack.token (m) != null) return false;
		try (InputStream in = m.getAssets ().open ("base.zip")) { } catch (Exception e) { return false; }
		return !ver (m).equals (m.prefs.getString ("base.ver", ""));   /* (once a version: tried, it is not tried again until the next one) */
	}
	static String ver (Main m) { try { android.content.pm.PackageInfo pi = m.getPackageManager ().getPackageInfo (m.getPackageName (), 0); return pi.versionName + "/" + pi.lastUpdateTime; } catch (Exception e) { return "?"; } }
	static void install (Main m)
	{
		int n = 0, bad = 0;
		try (ZipInputStream z = new ZipInputStream (new java.io.BufferedInputStream (m.getAssets ().open ("base.zip")))) { ZipEntry e; byte[] b = new byte[1 << 16];
			while ((e = z.getNextEntry ()) != null) { if (e.isDirectory () || e.getName ().contains ("..")) continue; File out = new File (m.ext (), e.getName ()); out.getParentFile ().mkdirs ();
				try (OutputStream o = new FileOutputStream (out)) { int r; while ((r = z.read (b)) > 0) o.write (b, 0, r); n++; }
				catch (Exception x) { bad++; if (bad <= 3) Main.log ("base: " + e.getName () + " - " + x); } } }   /* (one that cannot be written - left; the rest goes on) */
		catch (Exception e) { Main.log ("base: " + e); }
		m.prefs.edit ().putString ("base.ver", ver (m)).apply (); Main.log ("base: " + n + " files unpacked" + (bad > 0 ? ", " + bad + " could not be written" : ""));
	}
}
