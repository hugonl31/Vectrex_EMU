/* pitrex_cart.c - the PiTrex in the cartridge slot (included by vecx.c): a Raspberry Pi Zero (arm11.c) on
 * a small board that holds the 6809 in /HALT and reaches the VIA through the cartridge port.
 *
 * A PiTrex program is a bare-metal Pi image (kernel.img style: loaded at 0x8000, run from there in ARM
 * state, SVC mode). It reaches the Vectrex through GPIO only (Kevin Koster's PiTrex I/O, pitrexio-gpio.c):
 *
 *   D0-D7 GPIO16-23 (out for a write, in for a read), A0-A3 GPIO7-10, A12 GPIO11, A14 GPIO12, R/W GPIO13,
 *   PB6 GPIO5 (in), IRQ_LATCH GPIO6, RDY GPIO24 (in), LATCH_EN GPIO25, the VIA's IRQ GPIO26 (in)
 *
 * An access: the address (and data) and R/W set, LATCH_EN raised; the board then does it in the next
 * E period - RDY low meanwhile, high when done (a read's byte then on D0-D7) - and LATCH_EN is dropped.
 * The 6809 never runs. Of the Pi: the system timer, the interrupt controller, the ARM timer, the mailbox
 * (the property tags a program asks), GPIO, the UARTs (their text to the log), and an SD controller that
 * finds no card (programs carry on without their ini files). The Pi runs at 1 GHz: 666.7 of its cycles
 * per E period, sliced in two; its waits - polling RDY, the cycle counter's delays - are slept through.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include "arm11.h"

int vt_pitrex;                              /* a PiTrex program in the slot */
#define PX_RAM  (256u << 20)
#define PX_PERI 0x20000000u
static arm11_t px_cpu; static arm11_sys_t px_sys; static uint8_t *px_ram;
static double px_frac;
static uint32_t px_fsel[6], px_out[2], px_eds[2], px_ren[2], px_fen[2];
static int px_rdy = 1, px_pending, px_rw; static unsigned px_addr; static uint8_t px_wdata, px_rdata;
static uint32_t px_st_cmp[4], px_st_cs;
static uint32_t px_irq_en1, px_irq_en2, px_irq_enb, px_fiq;
static uint32_t px_at_load, px_at_ctrl, px_at_raw, px_at_prediv = 0x7d; static uint64_t px_at_t0;
static uint32_t px_mbox_resp; static int px_mbox_full;
static char px_line[256]; static int px_ln;
static char px_fault_told;
static unsigned long px_n_acc;
static uint8_t px_image[8u << 20]; static size_t px_image_n;

/* the SD controller (the Arasan SDHCI at 0x20300000) and an SDHC card in it: an image file (VT_PITREX_SD) */
static FILE *px_sd; static uint32_t px_sd_blocks;
static uint32_t sd_arg1, sd_blksizecnt, sd_resp[4], sd_irq, sd_ctl0, sd_ctl1, sd_irpt_mask, sd_irpt_en;
static int sd_app, sd_rca = 0x1234;
static uint8_t sd_buf[512]; static uint32_t sd_lba; static int sd_blocks_left, sd_pos, sd_bsize, sd_dir;   /* (dir 1 read, 2 write) */
static void sd_seek_blk (uint32_t b)
{
#ifdef _WIN32
	_fseeki64 (px_sd, (long long) b * 512, SEEK_SET);
#else
	fseeko (px_sd, (off_t) b * 512, SEEK_SET);
#endif
}
static void sd_fill (void)                                                  /* the next block to read */
{
	memset (sd_buf, 0, sizeof sd_buf);
	if (sd_dir == 1 && sd_bsize == 512 && px_sd && sd_lba < px_sd_blocks) { sd_seek_blk (sd_lba); if (fread (sd_buf, 1, 512, px_sd) != 512) memset (sd_buf, 0, 512); }
	sd_pos = 0;
	sd_irq |= 0x20;                                                         /* READ_RDY */
}
static void sd_command (uint32_t cmdtm)
{
	unsigned cmd = (cmdtm >> 24) & 63, rsp = (cmdtm >> 16) & 3;
	int app = sd_app; sd_app = 0;
	uint32_t r0 = 0x00000900u;                                              /* (R1: ready for data, transfer state) */
	memset (sd_resp, 0, sizeof sd_resp);
	if (!px_sd) { sd_irq |= 0x00018000u; return; }                          /* (no card: every command times out) */
	if (app) {
		switch (cmd) {
		case 41: r0 = 0xc0ff8000u; break;                                   /* OCR: powered up, SDHC */
		case 51: sd_dir = 1; sd_bsize = sd_blksizecnt & 0x3ff; sd_blocks_left = 1;    /* SCR */
		         memset (sd_buf, 0, sizeof sd_buf); sd_buf[0] = 0x02; sd_buf[1] = 0x35; sd_buf[2] = 0x80; sd_pos = 0; sd_irq |= 0x20; break;
		default: break;
		}
	} else switch (cmd) {
	case 0: r0 = 0; break;
	case 5: sd_irq |= 0x00018000u; return;                                  /* (not an SDIO card: no answer) */
	case 8: r0 = sd_arg1 & 0xfff; break;
	case 55: r0 = 0x120; sd_app = 1; break;
	case 2: sd_resp[0] = 0x0000beef; sd_resp[1] = 0x56454d55; sd_resp[2] = 0x2020; sd_resp[3] = 0x00035058; r0 = sd_resp[0]; break;   /* CID */
	case 3: r0 = ((uint32_t) sd_rca << 16) | 0x0500; break;                  /* RCA */
	case 9: {                                                               /* CSD v2 (the bits from 8 up, as the controller gives them) */
		uint32_t cs = px_sd_blocks / 1024; if (cs) cs--;
		uint8_t csd[16] = { 0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0, 0, 0, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01 };
		csd[7] = (uint8_t) ((cs >> 16) & 0x3f); csd[8] = (uint8_t) (cs >> 8); csd[9] = (uint8_t) cs;
		/* (resp[0] = CSD bits 39:8, resp[1] 71:40, resp[2] 103:72, resp[3] 127:104) */
		for (int i = 0; i < 4; i++) { uint32_t w = 0; for (int k = 0; k < 4; k++) { int bit = 8 + 32 * i + 8 * k, byte = 15 - bit / 8; if (byte >= 0 && byte < 16) w |= (uint32_t) csd[byte] << (8 * k); } sd_resp[i] = w; }
		r0 = sd_resp[0];
		break;
	}
	case 7: case 16: case 6: case 13: case 12: case 23: r0 = 0x00000900u; break;
	case 17: case 18:
		sd_dir = 1; sd_bsize = 512; sd_lba = sd_arg1;
		sd_blocks_left = cmd == 17 ? 1 : (int) ((sd_blksizecnt >> 16) & 0xffff); if (sd_blocks_left < 1) sd_blocks_left = 1;
		sd_fill (); break;
	case 24: case 25:
		sd_dir = 2; sd_bsize = 512; sd_lba = sd_arg1; sd_pos = 0;
		sd_blocks_left = cmd == 24 ? 1 : (int) ((sd_blksizecnt >> 16) & 0xffff); if (sd_blocks_left < 1) sd_blocks_left = 1;
		sd_irq |= 0x10;                                                     /* WRITE_RDY */
		break;
	default: break;
	}
	if (rsp == 1) { if (cmd != 2 && cmd != 9) sd_resp[0] = r0; } else sd_resp[0] = r0;
	sd_irq |= 1;                                                            /* CMD_DONE */
	if (!(cmdtm & (1u << 21)) && rsp == 3) sd_irq |= 2;                   /* (busy: done at once) */
}
static uint32_t sd_data_read (void)
{
	if (sd_dir != 1 || sd_blocks_left <= 0) return 0;
	uint32_t v; memcpy (&v, sd_buf + sd_pos, 4); sd_pos += 4;
	if (sd_pos >= sd_bsize) {
		sd_irq &= ~0x20u;
		if (--sd_blocks_left > 0) { sd_lba++; sd_fill (); } else { sd_irq |= 2; sd_dir = 0; }   /* DATA_DONE */
	}
	return v;
}
static void sd_data_write (uint32_t v)
{
	if (sd_dir != 2 || sd_blocks_left <= 0) return;
	memcpy (sd_buf + sd_pos, &v, 4); sd_pos += 4;
	if (sd_pos >= 512) {
		if (px_sd && sd_lba < px_sd_blocks) { sd_seek_blk (sd_lba); fwrite (sd_buf, 1, 512, px_sd); fflush (px_sd); }
		sd_pos = 0; sd_irq &= ~0x10u;
		if (--sd_blocks_left > 0) { sd_lba++; sd_irq |= 0x10; } else { sd_irq |= 2; sd_dir = 0; }
	}
}
static uint32_t sd_reg_read (uint32_t r)
{
	switch (r) {
	case 0x04: return sd_blksizecnt; case 0x08: return sd_arg1;
	case 0x10: return sd_resp[0]; case 0x14: return sd_resp[1]; case 0x18: return sd_resp[2]; case 0x1c: return sd_resp[3];
	case 0x20: return sd_data_read ();
	case 0x24: return 0x01ff0000u | (sd_dir == 1 && sd_blocks_left > 0 ? 0x800 : 0);   /* (card in, DAT lines high; read buffer ready) */
	case 0x28: return sd_ctl0;
	case 0x2c: return (sd_ctl1 & ~0x07000000u) | ((sd_ctl1 & 1) ? 2 : 0);   /* (resets done at once; the clock stable) */
	case 0x30: return sd_irq | ((sd_irq & 0xffff0000u) ? 0x8000 : 0);
	case 0x34: return sd_irpt_mask; case 0x38: return sd_irpt_en;
	case 0x40: return 0x00000000u;
	case 0xfc: return 0x99020000u;                                          /* SLOTISR_VER: SDHCI 3 */
	default: return 0;
	}
}
static void sd_reg_write (uint32_t r, uint32_t v)
{
	switch (r) {
	case 0x04: sd_blksizecnt = v; break; case 0x08: sd_arg1 = v; break;
	case 0x0c: sd_command (v); break;
	case 0x20: sd_data_write (v); break;
	case 0x28: sd_ctl0 = v; break;
	case 0x2c: sd_ctl1 = v; if (v & 0x01000000u) { sd_irq = 0; sd_dir = 0; } break;
	case 0x30: sd_irq &= ~v; break;
	case 0x34: sd_irpt_mask = v; break; case 0x38: sd_irpt_en = v; break;
	default: break;
	}
}

static uint64_t px_us (void) { return px_cpu.cycles / 1000u; }                /* (the 1 MHz system timer) */
static int px_out_dir (int pin) { return ((px_fsel[pin / 10] >> ((pin % 10) * 3)) & 7) == 1; }
static uint32_t px_outmask;                                                   /* (the pins set as outputs) */
static void px_fsel_changed (void)
{
	px_outmask = 0;
	for (int p = 0; p < 32; p++) if (px_out_dir (p)) px_outmask |= 1u << p;
}
static uint32_t px_lev0 (void)
{
	uint32_t in = ((uint32_t) px_rdata << 16) | (px_rdy ? 1u << 24 : 0) | ((via_ifr & 0x80) ? 0 : 1u << 26)   /* (D0-D7, RDY, the VIA's /IRQ) */
	            | ((!(via_ddrb & 0x40) || (via_orb & 0x40)) ? 1u << 5 : 0);                                   /* PB6 */
	return (px_out[0] & px_outmask) | (in & ~px_outmask);
}
/* LATCH_EN raised: an access for the next E period */
static void px_latch (void)
{
	uint32_t o = px_out[0];
	px_addr = ((o >> 7) & 15) | (((o >> 11) & 1) << 12) | (((o >> 12) & 1) << 14) | 0x8000u;
	px_rw = (o >> 13) & 1;
	px_wdata = (uint8_t) (o >> 16);
	px_pending = 1; px_rdy = 0;
}
static void px_set_out (uint32_t nv)
{
	uint32_t old = px_out[0];
	px_out[0] = nv;
	if (!(old & (1u << 25)) && (nv & (1u << 25))) px_latch ();
}

/* the interrupt controller's pending bits */
static void px_irq_update (void)
{
	uint32_t p1 = px_st_cs & 0x0f;
	uint32_t basic = ((px_at_raw & 1) && (px_at_ctrl & 0x20)) ? 1 : 0;
	int irq = (p1 & px_irq_en1) || (basic & px_irq_enb);
	px_cpu.irq_line = irq;
}
/* the system timer's compares: matched since last time */
static void px_timer_tick (uint64_t us0, uint64_t us1)
{
	for (int i = 0; i < 4; i++) {
		uint32_t c = px_st_cmp[i];
		if ((uint32_t) (c - (uint32_t) us0 - 1) < (uint32_t) (us1 - us0)) px_st_cs |= 1u << i;
	}
	if (px_at_ctrl & 0x80) {                                                 /* the ARM timer (250 MHz core / prescale) */
		uint32_t div = (px_at_prediv + 1) * ((px_at_ctrl & 0x0c) == 4 ? 16 : (px_at_ctrl & 0x0c) == 8 ? 256 : 1);
		uint64_t ticks = (px_cpu.cycles - px_at_t0) / 4 / div;
		if (px_at_load && ticks >= px_at_load) { px_at_raw = 1; px_at_t0 = px_cpu.cycles; }
	}
}

/* the mailbox's property channel: the tags a bare-metal program asks */
static void px_mbox_props (uint32_t buf)
{
	buf &= 0x3fffffffu;
	if (buf + 8 > PX_RAM) return;
	uint32_t size; memcpy (&size, px_ram + buf, 4);
	uint32_t p = buf + 8, end = buf + size;
	while (p + 12 <= end && p + 12 <= PX_RAM) {
		uint32_t tag, vlen, *v; memcpy (&tag, px_ram + p, 4); if (!tag) break;
		memcpy (&vlen, px_ram + p + 4, 4);
		v = (uint32_t *) (px_ram + p + 12);
		uint32_t rlen = 0;
		switch (tag) {
		case 0x00000001: v[0] = 0x5f0e8a5c; rlen = 4; break;                   /* firmware revision */
		case 0x00010001: v[0] = 0; rlen = 4; break;                             /* board model */
		case 0x00010002: v[0] = 0x900093; rlen = 4; break;                      /* board revision: a Pi Zero 1.3 */
		case 0x00010003: v[0] = 0x27eb8bb8; v[1] = 0x0000; rlen = 6; break;    /* MAC */
		case 0x00010004: v[0] = 0x12345678; v[1] = 0; rlen = 8; break;         /* serial */
		case 0x00010005: v[0] = 0; v[1] = PX_RAM; rlen = 8; break;              /* ARM memory */
		case 0x00010006: v[0] = PX_RAM; v[1] = 0x04000000; rlen = 8; break;     /* VC memory */
		case 0x00020001: case 0x00020002: v[1] = 1; rlen = 8; break;            /* power state / timing */
		case 0x00028001: v[1] = v[1] & 1; rlen = 8; break;                      /* set power state */
		case 0x00030001: v[1] = 1; rlen = 8; break;                             /* clock state */
		case 0x00030002: case 0x00030004: case 0x00030007: {                    /* clock rates */
			uint32_t id = v[0];
			v[1] = id == 3 ? 1000000000u : id == 4 ? 250000000u : id == 1 ? 250000000u : id == 2 ? 48000000u : 250000000u;
			rlen = 8; break;
		}
		case 0x00038002: rlen = 8; break;                                       /* set clock rate: as asked */
		case 0x00030006: v[1] = 40000; rlen = 8; break;                          /* temperature */
		case 0x0003000a: v[1] = 85000; rlen = 8; break;                          /* max temperature */
		default: rlen = 0; break;
		}
		uint32_t rl = 0x80000000u | rlen; memcpy (px_ram + p + 8, &rl, 4);
		p += 12 + ((vlen + 3) & ~3u);
	}
	uint32_t ok = 0x80000000u; memcpy (px_ram + buf + 4, &ok, 4);
}

static uint32_t px_rd (arm11_t *c, uint32_t a, int n)
{
	(void) c;
	uint32_t off = a & 0x00ffffffu, v = 0;
	if ((a & 0xff000000u) != PX_PERI && (a & 0xff000000u) != 0x7e000000u) return 0;
	if (off >= 0x200000 && off < 0x2000b4) {                                    /* GPIO */
		unsigned r = off - 0x200000;
		if (r < 0x18) v = px_fsel[r / 4];
		else if (r == 0x34) v = px_lev0 ();
		else if (r == 0x40) v = px_eds[0];
		else if (r == 0x4c) v = px_ren[0];
		else if (r == 0x58) v = px_fen[0];
		else v = 0;
	} else if (off >= 0x3000 && off < 0x3020) {                                  /* the system timer */
		uint64_t us = px_us ();
		switch (off - 0x3000) {
		case 0: v = px_st_cs; break; case 4: v = (uint32_t) us; break; case 8: v = (uint32_t) (us >> 32); break;
		default: v = px_st_cmp[((off - 0x3000) - 0x0c) / 4]; break;
		}
	} else if (off >= 0xb200 && off < 0xb228) {                                  /* the interrupt controller */
		switch (off - 0xb200) {
		case 0x00: v = (((px_at_raw & 1) && (px_at_ctrl & 0x20)) ? 1 : 0) | ((px_st_cs & 0x0f) ? 0x100 : 0); break;
		case 0x04: v = px_st_cs & 0x0f; break;
		case 0x0c: v = px_fiq; break;
		case 0x10: v = px_irq_en1; break; case 0x14: v = px_irq_en2; break; case 0x18: v = px_irq_enb; break;
		default: v = 0;
		}
	} else if (off >= 0xb400 && off < 0xb424) {                                  /* the ARM timer */
		switch (off - 0xb400) {
		case 0x00: v = px_at_load; break; case 0x08: v = px_at_ctrl; break;
		case 0x04: { uint32_t div = px_at_prediv + 1; uint64_t t = (px_cpu.cycles - px_at_t0) / 4 / div; v = px_at_load > t ? px_at_load - (uint32_t) t : 0; break; }
		case 0x10: v = px_at_raw; break; case 0x14: v = (px_at_raw && (px_at_ctrl & 0x20)) ? 1 : 0; break;
		case 0x1c: v = px_at_prediv; break; case 0x20: v = (uint32_t) (px_cpu.cycles / 4); break;
		default: v = 0;
		}
	} else if (off >= 0xb880 && off < 0xb8c0) {                                  /* the mailbox */
		switch (off - 0xb880) {
		case 0x00: v = px_mbox_resp; px_mbox_full = 0; break;
		case 0x18: v = px_mbox_full ? 0 : 0x40000000u; break;                     /* (status: empty, never full) */
		case 0x38: v = 0; break;
		default: v = 0;
		}
	} else if (off >= 0x215000 && off < 0x215100) {                              /* AUX: the mini UART */
		switch (off - 0x215000) {
		case 0x04: v = 1; break; case 0x54: v = 0x60; break; case 0x64: v = 0x0e; break; default: v = 0;
		}
	} else if (off >= 0x201000 && off < 0x201090) {                              /* PL011 */
		v = off == 0x201018 ? 0x90 : 0;
	} else if (off >= 0x300000 && off < 0x300100) {                              /* the SD controller */
		v = sd_reg_read (off - 0x300000);
	} else if ((off >= 0x205000 && off < 0x205020) || (off >= 0x804000 && off < 0x804020)) {   /* I2C (BSC0 / BSC1): no device answers */
		v = (off & 0xff) == 0x04 ? 0x00000302u : 0;                                /* (S: DONE, ERR - no acknowledge, CLKT) */
	} else if (off >= 0x100000 && off < 0x100100) v = 0;                          /* PM */
	else {
		static int told; if (told++ < 60) fprintf (stderr, "PITREX: read %08X (unmodelled)\n", a);
	}
	(void) n;
	return v;
}
static void px_wr (arm11_t *c, uint32_t a, uint32_t v, int n)
{
	(void) c; (void) n;
	uint32_t off = a & 0x00ffffffu;
	if ((a & 0xff000000u) != PX_PERI && (a & 0xff000000u) != 0x7e000000u) return;
	if (off >= 0x200000 && off < 0x2000b4) {
		unsigned r = off - 0x200000;
		if (r < 0x18) { px_fsel[r / 4] = v; px_fsel_changed (); }
		else if (r == 0x1c) px_set_out (px_out[0] | v);
		else if (r == 0x20) px_out[1] |= v;
		else if (r == 0x28) px_set_out (px_out[0] & ~v);
		else if (r == 0x2c) px_out[1] &= ~v;
		else if (r == 0x40) px_eds[0] &= ~v;
		else if (r == 0x4c) px_ren[0] = v;
		else if (r == 0x58) px_fen[0] = v;
		return;
	}
	if (off >= 0x3000 && off < 0x3020) {
		if (off == 0x3000) px_st_cs &= ~v;
		else if (off >= 0x300c) px_st_cmp[(off - 0x300c) / 4] = v;
		px_irq_update (); return;
	}
	if (off >= 0xb200 && off < 0xb228) {
		switch (off - 0xb200) {
		case 0x0c: px_fiq = v; break;
		case 0x10: px_irq_en1 |= v; break; case 0x14: px_irq_en2 |= v; break; case 0x18: px_irq_enb |= v; break;
		case 0x1c: px_irq_en1 &= ~v; break; case 0x20: px_irq_en2 &= ~v; break; case 0x24: px_irq_enb &= ~v; break;
		}
		px_irq_update (); return;
	}
	if (off >= 0xb400 && off < 0xb424) {
		switch (off - 0xb400) {
		case 0x00: px_at_load = v; px_at_t0 = px_cpu.cycles; break;
		case 0x08: px_at_ctrl = v; px_at_t0 = px_cpu.cycles; break;
		case 0x0c: px_at_raw = 0; break;
		case 0x18: px_at_load = v; break;
		case 0x1c: px_at_prediv = v & 0x3ff; break;
		}
		px_irq_update (); return;
	}
	if (off >= 0xb880 && off < 0xb8c0) {
		if (off - 0xb880 == 0x20) {                                              /* a message: the property channel answers */
			if ((v & 15) == 8) px_mbox_props (v & ~15u);
			px_mbox_resp = v; px_mbox_full = 1;
		}
		return;
	}
	if (off >= 0x300000 && off < 0x300100) { sd_reg_write (off - 0x300000, v); return; }
	if ((off >= 0x215040 && off < 0x215044) || off == 0x201000) {              /* a UART's character: the log */
		char ch = (char) v;
		if (ch == '\n' || px_ln >= (int) sizeof px_line - 1) { px_line[px_ln] = 0; fprintf (stderr, "PITREX: %s\n", px_line); px_ln = 0; }
		else if (ch != '\r') px_line[px_ln++] = ch;
		return;
	}
	if (off >= 0x100000 && off < 0x100100) {                                     /* PM: a reset asked */
		if (off == 0x10001c && (v & 0xfff00000u) == 0x5a000000u && (v & 0x30)) snprintf (px_cpu.fault, sizeof px_cpu.fault, "the program restarts the Pi");
		return;
	}
}

/* a value the waiting CPU reads, peeked again: still the same? */
static int px_peek_same (void)
{
	uint32_t a = px_cpu.wait_addr, v;
	if (a < PX_RAM) memcpy (&v, px_ram + a, 4);
	else if ((a & 0x00ffffffu) == 0x200034) v = px_lev0 ();
	else if ((a & 0x00ffffffu) == 0x003004) v = (uint32_t) px_us ();
	else return 0;
	return v == px_cpu.wait_val;
}
static void px_run (uint32_t cyc)
{
	uint64_t until = px_cpu.cycles + cyc, us0 = px_us ();
	while (px_cpu.cycles < until && !px_cpu.halted) {
		if (px_cpu.wait_on) {
			if (px_peek_same () && !(px_cpu.irq_line && !px_cpu.ibit)) { px_cpu.cycles = until; break; }
			px_cpu.wait_on = 0;
		}
		arm11_run (&px_cpu, until);
	}
	if (px_cpu.halted) px_cpu.cycles = until;
	px_timer_tick (us0, px_us ());
	px_irq_update ();
}

int pitrex_open (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f) { perror (path); return -1; }
	px_image_n = fread (px_image, 1, sizeof px_image, f);
	fclose (f);
	if (px_image_n < 64) { fprintf (stderr, "PITREX: %s is not a PiTrex program\n", path); return -1; }
	if (!px_ram) px_ram = (uint8_t *) calloc (1, PX_RAM);
	if (!px_ram) { fprintf (stderr, "PITREX: no memory\n"); return -1; }
	{ const char *z = getenv ("VT_PITREX_SD");                               /* the SD card: a FAT image */
	  if (z && *z) { px_sd = fopen (z, "r+b");
	    if (px_sd) {
#ifdef _WIN32
	      _fseeki64 (px_sd, 0, SEEK_END); px_sd_blocks = (uint32_t) (_ftelli64 (px_sd) / 512);
#else
	      fseeko (px_sd, 0, SEEK_END); px_sd_blocks = (uint32_t) (ftello (px_sd) / 512);
#endif
	    } } }
	vt_pitrex = 1;
	return 0;
}
void pitrex_power_on (void)
{
	if (!px_ram) return;
	memset (px_ram, 0, PX_RAM);
	memcpy (px_ram + 0x8000, px_image, px_image_n);
	px_sys.rd = px_rd; px_sys.wr = px_wr; px_sys.ram = px_ram; px_sys.ram_size = PX_RAM;
	px_cpu.sys = &px_sys;
	arm11_reset (&px_cpu, 0x8000);
	px_cpu.r[0] = 0; px_cpu.r[1] = 0xc42; px_cpu.r[2] = 0x100;                /* (as the firmware starts a kernel) */
	memset (px_fsel, 0, sizeof px_fsel); px_out[0] = px_out[1] = 0; px_fsel_changed ();
	px_rdy = 1; px_pending = 0; px_rdata = 0xff; px_frac = 0;
	px_st_cs = 0; memset (px_st_cmp, 0, sizeof px_st_cmp); px_irq_en1 = px_irq_en2 = px_irq_enb = 0;
	px_at_ctrl = 0; px_at_raw = 0; px_mbox_full = 0; px_ln = 0; px_fault_told = 0;
}

/* one E period: the Pi runs, an access it latched happens; returns 1 (the 6809 is held) */
static int pitrex_cycle (void)
{
	px_frac += 1000000000.0 / 1500000.0;
	uint32_t per = (uint32_t) px_frac; px_frac -= per;
	px_run (per / 2);
	if (px_pending) {                                                         /* (the access, as E rises) */
		if (px_rw) px_rdata = read8 (px_addr & 0xffff);
		else write8 (px_addr & 0xffff, px_wdata);
		px_pending = 0; px_rdy = 1; px_n_acc++;
	}
	px_run (per - per / 2);
	if (!px_fault_told && px_cpu.halted) {
		fprintf (stderr, "PITREX: the program stopped: %s\n", px_cpu.fault);
		px_fault_told = 1;
	}
	{ static int lg = -1; static unsigned long long n; if (lg < 0) lg = getenv ("VT_PXLOG") != NULL;
	  if (lg && ++n % 1500000 == 0) fprintf (stderr, "pitrex: pc %08X lr %08X sp %08X cpsr %08X cycles %llu steps %llu accesses %lu wait %d irq %d\n",
	      px_cpu.r[15], px_cpu.r[14], px_cpu.r[13], arm11_cpsr (&px_cpu), (unsigned long long) px_cpu.cycles, (unsigned long long) px_cpu.steps, px_n_acc, px_cpu.wait_on, px_cpu.irq_line); }
	return 1;
}
