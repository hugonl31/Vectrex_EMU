/* usbkm.c - Vectrex_EMU's libretro core: RetroArch's keyboard and mouse as the UVMC2's / PiTrex's USB devices (the
 * desktop's Ctrl+K typing and its mouse: vectrex_emu.py usb_key, hid_usage, mouse_moved, mouse_button). The keyboard
 * through RetroArch's keyboard callback - in Game Focus (Scroll Lock) every key reaches the game - as a boot keyboard's
 * report (the modifiers, the last 3 keys held); the mouse's moves scaled so the cartridge's pointer follows it about
 * one to one on the picture (VecOS: ~0.0015 picture heights a step), its buttons, its wheel. Sent to the core
 * (usbhost.c vt_usb_report) at a refresh's start, the emulation idle. Included by libretro_vx.c.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

static unsigned char km_down[16]; static double km_fx, km_fy;   /* (km_on, km_mods ...: libretro_vx.c) */
static int km_usage (unsigned k)                 /* (vectrex_emu.py hid_usage / HID_KEYS: RetroArch's key -> the HID usage) */
{
	if (k >= RETROK_a && k <= RETROK_z) return 0x04 + (int) (k - RETROK_a);
	if (k >= RETROK_1 && k <= RETROK_9) return 0x1E + (int) (k - RETROK_1);
	if (k == RETROK_0) return 0x27;
	if (k >= RETROK_F1 && k <= RETROK_F12) return 0x3A + (int) (k - RETROK_F1);
	switch (k) {
	case RETROK_RETURN: return 0x28; case RETROK_KP_ENTER: return 0x58; case RETROK_ESCAPE: return 0x29; case RETROK_BACKSPACE: return 0x2A;
	case RETROK_TAB: return 0x2B; case RETROK_SPACE: return 0x2C; case RETROK_MINUS: return 0x2D; case RETROK_EQUALS: return 0x2E;
	case RETROK_LEFTBRACKET: return 0x2F; case RETROK_RIGHTBRACKET: return 0x30; case RETROK_BACKSLASH: return 0x31; case RETROK_SEMICOLON: return 0x33;
	case RETROK_QUOTE: return 0x34; case RETROK_BACKQUOTE: return 0x35; case RETROK_COMMA: return 0x36; case RETROK_PERIOD: return 0x37;
	case RETROK_SLASH: return 0x38; case RETROK_CAPSLOCK: return 0x39; case RETROK_INSERT: return 0x49; case RETROK_HOME: return 0x4A;
	case RETROK_PAGEUP: return 0x4B; case RETROK_DELETE: return 0x4C; case RETROK_END: return 0x4D; case RETROK_PAGEDOWN: return 0x4E;
	case RETROK_RIGHT: return 0x4F; case RETROK_LEFT: return 0x50; case RETROK_DOWN: return 0x51; case RETROK_UP: return 0x52;
	default: return 0; }
}
static int km_mod (unsigned k)                   /* (HID_MODS) */
{
	switch (k) { case RETROK_LCTRL: return 1; case RETROK_LSHIFT: return 2; case RETROK_LALT: return 4; case RETROK_LSUPER: return 8;
	case RETROK_RCTRL: return 16; case RETROK_RSHIFT: return 32; case RETROK_RALT: return 64; case RETROK_RSUPER: return 128; default: return 0; }
}
static void RETRO_CALLCONV km_key (bool down, unsigned keycode, uint32_t character, uint16_t mods)
{
	(void) character; (void) mods;
	if (!km_on) return;
	const int m = km_mod (keycode);
	if (m) { km_mods = (unsigned char) (down ? km_mods | m : km_mods & ~m); km_dirty = 1; return; }
	const int u = km_usage (keycode); if (!u) return;
	int i = 0; while (i < km_n && km_down[i] != u) i++;
	if (down && i == km_n && km_n < 16) { km_down[km_n++] = (unsigned char) u; km_dirty = 1; }
	else if (!down && i < km_n) { memmove (km_down + i, km_down + i + 1, (size_t) (km_n - i - 1)); km_n--; km_dirty = 1; }
}
/* (at a refresh's start, the emulation idle: the keyboard's report when it changed, the mouse's moves and buttons) */
static void km_send (void)
{
	extern void vt_usb_report (int id, const unsigned char *b);
	if (!km_on) return;
	if (km_dirty) { unsigned char r[4] = { km_mods, 0, 0, 0 };
		for (int k = 0; k < 3 && k < km_n; k++) r[1 + k] = km_down[km_n - (km_n < 3 ? km_n : 3) + k];
		vt_usb_report (0x80, r); km_dirty = 0; }
	const int b = (input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) ? 1 : 0) | (input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) ? 2 : 0)
	            | (input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) ? 4 : 0);
	const int wh = (input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP) ? 1 : 0) - (input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN) ? 1 : 0);
	const double k = 1.0 / (0.0015 * (CH > 100 ? CH / 0.924 : 800));   /* (one mouse step: a pixel of the picture's height) */
	km_fx += input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) * k; km_fy += input_cb (0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) * k;
	int dx = (int) km_fx, dy = (int) km_fy; dx = dx < -127 ? -127 : dx > 127 ? 127 : dx; dy = dy < -127 ? -127 : dy > 127 ? 127 : dy;
	km_fx -= dx; km_fy -= dy;
	if (dx || dy || wh || b != km_btn) { unsigned char r[4] = { (unsigned char) b, (unsigned char) (signed char) dx, (unsigned char) (signed char) dy, (unsigned char) (signed char) wh };
		vt_usb_report (0x81, r); km_btn = b; }
}
