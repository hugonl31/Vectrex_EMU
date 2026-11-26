/* VX.java - the native side (jni/vxa.c: the core and its host) as the Java side sees it.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

final class VX {
	static { System.loadLibrary ("vxa"); }
	static native void logStart (String filesDir);
	static native void log (String s);
	static native String takeLog ();                       /* the log's new lines (for the live sender) */
	static native void start (String sysDir, String savesDir, String game, String defaults);
	static native void setTune (int longPx, int shortPx, int maxHz);   /* (the screen for the first start's tuning: its sides, its fastest rate) */
	static native int tuning ();                           /* 0 not yet, 3 waiting for the player's yes, 1 testing this device, 2 done (or nothing to test) */
	static native void tuneAnswer (int go);                /* 1 test now, -1 later */
	static native String tuneStatus ();                    /* the test's step now */
	static native String tuneReport ();                    /* the result: a line a screen rate - every effect on, or what went off */
	static native void inputMark (long eventMs);            /* a button pressed: its event time (uptimeMillis) - the input delay measurement */
	static native int rate ();                             /* the game's screen rate now, Hz (0: not known yet) */
	static native int status ();                           /* 0 starting, 1 running, -1 failed */
	static native void setSurface (Object surface);
	static native void setRoom (int x, int y, int w, int h, boolean top);
	static native float[] picRect ();
	static native void setRunning (boolean on);
	static native void setLandscape (boolean land);       /* sideways: the deck off, as the iOS app (the picture the whole height) */
	static native void setHold (boolean on);
	static native void setHdr (boolean can, float headroom, float frac);
	static native void setTrailMul (float k);                   /* (the trail for the look and the screen's brightness) */
	static native void testCart (String path);                  /* (the Test Cart's file made) */
	static native void pitrexTiles (String thumbs);            /* (the PiTrex's cartridge and box made there) */
	static native int[] genart (int look, String name, int width);   /* (a cartridge (0) / box (1) drawn for a game with none, width px wide: { w, h, ARGB... } - libretro/genart.c) */
	static native int[] genlabel (String path, int width);     /* (the cartridge with the player's own label on it: the same) */
	static native int gensize (int px);                        /* (the width to make a tile shown px wide at: 256 / 384 / 512 / 640 / 840) */
	static native int[] genfit (String path, int maxw);        /* (a picture of the player's own, at most maxw wide: the same) */
	static native int[] genlabelb (byte[] pic, int width);     /* (genlabel / genfit from a picture's bytes, PNG or JPEG: the app's own, assets/shipped) */
	static native int[] genfitb (byte[] pic, int maxw);
	static native int palette ();                               /* (the core's colour scheme now: 0 blue, 1 green) */
	static native void setBright (int on);                      /* (the look: 0 Vectrex, 1 Bright - the lines lifted and wider, 2 Medium - halfway) */
	static native void setHdrLook (int look);                   /* (0 bright - as SDR, 1 the whole picture, 2 the Vectrex: iOS alike) */
	static native int hdrActive ();
	static native void snap (int runs, String path);        /* tests: the shown picture at that run after the next load */
	static native void setPad (int port, int btn, int lx, int ly, int rx, int ry);
	static native void setTouchPad (int btn, int ax, int ay);
	static native void setPointer (boolean on, float x, float y);
	static native void key (int retrok, boolean down, int ch, int mods);
	static native String takeMessage ();
	static native boolean takeBrowser ();
	static native int[] stats ();
	static native int nopts ();
	static native String opt (int i, int what);            /* 0 key 1 desc 2 cat 3 info 4 vals 5 val 6 def */
	static native int ofind (String key);
	static native void oset (int i, String v);
	static native int ncats ();
	static native String cat (int i, int what);            /* 0 key 1 desc 2 info */
	static native void load (String path);
	static native void act (int what, double d);           /* 1 reset 2 eject 3 power 4 volume turn */
	static native String game ();
	static native int state (boolean save, int slot);
	static native int stateExists (int slot);
	static native int stateSupported ();
	static native int[] deckState ();                      /* port 2's device, cart out, power on, paused, volume % */
	static native String osd ();                          /* the deck's figures: "fps|hz|emu|cpu|gpu" */
	static native int untgz (String tgz, String root);
	static native int netHost (int mode, String gamer, String key, String rom, boolean skip);
	static native int netJoin (String where, String gamer, String key);
	static native int netRegame (String rom, boolean skip);
	static native void netRegameAnswer (String path);
	static native void netChat (String text);
	static native String netStatus ();
	static native int net (int what, int arg);             /* 0 accept 1 prepare 2 go 3 leave 4 pause 5 room 6 lan 7 go_hot 8 prepare_hot 9 take_port */
	static native String md5 (String path);
	static native String netClaim (String name, String key, String old);   /* "1NAME" reserved, "0" another player's, "-1" not allowed, "-2" no answer */
	static native String netWhoami (String key);                         /* the name kept for this key, "" none, null no answer */
	static String val (String key) { int i = ofind (key); return i >= 0 ? opt (i, 5) : ""; }
}
