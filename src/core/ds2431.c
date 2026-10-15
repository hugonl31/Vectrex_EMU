/* ds2431.c - DS2431 1-Wire EEPROM on PB6 (included by vecx.c).
 *
 * The cartridge has the DS2431 data pin on PB6 (the same line that drives EPROM A15) with a 3.3K
 * pull-up. The line is low when the VIA drives it low (DDRB.6 = 1, ORB.6 = 0) or the DS2431 pulls
 * it low (presence pulse, or a 0 bit it sends); otherwise the pull-up makes it high.
 *
 * Enabled when the environment variable VT_DS2431 names a 128-byte file (created if missing); the
 * memory is written back to it after every Copy Scratchpad. Without it no device answers.
 * Supported: reset/presence, Read ROM (33), Skip ROM (CC), Match ROM (55), Write Scratchpad (0F),
 * Read Scratchpad (AA), Copy Scratchpad (55 after a ROM command), Read Memory (F0).
 * Timing in CPU cycles (1.5 per microsecond) from vx_cyc.
 */
#include <stdlib.h>

static unsigned long long vx_cyc;          /* CPU cycles since reset (start of the current instruction) */

static int ow_on;                           /* a device is present */
static char ow_file[512];
static unsigned char ow_mem[128];
static unsigned char ow_rom[8] = { 0x2D, 0x31, 0x42, 0x0A, 0x20, 0x26, 0x00, 0 };
static unsigned char ow_sp[8], ow_ta1, ow_ta2, ow_es;
static int ow_mlow;                         /* the VIA drives the line low */
static unsigned long long ow_fall;          /* start of the master's low */
static unsigned long long ow_pres0, ow_pres1;   /* presence pulse window */
static unsigned long long ow_dev_until;     /* the device holds the line low until */
/* protocol */
enum { OW_IDLE, OW_ROMCMD, OW_MATCH, OW_FUNC, OW_RX, OW_TX };
static int ow_state, ow_func, ow_cnt, ow_bit, ow_byte;
static unsigned char ow_txbuf[160]; static int ow_txlen, ow_txpos;

#define US(x) ((unsigned long long) ((x) * 1.5))

static unsigned char ow_crc8 (const unsigned char *p, int n)
{
	unsigned char c = 0;
	while (n--) { unsigned char b = *p++; for (int i = 0; i < 8; i++) { unsigned char m = (c ^ b) & 1; c >>= 1; if (m) c ^= 0x8C; b >>= 1; } }
	return c;
}

static void ow_init (void)
{
	const char *f = getenv ("VT_DS2431");
	if (!f || !*f) return;
	ow_on = 1;
	snprintf (ow_file, sizeof ow_file, "%s", f);
	memset (ow_mem, 0xff, sizeof ow_mem);
	FILE *fp = fopen (ow_file, "rb");
	if (fp) { if (fread (ow_mem, 1, 128, fp) != 128) {} fclose (fp); }
	ow_rom[7] = ow_crc8 (ow_rom, 7);
}

static void ow_store (void)
{
	FILE *fp = fopen (ow_file, "wb");
	if (fp) { fwrite (ow_mem, 1, 128, fp); fclose (fp); }
}

static void ow_tx (const unsigned char *p, int n) { memcpy (ow_txbuf, p, n); ow_txlen = n; ow_txpos = 0; ow_bit = 0; ow_state = OW_TX; }
static void ow_rx (int func, int n) { ow_func = func; ow_cnt = n; ow_bit = 0; ow_byte = 0; ow_state = OW_RX; }

/* a whole byte arrived from the master */
static void ow_got_byte (unsigned char b)
{
	static unsigned char buf[16]; static int n;
	switch (ow_func) {
	case 0x100:                                   /* ROM command */
		if (b == 0xCC) ow_rx (0x101, 1);
		else if (b == 0x33) ow_tx (ow_rom, 8), ow_func = 0x102;
		else if (b == 0x55) { n = 0; ow_rx (0x103, 8); }
		else ow_state = OW_IDLE;
		return;
	case 0x103:                                   /* Match ROM: 8 bytes */
		buf[n++] = b;
		if (n < 8) { ow_rx (0x103, 8 - n); return; }
		if (memcmp (buf, ow_rom, 8)) ow_state = OW_IDLE; else ow_rx (0x101, 1);
		return;
	case 0x101:                                   /* memory function */
		n = 0;
		if (b == 0x0F || b == 0xAA || b == 0x55 || b == 0xF0) { buf[0] = b; }
		if (b == 0x0F || b == 0x55 || b == 0xF0) { ow_rx (0x200 | b, b == 0x55 ? 3 : 2); return; }
		if (b == 0xAA) {
			unsigned char out[11]; out[0] = ow_ta1; out[1] = ow_ta2; out[2] = ow_es;
			memcpy (out + 3, ow_sp, 8);
			ow_tx (out, 11); return;
		}
		ow_state = OW_IDLE;
		return;
	case 0x20F:                                   /* Write Scratchpad: TA1 TA2, then data */
		buf[n++] = b;
		if (n == 2) { ow_ta1 = buf[0]; ow_ta2 = buf[1]; ow_es = ow_ta1 & 7; ow_rx (0x30F, 8 - (ow_ta1 & 7)); n = 0; return; }
		ow_rx (0x20F, 2 - n);
		return;
	case 0x30F:
		ow_sp[(ow_ta1 & 7) + n] = b; ow_es = (ow_ta1 & 7) + n; n++;
		if (n < 8 - (ow_ta1 & 7)) { ow_rx (0x30F, 1); return; }
		ow_state = OW_IDLE;                       /* (CRC16 not modelled) */
		return;
	case 0x255:                                   /* Copy Scratchpad: TA1 TA2 ES */
		buf[n++] = b;
		if (n < 3) { ow_rx (0x255, 3 - n); return; }
		if (buf[0] == ow_ta1 && buf[1] == ow_ta2 && buf[2] == ow_es && ow_ta1 < 128 && ow_ta2 == 0) {
			int a = ow_ta1 & ~7;
			for (int i = 0; i <= (ow_es & 7); i++) ow_mem[a + i] = ow_sp[i];
			ow_es |= 0x80;
			ow_store ();
			static const unsigned char aa[16] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
			                                      0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
			ow_tx (aa, 16);
		} else ow_state = OW_IDLE;
		return;
	case 0x2F0:                                   /* Read Memory: TA1 TA2, then data to the end */
		buf[n++] = b;
		if (n < 2) { ow_rx (0x2F0, 1); return; }
		{
			int a = buf[0] | (buf[1] << 8);
			int len = 0;
			for (; a < 128 && len < 160; a++) ow_txbuf[len++] = ow_mem[a];
			while (len < 160) ow_txbuf[len++] = 0xFF;
			ow_txlen = len; ow_txpos = 0; ow_bit = 0; ow_state = OW_TX;
		}
		return;
	}
	ow_state = OW_IDLE;
}

/* the master's line drive changed */
static void ow_master (int low)
{
	if (!ow_on || low == ow_mlow) { ow_mlow = low; return; }
	ow_mlow = low;
	unsigned long long t = vx_cyc;
	if (low) {                                     /* falling edge: a slot (or a reset) starts */
		ow_fall = t;
		if (ow_state == OW_TX) {
			int bit = ow_txpos < ow_txlen ? (ow_txbuf[ow_txpos] >> ow_bit) & 1 : 1;
			if (!bit) ow_dev_until = t + US (30);
		}
		return;
	}
	unsigned long long dur = t - ow_fall;          /* rising edge */
	if (dur >= US (400)) {                         /* reset: presence 30..150 us after release */
		ow_pres0 = t + US (30); ow_pres1 = t + US (150);
		ow_func = 0x100; ow_cnt = 1; ow_bit = 0; ow_byte = 0; ow_state = OW_RX;
		return;
	}
	if (ow_state == OW_TX) {
		if (++ow_bit == 8) { ow_bit = 0; ow_txpos++; }
		return;
	}
	if (ow_state == OW_RX) {
		int bit = dur < US (30);                   /* short low = 1, long low (60+ us) = 0 */
		ow_byte |= bit << ow_bit;
		if (++ow_bit == 8) { unsigned char b = ow_byte; ow_bit = 0; ow_byte = 0; ow_got_byte (b); }
	}
}

/* the physical PB6 level now */
static int ow_line (void)
{
	if (ow_mlow) return 0;
	if (!ow_on) return 1;
	unsigned long long t = vx_cyc;
	if (t >= ow_pres0 && t < ow_pres1) return 0;
	if (t < ow_dev_until) return 0;
	return 1;
}
