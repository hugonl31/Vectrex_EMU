/* Adhoc.java - play nearby without a router or the internet: this device makes a Wi-Fi network of its own (a Wi-Fi Direct
 * group - other phones, iPhones and PCs join it as any network, in their Wi-Fi settings) and hosts its game on it; the
 * game is then in "On this Wi-Fi" on the others, or "Join the nearby host" goes straight to it (the network's host is
 * always 192.168.49.1 here; an iPhone's hotspot 172.20.10.1, a Windows PC's 192.168.137.1, a Linux PC's 10.42.0.1).
 * The network's name and password are made once and kept: the second round, the other device knows the network.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;
import android.net.wifi.p2p.WifiP2pConfig;
import android.net.wifi.p2p.WifiP2pGroup;
import android.net.wifi.p2p.WifiP2pManager;
import android.os.Build;
import android.provider.Settings;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.security.SecureRandom;
import java.util.Collections;

final class Adhoc {
	static WifiP2pManager mgr; static WifiP2pManager.Channel ch; static boolean on; static String ssidOn = "";
	static WifiManager.MulticastLock mlock;
	/* the network's name and password: made once on this device, kept (letters and digits that can not be mistaken) */
	static String suffix (Main m)
	{
		String s = m.prefs.getString ("adhoc.id", "");
		if (s.isEmpty ()) { final String A = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; SecureRandom r = new SecureRandom (); StringBuilder b = new StringBuilder ();
			for (int i = 0; i < 4; i++) b.append (A.charAt (r.nextInt (A.length ())));
			StringBuilder p = new StringBuilder ("vectrex"); for (int i = 0; i < 4; i++) p.append ((char) ('0' + r.nextInt (10)));
			s = b.toString (); m.prefs.edit ().putString ("adhoc.id", s).putString ("adhoc.pass", p.toString ()).apply (); }
		return s;
	}
	static String ssid (Main m) { return "DIRECT-VX-VECTREX-" + suffix (m); }        /* (Wi-Fi Direct: the name must begin "DIRECT-" and two letters) */
	static String pass (Main m) { suffix (m); return m.prefs.getString ("adhoc.pass", "vectrex0000"); }
	static String perm () { return Build.VERSION.SDK_INT >= 33 ? "android.permission.NEARBY_WIFI_DEVICES" : Manifest.permission.ACCESS_FINE_LOCATION; }
	interface Done { void go (boolean ok, String msg); }
	static Done pending;
	/* the network made (the permission asked for first, once); then done (true) or why not */
	static void start (Main m, Done done)
	{
		if (Build.VERSION.SDK_INT < 29) { done.go (false, "Making a network needs Android 10 or newer: use the phone's hotspot, then host on this Wi-Fi"); return; }
		if (m.checkSelfPermission (perm ()) != PackageManager.PERMISSION_GRANTED) { pending = done; Main.log ("ad hoc: asking for " + perm ()); m.requestPermissions (new String[] { perm () }, 7); return; }
		WifiManager wm = m.getApplicationContext ().getSystemService (WifiManager.class);
		if (wm != null && !wm.isWifiEnabled ()) { done.go (false, "Switch Wi-Fi on first (it need not be connected)"); return; }
		if (mgr == null) { mgr = m.getSystemService (WifiP2pManager.class); if (mgr != null) ch = mgr.initialize (m, m.getMainLooper (), () -> { Main.log ("ad hoc: Wi-Fi Direct channel lost"); ch = null; on = false; }); }
		if (mgr == null || ch == null) { done.go (false, "This device has no Wi-Fi Direct: use its hotspot, then host on this Wi-Fi"); return; }
		final String ss = ssid (m), pw = pass (m);
		try {
			mgr.removeGroup (ch, null);                                     /* (a group left over: gone first) */
			WifiP2pConfig cfg = new WifiP2pConfig.Builder ().setNetworkName (ss).setPassphrase (pw).enablePersistentMode (false)
				.setGroupOperatingBand (WifiP2pConfig.GROUP_OWNER_BAND_2GHZ).build ();
			m.h.postDelayed (() -> { try { mgr.createGroup (ch, cfg, new WifiP2pManager.ActionListener () {
				public void onSuccess () { Main.log ("ad hoc: network " + ss + " asked for"); waitGroup (m, done, 0); }
				public void onFailure (int r) { Main.log ("ad hoc: no network (" + r + ")"); done.go (false, r == WifiP2pManager.P2P_UNSUPPORTED ? "This device has no Wi-Fi Direct: use its hotspot, then host on this Wi-Fi"
					: r == WifiP2pManager.BUSY ? "Wi-Fi is busy: try again in a moment" : "The network could not be made (" + r + "): try again, or use the phone's hotspot"); } }); }
				catch (SecurityException e) { done.go (false, "No permission to make a network"); } }, 300);
		} catch (SecurityException e) { done.go (false, "No permission to make a network"); }
	}
	static void waitGroup (Main m, Done done, int tries)
	{
		try { mgr.requestGroupInfo (ch, (WifiP2pGroup g) -> {
			if (g != null && g.isGroupOwner ()) { on = true; ssidOn = g.getNetworkName (); Main.log ("ad hoc: network " + g.getNetworkName () + " up on " + g.getInterface () + ", host " + hostAddr ());
				lock (m, true); done.go (true, ""); }
			else if (tries < 40) m.h.postDelayed (() -> waitGroup (m, done, tries + 1), 250);
			else done.go (false, "The network did not come up: try again"); }); }
		catch (SecurityException e) { done.go (false, "No permission to make a network"); }
	}
	static void onPermission (Main m, boolean granted)
	{
		Done d = pending; pending = null; if (d == null) return; Main.log ("ad hoc: permission " + (granted ? "given" : "refused"));
		if (granted) start (m, d); else d.go (false, "Without the permission (nearby devices) no network can be made");
	}
	static void stop (Main m)
	{
		if (!on || mgr == null || ch == null) return; on = false; ssidOn = "";
		try { mgr.removeGroup (ch, null); } catch (SecurityException e) { } Main.log ("ad hoc: network ended");
	}
	/* (broadcasts heard on Wi-Fi: some devices filter them unless asked - the LAN list and the announcements) */
	static void lock (Main m, boolean hold)
	{
		try { if (mlock == null) { WifiManager wm = m.getApplicationContext ().getSystemService (WifiManager.class); if (wm == null) return; mlock = wm.createMulticastLock ("vx-lan"); mlock.setReferenceCounted (false); }
			if (hold && !mlock.isHeld ()) mlock.acquire (); else if (!hold && mlock.isHeld () && !on) mlock.release (); } catch (Exception e) { }
	}
	/* the nearby host: the network's own host - its address .1 on the network this device joined (an ad-hoc network's host
	 * is its router: a phone's group, a hotspot) */
	static String hostAddr ()
	{
		String best = null;
		try { for (NetworkInterface ni : Collections.list (NetworkInterface.getNetworkInterfaces ())) { if (!ni.isUp () || ni.isLoopback ()) continue;
			for (InetAddress a : Collections.list (ni.getInetAddresses ())) { if (!(a instanceof Inet4Address)) continue; String ip = a.getHostAddress ();
				String gw = ip.substring (0, ip.lastIndexOf ('.')) + ".1";
				if (ip.startsWith ("192.168.49.") || ip.startsWith ("172.20.10.") || ip.startsWith ("192.168.137.") || ip.startsWith ("10.42.0.")) return gw;   /* (the ad-hoc kinds first) */
				if (best == null && (ip.startsWith ("192.168.") || ip.startsWith ("10.") || ip.startsWith ("172."))) best = gw; } } } catch (Exception e) { }
		return best;
	}
	static boolean onAdhocNet ()
	{
		try { for (NetworkInterface ni : Collections.list (NetworkInterface.getNetworkInterfaces ())) for (InetAddress a : Collections.list (ni.getInetAddresses ())) {
			String ip = a.getHostAddress (); if (a instanceof Inet4Address && (ip.startsWith ("192.168.49.") || ip.startsWith ("172.20.10.") || ip.startsWith ("192.168.137.") || ip.startsWith ("10.42.0."))) return true; } } catch (Exception e) { }
		return false;
	}
	static void wifiSettings (Main m)
	{
		try { m.startActivity (new Intent (Build.VERSION.SDK_INT >= 29 ? Settings.Panel.ACTION_WIFI : Settings.ACTION_WIFI_SETTINGS)); }
		catch (Exception e) { try { m.startActivity (new Intent (Settings.ACTION_WIFI_SETTINGS)); } catch (Exception x) { } }
	}
}
