/* Input.java - controllers and keyboards, as the iOS app: the Vectrex's buttons 1-4 (the core's RetroPad Y X B A) on a
 * modern pad's diamond as the Vectrex controller's row - 1 left (X), 2 top (Y), 3 bottom (A), 4 right (B) - or the
 * player's own (Settings > Controls); the first controller port 1, the second port 2. Over a screen: the stick / pad
 * moves the highlight, button 4 picks, button 3 goes back (the owner: one button for back everywhere), L / R a page.
 * A keyboard: the core's keys (Keyboard as on the PC), M the game menu.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
package com.hugob.vectrexemu;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;

final class Input {
	static final int B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7, A = 8, X = 9, L = 10, R = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15;
	static final String[] PHYS = { "A (bottom)", "B (right)", "X (left)", "Y (top)", "L1", "R1", "L2", "R2" };
	static final int[] PHYS_KEY = { KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2 };
	static final int[] DEF = { 2, 3, 0, 1 };                    /* (Vectrex 1-4 on: X, Y, A, B) */
	static final int[] VID = { Y, X, B, A };                    /* (Vectrex 1-4: the RetroPad's) */
	static final ArrayList<Integer> devs = new ArrayList<> ();  /* (the controllers seen, in order: ports 1, 2) */
	static final int[] btn = new int[2], hat = new int[2], trig = new int[2]; static final int[][] ax = new int[2][4];
	static int phys (int vec) { return Main.me.prefs.getInt ("map." + (vec + 1), DEF[vec]); }
	static boolean isPad (InputDevice d) { if (d == null || d.isVirtual ()) return false; int s = d.getSources ();
		return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK; }
	static int pads () { int n = 0; for (int id : InputDevice.getDeviceIds ()) if (isPad (InputDevice.getDevice (id))) n++; return n; }
	static int port (int dev) { int i = devs.indexOf (dev); if (i < 0) { devs.add (dev); i = devs.size () - 1; InputDevice d = InputDevice.getDevice (dev);
		Main.log ("controller " + (d != null ? d.getName () : "?") + " -> port " + (i + 1)); } return i < 2 ? i : -1; }
	static String bname (int b) { return new String[] { "B(3)", "Y(1)", "Select", "Start", "Up", "Down", "Left", "Right", "A(4)", "X(2)", "L1", "R1", "L2", "R2", "L3", "R3" }[b]; }
	/* the RetroPad button a key is, the player's mapping (-1: none) */
	static int pbit (int code)
	{
		for (int v = 0; v < 4; v++) { int p = phys (v); if (p >= 0 && p < 8 && PHYS_KEY[p] == code) return VID[v]; }
		switch (code) {
		case KeyEvent.KEYCODE_BUTTON_L1: return L; case KeyEvent.KEYCODE_BUTTON_R1: return R; case KeyEvent.KEYCODE_BUTTON_L2: return L2; case KeyEvent.KEYCODE_BUTTON_R2: return R2;
		case KeyEvent.KEYCODE_BUTTON_THUMBL: return L3; case KeyEvent.KEYCODE_BUTTON_THUMBR: return R3; case KeyEvent.KEYCODE_BUTTON_SELECT: return SELECT; case KeyEvent.KEYCODE_BUTTON_START: return START;
		case KeyEvent.KEYCODE_DPAD_UP: return UP; case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN; case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT; case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
		}
		return -1;
	}
	static void send (int p)
	{
		int b = btn[p] | hat[p] | trig[p]; if (ax[p][1] < -16000) b |= 1 << UP; if (ax[p][1] > 16000) b |= 1 << DOWN; if (ax[p][0] < -16000) b |= 1 << LEFT; if (ax[p][0] > 16000) b |= 1 << RIGHT;
		VX.setPad (p, b, ax[p][0], ax[p][1], ax[p][2], ax[p][3]);
	}
	static int prevAll;
	static boolean key (Main m, KeyEvent e)
	{
		InputDevice d = e.getDevice (); final int code = e.getKeyCode (), act = e.getAction ();
		if (act == KeyEvent.ACTION_MULTIPLE) return false;
		final boolean down = act == KeyEvent.ACTION_DOWN, pad = isPad (d) || KeyEvent.isGamepadButton (code);
		if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_VOLUME_MUTE) return false;
		if (pad) {
			if (!m.stack.isEmpty ()) return screenKey (m, e, code, down);
			if (!m.started) return false;
			int p = port (e.getDeviceId ()); if (p < 0) return true;
			int b = pbit (code); if (b < 0) { if (code == KeyEvent.KEYCODE_BUTTON_MODE && down && e.getRepeatCount () == 0) Screens.gameMenu (m); return true; }
			if (e.getRepeatCount () > 0) return true;
			int was = btn[p]; if (down) btn[p] |= 1 << b; else btn[p] &= ~(1 << b);
			if (was != btn[p]) Main.log ("in p" + (p + 1) + " " + bname (b) + (down ? " down" : " up"));
			if (down && (btn[p] >> SELECT & 1) != 0 && b == X) { Main.log ("Select + X: the game menu"); btn[p] = 0; send (p); Screens.gameMenu (m); return true; }
			send (p); return true; }
		/* a keyboard */
		if (!m.stack.isEmpty ()) { if (code == KeyEvent.KEYCODE_ESCAPE && down) { m.pop (); return true; } return false; }
		if (!m.started) return false;
		if (code == KeyEvent.KEYCODE_M && !e.isCtrlPressed ()) { if (down && e.getRepeatCount () == 0) { Main.log ("M: the game menu"); Screens.gameMenu (m); } return true; }
		int k = retrok (code); if (k == 0) return false;
		if (e.getRepeatCount () > 0) return true;
		int ch = down ? e.getUnicodeChar () : 0; int mods = (e.isShiftPressed () ? 1 : 0) | (e.isCtrlPressed () ? 2 : 0) | (e.isAltPressed () ? 4 : 0);
		VX.key (k, down, ch, mods); return true;
	}
	/* over a screen: the pad as the arrows, button 4 picks, button 3 back, L / R a page */
	static boolean screenKey (Main m, KeyEvent e, int code, boolean down)
	{
		int b = pbit (code); View root = m.top ();
		if (b == B) { if (down && e.getRepeatCount () == 0) { Main.log ("pad: back"); Screens.back (m); } return true; }
		if (b == A && root instanceof Library) { root.dispatchKeyEvent (new KeyEvent (e.getDownTime (), e.getEventTime (), e.getAction (), KeyEvent.KEYCODE_ENTER, e.getRepeatCount ())); return true; }
		if (b == A) { if (root != null) { View f = root.findFocus (); KeyEvent k = new KeyEvent (e.getDownTime (), e.getEventTime (), e.getAction (), KeyEvent.KEYCODE_DPAD_CENTER, e.getRepeatCount ());
			if (f != null) f.dispatchKeyEvent (k); else root.dispatchKeyEvent (k); } return true; }
		if (b == L || b == R) { KeyEvent k = new KeyEvent (e.getDownTime (), e.getEventTime (), e.getAction (), b == L ? KeyEvent.KEYCODE_PAGE_UP : KeyEvent.KEYCODE_PAGE_DOWN, e.getRepeatCount ());
			if (root instanceof Library) { root.dispatchKeyEvent (k); return true; }
			View f = root != null ? root.findFocus () : null; if (f != null) f.dispatchKeyEvent (k); return true; }
		if (b == UP || b == DOWN || b == LEFT || b == RIGHT) return false;   /* (the screen's own focus moving) */
		return b >= 0;                                                      /* (the rest: nothing over a screen) */
	}
	static boolean motion (Main m, MotionEvent e)
	{
		if (!isPad (e.getDevice ()) || (e.getSource () & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK || e.getAction () != MotionEvent.ACTION_MOVE) return false;
		if (!m.stack.isEmpty ()) return false;                               /* (a screen: the system turns the stick into the arrows) */
		int p = port (e.getDeviceId ()); if (p < 0) return true;
		ax[p][0] = (int) (dz (e.getAxisValue (MotionEvent.AXIS_X)) * 32767); ax[p][1] = (int) (dz (e.getAxisValue (MotionEvent.AXIS_Y)) * 32767);
		ax[p][2] = (int) (dz (e.getAxisValue (MotionEvent.AXIS_Z)) * 32767); ax[p][3] = (int) (dz (e.getAxisValue (MotionEvent.AXIS_RZ)) * 32767);
		float hx = e.getAxisValue (MotionEvent.AXIS_HAT_X), hy = e.getAxisValue (MotionEvent.AXIS_HAT_Y); int hb = 0;
		if (hy < -0.5f) hb |= 1 << UP; if (hy > 0.5f) hb |= 1 << DOWN; if (hx < -0.5f) hb |= 1 << LEFT; if (hx > 0.5f) hb |= 1 << RIGHT; hat[p] = hb;
		int tb = 0; if (Math.max (e.getAxisValue (MotionEvent.AXIS_LTRIGGER), e.getAxisValue (MotionEvent.AXIS_BRAKE)) > 0.5f) tb |= 1 << L2;
		if (Math.max (e.getAxisValue (MotionEvent.AXIS_RTRIGGER), e.getAxisValue (MotionEvent.AXIS_GAS)) > 0.5f) tb |= 1 << R2; trig[p] = tb;
		send (p); return true;
	}
	static float dz (float v) { return Math.abs (v) < 0.12f ? 0 : v; }
	static int lastPads = -1; static long lastCheck;
	static void poll (Main m)
	{
		long now = System.currentTimeMillis (); if (now - lastCheck < 1000) return; lastCheck = now;
		int n = pads (); if (n != lastPads) { lastPads = n; StringBuilder s = new StringBuilder ();
			for (int id : InputDevice.getDeviceIds ()) { InputDevice d = InputDevice.getDevice (id); if (isPad (d)) s.append (d.getName ()).append ("; "); }
			Main.log ("devices: controllers " + (n > 0 ? s : "none")); m.layout (); }
	}
	static int retrok (int c)
	{
		if (c >= KeyEvent.KEYCODE_A && c <= KeyEvent.KEYCODE_Z) return 'a' + (c - KeyEvent.KEYCODE_A);
		if (c >= KeyEvent.KEYCODE_0 && c <= KeyEvent.KEYCODE_9) return '0' + (c - KeyEvent.KEYCODE_0);
		if (c >= KeyEvent.KEYCODE_F1 && c <= KeyEvent.KEYCODE_F12) return 282 + (c - KeyEvent.KEYCODE_F1);
		switch (c) {
		case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return 13; case KeyEvent.KEYCODE_ESCAPE: return 27; case KeyEvent.KEYCODE_DEL: return 8;
		case KeyEvent.KEYCODE_FORWARD_DEL: return 127; case KeyEvent.KEYCODE_TAB: return 9; case KeyEvent.KEYCODE_SPACE: return 32;
		case KeyEvent.KEYCODE_MINUS: return '-'; case KeyEvent.KEYCODE_EQUALS: return '='; case KeyEvent.KEYCODE_GRAVE: return '`'; case KeyEvent.KEYCODE_COMMA: return ',';
		case KeyEvent.KEYCODE_PERIOD: return '.'; case KeyEvent.KEYCODE_SLASH: return '/'; case KeyEvent.KEYCODE_SEMICOLON: return ';'; case KeyEvent.KEYCODE_APOSTROPHE: return '\'';
		case KeyEvent.KEYCODE_LEFT_BRACKET: return '['; case KeyEvent.KEYCODE_RIGHT_BRACKET: return ']'; case KeyEvent.KEYCODE_BACKSLASH: return '\\';
		case KeyEvent.KEYCODE_DPAD_UP: return 273; case KeyEvent.KEYCODE_DPAD_DOWN: return 274; case KeyEvent.KEYCODE_DPAD_RIGHT: return 275; case KeyEvent.KEYCODE_DPAD_LEFT: return 276;
		case KeyEvent.KEYCODE_INSERT: return 277; case KeyEvent.KEYCODE_MOVE_HOME: return 278; case KeyEvent.KEYCODE_MOVE_END: return 279; case KeyEvent.KEYCODE_PAGE_UP: return 280; case KeyEvent.KEYCODE_PAGE_DOWN: return 281;
		case KeyEvent.KEYCODE_SHIFT_RIGHT: return 303; case KeyEvent.KEYCODE_SHIFT_LEFT: return 304; case KeyEvent.KEYCODE_CTRL_RIGHT: return 305; case KeyEvent.KEYCODE_CTRL_LEFT: return 306;
		case KeyEvent.KEYCODE_ALT_RIGHT: return 307; case KeyEvent.KEYCODE_ALT_LEFT: return 308;
		}
		return 0;
	}
}
