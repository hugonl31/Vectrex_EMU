/* pitrex_usb.c - the Pi's USB host controller (included by pitrex_cart.c): the Synopsys DesignWare OTG core of the
 * BCM2835 at 0x20980000, IRQ 9, in host mode with internal DMA - as USPi (R. Stange's bare-metal USB library, which
 * the PiTrex programs link: Elite, GS BASIC, the loader...) drives it. On its one root port: the keyboard + mouse
 * device of usbhost.c (the PC's keyboard and mouse, sent by the frontend as HID reports), plugged in only with
 * VT_USBHID=1.
 *
 * What USPi uses, and nothing more: the core's identity (vendor id, hardware config: 8 host channels, internal DMA,
 * UTMI+), its soft reset and FIFO flushes (done at once), the interrupt masks (AHB global, GINTMSK, HAINTMSK, each
 * channel's HCINTMSK), the root port (connect, power, reset -> enabled at full speed), the frame number (1 ms
 * frames of the Pi's own time: USPi waits for the next one before an interrupt transfer), and the host channels:
 * enabling one runs its whole transfer a few microseconds later - SETUP / OUT from the Pi's RAM, IN into it (DMA),
 * packet by packet until a short one - then HCTSIZ holds the bytes and packets left and HCINT the result
 * (XFER_COMPLETE + ACK, NAK, or STALL; with HALTED). The device answers at once: no bus timing, no data toggles.
 * An interrupt IN that the device NAKs stays armed and is asked again every frame (see dw_xfer).
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

extern void usbdev_bus_reset (void); extern int usbdev_addr (void); extern void usbdev_log (int on);
extern int usbdev_setup (const uint8_t *s); extern int usbdev_in (int ep, uint8_t *buf, int max);
extern int usbdev_out (int ep, const uint8_t *buf, int n);

#define DW_CH        8                          /* host channels (HW_CFG2: as on the Pi) */
#define DW_GOTGCTL   0x000
#define DW_GAHBCFG   0x008
#define DW_GUSBCFG   0x00c
#define DW_GRSTCTL   0x010
#define DW_GINTSTS   0x014
#define DW_GINTMSK   0x018
#define DW_GSNPSID   0x040
#define DW_GHWCFG1   0x044
#define DW_GHWCFG2   0x048
#define DW_GHWCFG3   0x04c
#define DW_GHWCFG4   0x050
#define DW_HCFG      0x400
#define DW_HFNUM     0x408
#define DW_HAINT     0x414
#define DW_HAINTMSK  0x418
#define DW_HPRT      0x440
#define DW_PCGCCTL   0xe00

#define HC_XFERC     (1u << 0)
#define HC_HALTED    (1u << 1)
#define HC_STALL     (1u << 3)
#define HC_NAK       (1u << 4)
#define HC_ACK       (1u << 5)
#define HC_XACTERR   (1u << 7)

static struct {
	int on, log, inited;
	uint32_t reg[0x100];                        /* the core and host registers 0x000-0x3ff, by word */
	uint32_t hcfg, haintmsk, pcgcctl;
	uint32_t hprt;                              /* power (12), reset (8), enable (2), connect changed (1) */
	uint32_t hcchar[DW_CH], hcsplt[DW_CH], hcint[DW_CH], hcintmsk[DW_CH], hctsiz[DW_CH], hcdma[DW_CH];
	uint64_t due[DW_CH];                        /* (the Pi cycle a channel's transfer is done at; 0: none) */
} D;

static void dw_init (void)
{
	if (D.inited) return;
	D.inited = 1;
	const char *e = getenv ("VT_USBHID");
	D.on = e && atoi (e) != 0;
	D.log = getenv ("VT_USBLOG") != NULL;
	usbdev_log (D.log);
	D.hprt = 1u << 1;                           /* (a device seen at power on: connect changed) */
	D.reg[DW_GRSTCTL >> 2] = 1u << 31;          /* AHB idle */
	usbdev_bus_reset ();
}

static uint32_t dw_haint (void)
{
	uint32_t m = 0;
	for (int c = 0; c < DW_CH; c++) if (D.hcint[c] & D.hcintmsk[c]) m |= 1u << c;
	return m;
}
static uint32_t dw_gintsts (void)
{
	uint32_t v = (D.reg[DW_GINTSTS >> 2] & ~((1u << 25) | (1u << 24) | 1u)) | 1u;   /* (bit 0: host mode) */
	if (dw_haint () & D.haintmsk) v |= 1u << 25;                                     /* a channel's interrupt */
	if (D.hprt & ((1u << 1) | (1u << 3))) v |= 1u << 24;                             /* a port change */
	return v;
}
/* IRQ 9 to the Pi's interrupt controller */
static int px_usb_irq (void)
{
	if (!D.inited || !(D.reg[DW_GAHBCFG >> 2] & 1)) return 0;
	return (dw_gintsts () & D.reg[DW_GINTMSK >> 2]) != 0;
}

static int dw_connected (void) { return D.on && (D.hprt & (1u << 12)); }
static uint32_t dw_hprt (void)
{
	uint32_t v = D.hprt & ((1u << 12) | (1u << 8) | (1u << 2) | (1u << 1) | (1u << 3));
	if (dw_connected ()) v |= 1u | (1u << 17);                                      /* connected, full speed */
	return v;
}

/* one host channel's transfer, run to its end */
static void dw_xfer (int c)
{
	uint32_t ch = D.hcchar[c], ts = D.hctsiz[c];
	int mps = (int) (ch & 0x7ff), ep = (int) ((ch >> 11) & 15), in = (ch >> 15) & 1, dev = (int) ((ch >> 22) & 0x7f);
	int bytes = (int) (ts & 0x7ffff), pkts = (int) ((ts >> 19) & 0x3ff), pid = (int) ((ts >> 29) & 3);
	uint32_t dma = D.hcdma[c] & 0x3fffffffu;
	uint32_t st;
	int done = 0, k = 0;
	if (mps <= 0) mps = 8;
	if (!dw_connected () || !(D.hprt & (1u << 2)) || dev != usbdev_addr () || dma + (uint32_t) bytes > PX_RAM) {
		st = HC_XACTERR;                                                          /* (no answer) */
	} else if (!in) {
		int r;
		if (ep == 0 && pid == 3) { r = bytes >= 8 ? usbdev_setup (px_ram + dma) : -2; if (r >= 0) r = 0; }
		else r = usbdev_out (ep, px_ram + dma, bytes);
		if (r == -1) st = HC_NAK;
		else if (r == -2) st = HC_STALL;
		else { st = HC_XFERC | HC_ACK; done = bytes; k = pkts; }
	} else {
		uint8_t tmp[1024];
		st = HC_XFERC | HC_ACK;
		while (k < (pkts ? pkts : 1)) {
			int n = usbdev_in (ep, tmp, mps < (int) sizeof tmp ? mps : (int) sizeof tmp);
			if (n == -1) { if (k == 0) st = HC_NAK; break; }
			if (n == -2) { st = HC_STALL; break; }
			if (done + n > bytes) n = bytes - done;
			memcpy (px_ram + dma + done, tmp, (size_t) n);
			done += n; k++;
			if (n < mps) break;                                                   /* (a short packet: the end) */
		}
	}
	if (st == HC_NAK && in && ((ch >> 18) & 3) == 3) {
		/* an interrupt endpoint with nothing new (the keyboard, the mouse): the channel stays armed and asks again
		 * next frame, as a NAK the hardware retries. Halted with NAK instead, USPi would retry it after the
		 * endpoint's interval - and the PiTrex's StartKernelTimer waits that interval out inside the interrupt
		 * (10 ms in every 10): the program itself would never run again */
		D.due[c] = px_cpu.cycles + 1000000u;
		return;
	}
	if (st & (HC_NAK | HC_STALL | HC_XACTERR)) { done = 0; k = 0; }
	if (pkts == 0) k = 0;
	D.hctsiz[c] = (ts & (3u << 29)) | ((uint32_t) (pkts - k) << 19) | (uint32_t) (bytes - done);
	D.hcchar[c] &= ~((1u << 31) | (1u << 30));
	D.hcint[c] |= st | HC_HALTED;
	if (D.log) fprintf (stderr, "usb: ch%d dev %d ep %d %s pid %d %d bytes / %d pkts -> %d bytes, int %03X\n",
	                    c, dev, ep, in ? "IN" : "OUT", pid, bytes, pkts, done, (unsigned) D.hcint[c]);
}

/* every slice of Pi time: the transfers that are due */
static void px_usb_tick (void)
{
	if (!D.inited) return;
	for (int c = 0; c < DW_CH; c++)
		if (D.due[c] && px_cpu.cycles >= D.due[c]) { D.due[c] = 0; dw_xfer (c); }
}

static uint32_t px_usb_rd (uint32_t off)
{
	dw_init ();
	if (off >= 0x500 && off < 0x500 + DW_CH * 0x20) {
		int c = (int) ((off - 0x500) >> 5);
		switch ((off - 0x500) & 0x1f) {
		case 0x00: return D.hcchar[c]; case 0x04: return D.hcsplt[c]; case 0x08: return D.hcint[c];
		case 0x0c: return D.hcintmsk[c]; case 0x10: return D.hctsiz[c]; case 0x14: return D.hcdma[c];
		default: return 0;
		}
	}
	switch (off) {
	case DW_GRSTCTL: return (D.reg[DW_GRSTCTL >> 2] & ~((1u << 0) | (1u << 4) | (1u << 5))) | (1u << 31);   /* (resets and flushes: done) */
	case DW_GINTSTS: return dw_gintsts ();
	case DW_GSNPSID: return 0x4f54280au;                                             /* "OT2", 2.80a */
	case DW_GHWCFG1: return 0;
	case DW_GHWCFG2: return 0x228ddd50u;        /* (the Pi's: internal DMA, UTMI+, dedicated FS PHY, 8 host channels) */
	case DW_GHWCFG3: return 0x0ff000e8u;
	case DW_GHWCFG4: return 0x1ff00020u;
	case 0x03c: return 0x2708a000u;                                                  /* (the user id) */
	case DW_HCFG: return D.hcfg;
	case DW_HFNUM: { uint64_t us = px_us (); return (uint32_t) ((us / 1000u) & 0x3fff) | ((uint32_t) (1000u - us % 1000u) * 60u << 16); }
	case DW_HAINT: return dw_haint ();
	case DW_HAINTMSK: return D.haintmsk;
	case DW_HPRT: return dw_hprt ();
	case DW_PCGCCTL: return D.pcgcctl;
	}
	return off < 0x400 ? D.reg[off >> 2] : 0;
}

static void px_usb_wr (uint32_t off, uint32_t v)
{
	dw_init ();
	if (off >= 0x500 && off < 0x500 + DW_CH * 0x20) {
		int c = (int) ((off - 0x500) >> 5);
		switch ((off - 0x500) & 0x1f) {
		case 0x00:
			if ((v & (1u << 30)) && (D.hcchar[c] & (1u << 31)) && !(v & (1u << 31))) {   /* a channel disabled: halted */
				D.due[c] = 0; D.hcchar[c] = v & ~((1u << 31) | (1u << 30)); D.hcint[c] |= HC_HALTED; break;
			}
			D.hcchar[c] = v;
			if (v & (1u << 31)) D.due[c] = px_cpu.cycles + 5000u;              /* (enabled: done 5 us later) */
			break;
		case 0x04: D.hcsplt[c] = v; break;
		case 0x08: D.hcint[c] &= ~v; break;                                        /* (write 1 to clear) */
		case 0x0c: D.hcintmsk[c] = v; break;
		case 0x10: D.hctsiz[c] = v; break;
		case 0x14: D.hcdma[c] = v; break;
		}
		return;
	}
	switch (off) {
	case DW_GINTSTS: D.reg[DW_GINTSTS >> 2] &= ~v; return;
	case DW_GRSTCTL: D.reg[DW_GRSTCTL >> 2] = v & ~((1u << 0) | (1u << 4) | (1u << 5)); return;
	case DW_HCFG: D.hcfg = v; return;
	case DW_HAINT: return;
	case DW_HAINTMSK: D.haintmsk = v; return;
	case DW_PCGCCTL: D.pcgcctl = v; return;
	case DW_HPRT: {
		uint32_t old = D.hprt;
		D.hprt &= ~(v & ((1u << 1) | (1u << 3) | (1u << 5)));                   /* (the change bits: write 1 to clear) */
		if (v & (1u << 2)) D.hprt &= ~(1u << 2);                                  /* (enable: write 1 to disable) */
		D.hprt = (D.hprt & ~((1u << 12) | (1u << 8))) | (v & ((1u << 12) | (1u << 8)));
		if ((old & (1u << 8)) && !(v & (1u << 8)) && dw_connected ()) {          /* the port's reset ends: enabled */
			D.hprt |= (1u << 2) | (1u << 3);
			usbdev_bus_reset ();
			if (D.log) fprintf (stderr, "usb: port reset, device enabled (full speed)\n");
		}
		return;
	}
	}
	if (off < 0x400) D.reg[off >> 2] = v;
}
