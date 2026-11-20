/* Online.java - online play, as the iOS app (ios/App/VXOnline.m and vxa_net_tick): the core's netvx.c speaks the PC's
 * protocol - phones and PCs together; host in the game room, with a code or on this Wi-Fi, join by a code, an address or
 * from the lists. Each moment (tick) the game the two agreed is started, a guest joins a running game (no reset), the
 * host's next game starts for both, RESET from either resets both; only Leave ends it - a gap is waited out.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.net.Inet4Address;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.Locale;

final class Online {
	static boolean playing; static String hostRom, joinRom; static int hostMode = -1;
	static final HashMap<String, String> md5s = new HashMap<> ();
	static JSONObject status () { try { return new JSONObject (VX.netStatus ()); } catch (Exception e) { return new JSONObject (); } }
	static String name (Main m) { return m.prefs.getString ("net.name", ""); }
	/* this device's secret for its name at the meeting point: made from the device (Android's ID for this app - the same after
	 * a reinstall), hashed with the app's own salt (the meeting point never sees the ID): a fresh install gets its name back */
	static String key (Main m)
	{
		String k = m.prefs.getString ("net.key", "");
		if (k.isEmpty ()) {
			String id = null; try { id = android.provider.Settings.Secure.getString (m.getContentResolver (), android.provider.Settings.Secure.ANDROID_ID); } catch (Exception e) { }
			byte[] b;
			try { if (id == null || id.isEmpty ()) throw new Exception ("no id");
				b = java.security.MessageDigest.getInstance ("SHA-256").digest (("vectrex_emu-player-" + id).getBytes ("UTF-8")); }
			catch (Exception e) { b = new byte[32]; new SecureRandom ().nextBytes (b); }
			StringBuilder s = new StringBuilder (); for (int i = 0; i < 16; i++) s.append (String.format ("%02x", b[i] & 255));
			k = s.toString (); m.prefs.edit ().putString ("net.key", k).apply (); }
		return k;
	}
	/* the name reserved at the meeting point (a background thread; then on the main one): ok, or why not */
	interface Claimed { void go (int r, String name); }
	static void claim (Main m, String name, Claimed done)
	{
		final String old = m.prefs.getBoolean ("net.name_ok", false) ? name (m) : null, k = key (m);
		new Thread (() -> { String r = VX.netClaim (name, k, old != null && !old.equals (name) ? old : null);
			final int code = Integer.parseInt (r.startsWith ("1") ? "1" : r); final String got = code == 1 ? r.substring (1) : name;
			m.h.post (() -> {
				if (code == 1 || code == -2) m.prefs.edit ().putString ("net.name", got).putBoolean ("net.name_ok", code == 1).apply ();
				Main.log ("online: name " + name + ": " + (code == 1 ? "reserved" : code == 0 ? "another player's" : code == -1 ? "not allowed" : "no answer - kept here"));
				done.go (code, got); }); }).start ();
	}
	static String claimSays (int r, String name)
	{
		return r == 1 ? "YOUR PLAYER NAME: " + name : r == 0 ? name + " BELONGS TO ANOTHER PLAYER - PICK ANOTHER" : r == -1 ? "THAT NAME IS NOT ALLOWED (A-Z, 0-9)"
			: "NO ANSWER FROM THE MEETING POINT: " + name + " KEPT HERE, RESERVED AT THE NEXT ONLINE GAME";
	}
	/* the first start: the name this device had (a reinstall), else asked for once */
	static void firstName (Main m)
	{
		if (!name (m).isEmpty () && m.prefs.getBoolean ("net.name_ok", false)) return;
		final String k = key (m);
		new Thread (() -> { String got = VX.netWhoami (k);
			m.h.post (() -> {
				if (got != null && !got.isEmpty ()) { m.prefs.edit ().putString ("net.name", got).putBoolean ("net.name_ok", true).apply (); Main.log ("online: welcome back " + got); m.toast ("WELCOME BACK, " + got, 3); return; }
				if (!name (m).isEmpty ()) { claim (m, name (m), (r, n) -> { }); return; }   /* (a name kept here while offline: reserved now) */
				if (m.stack.isEmpty ()) nameScreen (m, false); }); }).start ();
	}
	/* the name asked for: typed (A-Z 0-9), checked at the meeting point and kept */
	static void nameScreen (Main m, boolean change)
	{
		Screens.Panel p = new Screens.Panel (m, "Your player name");
		p.foot ("The name other players see when you play online - in the game room, the chat, on the deck. Up to 10 letters A-Z and digits 0-9. It is reserved for this device and stays the same in every online mode.");
		EditText e = new EditText (m); e.setText (name (m)); e.setHint ("YOUR NAME"); e.setTextColor (Main.HI); e.setHintTextColor (Main.DIM); e.setSingleLine (true); e.setTextSize (20);
		e.setInputType (android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
		e.setBackground (Screens.states (m, Main.PANEL, Main.EDGE, m.dp (10))); e.setPadding ((int) m.dp (14), (int) m.dp (12), (int) m.dp (14), (int) m.dp (12));
		p.col.addView (e);
		TextView said = m.text ("", 14, Main.CHG, false); said.setPadding ((int) m.dp (4), (int) m.dp (8), 0, (int) m.dp (8)); p.col.addView (said);
		p.row ("Check and keep", "Reserved at the meeting point for this device", () -> {
			String n = clean (e.getText ().toString ()); e.setText (n);
			if (n.isEmpty ()) { said.setText ("A name first: 1 to 10 letters A-Z or digits 0-9"); return; }
			said.setText ("Asking the meeting point...");
			claim (m, n, (r, got) -> { said.setText (claimSays (r, got)); if (r == 1 || r == -2) { m.toast (claimSays (r, got), 3); if (m.top () == p) m.pop (); } }); });
		p.row (change ? "Cancel" : "Later", change ? "" : "Asked again when you go online", () -> m.pop ());
		m.push (p); e.requestFocus ();
	}
	static String clean (String s)                       /* (a gamer name: A-Z and 0-9 only, up to 10 - the rest left out) */
	{
		StringBuilder o = new StringBuilder (); s = s.toUpperCase (Locale.ROOT);
		for (int i = 0; i < s.length () && o.length () < 10; i++) { char c = s.charAt (i); if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) o.append (c); }
		return o.toString ();
	}
	static String ip (Main m)                             /* (this phone's address on its Wi-Fi: what player 2 enters) */
	{
		try { ConnectivityManager cm = m.getSystemService (ConnectivityManager.class); LinkProperties lp = cm.getLinkProperties (cm.getActiveNetwork ());
			if (lp != null) for (LinkAddress a : lp.getLinkAddresses ()) if (a.getAddress () instanceof Inet4Address) return a.getAddress ().getHostAddress (); } catch (Exception e) { }
		return "?";
	}
	static boolean skip () { return VX.val ("vectrex_emu_skip_intro").equals ("on"); }
	static String stem (String n) { n = new File (n).getName (); int d = n.lastIndexOf ('.'); return d > 0 ? n.substring (0, d) : n; }
	static String find (Main m, String md5)                /* (the host's game here: by its MD5, as the desktop's find_rom) */
	{
		java.util.ArrayList<String> paths = new java.util.ArrayList<> (); for (Library.Game g : Library.scan (m)) paths.add (g.path);
		for (String p : paths) { String h = md5s.get (p); if (h == null) { h = VX.md5 (p); if (h == null) continue; md5s.put (p, h); } if (h.equals (md5)) return p; }
		return null;
	}
	static void load (Main m, String path) { VX.load (path); Library.remember (m, path); }
	/* online, a game picked in the library: the host's starts on both sides, player 2 can not */
	static boolean pick (Main m, String path)
	{
		if (!playing) return false;
		if (status ().optInt ("role") != 1) { m.toast ("ONLINE: ONLY THE HOST CHANGES THE GAME", 3); Main.log ("online: player 2 picked " + path + " - only the host changes the game"); return true; }
		if (VX.netRegame (path, skip ()) != 0) { m.toast ("ONLINE: STARTING " + stem (path).toUpperCase (Locale.ROOT) + " FOR BOTH...", 3); Main.log ("online: the host picked " + path + " - offered to player 2"); }
		return true;
	}
	static int asked, saidPause, saidDesync, saidLost, hotStarted, seenRg, seenReset, gap; static long lastTick, hotSaid, lastSum; static int lastPh = -1, lastRole = -1;
	static void tick (Main m)
	{
		long now = System.currentTimeMillis (); if (now - lastTick < 150) return; lastTick = now;
		JSONObject st = status (); if (st.length () == 0) return;
		final int ph = st.optInt ("phase"), role = st.optInt ("role"), acc = st.optInt ("accepted");
		String peer = st.optString ("peer", ""); peer = peer.isEmpty () ? (role == 1 ? "PLAYER 2" : "THE HOST") : peer.toUpperCase (Locale.ROOT);
		final String relay = st.optInt ("relay") != 0 ? " (RELAYED)" : "";
		if (ph != lastPh || role != lastRole) { Main.log ("online: phase " + lastPh + " -> " + ph + ", role " + role + ", accepted " + acc + ", ready " + st.optInt ("ready") + ", hot " + st.optInt ("hot") + (st.optString ("code").isEmpty () ? "" : ", code " + st.optString ("code")) + (st.optString ("error").isEmpty () ? "" : ", error " + st.optString ("error"))); lastPh = ph; lastRole = role; }
		if (ph < 3) { asked = 0; hotStarted = 0; joinRom = null; }
		if (role == 2 && hotStarted != 0 && !playing) {
			if (ph == 4) { playing = true; saidPause = saidDesync = saidLost = 0; Main.log ("online: in the host's running game as player 2"); m.toast ("ONLINE: IN " + peer + "'S GAME - YOU ARE PLAYER 2" + relay, 3); }
			else if (now - hotSaid > 1000 && st.optInt ("hot_total") > 0) { hotSaid = now; m.toast ("ONLINE: JOINING " + peer + "'S GAME... " + 100 * st.optInt ("hot_got") / Math.max (1, st.optInt ("hot_total")) + " %", 1); } }
		if (ph == 3 && !playing) {
			if (role == 1 && acc == 1 && st.optInt ("ready") != 0 && hostRom != null) {
				if (st.optInt ("hot") != 0 && VX.net (7, 0) != 0) { playing = true; saidPause = saidDesync = saidLost = 0; Main.log ("online: player 2 joins the running game as player 2");
					m.toast ("ONLINE: " + peer + " JOINED AS PLAYER 2" + relay + " - YOUR GAME GOES ON", 3.5); }
				else { VX.net (1, 0); load (m, hostRom); VX.net (2, 0); playing = true; saidPause = saidDesync = saidLost = 0;
					Main.log ("online: playing " + hostRom + " as player 1 (both started anew)"); m.toast ("ONLINE: " + peer + " JOINED" + relay + " - BOTH START ANEW, YOU ARE PLAYER 1", 3.5); } }
			else if (role == 2 && asked == 0) { asked = 1;
				JSONObject g = st.optJSONObject ("game"); String md5 = g != null ? g.optString ("md5", null) : null, nm = g != null ? stem (g.optString ("name", "?")) : "?";
				String p = md5 != null ? find (m, md5) : null;
				if (p == null) { VX.net (0, 0); m.toast ("ONLINE: YOU DO NOT HAVE " + nm.toUpperCase (Locale.ROOT), 4); Main.log ("online: no ROM with md5 " + md5); }
				else { VX.net (0, 1); joinRom = p; Main.log ("online: the host's game here: " + p + " - waiting for the start"); } }
			else if (role == 2 && acc == 1 && st.optInt ("ready") != 0 && joinRom != null && st.optInt ("hot") != 0) {
				if (hotStarted == 0) { hotStarted = 1; hotSaid = 0; VX.net (8, 0); load (m, joinRom); Main.log ("online: joining the host's running game " + joinRom); m.toast ("ONLINE: JOINING " + peer + "'S GAME...", 2); } }
			else if (role == 2 && acc == 1 && st.optInt ("ready") != 0 && joinRom != null) {
				VX.net (1, 0); load (m, joinRom); VX.net (2, 0); playing = true; saidPause = saidDesync = saidLost = 0;
				Main.log ("online: playing " + joinRom + " as player 2");
				m.toast ("ONLINE: CONNECTED" + relay + " - YOU ARE PLAYER 2" + (st.optInt ("stream") != 0 ? " - STREAMED BY THE HOST" : st.optInt ("card") != 0 ? " - UVMC2 " + st.optString ("uvm2").toUpperCase (Locale.ROOT) : ""), 3.5); } }   /* (streamed: a card game run by the host - netvx.c) */
		if (playing && now - lastSum > 10000) { lastSum = now;   /* (the lockstep's state, for the log: the frame, the checks, out of step) */
			Main.log ("online: frame " + st.optInt ("frame") + ", checked " + st.optInt ("checked") + ", out of step " + st.optInt ("desync", -1) + ", delay " + st.optInt ("delay") + ", rtt " + st.optInt ("rtt") + " ms" + (st.optInt ("relay") != 0 ? ", relayed" : "")); }
		if (playing) {
			String msg = st.optString ("regame_msg", ""); if (!msg.isEmpty ()) { m.toast ("ONLINE: " + msg.toUpperCase (Locale.ROOT), 3); Main.log ("online: " + msg); }
			JSONObject off = st.optJSONObject ("regame_offer");
			if (off != null) { String p = off.has ("md5") ? find (m, off.optString ("md5")) : null; VX.netRegameAnswer (p != null ? p : "");
				if (p == null) m.toast ("ONLINE: YOU DO NOT HAVE " + stem (off.optString ("name", "?")).toUpperCase (Locale.ROOT), 4);
				else Library.remember (m, p);
				Main.log ("online: the host's next game " + off.optString ("name") + ": " + (p != null ? p : "not here")); }
			int n = st.optInt ("regame_n"); if (n != seenRg) { seenRg = n; JSONObject g = st.optJSONObject ("game"); String gn = g != null ? g.optString ("name", "") : "";
				m.toast ("ONLINE: NOW PLAYING " + stem (gn).toUpperCase (Locale.ROOT), 3); Main.log ("online: both switched to " + gn); }
			n = st.optInt ("reset_n"); if (n != seenReset) { seenReset = n; String who = st.optInt ("reset_by") == role ? "YOU" : peer; m.toast ("RESET (" + who + ")", 2); Main.log ("online: reset by " + who); }
			chat (m, st.optJSONArray ("chat"));
			int pw = st.optInt ("peer_why");   /* (the other player's own pause: 1 a menu, 2 another app - the line stays until they are back) */
			if (pw != 0) { m.banner ("PAUSED BY " + peer + " - " + (pw == 2 ? "IN ANOTHER APP" : "IN THE MENU"));
				if (saidPause != pw) { saidPause = pw; Main.log ("online: paused by the other player (" + (pw == 2 ? "another app" : "the menu") + ")"); } }
			else if (saidPause != 0) { saidPause = 0; m.banner (null); m.toast (peer + " IS BACK", 2); Main.log ("online: the other player is back"); }
			if (st.optInt ("desync", -1) >= 0 && saidDesync == 0) { saidDesync = 1; m.toast ("ONLINE: OUT OF STEP - THE TWO GAMES DIFFER", 4); }
			if (st.optInt ("left") != 0 && saidLost == 0) { saidLost = 1; VX.net (3, 0); playing = false;
				if (role == 1) { boolean again = rehost (m); m.toast ("ONLINE: " + peer + " LEFT - " + (again ? "YOUR GAME GOES ON, HOSTED AGAIN" : "YOUR GAME GOES ON"), 4); Main.log ("online: the other player left - hosted again: " + again); }
				else { m.toast ("ONLINE: " + peer + " LEFT - YOUR GAME GOES ON", 4); Main.log ("online: the host left"); } }
			else if (saidLost == 0) { int l = st.optInt ("lost");
				if (l != 0 && gap == 0) { gap = 1; if (st.optInt ("peer_why") != 2) m.toast ("ONLINE: NO CONNECTION - WAITING FOR THE OTHER PLAYER (ONLINE > LEAVE TO STOP)", 4); Main.log ("online: no connection - waiting"); }
				else if (l == 0 && gap != 0) { gap = 0; m.toast ("ONLINE: CONNECTED AGAIN", 2); Main.log ("online: connected again"); } }
			if (ph != 4 && ph != 3 && saidLost == 0) { playing = false;   /* (3: the start just asked for - it reaches the core at its next refresh) */  Main.log ("online: the game for two ended (phase " + ph + ")"); } }
		if (!playing && m.ban != null) { m.banner (null); saidPause = 0; }   /* (not online any more: the pause line away) */
	}
	static android.media.ToneGenerator tone;
	static void chat (Main m, JSONArray c)
	{
		int n = 0; if (c != null) for (int i = 0; i < c.length (); i++) { String t = c.optString (i, ""); if (!t.isEmpty ()) { Main.log ("online chat in: " + t); m.toast ("💬 " + t, 5); n++; } }
		if (n > 0) {   /* (a line in: heard and felt - the game goes on) */
			try { if (tone == null) tone = new android.media.ToneGenerator (android.media.AudioManager.STREAM_NOTIFICATION, 70); tone.startTone (android.media.ToneGenerator.TONE_PROP_ACK, 150); } catch (RuntimeException e) { tone = null; }
			m.root.performHapticFeedback (android.view.HapticFeedbackConstants.LONG_PRESS, android.view.HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING); }
	}
	static boolean rehost (Main m)                         /* (the guest left: the host's game goes on, hosted again the same way) */
	{
		String rom = VX.game (); if (hostMode < 0 || rom.isEmpty ()) return false;
		String nm = clean (name (m)); if (hostMode == 0 && nm.isEmpty ()) return false;
		hostRom = rom; int ok = VX.netHost (hostMode, nm, key (m), rom, skip ()); Main.log ("online: hosting " + rom + " again"); return ok != 0;
	}

	/* ---------------------------------------------------------------- the Online screen */
	static void show (Main m) { if (!m.stack.isEmpty () || !m.started) return; Main.log ("ui online opened"); Screen s = new Screen (m); m.push (s); }
	static final class Screen extends Screens.Panel {
		String last = ""; int shownPlay, sawSetup; EditText nameField; final Runnable poll = this::poll;
		Screen (Main m) { super (m, "Online"); Adhoc.lock (m, true); VX.net (6, 1); VX.net (5, 0); onClose = () -> { removeCallbacks (poll); VX.net (6, 0); Adhoc.lock (m, false); }; poll (); }
		int phase (JSONObject st) { return st.optInt ("phase"); }
		void poll ()
		{
			postDelayed (poll, 300);
			JSONObject st = status (); chat (m, st.optJSONArray ("chat"));
			st.remove ("chat"); st.remove ("waiting"); st.remove ("frame"); st.remove ("sums"); st.remove ("checked");
			String k = st.toString (); int ph = phase (st);
			if (ph >= 1 && ph <= 3) sawSetup = 1;
			if (ph == 4 && sawSetup != 0 && shownPlay == 0) { shownPlay = 1; m.pop (); return; }   /* (the game started here: back to it) */
			if (k.equals (last)) return; last = k; build (st);
		}
		void build (JSONObject st)
		{
			View f = findFocus (); String ft = f != null && f.findViewWithTag ("t") instanceof TextView ? ((TextView) f.findViewWithTag ("t")).getText ().toString () : null;
			col.removeAllViews (); int ph = phase (st), role = st.optInt ("role"); String game = Online.stem (VX.game ());
			String peer = st.optString ("peer", "");
			if (ph == 4) {
				header ("Playing online");
				row ("With " + (peer.isEmpty () ? "the other player" : peer), st.optInt ("delay") + " frames of input delay" + (st.optInt ("desync", -1) >= 0 ? " - OUT OF STEP: the two games differ" : ""), null);
				row ("Chat", "A line to the other player - the game goes on meanwhile", () -> { m.popAll (); Screens.ask (m, "Chat", "A line to the other player", false, s -> { VX.netChat (s); Main.log ("online chat out: " + s); }); });
				row ("Leave the online game", Adhoc.on ? "Your network " + Adhoc.ssidOn + " ends too" : "", () -> { VX.net (3, 0); playing = false; Adhoc.stop (m); m.pop (); });
				foot ("You are player " + (role == 1 ? 1 : 2) + ". " + (st.optInt ("relay") != 0 ? "The game goes through the meeting point (relayed): a little more delay." : "A direct connection."));
			} else if (ph >= 1 && ph <= 3) {
				String t, d; String code = st.optString ("code", "");
				if (role == 1) { int mode = st.optInt ("mode");
					if (ph == 1) { t = "Asking the meeting point for a code..."; d = ""; }
					else if (mode == 2) { t = Adhoc.on ? "Waiting on your own network" : "Waiting on this Wi-Fi"; d = "Player 2 picks your game under \"On this Wi-Fi\", uses \"Join the nearby host\" or enters this device's address: " + ip (m); }
					else { t = "Your code:  " + (code.isEmpty () ? "..." : code); d = mode == 0 ? "Your game is in the game room. Player 2 picks it there, or enters the code." : "Tell player 2 this code: they enter it under Join."; }
					if (!peer.isEmpty ()) d = peer + " is connecting..."; }
				else { t = ph == 3 ? "The host's game came: starting..." : "Connecting to the host..."; d = peer.isEmpty () ? "" : "Host: " + peer; }
				header ("Online"); row (t, d, null);
				if (Adhoc.on && role == 1) { row ("Network:  " + Adhoc.ssidOn, "Players join it in their Wi-Fi settings", null); row ("Password:  " + Adhoc.pass (m), "Kept on this device: the next time their phone or PC knows the network", null); }
				row ("Cancel", "", () -> { VX.net (3, 0); Adhoc.stop (m); last = ""; });
			} else {
				String err = st.optString ("error", ""); if (!err.isEmpty () && ph == 5) row ("Not connected", err, null);
				header ("Your name");
				EditText e = new EditText (m); nameField = e; e.setText (name (m)); e.setHint ("YOUR NAME"); e.setTextColor (Main.HI); e.setHintTextColor (Main.DIM); e.setSingleLine (true);
				e.setInputType (android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
				e.setBackground (Screens.states (m, Main.PANEL, Main.EDGE, m.dp (10))); e.setPadding ((int) m.dp (14), (int) m.dp (10), (int) m.dp (14), (int) m.dp (10));
				e.setOnFocusChangeListener ((v, has) -> { if (!has) { String n = clean (e.getText ().toString ()); e.setText (n);
					if (!n.isEmpty () && !(n.equals (name (m)) && m.prefs.getBoolean ("net.name_ok", false))) claim (m, n, (r, got) -> m.toast (claimSays (r, got), 3));   /* (a new name: reserved) */
					else if (n.isEmpty ()) m.prefs.edit ().putString ("net.name", "").putBoolean ("net.name_ok", false).apply (); } });
				col.addView (e); foot ("Up to 10 letters A-Z and digits 0-9. Players see it in the game room and the chat.");
				header (game.isEmpty () ? "Host" : "Host " + game);
				row ("In the game room", "Public: anyone can join it there", () -> host (0));
				row ("With a code", "Private: only who you tell the code", () -> host (1));
				row ("On this Wi-Fi", "No internet needed: players on the same network", () -> host (2));
				foot (game.isEmpty () ? "Start a game first, then host it here." : "Player 2 needs the same game. You are player 1.");
				header ("Nearby - no router, no internet");
				row ("Make a network and host" + (game.isEmpty () ? "" : " " + game), "This device makes the Wi-Fi network " + Adhoc.ssid (m) + " (password " + Adhoc.pass (m) + "); players join it in their Wi-Fi settings", () -> adhocHost ());
				row ("Join the nearby host", Adhoc.onAdhocNet () ? "This device is on a nearby host's network: " + Adhoc.hostAddr () : "First join the host's network in Wi-Fi settings: DIRECT-VX-VECTREX-..., VECTREX-EMU-... or the host phone's hotspot", () -> {
					String a = Adhoc.hostAddr (); if (a == null) { m.toast ("Not on a Wi-Fi network: join the host's network first", 3); Adhoc.wifiSettings (m); return; } join (a); });
				row ("Wi-Fi settings", "Find and join the host's network", () -> Adhoc.wifiSettings (m));
				foot ("The host's game also shows under \"On this Wi-Fi\" below. The network's name and password stay the same on this device.");
				header ("Join");
				row ("Enter a code", "Five letters from the host", () -> Screens.ask (m, "The host's code", "CODE", true, v -> join (v)));
				row ("Enter an address", "A host on this Wi-Fi (its address)", () -> Screens.ask (m, "The host's address", "192.168.1.20", false, v -> join (v)));
				header ("Game room");
				JSONObject r = st.optJSONObject ("room"); int n = 0;
				if (r != null) { JSONArray g = r.optJSONArray ("g"); if (g != null) for (int i = 0; i < g.length (); i++) { JSONArray x = g.optJSONArray (i); if (x == null || x.length () < 6) continue;
					final String code = x.optString (0); row (stem (x.optString (2)), (x.isNull (1) ? "?" : x.optString (1)) + " - waiting " + x.optInt (5) + " s", () -> join (code)); n++; } }
				if (n == 0) { int rs = st.optInt ("room_state"); row (rs == 1 ? "Looking..." : rs == 3 ? "The meeting point does not answer" : "No games waiting", "", null); }
				row ("Look again", "", () -> VX.net (5, 0));
				foot ("Public games waiting for a player 2, from phones and PCs.");
				header ("On this Wi-Fi"); n = 0;
				JSONArray lan = st.optJSONArray ("lan");
				if (lan != null) for (int i = 0; i < lan.length (); i++) { JSONObject g = lan.optJSONObject (i); if (g == null) continue; final String where = g.optString ("ip") + ":" + g.optString ("port");
					row (stem (g.optString ("name")), (g.optString ("gamer").isEmpty () ? "?" : g.optString ("gamer")) + " - " + where, () -> join (where)); n++; }
				if (n == 0) row ("None found", "This device: " + ip (m), null);
				foot ("Games hosted on this network. Not listed? Enter the host's address under Join.");
			}
			if (ft != null) for (int i = 0; i < col.getChildCount (); i++) { View c = col.getChildAt (i); View t = c.findViewWithTag ("t");
				if (t instanceof TextView && ((TextView) t).getText ().toString ().equals (ft) && c.isFocusable ()) { c.requestFocus (); break; } }
		}
		/* a player name is needed to host or join (the owner, 2026-11-19): the field's text taken, else said */
		boolean named ()
		{
			if (nameField != null) { String n = clean (nameField.getText ().toString ()); nameField.setText (n); m.prefs.edit ().putString ("net.name", n).apply (); }
			if (!clean (name (m)).isEmpty ()) { if (!m.prefs.getBoolean ("net.name_ok", false)) claim (m, clean (name (m)), (r, got) -> { if (r == 0 || r == -1) m.toast (claimSays (r, got), 4); }); return true; }
			m.toast ("ONLINE: YOUR PLAYER NAME IS MISSING - ENTER IT ABOVE (A-Z, 0-9)", 4); Main.log ("online: no player name");
			if (nameField != null) { nameField.requestFocus (); sc.smoothScrollTo (0, 0); }
			return false;
		}
		void host (int mode)
		{
			String rom = VX.game (); if (rom.isEmpty ()) { m.toast ("Start a game first: the one you host", 3); return; }
			if (!named ()) return;
			String nm = clean (name (m));
			hostRom = rom; hostMode = mode; VX.netHost (mode, nm, key (m), rom, skip ());
			Main.log ("online: hosting " + rom + " (" + (mode == 0 ? "game room" : mode == 1 ? "code" : "Wi-Fi") + ")"); last = "";
		}
		void adhocHost ()
		{
			if (VX.game ().isEmpty ()) { m.toast ("Start a game first: the one you host", 3); return; }
			if (!named ()) return;
			m.toast ("Making the network " + Adhoc.ssid (m) + "...", 2);
			Adhoc.start (m, (ok, msg) -> { if (!ok) { m.toast (msg, 4); return; } host (2); last = ""; });
		}
		void join (String where) { if (!named ()) return; Main.log ("online: joining " + where); VX.netJoin (where, clean (name (m)), key (m)); last = ""; }
	}
}
