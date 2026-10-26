/* usbhost.c - the RP2350's USB controller in host mode (USBCTRL_REGS 0x50110000, its DPRAM 0x50100000, IRQ 14)
 * with one device plugged in: a composite HID device, a boot keyboard (interface 0, endpoint 0x81) and a boot
 * mouse (interface 1, endpoint 0x82) - the PC's keyboard and mouse, sent by the frontend as HID reports
 * (live.c: settings ids 0x80 / 0x81, vt_usb_report). VecOS on the UVMC2 asks for one ("USB keyboards and mice
 * work too"). On only with VT_USBHID=1 (the frontend sets it for a UVMC2 game).
 *
 * The controller as the RP2040 / RP2350 datasheets describe it: a transfer is started with SIE_CTRL
 * START_TRANS (SEND_SETUP: the 8 bytes at DPRAM 0; RECEIVE_DATA / SEND_DATA: EPX, its control word at DPRAM
 * 0x100, its buffer control at 0x80); each buffer handled sets its BUFF_STATUS bit, the last one (LAST, or a
 * short packet in) TRANS_COMPLETE. The interrupt endpoints 1-15 (INT_EP_CTRL, their control words at 0x08,
 * buffer controls at 0x88, addresses in ADDR_ENDP1-15) are polled by the controller itself. The device
 * answers at once (no bus timing): what the software sees is the same, sooner. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define R_ADDR_ENDP 0x00
#define R_MAIN_CTRL 0x40
#define R_SOF_RD    0x48
#define R_SIE_CTRL  0x4c
#define R_SIE_STAT  0x50
#define R_INT_EP    0x54
#define R_BUFF_STAT 0x58
#define R_INTR      0x8c
#define R_INTE      0x90
#define R_INTF      0x94
#define R_INTS      0x98

#define SC_START    (1u << 0)
#define SC_SETUP    (1u << 1)
#define SC_SEND     (1u << 2)
#define SC_RECV     (1u << 3)
#define SC_STOP     (1u << 4)
#define SC_SOF_EN   (1u << 9)
#define SC_RESET    (1u << 13)
#define SC_PULLDOWN (1u << 15)

#define SS_VBUS     (1u << 0)
#define SS_SPEED    (3u << 8)
#define SS_TRANS    (1u << 18)
#define SS_TIMEOUT  (1u << 27)
#define SS_STALL    (1u << 29)
#define SS_ACK      (1u << 30)
#define SS_W1C      (SS_TRANS | SS_TIMEOUT | SS_STALL | SS_ACK | (1u << 28) | (1u << 31) | (1u << 24) | (1u << 25) | (1u << 26) | (1u << 19) | (1u << 17) | (1u << 11))

#define BC_FULL     (1u << 15)
#define BC_LAST     (1u << 14)
#define BC_AVAIL    (1u << 10)

static struct {
	int on;                                   /* VT_USBHID: a device is plugged in */
	int log;
	uint8_t dpram[4096];
	uint32_t r[0x120 / 4];
	int conn_irq, sof_irq, connected;
	uint64_t conn_at, next_tick;
	uint32_t frame;
	int level;
	/* the transfer on EPX */
	int xfer, xfer_in, xfer_ep, xfer_buf;
	/* the device */
	int addr, pend_addr, config, proto[2], idle;
	uint8_t ctl[256]; int ctl_len, ctl_pos, ctl_in, ctl_stall, ctl_nodata;
	uint8_t kbd[8]; int kbd_new;
	uint8_t mbtn; int mdx, mdy, mwh, m_new;
	uint8_t leds;
} U;

/* ------------------------------------------------------------------ the device's descriptors */
static const uint8_t KBD_RD[] = {               /* the boot keyboard report: modifiers, reserved, 6 keys; 5 LEDs out */
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
	0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
	0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xc0 };
static const uint8_t MOUSE_RD[] = {             /* the boot mouse report: 3 buttons, X, Y, and a wheel */
	0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
	0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
	0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03,
	0x81, 0x06, 0xc0, 0xc0 };
static const uint8_t DEV_D[18] = { 18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 0, 1 };
static uint8_t CFG_D[59];
static void make_cfg (void)
{
	const uint8_t c[] = {
		9, 2, 59, 0, 2, 1, 0, 0xa0, 50,
		9, 4, 0, 0, 1, 3, 1, 1, 0,  9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof KBD_RD, 0,  7, 5, 0x81, 3, 8, 0, 10,
		9, 4, 1, 0, 1, 3, 1, 2, 0,  9, 0x21, 0x11, 0x01, 0, 1, 0x22, sizeof MOUSE_RD, 0,  7, 5, 0x82, 3, 4, 0, 10 };
	memcpy (CFG_D, c, sizeof c);
}
static int str_d (int i, uint8_t *out)
{
	static const char *s[] = { NULL, "Vectrex_EMU", "PC keyboard and mouse" };
	if (i == 0) { out[0] = 4; out[1] = 3; out[2] = 0x09; out[3] = 0x04; return 4; }
	if (i < 1 || i > 2) return -1;
	int n = (int) strlen (s[i]);
	out[0] = (uint8_t) (2 + 2 * n); out[1] = 3;
	for (int k = 0; k < n; k++) { out[2 + 2 * k] = (uint8_t) s[i][k]; out[3 + 2 * k] = 0; }
	return 2 + 2 * n;
}

/* the frontend's reports (live.c): 0x80 the keyboard (modifiers, up to 3 keys), 0x81 the mouse (buttons, dx, dy, wheel) */
void vt_usb_report (int id, const unsigned char *b)
{
	if (id == 0x80) {
		uint8_t k[8] = { b[0], 0, b[1], b[2], b[3], 0, 0, 0 };
		if (memcmp (k, U.kbd, 8)) { memcpy (U.kbd, k, 8); U.kbd_new = 1; }
	} else if (id == 0x81) {
		if (b[0] != U.mbtn) U.m_new = 1;
		U.mbtn = b[0]; U.mdx += (int8_t) b[1]; U.mdy += (int8_t) b[2]; U.mwh += (int8_t) b[3];
		if (b[1] || b[2] || b[3]) U.m_new = 1;
	}
}

/* ------------------------------------------------------------------ the device's endpoints */
static void setup_packet (const uint8_t *s)
{
	int bm = s[0], req = s[1], wv = s[2] | (s[3] << 8), wi = s[4] | (s[5] << 8), wl = s[6] | (s[7] << 8);
	int n = 0; const uint8_t *d = NULL; uint8_t tmp[64];
	U.ctl_stall = 0; U.ctl_pos = 0; U.ctl_in = (bm & 0x80) != 0; U.ctl_nodata = wl == 0;
	if (U.log) fprintf (stderr, "usb: setup %02X %02X %04X %04X %d\n", bm, req, wv, wi, wl);
	if (bm == 0x80 && req == 6) {
		int t = wv >> 8, i = wv & 0xff;
		if (t == 1) { d = DEV_D; n = 18; }
		else if (t == 2) { d = CFG_D; n = 59; }
		else if (t == 3) { n = str_d (i, tmp); d = tmp; }
		else n = -1;
	} else if (bm == 0x81 && req == 6 && (wv >> 8) == 0x22) {
		if (wi == 0) { d = KBD_RD; n = sizeof KBD_RD; } else { d = MOUSE_RD; n = sizeof MOUSE_RD; }
	} else if (bm == 0x81 && req == 6 && (wv >> 8) == 0x21) {
		d = CFG_D + (wi == 0 ? 18 : 43); n = 9;
	} else if (bm == 0x00 && req == 5) { U.pend_addr = wv & 0x7f; }
	else if (bm == 0x00 && req == 9) { U.config = wv & 0xff; }
	else if (bm == 0x80 && req == 8) { tmp[0] = (uint8_t) U.config; d = tmp; n = 1; }
	else if ((bm & 0x9f) == 0x80 && req == 0) { tmp[0] = tmp[1] = 0; d = tmp; n = 2; }
	else if (bm == 0x81 && req == 10) { tmp[0] = 0; d = tmp; n = 1; }        /* GET_INTERFACE */
	else if ((bm & 0x7f) <= 2 && (req == 1 || req == 3 || req == 11)) { }    /* CLEAR / SET_FEATURE, SET_INTERFACE */
	else if (bm == 0x21 && req == 0x0a) { U.idle = wv >> 8; }                 /* HID SET_IDLE */
	else if (bm == 0x21 && req == 0x0b) { U.proto[wi & 1] = wv & 1; }          /* HID SET_PROTOCOL */
	else if (bm == 0x21 && req == 0x09) { }                                   /* HID SET_REPORT: the LEDs follow */
	else if (bm == 0xa1 && req == 0x03) { tmp[0] = (uint8_t) U.proto[wi & 1]; d = tmp; n = 1; }
	else if (bm == 0xa1 && req == 0x02) { tmp[0] = (uint8_t) U.idle; d = tmp; n = 1; }
	else if (bm == 0xa1 && req == 0x01) {                                     /* HID GET_REPORT */
		if ((wi & 1) == 0) { d = U.kbd; n = 8; } else { memset (tmp, 0, 4); tmp[0] = U.mbtn; d = tmp; n = 4; }
	} else n = -1;
	if (n < 0) { U.ctl_stall = 1; U.ctl_len = 0; if (U.log) fprintf (stderr, "usb: stall\n"); return; }
	if (n > wl) n = wl;
	if (n > (int) sizeof U.ctl) n = sizeof U.ctl;
	if (d && n) memcpy (U.ctl, d, (size_t) n);
	U.ctl_len = n;
}
/* an IN packet from endpoint ep: its length, -1 NAK, -2 STALL */
static int dev_in (int ep, uint8_t *buf, int max)
{
	if (ep == 0) {
		if (U.ctl_stall) return -2;
		if (U.ctl_in && !U.ctl_nodata) {                                      /* the data stage */
			int n = U.ctl_len - U.ctl_pos; if (n > max) n = max; if (n < 0) n = 0;
			memcpy (buf, U.ctl + U.ctl_pos, (size_t) n); U.ctl_pos += n;
			return n;
		}
		if (U.pend_addr >= 0) { U.addr = U.pend_addr; U.pend_addr = -1; }     /* the status stage: SET_ADDRESS takes hold */
		return 0;
	}
	if (ep == 1 && U.config) {
		if (!U.kbd_new) return -1;
		U.kbd_new = 0; memcpy (buf, U.kbd, 8);
		if (U.log) fprintf (stderr, "usb: keyboard report %02X %02X %02X %02X taken%c", U.kbd[0], U.kbd[2], U.kbd[3], U.kbd[4], 10);
		return max < 8 ? max : 8;
	}
	if (ep == 2 && U.config) {
		if (!U.m_new) return -1;
		int x = U.mdx < -127 ? -127 : U.mdx > 127 ? 127 : U.mdx, y = U.mdy < -127 ? -127 : U.mdy > 127 ? 127 : U.mdy;
		int w = U.mwh < -127 ? -127 : U.mwh > 127 ? 127 : U.mwh;
		U.mdx -= x; U.mdy -= y; U.mwh -= w;
		U.m_new = U.mdx || U.mdy || U.mwh;
		buf[0] = U.mbtn; buf[1] = (uint8_t) x; buf[2] = (uint8_t) y; buf[3] = (uint8_t) w;
		return max < 4 ? max : 4;
	}
	return -2;
}
static int dev_out (int ep, const uint8_t *buf, int n)
{
	if (ep == 0) {
		if (U.ctl_stall) return -2;
		if (!U.ctl_in && n >= 1) U.leds = buf[0];                             /* (SET_REPORT: the keyboard's LEDs) */
		return 0;
	}
	return -2;
}

/* ------------------------------------------------------------------ the controller */
static uint32_t rd32 (int o) { uint32_t v; memcpy (&v, U.dpram + o, 4); return v; }
static void wr32 (int o, uint32_t v) { memcpy (U.dpram + o, &v, 4); }
static uint32_t intr (void)
{
	uint32_t s = U.r[R_SIE_STAT >> 2], v = 0;
	if (U.conn_irq) v |= 1u << 0;
	if (U.sof_irq) v |= 1u << 2;
	if (s & SS_TRANS) v |= 1u << 3;
	if (U.r[R_BUFF_STAT >> 2]) v |= 1u << 4;
	if (s & (1u << 31)) v |= 1u << 5;
	if (s & SS_TIMEOUT) v |= 1u << 6;
	if (s & SS_STALL) v |= 1u << 10;
	return v;
}
static uint32_t ints (void) { return (intr () | U.r[R_INTF >> 2]) & U.r[R_INTE >> 2]; }
int usb_irq (void) { return U.on ? ints () != 0 : 0; }

/* one buffer of EPX (b = 0 or 1, double buffering): handled now, or left for later (a NAK) */
static int epx_buffer (int b)
{
	uint32_t bc = rd32 (0x80), ec = rd32 (0x100);
	uint32_t half = b ? bc >> 16 : bc & 0xffff;
	if (!(half & BC_AVAIL)) return 0;
	int off = (int) (ec & 0xffc0u) + b * 64, len = (int) (half & 0x3ff), done = 0;
	if (off < 0x180 || off + 64 > 4096) off = 0x180;
	if (U.xfer_in) {
		int n = dev_in (U.xfer_ep, U.dpram + off, len);
		if (n == -1) return 0;
		if (n == -2) { U.r[R_SIE_STAT >> 2] |= SS_STALL; U.xfer = 0; return 0; }
		half = (half & ~(BC_AVAIL | 0x3ffu)) | BC_FULL | (uint32_t) n;
		done = n < len || (half & BC_LAST);
	} else {
		int n = dev_out (U.xfer_ep, U.dpram + off, len);
		if (n == -2) { U.r[R_SIE_STAT >> 2] |= SS_STALL; U.xfer = 0; return 0; }
		half &= ~(BC_AVAIL | BC_FULL);
		done = (half & BC_LAST) != 0;
	}
	bc = b ? (bc & 0xffffu) | (half << 16) : (bc & 0xffff0000u) | half;
	wr32 (0x80, bc);
	U.r[R_BUFF_STAT >> 2] |= 1u;
	if (done) { U.xfer = 0; U.r[R_SIE_STAT >> 2] |= SS_TRANS | SS_ACK; }
	return 1;
}
static void epx_service (void)
{
	for (int k = 0; k < 4 && U.xfer; k++) {
		uint32_t ec = rd32 (0x100);
		int dbl = (ec >> 30) & 1;
		if (!epx_buffer (U.xfer_buf)) break;
		if (dbl) U.xfer_buf ^= 1;
	}
}
static void start_trans (void)
{
	uint32_t sc = U.r[R_SIE_CTRL >> 2], ae = U.r[R_ADDR_ENDP >> 2];
	int addr = (int) (ae & 0x7f), ep = (int) ((ae >> 16) & 0xf);
	if (!U.connected || addr != U.addr) { U.r[R_SIE_STAT >> 2] |= SS_TIMEOUT; return; }
	if (sc & SC_SETUP) {
		setup_packet (U.dpram);
		U.r[R_SIE_STAT >> 2] |= SS_TRANS | SS_ACK;
		return;
	}
	U.xfer = 1; U.xfer_in = (sc & SC_RECV) != 0; U.xfer_ep = ep; U.xfer_buf = 0;
	epx_service ();
}
/* the interrupt endpoints: polled by the controller */
static void int_eps (void)
{
	uint32_t act = U.r[R_INT_EP >> 2];
	for (int i = 1; i < 16; i++) {
		if (!((act >> i) & 1)) continue;
		uint32_t ctrl = rd32 (0x08 + (i - 1) * 8), bc = rd32 (0x88 + (i - 1) * 8), ae = U.r[i];
		if (!(ctrl & 0x80000000u) || !(bc & BC_AVAIL)) continue;
		if ((ae & 0x7f) != (uint32_t) U.addr || !U.connected) continue;
		int ep = (int) ((ae >> 16) & 0xf), off = (int) (ctrl & 0xffc0u), len = (int) (bc & 0x3ff);
		if (off + 64 > 4096) continue;
		if (ae & (1u << 25)) {                                                /* (OUT) */
			dev_out (ep, U.dpram + off, len);
			bc &= ~(BC_AVAIL | BC_FULL);
			wr32 (0x88 + (i - 1) * 8, bc); U.r[R_BUFF_STAT >> 2] |= 1u << (i * 2 + 1);
			continue;
		}
		int n = dev_in (ep, U.dpram + off, len);
		if (n < 0) continue;
		bc = (bc & ~(BC_AVAIL | 0x3ffu)) | BC_FULL | (uint32_t) n;
		wr32 (0x88 + (i - 1) * 8, bc);
		U.r[R_BUFF_STAT >> 2] |= 1u << (i * 2);
	}
}

void usb_init (void)
{
	memset (&U, 0, sizeof U);
	U.on = getenv ("VT_USBHID") && atoi (getenv ("VT_USBHID")) != 0;
	U.log = getenv ("VT_USBLOG") ? atoi (getenv ("VT_USBLOG")) : 0;
	if (getenv ("VT_USBLOGFILE")) freopen (getenv ("VT_USBLOGFILE"), "w", stderr);
	U.pend_addr = -1;
	U.r[R_MAIN_CTRL >> 2] = 1u << 2;                                          /* (PHY_ISO at reset) */
	make_cfg ();
}
int usb_on (void) { return U.on; }

static uint32_t usb_read_x (uint32_t a);
uint32_t usb_read (uint32_t a)
{
	uint32_t v = usb_read_x (a);
	static unsigned nlog; static uint32_t la, lv;
	if (U.log > 1 && (a != la || v != lv) && nlog++ < 3000) fprintf (stderr, "usb: rd %08X = %08X\n", a, v);
	la = a; lv = v;
	return v;
}
static uint32_t usb_read_x (uint32_t a)
{
	if (a < 0x50110000u) { uint32_t v; memcpy (&v, U.dpram + (a & 0xffc), 4); return v; }
	uint32_t o = a & 0xffc;
	switch (o) {
	case R_INTR: return intr ();
	case R_INTS: return ints ();
	case R_SOF_RD: U.sof_irq = 0; return U.frame & 0x7ff;
	}
	return o < sizeof U.r ? U.r[o >> 2] : 0;
}
/* a write: DPRAM bytes as written (n 1, 2 or 4), a register with its atomic alias (XOR / SET / CLR) */
void usb_write (uint32_t a, uint32_t v, int n)
{
	static unsigned nlog;
	if (U.log > 1 && nlog++ < 3000) fprintf (stderr, "usb: wr %08X = %08X (%d)\n", a, v, n);
	if (a < 0x50110000u) {
		uint32_t o = a & 0xfff;
		if (o + (uint32_t) n <= 4096) memcpy (U.dpram + o, &v, (size_t) n);
		if (U.xfer && (o & ~3u) == 0x80) epx_service ();                    /* (EPX's buffer handed over: on with the transfer) */
		return;
	}
	uint32_t o = a & 0xffc, alias = (a >> 12) & 3, old = o < sizeof U.r ? U.r[o >> 2] : 0, nv;
	if (o == R_SIE_STAT || o == R_BUFF_STAT) {                                /* (write 1 to clear, through any alias) */
		if (o == R_SIE_STAT) { if (v & SS_SPEED) U.conn_irq = 0; v &= SS_W1C; }
		U.r[o >> 2] &= ~v;
		return;
	}
	nv = alias == 0 ? v : alias == 1 ? old ^ v : alias == 2 ? old | v : old & ~v;
	if (o >= sizeof U.r) return;
	U.r[o >> 2] = nv;
	if (o == R_SIE_CTRL) {
		if (nv & SC_RESET) { U.addr = 0; U.pend_addr = -1; U.config = 0; U.xfer = 0; U.r[o >> 2] &= ~SC_RESET; }
		if (nv & SC_STOP) { U.xfer = 0; U.r[o >> 2] &= ~SC_STOP; }
		if (nv & SC_START) { U.r[o >> 2] &= ~SC_START; start_trans (); }
	}
}

/* every piece of chip time: the device plugs in a little after the host side is set up; 1 ms frames
 * (SOF), the interrupt endpoints, a NAKed transfer tried again */
void usb_tick (uint64_t now, uint32_t sys_hz)
{
	if (!U.on || now < U.next_tick) return;
	U.next_tick = now + sys_hz / 1000u;
	uint32_t mc = U.r[R_MAIN_CTRL >> 2], sc = U.r[R_SIE_CTRL >> 2];
	int host = (mc & 3) == 3;
	if (host && !U.connected) {
		if (!U.conn_at) U.conn_at = now + sys_hz / 10u;                       /* (100 ms after: plugged in) */
		else if (now >= U.conn_at) {
			U.connected = 1; U.conn_irq = 1;
			U.r[R_SIE_STAT >> 2] = (U.r[R_SIE_STAT >> 2] & ~(SS_SPEED | (3u << 2))) | (2u << 8) | (1u << 2) | SS_VBUS;
			if (U.log) fprintf (stderr, "usb: device connected (full speed)\n");
		}
	}
	(void) sc;
	if (!U.connected) return;
	U.frame++;
	if (U.r[R_SIE_CTRL >> 2] & SC_SOF_EN) U.sof_irq = 1;
	if (U.xfer) epx_service ();
	int_eps ();
}

/* ------------------------------------------------------------------ the device for another controller */
/* The same keyboard + mouse device behind another host controller (the PiTrex's DWC OTG, pitrex_usb.c): a bus
 * reset, its address, and its endpoints - a SETUP packet, an IN packet (length, -1 NAK, -2 STALL), an OUT
 * packet (0, -1 NAK, -2 STALL). */
void usbdev_bus_reset (void) { make_cfg (); U.addr = 0; U.pend_addr = -1; U.config = 0; U.ctl_len = U.ctl_pos = 0; U.ctl_stall = 0; }
int usbdev_addr (void) { return U.addr; }
void usbdev_log (int on) { U.log = on; }
int usbdev_setup (const uint8_t *s) { setup_packet (s); return 0; }
int usbdev_in (int ep, uint8_t *buf, int max) { return dev_in (ep, buf, max); }
int usbdev_out (int ep, const uint8_t *buf, int n) { return dev_out (ep, buf, n); }
