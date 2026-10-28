/* rp2350.c - the RP2350 of the Ultimate Vectrex Multicart 2: the memory map, SIO, timers, DMA, PIO, the
 * QMI's PSRAM, SPI0 with an SD card, a UART for debug text, and the bootrom's few services a pico-sdk
 * image asks for. Register maps from the pico-sdk's hardware_regs (BSD-3-Clause); behaviour from the
 * RP2350 datasheet. Everything not modelled keeps what is written to it (so status reads of the
 * clock / reset / pad blocks find sensible values), and the first touch of each unknown register is
 * logged with VT_RPLOG=1.
 *
 * Time: `now` counts system clock cycles; the cores run in slices (cm33_run), the PIO state machines
 * and the DMA are serviced between them. Polling loops (a core reading the same peripheral value twice
 * in a slice) end the slice early: their time passes without running every instruction. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cm33.h"
#include "rp2350.h"
#include "host.h"

#define SRAM_BASE   0x20000000u
#define SRAM_SIZE   0x82000u
#define PSRAM_SIZE  (8u << 20)
#define NPINS       48

typedef struct {
	uint32_t pc, x, y, isr, osr;
	int isr_cnt, osr_cnt;
	uint32_t tx[8], rx[8]; int txn, rxn, txr, rxr;   /* FIFOs (ring: read index, count) */
	int delay, stalled;
	uint32_t clkdiv, execctrl, shiftctrl, pinctrl;
	uint32_t frac;                                   /* (clock divider phase, 1/256 cycles) */
	int exec_pending; uint32_t exec_instr;
	int irq_wait;
	int wgpio, wpol, wtx;                            /* (stalled on: a GPIO level (pin + 1), an empty TX FIFO) */
} pio_sm_t;
typedef struct {
	uint16_t instr[32];
	pio_sm_t sm[4];
	uint32_t ctrl, irq, gpiobase, inte[2], intf[2];
	uint32_t pins, dirs;                             /* the block's pin outputs and directions (window) */
	uint32_t fdebug, input_sync_bypass;
} pio_t;
typedef struct {
	uint32_t read, write, count, ctrl, count_reload;
	int busy;
} dma_ch_t;

struct rp2350 {
	cm33_t cpu[2];
	cm33_sys_t sys;
	uint8_t sram[SRAM_SIZE];
	uint8_t *psram;
	uint8_t rom[0x8000];
	uint64_t now;                                    /* system clock cycles */
	/* core 0 on a host thread of its own (see th_main); what one thread writes often and the other reads is on a
	 * cache line of its own (the pads), else the two host cores pass that line back and forth all the time */
	char th_pad0[64];
	volatile uint32_t lk_next, lk_serve; volatile int lk_want, th_on, th_quit, th_exited;
	char th_pad1[64];
	volatile uint64_t th_target;                     /* (the main thread writes it) */
	char th_pad2[64];
	volatile uint64_t th_done;                       /* (core 0's thread writes it) */
	uint64_t th_spin_idle, th_spin_lk;               /* (core 0's thread's statistics) */
	char th_pad3[64];
	int th_enabled, th_made; void *th_handle; uint32_t th_io, th_epochs; uint64_t th_off_until;
	uint64_t th_n_on, th_n_off, th_n_ep, th_spin_main, th_n_yield, th_cyc_on;   /* (the main thread's statistics) */
	char th_pad4[64];
	uint32_t sys_hz;
	/* SIO */
	uint64_t gpio_out, gpio_oe;
	uint32_t fifo[2][8]; int fifo_n[2], fifo_r[2];   /* fifo[k]: the words for core k */
	int fifo_err[2];
	uint32_t spinlock, bootlock;
	uint32_t doorbell[2];
	/* core 1 waiting in the bootrom for its launch (the FIFO handshake) */
	int core1_parked, launch_i; uint32_t launch_seq[6];
	/* pins */
	uint64_t ext_mask, ext_val;                      /* driven from outside */
	uint8_t funcsel[NPINS];
	uint64_t m_sio, m_spi, m_pull, m_pio[3];       /* (fmasks) */
	uint32_t pads[NPINS + 2];
	uint32_t gpio_ctrl[NPINS];
	/* peripherals */
	uint64_t timer_base[2];                          /* (microseconds = (now - base) / mhz) */
	uint32_t alarm[2][4], armed[2], tintr[2], tinte[2], tintf[2];
	uint64_t timer_next;
	dma_ch_t dma[16];
	uint32_t dma_intr, dma_inte[4], dma_intf[4], dma_timer[4];
	int dma_irq_on; uint32_t dma_busy, dma_other;            /* (dma_other: busy channels paced by something else than a FIFO) */
	pio_t pio[3];
	/* QMI direct mode, PSRAM */
	uint32_t qmi_csr, qmi_rx[8]; int qmi_rxn, qmi_rxr;
	uint8_t qmi_cmd[8]; int qmi_cmdn;
	uint32_t xip_ctrl;
	/* SPI0 + the SD card */
	uint32_t spi_cr0, spi_cr1, spi_cpsr;
	uint8_t spi_rx[8]; int spi_rxn, spi_rxr;
	FILE *sd; uint32_t sd_blocks;
	int sd_state, sd_idle, sd_app, sd_hc;
	uint8_t sd_cmd[6]; int sd_cmdn;
	uint8_t sd_resp[600]; int sd_respn, sd_respr;
	uint8_t sd_wbuf[514]; int sd_wn; uint32_t sd_waddr; int sd_writing, sd_wmulti;
	int sd_rmulti; uint32_t sd_raddr; unsigned long sd_nread, sd_nwrite;
	/* UART text */
	char uart_line[256]; int uart_n;
	/* the PT8211 (PIO2 pins 43-45): captured samples */
	int16_t aud[16384]; int aud_n; int aud_rate;
	uint32_t pt_shift; int pt_bits; int pt_ws, pt_bck; int pt_lr; int16_t pt_l;
	uint64_t pt_last_frame; uint32_t pt_frames;
	/* the generic register file (everything else) */
	struct { uint32_t addr, val; } regs[4096];
	int log;
	char fault[200];
	/* polling detection */
	uint32_t poll_pc[2], poll_val[2], poll_addr[2];
	int poll_hits[2];
};

/* ------------------------------------------------------------------ the generic register file */
static uint32_t *greg (rp2350_t *rp, uint32_t a, int create)
{
	uint32_t h = (a >> 2) * 2654435761u;
	for (int i = 0; i < 4096; i++) {
		unsigned k = (h + (unsigned) i) & 4095;
		if (rp->regs[k].addr == a) return &rp->regs[k].val;
		if (rp->regs[k].addr == 0) {
			if (!create) return NULL;
			rp->regs[k].addr = a;
			rp->regs[k].val = 0;
			return &rp->regs[k].val;
		}
	}
	return NULL;
}
static uint32_t greg_rd (rp2350_t *rp, uint32_t a)
{
	uint32_t *p = greg (rp, a, 0);
	static unsigned n_unmod;                                           /* (the first few: a stray pointer may read on forever) */
	if (!p && rp->log && n_unmod++ < 400) fprintf (stderr, "rp2350: read  %08X (unmodelled)\n", a);
	return p ? *p : 0;
}
static void greg_wr (rp2350_t *rp, uint32_t a, uint32_t v)
{
	uint32_t *p = greg (rp, a, 1);
	if (p) *p = v;
}

/* the atomic aliases of the APB / AHB peripherals: +0x1000 XOR, +0x2000 SET, +0x3000 CLR */
static uint32_t apply_alias (uint32_t old, uint32_t v, int alias)
{
	switch (alias) { case 1: return old ^ v; case 2: return old | v; case 3: return old & ~v; default: return v; }
}

/* ------------------------------------------------------------------ pins */
/* which function owns each pin, as masks (recomputed when a FUNCSEL or a pad changes) */
static void fmasks (rp2350_t *rp)
{
	rp->m_sio = rp->m_spi = rp->m_pull = 0; rp->m_pio[0] = rp->m_pio[1] = rp->m_pio[2] = 0;
	for (int i = 0; i < NPINS; i++) {
		int f = rp->funcsel[i]; uint64_t b = 1ull << i;
		if (f == 5) rp->m_sio |= b;
		else if (f >= 6 && f <= 8) rp->m_pio[f - 6] |= b;
		else if (f == 1 && (i == 34 || i == 35)) rp->m_spi |= b;
		if (rp->pads[i] & 8) rp->m_pull |= b;                                 /* PUE */
	}
}
static uint64_t pull_levels (rp2350_t *rp) { return rp->m_pull; }
static void chip_drive (rp2350_t *rp, uint64_t *val, uint64_t *oe)
{
	uint64_t o = rp->gpio_oe & rp->m_sio, v = rp->gpio_out & o;
	for (int k = 0; k < 3; k++) {
		if (!rp->m_pio[k]) continue;
		pio_t *p = &rp->pio[k];
		uint64_t d = ((uint64_t) p->dirs << p->gpiobase) & rp->m_pio[k];
		o |= d; v |= ((uint64_t) p->pins << p->gpiobase) & d;
	}
	o |= rp->m_spi;
	*val = v; *oe = o;
}
/* (while the PIO runs its part of a slice only its own pin writes change the levels: pl_on, the levels kept until one -
 * pio_set_pins drops them) */
static int pl_on, pl_ok; static uint64_t pl_val; static rp2350_t *pl_rp;
static uint64_t pin_levels (rp2350_t *rp)
{
	if (pl_ok && pl_rp == rp) return pl_val;
	uint64_t v, o;
	chip_drive (rp, &v, &o);
	uint64_t lv = (v & o) | (pull_levels (rp) & ~o);
	lv = (lv & ~rp->ext_mask) | (rp->ext_val & rp->ext_mask);
	if (pl_on) { pl_val = lv; pl_ok = 1; pl_rp = rp; }
	return lv;
}
uint64_t rp_pins (rp2350_t *rp, uint64_t *oe) { uint64_t v, o; chip_drive (rp, &v, &o); if (oe) *oe = o; return v; }
void rp_set_ext (rp2350_t *rp, uint64_t mask, uint64_t val) { rp->ext_mask = mask; rp->ext_val = val & mask; }

/* ------------------------------------------------------------------ time */
static uint32_t mhz (rp2350_t *rp) { return rp->sys_hz / 1000000u ? rp->sys_hz / 1000000u : 1; }
static uint64_t us_now (rp2350_t *rp, int t) { return (rp->now - rp->timer_base[t]) / mhz (rp); }
static void recompute_sys_hz (rp2350_t *rp)
{
	uint32_t ctrl = greg_rd (rp, 0x4001003c), div = greg_rd (rp, 0x40010040) >> 16;
	uint64_t f;
	if (ctrl & 1) {
		uint32_t aux = (ctrl >> 5) & 7;
		uint32_t base = aux == 1 ? 0x40058000u : 0x40050000u;
		uint32_t fb = greg_rd (rp, base + 8) & 0xfff, prim = greg_rd (rp, base + 0xc);
		uint32_t p1 = (prim >> 16) & 7, p2 = (prim >> 12) & 7;
		if (aux <= 1 && fb && p1 && p2) f = 12000000ull * fb / (p1 * p2); else f = 12000000;
	} else f = 12000000;
	if (div > 1) f /= div;
	if (f < 1000000) f = 1000000;
	rp->sys_hz = (uint32_t) f;
}
uint32_t rp_sys_hz (rp2350_t *rp) { return rp->sys_hz; }

static void irq_line (rp2350_t *rp, int n, int level)
{
	for (int k = 0; k < 2; k++) {
		cm33_t *c = &rp->cpu[k];
		if (level) cm33_irq (c, n, 1);
		else c->nvic_pend[n >> 5] &= ~(1u << (n & 31));
	}
}
static void timers_update (rp2350_t *rp)
{
	if (!(rp->armed[0] | rp->armed[1] | rp->tinte[0] | rp->tinte[1])) return;
	if (rp->now < rp->timer_next) return;                                   /* (nothing due before then) */
	rp->timer_next = ~0ull;
	for (int t = 0; t < 2; t++) {
		uint32_t lo = (uint32_t) us_now (rp, t);
		for (int a = 0; a < 4; a++)
			if ((rp->armed[t] >> a) & 1) {
				int32_t left = (int32_t) (rp->alarm[t][a] - lo);
				if (left <= 0) { rp->armed[t] &= ~(1u << a); rp->tintr[t] |= 1u << a; }
				else { uint64_t due = rp->now + (uint64_t) left * mhz (rp); if (due < rp->timer_next) rp->timer_next = due; }
			}
		uint32_t s = (rp->tintr[t] | rp->tintf[t]) & rp->tinte[t];
		for (int a = 0; a < 4; a++) irq_line (rp, t * 4 + a, (s >> a) & 1);
	}
}

/* ------------------------------------------------------------------ SIO */
static int fifo_push (rp2350_t *rp, int to, uint32_t v)
{
	if (rp->fifo_n[to] >= 8) { rp->fifo_err[to ^ 1] |= 4; return 0; }       /* WOF on the writer */
	rp->fifo[to][(rp->fifo_r[to] + rp->fifo_n[to]) & 7] = v; rp->fifo_n[to]++;
	return 1;
}
static uint32_t fifo_pop (rp2350_t *rp, int k)
{
	if (!rp->fifo_n[k]) { rp->fifo_err[k] |= 8; return 0; }                   /* ROE */
	uint32_t v = rp->fifo[k][rp->fifo_r[k]]; rp->fifo_r[k] = (rp->fifo_r[k] + 1) & 7; rp->fifo_n[k]--;
	return v;
}
static void core1_bootrom (rp2350_t *rp)
{
	/* the bootrom's launch protocol on core 1: each word is echoed; the sequence is 0, 0, 1, the vector
	 * table, the stack, the entry - a mismatch starts again (a 0 counts as the first word) */
	while (rp->core1_parked && rp->fifo_n[1]) {
		uint32_t w = fifo_pop (rp, 1);
		fifo_push (rp, 0, w);
		rp->cpu[0].event = 1;                                        /* (the bootrom's SEV) */
		int i = rp->launch_i;
		if (w == 0) i = (i == 1) ? 2 : 1;
		else if (i == 2 && w == 1) i = 3;
		else if (i >= 3) { rp->launch_seq[i] = w; i++; }
		else i = 0;
		rp->launch_i = i;
		if (rp->launch_i >= 6) {
			cm33_t *c = &rp->cpu[1];
			cm33_reset (c, rp->launch_seq[3]);
			c->msp = rp->launch_seq[4]; c->r[13] = c->msp;
			c->r[15] = rp->launch_seq[5] & ~1u;
			c->cycles = rp->now;
			rp->core1_parked = 0;
			if (rp->log) fprintf (stderr, "rp2350: core 1 launched at %08X\n", rp->launch_seq[5]);
		}
	}
}
static uint32_t sio_read (rp2350_t *rp, cm33_t *c, uint32_t off)
{
	switch (off) {
	case 0x000: return (uint32_t) c->core;
	case 0x004: return (uint32_t) pin_levels (rp);
	case 0x008: return (uint32_t) (pin_levels (rp) >> 32) & 0xffff;
	case 0x010: return (uint32_t) rp->gpio_out;
	case 0x014: return (uint32_t) (rp->gpio_out >> 32);
	case 0x030: return (uint32_t) rp->gpio_oe;
	case 0x034: return (uint32_t) (rp->gpio_oe >> 32);
	case 0x050: { int k = c->core;
	              return (uint32_t) ((rp->fifo_n[k] ? 1 : 0) | (rp->fifo_n[k ^ 1] < 8 ? 2 : 0) | rp->fifo_err[k]); }
	case 0x058: { uint32_t v = fifo_pop (rp, c->core); if (c->core == 0 && rp->core1_parked) core1_bootrom (rp); return v; }
	case 0x05c: return rp->spinlock;
	case 0x188: return rp->doorbell[c->core];
	}
	if (off >= 0x100 && off < 0x180) {                                        /* spinlock claim */
		uint32_t b = 1u << ((off - 0x100) >> 2);
		if (rp->spinlock & b) return 0;
		rp->spinlock |= b; return b;
	}
	return greg_rd (rp, 0xd0000000u + off);
}
static void gpio_change (rp2350_t *rp) { (void) rp; }
static void sio_write (rp2350_t *rp, cm33_t *c, uint32_t off, uint32_t v)
{
	uint64_t lo = v, hi = (uint64_t) (v & 0xffff) << 32;
	switch (off) {
	case 0x010: rp->gpio_out = (rp->gpio_out & ~0xffffffffull) | lo; break;
	case 0x014: rp->gpio_out = (rp->gpio_out & 0xffffffffull) | hi; break;
	case 0x018: rp->gpio_out |= lo; break;
	case 0x01c: rp->gpio_out |= hi; break;
	case 0x020: rp->gpio_out &= ~lo; break;
	case 0x024: rp->gpio_out &= ~hi; break;
	case 0x028: rp->gpio_out ^= lo; break;
	case 0x02c: rp->gpio_out ^= hi; break;
	case 0x030: rp->gpio_oe = (rp->gpio_oe & ~0xffffffffull) | lo; break;
	case 0x034: rp->gpio_oe = (rp->gpio_oe & 0xffffffffull) | hi; break;
	case 0x038: rp->gpio_oe |= lo; break;
	case 0x03c: rp->gpio_oe |= hi; break;
	case 0x040: rp->gpio_oe &= ~lo; break;
	case 0x044: rp->gpio_oe &= ~hi; break;
	case 0x048: rp->gpio_oe ^= lo; break;
	case 0x04c: rp->gpio_oe ^= hi; break;
	case 0x050: rp->fifo_err[c->core] &= ~(v & 0xc); return;
	case 0x054:
		fifo_push (rp, c->core ^ 1, v);
		if (c->core == 0 && rp->core1_parked) core1_bootrom (rp);
		{ cm33_t *o = &rp->cpu[c->core ^ 1]; o->event = 1; }
		return;
	case 0x180: rp->doorbell[c->core ^ 1] |= v; return;
	case 0x18c: rp->doorbell[c->core] &= ~v; return;
	default:
		if (off >= 0x100 && off < 0x180) { rp->spinlock &= ~(1u << ((off - 0x100) >> 2)); return; }
		greg_wr (rp, 0xd0000000u + off, v); return;
	}
	gpio_change (rp);
}

/* ------------------------------------------------------------------ PIO */
static int sm_tx_level (pio_sm_t *s) { return s->txn; }
static int sm_tx_cap (pio_sm_t *s) { return (s->shiftctrl >> 30) & 1 ? 8 : (s->shiftctrl >> 31) & 1 ? 0 : 4; }
static int sm_rx_cap (pio_sm_t *s) { return (s->shiftctrl >> 31) & 1 ? 8 : (s->shiftctrl >> 30) & 1 ? 0 : 4; }
static int sm_tx_push (pio_sm_t *s, uint32_t v) { if (s->txn >= sm_tx_cap (s)) return 0; s->tx[(s->txr + s->txn) & 7] = v; s->txn++; return 1; }
static int dma_kick = 1;                        /* (something a DMA request may wait on changed) */
static int sm_tx_pop (pio_sm_t *s, uint32_t *v) { if (!s->txn) return 0; dma_kick = 1; *v = s->tx[s->txr]; s->txr = (s->txr + 1) & 7; s->txn--; return 1; }
static int sm_rx_push (pio_sm_t *s, uint32_t v) { if (s->rxn >= sm_rx_cap (s)) return 0; dma_kick = 1; s->rx[(s->rxr + s->rxn) & 7] = v; s->rxn++; return 1; }
static int sm_rx_pop (pio_sm_t *s, uint32_t *v) { if (!s->rxn) return 0; *v = s->rx[s->rxr]; s->rxr = (s->rxr + 1) & 7; s->rxn--; return 1; }

static uint32_t pio_pins_in (rp2350_t *rp, pio_t *p)
{
	uint64_t lv = pin_levels (rp);
	return (uint32_t) (lv >> p->gpiobase) | (uint32_t) ((p->gpiobase ? 0 : 0));
}
static inline uint32_t rotl32 (uint32_t x, unsigned n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
static void pio_set_pins (pio_t *p, uint32_t base, uint32_t count, uint32_t v, int dirs)
{
	if (!count) return;
	pl_ok = 0;
	uint32_t m = count >= 32 ? 0xffffffffu : (1u << count) - 1;
	uint32_t mask = rotl32 (m, base), val = rotl32 (v & m, base);    /* (the pins wrap around at 32) */
	if (dirs) p->dirs = (p->dirs & ~mask) | (val & mask);
	else p->pins = (p->pins & ~mask) | (val & mask);
}
static uint32_t bitrev (uint32_t x) { uint32_t r = 0; for (int i = 0; i < 32; i++) if ((x >> i) & 1) r |= 1u << (31 - i); return r; }

static void pio_sm_restart (pio_sm_t *s)
{
	s->isr = s->osr = 0; s->isr_cnt = 0; s->osr_cnt = 32; s->delay = 0; s->stalled = 0; s->x = s->y = 0;
	s->irq_wait = 0; s->exec_pending = 0;
}

/* the PT8211 (GPIO 43-45 on PIO2): each word the audio state machine pulls is one stereo frame
 * (left in the high half, right in the low half) - taken here, with the time it was taken (the rate) */
static unsigned long aud_pops, aud_kept, aud_nonzero; static int aud_peak;
static void aud_tap (rp2350_t *rp, int pidx, pio_sm_t *s, uint32_t w)
{
	if (pidx != 2) return;
	aud_pops++; if (w) aud_nonzero++;
	{ int a = abs ((int16_t) (w >> 16)), b = abs ((int16_t) w); if (a > aud_peak) aud_peak = a; if (b > aud_peak) aud_peak = b; }
	uint32_t base = ((s->pinctrl & 31) + rp->pio[2].gpiobase), sbase = (((s->pinctrl >> 10) & 31) + rp->pio[2].gpiobase);
	if (!((base >= 40 && base <= 46) || (sbase >= 40 && sbase <= 46))) return;
	aud_kept++;
	if (rp->aud_n + 2 <= (int) (sizeof rp->aud / sizeof rp->aud[0])) {
		rp->aud[rp->aud_n++] = (int16_t) (w >> 16); rp->aud[rp->aud_n++] = (int16_t) w;
	}
	if (!rp->pt_frames++) rp->pt_last_frame = rp->now;
	else if (rp->pt_frames >= 4096) {
		uint64_t dt = rp->now - rp->pt_last_frame;
		if (dt) rp->aud_rate = (int) ((uint64_t) (rp->pt_frames - 1) * rp->sys_hz / dt);
		rp->pt_frames = 0;
	}
}
#define POP(s, w) (sm_tx_pop ((s), (w)) ? (aud_tap (rp, pidx, (s), *(w)), 1) : 0)

/* a state machine's settings as its instructions read them - only the CPU changes them (pio_write), never a run: worked
 * out once per run (pio_run_sm), not per instruction */
#if defined(_MSC_VER)
#define PIO_INLINE static __forceinline
#else
#define PIO_INLINE static inline __attribute__ ((always_inline))
#endif
typedef struct { uint32_t ex, pc, sh, dmask; int side_cnt, side_en, delay_bits; } pio_k_t;
/* (and its registers, in a local for the run - so they can live in the host's registers, not in memory) */
typedef struct { uint32_t pc, x, y, isr, osr; int isr_cnt, osr_cnt, delay; } pio_r_t;
#define PIO_R_IN(R, s) ((R).pc = (s)->pc, (R).x = (s)->x, (R).y = (s)->y, (R).isr = (s)->isr, (R).osr = (s)->osr, (R).isr_cnt = (s)->isr_cnt, (R).osr_cnt = (s)->osr_cnt, (R).delay = (s)->delay)
#define PIO_R_OUT(R, s) ((s)->pc = (R).pc, (s)->x = (R).x, (s)->y = (R).y, (s)->isr = (R).isr, (s)->osr = (R).osr, (s)->isr_cnt = (R).isr_cnt, (s)->osr_cnt = (R).osr_cnt, (s)->delay = (R).delay)
static void pio_k (const pio_sm_t *s, pio_k_t *k)
{
	k->ex = s->execctrl; k->pc = s->pinctrl; k->sh = s->shiftctrl;
	k->side_cnt = (int) (k->pc >> 29); k->side_en = (k->ex >> 30) & 1; k->delay_bits = 5 - k->side_cnt;
	k->dmask = (1u << k->delay_bits) - 1;
}
/* one PIO cycle for one state machine; returns 1 when it is stalled on something outside */
PIO_INLINE int pio_exec_k (rp2350_t *rp, pio_t *p, int pidx, int si, uint16_t ins, int *jumped, const pio_k_t *k, pio_r_t *R)
{
	pio_sm_t *s = &p->sm[si];
	uint32_t ex = k->ex, pc = k->pc, sh = k->sh;
	int side_cnt = k->side_cnt, side_en = k->side_en;
	int delay_bits = k->delay_bits;
	unsigned field = (ins >> 8) & 31;
	/* side-set (applied whether or not the instruction stalls) */
	if (side_cnt) {
		int bits = side_cnt - side_en;
		int apply = !side_en || ((field >> 4) & 1);
		if (apply && bits > 0) {
			uint32_t v = (field >> delay_bits) & ((1u << bits) - 1);
			pio_set_pins (p, (pc >> 10) & 31, (uint32_t) bits, v, (ex >> 29) & 1);
		}
	}
	int delay = (int) (field & k->dmask);
	unsigned op = ins >> 13;
	*jumped = 0;
	switch (op) {
	case 0: {                                                                  /* JMP */
		unsigned cond = (ins >> 5) & 7, addr = ins & 31; int take;
		switch (cond) {
		case 0: take = 1; break;
		case 1: take = R->x == 0; break;
		case 2: take = R->x != 0; R->x--; break;
		case 3: take = R->y == 0; break;
		case 4: take = R->y != 0; R->y--; break;
		case 5: take = R->x != R->y; break;
		case 6: take = (pio_pins_in (rp, p) >> ((ex >> 24) & 31)) & 1; break;
		default: take = R->osr_cnt < (int) (((sh >> 25) & 31) ? ((sh >> 25) & 31) : 32); break;
		}
		if (take) { R->pc = addr; *jumped = 1; }
		break;
	}
	case 1: {                                                                  /* WAIT */
		unsigned pol = (ins >> 7) & 1, src = (ins >> 5) & 3, idx = ins & 31; int lv;
		if (src == 0) lv = (int) ((pin_levels (rp) >> ((idx + p->gpiobase) & 63)) & 1);
		else if (src == 1) lv = (int) ((pio_pins_in (rp, p) >> ((((pc >> 15) & 31) + idx) & 31)) & 1);
		else if (src == 2) {
			unsigned n = idx & 7;
			if ((idx & 0x18) == 0x10) n = (n & 4) | ((n + (unsigned) si) & 3);
			lv = (int) ((p->irq >> n) & 1);
			if (lv == (int) pol) { if (pol) p->irq &= ~(1u << n); return 0; }
			return 1;
		} else lv = (int) ((pio_pins_in (rp, p) >> ((((ex >> 24) & 31) + idx) & 31)) & 1);
		if (lv != (int) pol) {
			if (src == 0) { s->wgpio = (int) ((idx + p->gpiobase) & 63) + 1; s->wpol = (int) pol; }
			return 1;
		}
		break;
	}
	case 2: {                                                                  /* IN */
		unsigned src = (ins >> 5) & 7, n = ins & 31 ? ins & 31 : 32; uint32_t v;
		int thresh = (int) (((sh >> 20) & 31) ? ((sh >> 20) & 31) : 32);
		if (((sh >> 16) & 1) && R->isr_cnt >= thresh) {                       /* autopush first */
			if (!sm_rx_push (s, R->isr)) return 1;
			R->isr = 0; R->isr_cnt = 0;
		}
		switch (src) {
		case 0: { uint32_t w = pio_pins_in (rp, p), b = (pc >> 15) & 31; v = b ? (w >> b) | (w << (32 - b)) : w; break; }
		case 1: v = R->x; break;
		case 2: v = R->y; break;
		case 6: v = R->isr; break;
		case 7: v = R->osr; break;
		default: v = 0;
		}
		if (n < 32) v &= (1u << n) - 1;
		if ((sh >> 18) & 1) R->isr = n == 32 ? v : (R->isr >> n) | (v << (32 - n));   /* shift right */
		else R->isr = n == 32 ? v : (R->isr << n) | v;
		R->isr_cnt += (int) n; if (R->isr_cnt > 32) R->isr_cnt = 32;
		if (((sh >> 16) & 1) && R->isr_cnt >= thresh) {
			if (sm_rx_push (s, R->isr)) { R->isr = 0; R->isr_cnt = 0; }
		}
		break;
	}
	case 3: {                                                                  /* OUT */
		unsigned dst = (ins >> 5) & 7, n = ins & 31 ? ins & 31 : 32; uint32_t v;
		int thresh = (int) (((sh >> 25) & 31) ? ((sh >> 25) & 31) : 32);
		if (((sh >> 17) & 1) && R->osr_cnt >= thresh) {                       /* autopull */
			uint32_t w; if (!POP (s, &w)) return 1;
			R->osr = w; R->osr_cnt = 0;
		}
		if ((sh >> 19) & 1) { v = n == 32 ? R->osr : R->osr & ((1u << n) - 1); R->osr = n == 32 ? 0 : R->osr >> n; }
		else { v = n == 32 ? R->osr : R->osr >> (32 - n); R->osr = n == 32 ? 0 : R->osr << n; }
		R->osr_cnt += (int) n; if (R->osr_cnt > 32) R->osr_cnt = 32;
		switch (dst) {
		case 0: pio_set_pins (p, pc & 31, (pc >> 20) & 63, v, 0); break;
		case 1: R->x = v; break;
		case 2: R->y = v; break;
		case 4: pio_set_pins (p, pc & 31, (pc >> 20) & 63, v, 1); break;
		case 5: R->pc = v & 31; *jumped = 1; break;
		case 6: R->isr = v; R->isr_cnt = (int) n; break;
		case 7: s->exec_pending = 1; s->exec_instr = v & 0xffff; break;
		}
		if (((sh >> 17) & 1) && R->osr_cnt >= thresh) {
			uint32_t w; if (POP (s, &w)) { R->osr = w; R->osr_cnt = 0; }
		}
		break;
	}
	case 4: {
		if ((ins >> 4) & 1) {                                                  /* MOV to / from the RX FIFO (RP2350) */
			unsigned idx = (ins >> 3) & 1 ? (ins & 3) : (R->y & 3);
			if ((ins >> 7) & 1) R->osr = s->rx[idx & 3], R->osr_cnt = 0;
			else s->rx[idx & 3] = R->isr;
			break;
		}
		int ifx = (ins >> 6) & 1, blk = (ins >> 5) & 1;
		if (!((ins >> 7) & 1)) {                                               /* PUSH */
			int thresh = (int) (((sh >> 20) & 31) ? ((sh >> 20) & 31) : 32);
			if (ifx && R->isr_cnt < thresh) break;
			if (!sm_rx_push (s, R->isr)) { if (blk) return 1; }
			R->isr = 0; R->isr_cnt = 0;
		} else {                                                               /* PULL */
			int thresh = (int) (((sh >> 25) & 31) ? ((sh >> 25) & 31) : 32);
			if (ifx && R->osr_cnt < thresh) break;
			uint32_t w;
			if (POP (s, &w)) { R->osr = w; R->osr_cnt = 0; }
			else if (blk) { s->wtx = 1; return 1; }
			else { R->osr = R->x; R->osr_cnt = 0; }                         /* noblock: from X */
		}
		break;
	}
	case 5: {                                                                  /* MOV */
		unsigned dst = (ins >> 5) & 7, mop = (ins >> 3) & 3, src = ins & 7; uint32_t v;
		switch (src) {
		case 0: v = pio_pins_in (rp, p) >> ((pc >> 15) & 31); break;
		case 1: v = R->x; break;
		case 2: v = R->y; break;
		case 5: {
			unsigned sel = (ex >> 5) & 3, nn = ex & 31;
			if (sel == 0) v = sm_tx_level (s) < (int) nn ? 0xffffffffu : 0;
			else if (sel == 1) v = s->rxn < (int) nn ? 0xffffffffu : 0;
			else v = ((p->irq >> (nn & 7)) & 1) ? 0xffffffffu : 0;
			break;
		}
		case 6: v = R->isr; break;
		case 7: v = R->osr; break;
		default: v = 0;
		}
		if (mop == 1) v = ~v; else if (mop == 2) v = bitrev (v);
		switch (dst) {
		case 0: pio_set_pins (p, pc & 31, (pc >> 20) & 63, v, 0); break;
		case 1: R->x = v; break;
		case 2: R->y = v; break;
		case 3: pio_set_pins (p, pc & 31, (pc >> 20) & 63, v, 1); break;
		case 4: s->exec_pending = 1; s->exec_instr = v & 0xffff; break;
		case 5: R->pc = v & 31; *jumped = 1; break;
		case 6: R->isr = v; R->isr_cnt = 0; break;
		case 7: R->osr = v; R->osr_cnt = 0; break;
		}
		break;
	}
	case 6: {                                                                  /* IRQ */
		int clr = (ins >> 6) & 1, wait = (ins >> 5) & 1; unsigned mode = (ins >> 3) & 3, n = ins & 7;
		pio_t *tp = p;
		if (mode == 2) n = (n & 4) | ((n + (unsigned) si) & 3);
		else if (mode == 1) tp = &rp->pio[(pidx + 2) % 3];
		else if (mode == 3) tp = &rp->pio[(pidx + 1) % 3];
		if (s->irq_wait) {
			if ((tp->irq >> n) & 1) return 1;
			s->irq_wait = 0; break;
		}
		if (clr) tp->irq &= ~(1u << n);
		else {
			tp->irq |= 1u << n;
			if (wait) { s->irq_wait = 1; return 1; }
		}
		break;
	}
	default: {                                                                 /* SET */
		unsigned dst = (ins >> 5) & 7, v = ins & 31;
		switch (dst) {
		case 0: pio_set_pins (p, (pc >> 5) & 31, (pc >> 26) & 7, v, 0); break;
		case 1: R->x = v; break;
		case 2: R->y = v; break;
		case 4: pio_set_pins (p, (pc >> 5) & 31, (pc >> 26) & 7, v, 1); break;
		}
		break;
	}
	}
	R->delay = delay;
	return 0;
}
static int pio_exec (rp2350_t *rp, pio_t *p, int pidx, int si, uint16_t ins, int *jumped)
{
	pio_k_t k; pio_k (&p->sm[si], &k);
	pio_r_t R; PIO_R_IN (R, &p->sm[si]);
	int st = pio_exec_k (rp, p, pidx, si, ins, jumped, &k, &R);
	PIO_R_OUT (R, &p->sm[si]);
	return st;
}

/* run state machine si of block pidx for `cyc` system cycles */
static unsigned long long pio_count[3];
static void pio_run_sm (rp2350_t *rp, int pidx, int si, uint32_t cyc)
{
	pio_t *p = &rp->pio[pidx];
	pio_sm_t *s = &p->sm[si];
	uint32_t ip = s->clkdiv >> 16, fp = (s->clkdiv >> 8) & 0xff;      /* (INT.FRAC; INT 0 = 65536) */
	uint32_t div = (ip ? ip : 65536u) * 256u + fp;
	uint32_t steps;
	if (div == 256 && s->frac < 256) steps = cyc;             /* (a divider of 1: every cycle a step, the remainder stays - the usual case) */
	else {
		uint64_t acc = (uint64_t) s->frac + (uint64_t) cyc * 256;
		steps = (uint32_t) (acc / div);
		s->frac = (uint32_t) (acc % div);
	}
	if (s->stalled && !s->exec_pending) {                       /* still waiting on the same thing: no change */
		if (s->wgpio) { if ((int) ((pin_levels (rp) >> (s->wgpio - 1)) & 1) != s->wpol) { s->frac = 0; return; } }
		else if (s->wtx) { if (!s->txn) { s->frac = 0; return; } }
	}
	s->wgpio = 0; s->wtx = 0;
	pio_k_t k; pio_k (s, &k);
	pio_r_t R; PIO_R_IN (R, s);
	while (steps) {
		if (R.delay) { uint32_t d = (uint32_t) R.delay < steps ? (uint32_t) R.delay : steps; R.delay -= (int) d; steps -= d; continue; }
		steps--;
		uint16_t ins; int from_exec = 0;
		if (s->exec_pending) { ins = (uint16_t) s->exec_instr; s->exec_pending = 0; from_exec = 1; }
		else ins = p->instr[R.pc & 31];
		int jumped;
		pio_count[pidx]++;
		int st = pio_exec_k (rp, p, pidx, si, ins, &jumped, &k, &R);
		if (st) {
			if (from_exec) { s->exec_pending = 1; s->exec_instr = ins; }
			s->stalled = 1;
			/* stalled on the outside world: nothing changes until the next slice */
			s->frac = 0; PIO_R_OUT (R, s); return;
		}
		s->stalled = 0;                               /* (wgpio / wtx: set only by a stall, which ends the run) */
		if (!jumped && !from_exec) {
			uint32_t wt = (k.ex >> 12) & 31, wb = (k.ex >> 7) & 31;
			if ((R.pc & 31) == wt) R.pc = wb; else R.pc = (R.pc + 1) & 31;
		} else if (from_exec && !jumped) { /* (an EXEC'd instruction does not advance the PC) */ }
		if (s->exec_pending) continue;
	}
	PIO_R_OUT (R, s);
}

static void pio_irq_update (rp2350_t *rp)
{
	if (!(rp->pio[0].inte[0] | rp->pio[0].inte[1] | rp->pio[1].inte[0] | rp->pio[1].inte[1] | rp->pio[2].inte[0] | rp->pio[2].inte[1])) return;
	for (int k = 0; k < 3; k++) {
		pio_t *p = &rp->pio[k];
		uint32_t intr = 0;
		for (int i = 0; i < 4; i++) {
			if (p->sm[i].rxn) intr |= 1u << i;
			if (p->sm[i].txn < sm_tx_cap (&p->sm[i])) intr |= 1u << (4 + i);
		}
		intr |= (p->irq & 0xff) << 8;
		for (int l = 0; l < 2; l++) irq_line (rp, 15 + 2 * k + l, ((intr | p->intf[l]) & p->inte[l]) != 0);
	}
}

static uint32_t pio_read (rp2350_t *rp, int k, uint32_t off)
{
	pio_t *p = &rp->pio[k];
	if (off == 0x000) return p->ctrl;
	if (off == 0x004) {
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) {
			pio_sm_t *s = &p->sm[i];
			if (s->rxn >= sm_rx_cap (s)) v |= 1u << i;
			if (s->rxn == 0) v |= 1u << (8 + i);
			if (s->txn >= sm_tx_cap (s)) v |= 1u << (16 + i);
			if (s->txn == 0) v |= 1u << (24 + i);
		}
		return v;
	}
	if (off == 0x008) return p->fdebug;
	if (off == 0x00c) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t) (p->sm[i].txn | (p->sm[i].rxn << 4)) << (8 * i); return v; }
	if (off >= 0x020 && off < 0x030) { uint32_t v = 0; pio_sm_t *s = &p->sm[(off - 0x20) >> 2]; if (!sm_rx_pop (s, &v)) p->fdebug |= 1u << (8 + ((off - 0x20) >> 2)); return v; }
	if (off == 0x030) return p->irq;
	if (off == 0x038) return p->input_sync_bypass;
	if (off == 0x03c) return p->pins;
	if (off == 0x040) return p->dirs;
	if (off == 0x044) return 0x00200804u | (2u << 28);                       /* CFGINFO: 4 SMs, 32 instr, FIFO 4 */
	if (off >= 0x048 && off < 0x0c8) return p->instr[(off - 0x48) >> 2];
	if (off >= 0x0c8 && off < 0x128) {
		pio_sm_t *s = &p->sm[(off - 0xc8) / 0x18];
		switch ((off - 0xc8) % 0x18) {
		case 0x00: return s->clkdiv;
		case 0x04: return s->execctrl | (s->stalled ? 0x80000000u : 0);
		case 0x08: return s->shiftctrl;
		case 0x0c: return s->pc;
		case 0x10: return p->instr[s->pc & 31];
		case 0x14: return s->pinctrl;
		}
	}
	if (off >= 0x128 && off < 0x168) { pio_sm_t *s = &p->sm[(off - 0x128) >> 4]; return s->rx[((off - 0x128) >> 2) & 3]; }
	if (off == 0x168) return p->gpiobase;
	if (off == 0x16c) {
		uint32_t intr = 0;
		for (int i = 0; i < 4; i++) { if (p->sm[i].rxn) intr |= 1u << i; if (p->sm[i].txn < sm_tx_cap (&p->sm[i])) intr |= 1u << (4 + i); }
		return intr | ((p->irq & 0xff) << 8);
	}
	if (off == 0x170) return p->inte[0];
	if (off == 0x174) return p->intf[0];
	if (off == 0x17c) return p->inte[1];
	if (off == 0x180) return p->intf[1];
	return 0;
}
static void pio_write (rp2350_t *rp, int k, uint32_t off, uint32_t v, int alias)
{
	pio_t *p = &rp->pio[k];
	if (alias && !(off >= 0x010 && off < 0x020)) v = apply_alias (pio_read (rp, k, off), v, alias);
	if (off == 0x000) {
		for (int i = 0; i < 4; i++) {
			if ((v >> (4 + i)) & 1) pio_sm_restart (&p->sm[i]);
			if ((v >> (8 + i)) & 1) p->sm[i].frac = 0;
		}
		p->ctrl = v & 0xf;
		return;
	}
	if (off == 0x008) { p->fdebug &= ~v; return; }
	if (off >= 0x010 && off < 0x020) {
		pio_sm_t *s = &p->sm[(off - 0x10) >> 2];
		if (!sm_tx_push (s, v)) p->fdebug |= 1u << (16 + ((off - 0x10) >> 2));
		return;
	}
	if (off == 0x030) { p->irq &= ~v; return; }
	if (off == 0x034) { p->irq |= v & 0xff; return; }
	if (off == 0x038) { p->input_sync_bypass = v; return; }
	if (off >= 0x048 && off < 0x0c8) { p->instr[(off - 0x48) >> 2] = (uint16_t) v; return; }
	if (off >= 0x0c8 && off < 0x128) {
		pio_sm_t *s = &p->sm[(off - 0xc8) / 0x18];
		switch ((off - 0xc8) % 0x18) {
		case 0x00: s->clkdiv = v; return;
		case 0x04: s->execctrl = v & 0x7fffffffu; return;
		case 0x08:
			if ((v ^ s->shiftctrl) & 0xc0000000u) { s->txn = s->rxn = 0; s->txr = s->rxr = 0; }
			s->shiftctrl = v; return;
		case 0x10: { s->exec_pending = 0; int j; if (pio_exec (rp, p, k, (int) ((off - 0xc8) / 0x18), (uint16_t) v, &j)) { s->exec_pending = 1; s->exec_instr = v & 0xffff; } s->delay = 0; return; }
		case 0x14: s->pinctrl = v; return;
		}
		return;
	}
	if (off >= 0x128 && off < 0x168) { pio_sm_t *s = &p->sm[(off - 0x128) >> 4]; s->rx[((off - 0x128) >> 2) & 3] = v; return; }
	if (off == 0x168) { p->gpiobase = v & 16; return; }
	if (off == 0x170) { p->inte[0] = v; return; }
	if (off == 0x174) { p->intf[0] = v; return; }
	if (off == 0x17c) { p->inte[1] = v; return; }
	if (off == 0x180) { p->intf[1] = v; return; }
}

/* ------------------------------------------------------------------ DMA */
static int dreq_ok (rp2350_t *rp, unsigned treq)
{
	if (treq == 0x3f) return 1;
	if (treq < 24) {
		pio_t *p = &rp->pio[treq / 8];
		pio_sm_t *s = &p->sm[treq & 3];
		if ((treq & 4) == 0) return s->txn < sm_tx_cap (s);
		return s->rxn > 0;
	}
	if (treq == 24) return 1;                                                 /* SPI0 TX */
	if (treq == 25) return rp->spi_rxn > 0;                                   /* SPI0 RX */
	if (treq >= 59 && treq <= 62) return 1;                                   /* DMA timers: (paced loosely) */
	return 1;
}
static void dma_start (rp2350_t *rp, int ch);
static uint32_t bus_read (rp2350_t *rp, cm33_t *c, uint32_t a, int n);
static void bus_write (rp2350_t *rp, cm33_t *c, uint32_t a, uint32_t v, int n);
static void dma_finish (rp2350_t *rp, int ch)
{
	dma_ch_t *d = &rp->dma[ch];
	d->busy = 0; rp->dma_busy &= ~(1u << ch); rp->dma_other &= ~(1u << ch);
	if (!((d->ctrl >> 23) & 1)) rp->dma_intr |= 1u << ch;
	unsigned chain = (d->ctrl >> 13) & 15;
	if (chain != (unsigned) ch) dma_start (rp, (int) chain);
}
static void dma_start (rp2350_t *rp, int ch)
{
	dma_ch_t *d = &rp->dma[ch];
	if (!(d->ctrl & 1)) return;
	d->count = d->count_reload;
	if ((d->count & 0x0fffffffu) == 0 && (d->count >> 28) != 0xf) { if (!((d->ctrl >> 23) & 1)) rp->dma_intr |= 1u << ch; return; }
	d->busy = 1; rp->dma_busy |= 1u << ch; dma_kick = 1;
	if (((d->ctrl >> 17) & 0x3f) >= 24) rp->dma_other |= 1u << ch; else rp->dma_other &= ~(1u << ch);
}
/* run the busy channels as far as their DREQs allow (at most `budget` transfers in all) */
static void dma_service (rp2350_t *rp, int budget)
{
	uint32_t busy = rp->dma_busy;
	if (!busy && !rp->dma_irq_on) return;
	if (!dma_kick && !(busy & rp->dma_other)) return;           /* (nothing new: unless a channel is not paced by a FIFO) */
	dma_kick = 0;
	int progress = 1;
	while (progress && budget > 0) {
		progress = 0;
		for (int ch = 0; ch < 16 && budget > 0; ch++) {
			if (!((busy >> ch) & 1)) continue;
			dma_ch_t *d = &rp->dma[ch];
			if (!d->busy) continue;
			unsigned treq = (d->ctrl >> 17) & 0x3f;
			int moved = 0;
			while (budget > 0 && d->busy && dreq_ok (rp, treq)) {
				int sz = 1 << ((d->ctrl >> 2) & 3);
				uint32_t v = bus_read (rp, NULL, d->read, sz);
				if ((d->ctrl >> 24) & 1) {
					if (sz == 4) v = (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
					else if (sz == 2) v = ((v >> 8) & 0xff) | ((v & 0xff) << 8);
				}
				bus_write (rp, NULL, d->write, v, sz);
				unsigned ring = (d->ctrl >> 8) & 15;
				int rsel = (d->ctrl >> 12) & 1;
				uint32_t nr = d->read, nw = d->write;
				if ((d->ctrl >> 4) & 1) nr = (d->ctrl >> 5) & 1 ? d->read - (uint32_t) sz : d->read + (uint32_t) sz;
				if ((d->ctrl >> 6) & 1) nw = (d->ctrl >> 7) & 1 ? d->write - (uint32_t) sz : d->write + (uint32_t) sz;
				if (ring) {
					uint32_t m = (1u << ring) - 1;
					if (rsel) nw = (d->write & ~m) | (nw & m); else nr = (d->read & ~m) | (nr & m);
				}
				d->read = nr; d->write = nw;
				budget--; moved = 1;
				uint32_t mode = d->count >> 28, cnt = d->count & 0x0fffffffu;
				if (mode == 0xf) continue;                                    /* endless */
				cnt--;
				d->count = (d->count & 0xf0000000u) | cnt;
				if (cnt == 0) {
					if (mode == 1) { d->count = d->count_reload; if (!((d->ctrl >> 23) & 1)) rp->dma_intr |= 1u << ch; }
					else dma_finish (rp, ch);
				}
				if (treq != 0x3f && treq < 24 && (treq & 4) == 0) break;           /* (one per DREQ look) */
			}
			if (moved) progress = 1;
		}
	}
	rp->dma_irq_on = (rp->dma_inte[0] | rp->dma_inte[1] | rp->dma_inte[2] | rp->dma_inte[3]) != 0;
	if (rp->dma_irq_on) for (int l = 0; l < 4; l++) irq_line (rp, 10 + l, ((rp->dma_intr | rp->dma_intf[l]) & rp->dma_inte[l]) != 0);
}
static uint32_t dma_read (rp2350_t *rp, uint32_t off)
{
	if (off < 0x400) {
		dma_ch_t *d = &rp->dma[off >> 6];
		unsigned r = off & 0x3f;
		uint32_t ctrl = d->ctrl | (d->busy ? (1u << 26) : 0);
		switch (r) {
		case 0x00: case 0x14: case 0x28: case 0x3c: return d->read;
		case 0x04: case 0x18: case 0x34: return d->write;
		case 0x2c: return d->write;
		case 0x08: case 0x1c: case 0x24: case 0x38: return d->busy ? d->count : d->count_reload;
		default: return ctrl;
		}
	}
	if (off >= 0x400 && off < 0x440) {
		int l = (int) ((off - 0x400) >> 4);
		switch (off & 0xf) {
		case 0x0: return rp->dma_intr;
		case 0x4: return rp->dma_inte[l];
		case 0x8: return rp->dma_intf[l];
		default: return (rp->dma_intr | rp->dma_intf[l]) & rp->dma_inte[l];
		}
	}
	if (off >= 0x440 && off < 0x450) return rp->dma_timer[(off - 0x440) >> 2];
	if (off == 0x468) return 16;
	if (off >= 0x800 && off < 0xc00) { dma_ch_t *d = &rp->dma[(off - 0x800) >> 6]; return (off & 4) ? d->count_reload : 0; }
	return greg_rd (rp, 0x50000000u + off);
}
static void dma_write (rp2350_t *rp, uint32_t off, uint32_t v, int alias)
{
	if (alias) v = apply_alias (dma_read (rp, off), v, alias);
	if (off < 0x400) {
		int ch = (int) (off >> 6);
		dma_ch_t *d = &rp->dma[ch];
		unsigned r = off & 0x3f; int trig = 0;
		switch (r) {
		case 0x00: case 0x14: case 0x28: d->read = v; break;
		case 0x3c: d->read = v; trig = 1; break;
		case 0x04: case 0x18: case 0x34: d->write = v; break;
		case 0x2c: d->write = v; trig = 1; break;
		case 0x08: case 0x24: case 0x38: d->count_reload = v; break;
		case 0x1c: d->count_reload = v; trig = 1; break;
		case 0x0c: d->ctrl = v & 0x03ffffffu; trig = 1; break;
		case 0x10: case 0x20: case 0x30: d->ctrl = v & 0x03ffffffu; break;
		}
		if (trig && (d->count_reload || r != 0x1c)) dma_start (rp, ch);
		else if (trig) { /* (a zero count on the trigger: a null trigger) */ }
		return;
	}
	if (off >= 0x400 && off < 0x440) {
		int l = (int) ((off - 0x400) >> 4);
		switch (off & 0xf) {
		case 0x0: rp->dma_intr &= ~v; break;
		case 0x4: rp->dma_inte[l] = v; rp->dma_irq_on = 1; break;
		case 0x8: rp->dma_intf[l] = v; break;
		default: rp->dma_intr &= ~v; break;
		}
		return;
	}
	if (off >= 0x440 && off < 0x450) { rp->dma_timer[(off - 0x440) >> 2] = v; return; }
	if (off == 0x450) { for (int ch = 0; ch < 16; ch++) if ((v >> ch) & 1) dma_start (rp, ch); return; }
	if (off == 0x464) { for (int ch = 0; ch < 16; ch++) if ((v >> ch) & 1) { rp->dma[ch].busy = 0; rp->dma_busy &= ~(1u << ch); rp->dma_other &= ~(1u << ch); } return; }
	greg_wr (rp, 0x50000000u + off, v);
}

/* ------------------------------------------------------------------ the PT8211 on PIO2 (pins 43 DIN, 44 WS, 45 BCK) */
static void pt8211_watch (rp2350_t *rp)
{
	uint64_t v, o; chip_drive (rp, &v, &o);
	int bck = (int) ((v >> 45) & 1), ws = (int) ((v >> 44) & 1), din = (int) ((v >> 43) & 1);
	if (bck && !rp->pt_bck) {                                                 /* rising: sample */
		if (ws != rp->pt_ws) {
			/* the word select changed: the 16 bits clocked before belong to the other channel */
			int16_t s = (int16_t) (rp->pt_shift & 0xffff);
			if (rp->pt_ws == 0) rp->pt_l = s;
			else if (rp->aud_n + 2 <= (int) (sizeof rp->aud / sizeof rp->aud[0])) {
				rp->aud[rp->aud_n++] = rp->pt_l; rp->aud[rp->aud_n++] = s;
				rp->pt_frames++;
			}
			rp->pt_ws = ws;
		}
		rp->pt_shift = (rp->pt_shift << 1) | (uint32_t) din;
	}
	rp->pt_bck = bck;
}
int rp_audio_take (rp2350_t *rp, int16_t *buf, int max_frames, int *rate)
{
	int n = rp->aud_n / 2; if (n > max_frames) n = max_frames;
	memcpy (buf, rp->aud, (size_t) n * 4);
	memmove (rp->aud, rp->aud + n * 2, (size_t) (rp->aud_n - n * 2) * 2);
	rp->aud_n -= n * 2;
	if (rate) *rate = rp->aud_rate ? rp->aud_rate : 32000;
	return n;
}

/* ------------------------------------------------------------------ QMI direct mode: the PSRAM's ID */
static void qmi_tx (rp2350_t *rp, uint32_t v)
{
	uint8_t b = (uint8_t) v;
	if (!(v & (1u << 20))) {                                                  /* (NOPUSH clear: a reply byte) */
		uint8_t r = 0xff;
		if (rp->qmi_cmdn >= 1 && rp->qmi_cmd[0] == 0x9f && rp->qmi_cmdn >= 4) {
			static const uint8_t id[] = { 0x0d, 0x5d, 0x52, 0xd2, 0x00, 0x00, 0x00, 0x00 };   /* APS6404L: MF, KGD */
			int k = rp->qmi_cmdn - 4; r = id[k < 8 ? k : 7];
		}
		if (rp->qmi_rxn < 8) { rp->qmi_rx[(rp->qmi_rxr + rp->qmi_rxn) & 7] = r; rp->qmi_rxn++; }
	}
	if (rp->qmi_cmdn < 8) rp->qmi_cmd[rp->qmi_cmdn++] = b;
}

/* ------------------------------------------------------------------ SPI0 and the SD card (SPI mode) */
#ifdef _WIN32
#define sd_seek(f, b) _fseeki64 ((f), (long long) (b) * 512, SEEK_SET)
#else
#define sd_seek(f, b) fseeko ((f), (off_t) (b) * 512, SEEK_SET)
#endif
static void sd_block (rp2350_t *rp, uint8_t *R, int *pn, uint32_t blkno)
{
	int n = *pn;
	R[n++] = 0xff; R[n++] = 0xfe;                                             /* (a gap, then the data token) */
	uint8_t *blk = R + n; memset (blk, 0, 512);
	if (blkno < rp->sd_blocks) { sd_seek (rp->sd, blkno); if (fread (blk, 1, 512, rp->sd) != 512) memset (blk, 0, 512); }
	n += 512;
	R[n++] = 0xff; R[n++] = 0xff;                                             /* (the CRC) */
	*pn = n; rp->sd_nread++;
}
static uint8_t sd_xfer (rp2350_t *rp, uint8_t out)
{
	int cs = (int) ((pin_levels (rp) >> 39) & 1);
	if (cs || !rp->sd) { rp->sd_cmdn = 0; return 0xff; }
	if (rp->sd_writing) {                                                     /* a block (or a multi-block run) being written */
		if (rp->sd_respr < rp->sd_respn) return rp->sd_resp[rp->sd_respr++];
		if (rp->sd_writing == 1) {
			if (out == 0xfe || out == 0xfc) rp->sd_writing = 2, rp->sd_wn = 0;
			else if (out == 0xfd && rp->sd_wmulti) {                          /* stop tran: busy for a byte */
				rp->sd_writing = 0; rp->sd_wmulti = 0;
				rp->sd_respn = 2; rp->sd_respr = 0; rp->sd_resp[0] = 0x00; rp->sd_resp[1] = 0xff;
			}
			return 0xff;
		}
		rp->sd_wbuf[rp->sd_wn++] = out;
		if (rp->sd_wn == 514) {
			if (rp->sd_waddr < rp->sd_blocks) { sd_seek (rp->sd, rp->sd_waddr); fwrite (rp->sd_wbuf, 1, 512, rp->sd); fflush (rp->sd); }
			rp->sd_nwrite++;
			rp->sd_writing = rp->sd_wmulti ? 1 : 0; rp->sd_waddr++;
			rp->sd_respn = 2; rp->sd_respr = 0; rp->sd_resp[0] = 0x05; rp->sd_resp[1] = 0xff;
		}
		return 0xff;
	}
	/* a command coming in; during a multi-block read the card listens while it sends (for CMD12) */
	int incmd = rp->sd_cmdn > 0 || (out & 0xc0) == 0x40;
	if (rp->sd_respr < rp->sd_respn && !(rp->sd_rmulti && incmd)) return rp->sd_resp[rp->sd_respr++];
	uint8_t reply = 0xff;
	if (rp->sd_respr < rp->sd_respn) reply = rp->sd_resp[rp->sd_respr++];
	if (!incmd) {
		if (rp->sd_rmulti) {                                                  /* the next block of the run */
			int n = 0; sd_block (rp, rp->sd_resp, &n, rp->sd_raddr++);
			rp->sd_respn = n; rp->sd_respr = 0;
		}
		return reply;
	}
	rp->sd_cmd[rp->sd_cmdn++] = out;
	if (rp->sd_cmdn < 6) return reply;
	rp->sd_cmdn = 0;
	unsigned cmd = rp->sd_cmd[0] & 63;
	uint32_t arg = ((uint32_t) rp->sd_cmd[1] << 24) | ((uint32_t) rp->sd_cmd[2] << 16) | ((uint32_t) rp->sd_cmd[3] << 8) | rp->sd_cmd[4];
	if (rp->log) { static int n; if (n++ < 3000) fprintf (stderr, "sd CMD%u %08X\n", cmd, arg); }
	uint8_t *R = rp->sd_resp; int n = 0;
	R[n++] = 0xff;                                                            /* (one byte of latency) */
	int app = rp->sd_app; rp->sd_app = 0;
	uint8_t r1 = rp->sd_idle ? 1 : 0;
	if (rp->sd_rmulti && cmd != 12) { rp->sd_rmulti = 0; }                   /* (any other command ends the run too) */
	if (app && cmd == 41) { rp->sd_idle = 0; R[n++] = 0; }
	else switch (cmd) {
	case 0: rp->sd_idle = 1; rp->sd_rmulti = 0; R[n++] = 1; break;
	case 8: R[n++] = r1; R[n++] = 0; R[n++] = 0; R[n++] = 1; R[n++] = (uint8_t) arg; break;
	case 55: rp->sd_app = 1; R[n++] = r1; break;
	case 58: R[n++] = r1; R[n++] = 0xc0; R[n++] = 0xff; R[n++] = 0x80; R[n++] = 0; break;   /* CCS: SDHC */
	case 13: R[n++] = r1; R[n++] = 0; break;                                  /* status (R2) */
	case 12: rp->sd_rmulti = 0; R[n++] = 0xff; R[n++] = r1; R[n++] = 0xff; break;   /* (a stuff byte, R1, ready) */
	case 16: case 59: case 23: R[n++] = r1; break;
	case 9: {                                                                 /* CSD v2: the size */
		R[n++] = r1; R[n++] = 0xfe;
		uint8_t csd[16] = { 0x40, 0x0e, 0x00, 0x32, 0x5b, 0x59, 0x00, 0, 0, 0, 0x7f, 0x80, 0x0a, 0x40, 0x00, 0x01 };
		uint32_t cs = rp->sd_blocks / 1024; if (cs) cs--;
		csd[7] = (uint8_t) ((cs >> 16) & 0x3f); csd[8] = (uint8_t) (cs >> 8); csd[9] = (uint8_t) cs;
		for (int i = 0; i < 16; i++) R[n++] = csd[i];
		R[n++] = 0xff; R[n++] = 0xff;
		break;
	}
	case 10: R[n++] = r1; R[n++] = 0xfe; for (int i = 0; i < 16; i++) R[n++] = (uint8_t) "VXEMU SD CARD   "[i]; R[n++] = 0xff; R[n++] = 0xff; break;
	case 17: R[n++] = 0; sd_block (rp, R, &n, arg); break;
	case 18: R[n++] = 0; sd_block (rp, R, &n, arg); rp->sd_rmulti = 1; rp->sd_raddr = arg + 1; break;
	case 24: R[n++] = 0; rp->sd_writing = 1; rp->sd_wmulti = 0; rp->sd_waddr = arg; break;
	case 25: R[n++] = 0; rp->sd_writing = 1; rp->sd_wmulti = 1; rp->sd_waddr = arg; break;
	default: R[n++] = r1 | 4; break;                                          /* illegal command */
	}
	rp->sd_respn = n; rp->sd_respr = 0;
	return reply;
}
void rp_set_sd (rp2350_t *rp, const char *path)
{
	if (rp->sd) fclose (rp->sd);
	rp->sd = NULL; rp->sd_blocks = 0;
	if (path && *path) {
		rp->sd = fopen (path, "r+b");
		if (rp->sd) {
#ifdef _WIN32
			_fseeki64 (rp->sd, 0, SEEK_END); rp->sd_blocks = (uint32_t) (_ftelli64 (rp->sd) / 512);
#else
			fseeko (rp->sd, 0, SEEK_END); rp->sd_blocks = (uint32_t) (ftello (rp->sd) / 512);
#endif
		}
	}
}

/* ------------------------------------------------------------------ the bootrom (HLE) */
#define ROM_LOOKUP   0x0100u
#define ROM_STUBS    0x0200u
static uint32_t rom_codes[64]; static int rom_ncodes;
static void rom_build (rp2350_t *rp)
{
	uint8_t *r = rp->rom;
	memset (r, 0, sizeof rp->rom);
	r[0x10] = 'M'; r[0x11] = 'u'; r[0x12] = 2; r[0x13] = 0;
	r[0x14] = 0; r[0x15] = 0;                                                 /* (no function table) */
	r[0x16] = 0; r[0x17] = 0;
	r[0x16] = (uint8_t) ((ROM_LOOKUP | 1) & 0xff); r[0x17] = (uint8_t) ((ROM_LOOKUP | 1) >> 8);
	/* lookup: BKPT #0xFF ; BX LR */
	r[ROM_LOOKUP] = 0xff; r[ROM_LOOKUP + 1] = 0xbe; r[ROM_LOOKUP + 2] = 0x70; r[ROM_LOOKUP + 3] = 0x47;
	/* the stubs: BKPT #k ; BX LR */
	for (int k = 0; k < 64; k++) {
		uint32_t a = ROM_STUBS + 4u * (uint32_t) k;
		r[a] = (uint8_t) k; r[a + 1] = 0xbe; r[a + 2] = 0x70; r[a + 3] = 0x47;
	}
	/* (the reset vector of a core that asks: a stub that waits for events forever) */
	r[0x300] = 0x20; r[0x301] = 0xbf; r[0x302] = 0xfd; r[0x303] = 0xe7;      /* wfe ; b . */
}
static int rom_hle (cm33_t *c, int imm)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	if (imm == 0xff) {                                                        /* rom_table_lookup (code, mask) */
		uint32_t code = c->r[0], mask = c->r[1];
		if (mask & 0x40) { c->r[0] = 0; return 1; }                        /* (data: none) */
		int k;
		for (k = 0; k < rom_ncodes; k++) if (rom_codes[k] == code) break;
		if (k == rom_ncodes && rom_ncodes < 64) rom_codes[rom_ncodes++] = code;
		c->r[0] = (ROM_STUBS + 4u * (uint32_t) k) | 1;
		if (rp->log) fprintf (stderr, "rp2350: bootrom lookup '%c%c'\n", (int) (code & 0xff), (int) ((code >> 8) & 0xff));
		return 1;
	}
	if (imm < rom_ncodes) {
		uint32_t code = rom_codes[imm];
		char a = (char) (code & 0xff), b = (char) ((code >> 8) & 0xff);
		if (rp->log) fprintf (stderr, "rp2350: bootrom call '%c%c' (%08X %08X)\n", a, b, c->r[0], c->r[1]);
		c->r[0] = 0;
		if (a == 'R' && b == 'B') { snprintf (rp->fault, sizeof rp->fault, "the game asked for a reboot"); }
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ the bus */
static int is_mmio (uint32_t a) { return a >= 0x40000000u; }
static void poll_note (rp2350_t *rp, cm33_t *c, uint32_t a, uint32_t v)
{
	if (!c) return;
	int k = c->core;
	if (rp->poll_pc[k] == c->last_pc && rp->poll_addr[k] == a && rp->poll_val[k] == v) {
		if (++rp->poll_hits[k] >= 2) { c->poll_skip = 1; rp->poll_hits[k] = 0; }
	} else { rp->poll_pc[k] = c->last_pc; rp->poll_addr[k] = a; rp->poll_val[k] = v; rp->poll_hits[k] = 0; }
}

static uint32_t clocks_selected (rp2350_t *rp, uint32_t off)
{
	uint32_t ctrl = greg_rd (rp, 0x40010000u + off - 8);
	if (off == 0x38) return 1u << (ctrl & 3);
	if (off == 0x44) return 1u << (ctrl & 1);
	return 1;
}

static uint32_t periph_read (rp2350_t *rp, cm33_t *c, uint32_t a)
{
	uint32_t base = a & ~0x3fffu, off = a & 0xfffu;
	(void) base;
	switch (a & 0xffff8000u) {
	case 0x40000000: if ((a & 0xfff) == 0) return 0x00004927u | (2u << 28); if ((a & 0xfff) == 4) return 1; break;   /* SYSINFO */
	case 0x40010000:                                                          /* CLOCKS */
		if (off >= 0x30 && off <= 0x74 && ((off - 0x30) % 12) == 8) return clocks_selected (rp, off);
		if (off == 0xa4) return 0x11;                                         /* FC0_STATUS: done, passed (the count at once) */
		if (off == 0xa8) {                                                     /* FC0_RESULT: the source's kHz << 5 */
			uint32_t src = greg_rd (rp, 0x400100a0u) & 0xff, khz;
			switch (src) {
			case 0x05: case 0x08: khz = 12000; break;                         /* the crystal, clk_ref */
			case 0x02: case 0x0b: case 0x0c: khz = 48000; break;              /* pll_usb, clk_usb, clk_adc */
			case 0x03: case 0x04: khz = 11000; break;                         /* the ring oscillator */
			case 0x0e: khz = 32; break;                                        /* the low-power oscillator */
			default: khz = rp->sys_hz / 1000; break;                           /* pll_sys, clk_sys, clk_peri, clk_hstx */
			}
			return khz << 5;
		}
		break;
	case 0x40020000: if (off == 0x8) return ~greg_rd (rp, 0x40020000u) & 0x1fffffff; break;   /* RESETS: done */
	case 0x40028000: if (off < 0x180 && (off & 4)) return rp->gpio_ctrl[off >> 3]; if (off < 0x180) return 0; break;
	case 0x40038000: if (off >= 4 && off < 4 + 4 * (NPINS + 2)) return rp->pads[(off - 4) >> 2]; break;
	case 0x40048000: if (off == 4) return 0x80001000u; break;                 /* XOSC STATUS: stable, enabled */
	case 0x40050000: case 0x40058000: if (off == 0) return greg_rd (rp, a) | 0x80000000u; break;   /* PLL locked */
	case 0x40070000: case 0x40078000:                                         /* UART */
		if (off == 0x18) return 0x90;                                       /* TXFE, RXFE */
		break;
	case 0x40080000:                                                          /* SPI0 */
		if (off == 0x8) { uint32_t v = 0; if (rp->spi_rxn) { v = rp->spi_rx[rp->spi_rxr]; rp->spi_rxr = (rp->spi_rxr + 1) & 7; rp->spi_rxn--; } return v; }
		if (off == 0xc) return 0x3u | (rp->spi_rxn ? 4u : 0u) | (rp->spi_rxn >= 8 ? 8u : 0u);
		if (off == 0x0) return rp->spi_cr0;
		if (off == 0x4) return rp->spi_cr1;
		if (off == 0x10) return rp->spi_cpsr;
		if (off >= 0xfe0) { static const uint8_t id[] = { 0x22, 0x10, 0x34, 0x00, 0x0d, 0xf0, 0x05, 0xb1 }; return id[(off - 0xfe0) >> 2]; }
		break;
	case 0x400b0000: case 0x400b8000: {                                       /* TIMER0 / 1 */
		int t = (a >> 15) & 1; uint64_t us = us_now (rp, t);
		switch (off) {
		case 0x08: case 0x24: return (uint32_t) (us >> 32);
		case 0x0c: case 0x28: return (uint32_t) us;
		case 0x10: case 0x14: case 0x18: case 0x1c: return rp->alarm[t][(off - 0x10) >> 2];
		case 0x20: return rp->armed[t];
		case 0x3c: return rp->tintr[t];
		case 0x40: return rp->tinte[t];
		case 0x44: return rp->tintf[t];
		case 0x48: return (rp->tintr[t] | rp->tintf[t]) & rp->tinte[t];
		}
		break;
	}
	case 0x400c8000: if (off == 0) return rp->xip_ctrl; break;
	case 0x400d0000:                                                          /* QMI */
		if (off == 0x0) {
			uint32_t v = rp->qmi_csr & ~((1u << 1) | (1u << 10) | (1u << 11) | (1u << 16) | (1u << 17) | (7u << 18));
			v |= 1u << 11;                                                  /* TX empty */
			if (!rp->qmi_rxn) v |= 1u << 16; else v |= (uint32_t) rp->qmi_rxn << 18;
			return v;
		}
		if (off == 0x8) { uint32_t v = 0; if (rp->qmi_rxn) { v = rp->qmi_rx[rp->qmi_rxr]; rp->qmi_rxr = (rp->qmi_rxr + 1) & 7; rp->qmi_rxn--; } return v; }
		break;
	case 0x400e8000: if (off == 0x18) return 0x80001000u; break;            /* ROSC STATUS */
	case 0x400e0000:                                                          /* BOOTRAM: the bootrom's locks */
		if (off == 0x808) return rp->bootlock;
		if (off >= 0x80c && off <= 0x828) {
			uint32_t b = 1u << ((off - 0x80c) >> 2);
			if (rp->bootlock & b) return 0;
			rp->bootlock |= b; return b;
		}
		break;
	case 0x400f0000:                                                          /* TRNG */
		if (off == 0x104 || off == 0x100) return 1;                         /* RNG_ISR / IMR: valid */
		if (off >= 0x114 && off < 0x12c) return (uint32_t) rand () ^ ((uint32_t) rand () << 15) ^ ((uint32_t) rand () << 30);
		break;
	case 0x40108000: { uint32_t v = greg_rd (rp, a); if ((off % 12) == 0) v |= (v & 1) << 1; return v; }   /* TICKS: RUNNING */
	case 0x40100000: if (off == 0x30) return 0x40; break;                     /* POWMAN: (reads) */
	}
	if ((a & 0xfff00000u) == 0x50000000u && a < 0x50001000u) return dma_read (rp, a & 0xfff);
	if (a >= 0x50200000u && a < 0x50500000u) return pio_read (rp, (int) ((a - 0x50200000u) >> 20), a & 0xfff);
	(void) c;
	return greg_rd (rp, a & ~0x3000u);
}
static void periph_write (rp2350_t *rp, cm33_t *c, uint32_t a, uint32_t v)
{
	int alias = (int) ((a >> 12) & 3);
	uint32_t ra = a & ~0x3000u, off = ra & 0xfffu;
	if ((a & 0xfff00000u) == 0x50000000u && a < 0x50004000u) { dma_write (rp, off, v, alias); return; }
	if (a >= 0x50200000u && a < 0x50500000u) { pio_write (rp, (int) ((a - 0x50200000u) >> 20), off, v, alias); return; }
	switch (ra & 0xffff8000u) {
	case 0x40028000:                                                          /* IO_BANK0 */
		if (off < 0x180 && (off & 4)) {
			int pin = (int) (off >> 3);
			uint32_t nv = apply_alias (rp->gpio_ctrl[pin], v, alias);
			rp->gpio_ctrl[pin] = nv; rp->funcsel[pin] = (uint8_t) (nv & 31); fmasks (rp);
			return;
		}
		break;
	case 0x40038000:                                                          /* PADS_BANK0 */
		if (off >= 4 && off < 4 + 4 * (NPINS + 2)) { int k = (int) ((off - 4) >> 2); rp->pads[k] = apply_alias (rp->pads[k], v, alias); fmasks (rp); return; }
		break;
	case 0x40070000: case 0x40078000:                                         /* UART: the text */
		if (off == 0) {
			char ch = (char) v;
			if (ch == '\n' || rp->uart_n >= 250) { rp->uart_line[rp->uart_n] = 0; fprintf (stderr, "uvm2: %s\n", rp->uart_line); rp->uart_n = 0; }
			else if (ch != '\r') rp->uart_line[rp->uart_n++] = ch;
			return;
		}
		break;
	case 0x40080000:                                                          /* SPI0 */
		if (off == 0x8) {
			uint8_t r = sd_xfer (rp, (uint8_t) v);
			if (rp->spi_rxn < 8) { rp->spi_rx[(rp->spi_rxr + rp->spi_rxn) & 7] = r; rp->spi_rxn++; } dma_kick = 1;
			return;
		}
		if (off == 0x0) { rp->spi_cr0 = apply_alias (rp->spi_cr0, v, alias); return; }
		if (off == 0x4) { rp->spi_cr1 = apply_alias (rp->spi_cr1, v, alias); return; }
		if (off == 0x10) { rp->spi_cpsr = v; return; }
		break;
	case 0x400b0000: case 0x400b8000: {                                       /* TIMER */
		int t = (ra >> 15) & 1;
		rp->timer_next = 0;
		switch (off) {
		case 0x00: case 0x04: return;
		case 0x10: case 0x14: case 0x18: case 0x1c: rp->alarm[t][(off - 0x10) >> 2] = v; rp->armed[t] |= 1u << ((off - 0x10) >> 2); timers_update (rp); return;
		case 0x20: rp->armed[t] &= ~v; return;
		case 0x3c: rp->tintr[t] &= ~v; timers_update (rp); return;
		case 0x40: rp->tinte[t] = apply_alias (rp->tinte[t], v, alias); timers_update (rp); return;
		case 0x44: rp->tintf[t] = apply_alias (rp->tintf[t], v, alias); timers_update (rp); return;
		}
		break;
	}
	case 0x400c8000: if (off == 0) { rp->xip_ctrl = apply_alias (rp->xip_ctrl, v, alias); return; } break;
	case 0x400d0000:                                                          /* QMI */
		if (off == 0x0) {
			uint32_t nv = apply_alias (rp->qmi_csr, v, alias);
			if (!(nv & 8) && (rp->qmi_csr & 8)) rp->qmi_cmdn = 0;           /* CS1 released */
			if ((nv & 8) && !(rp->qmi_csr & 8)) rp->qmi_cmdn = 0;
			rp->qmi_csr = nv; return;
		}
		if (off == 0x4) { qmi_tx (rp, v); return; }
		break;
	case 0x40020000: break;
	case 0x400e0000:
		if (off >= 0x80c && off <= 0x828) { rp->bootlock &= ~(1u << ((off - 0x80c) >> 2)); return; }
		if (off == 0x808) { rp->bootlock = v & 0xff; return; }
		break;
	}
	uint32_t old = 0;
	{
		uint32_t *p = greg (rp, ra, 1);
		if (p) { old = *p; *p = apply_alias (*p, v, alias); }
	}
	if ((ra & 0xffff8000u) == 0x40010000u || (ra & 0xffff0000u) == 0x40050000u) recompute_sys_hz (rp);
	if (ra == 0x40018004u) {                                                  /* PSM FRCE_OFF: core 1 held / released */
		uint32_t now_off = greg_rd (rp, ra);
		if ((now_off & (1u << 24)) && !(old & (1u << 24))) { rp->core1_parked = 1; rp->launch_i = 0; rp->cpu[1].halted = 0; }
		if (!(now_off & (1u << 24)) && (old & (1u << 24))) {
			/* out of reset: core 1's bootrom says so with a 0 in the FIFO, then waits for its launch */
			rp->core1_parked = 1; rp->launch_i = 0;
			rp->fifo_n[0] = 0; rp->fifo_r[0] = 0; rp->fifo_n[1] = 0; rp->fifo_r[1] = 0;
			fifo_push (rp, 0, 0);
			rp->cpu[0].event = 1;
		}
	}
	(void) c;
}

/* the USB controller (usbhost.c): its registers and its DPRAM, when a USB device is plugged in (VT_USBHID) */
extern void usb_init (void); extern int usb_on (void), usb_irq (void);
extern uint32_t usb_read (uint32_t a); extern void usb_write (uint32_t a, uint32_t v, int n); extern void usb_tick (uint64_t now, uint32_t sys_hz);
static int usb_level;
#define IS_USB(a) (((a) >= 0x50100000u && (a) < 0x50101000u) || ((a) >= 0x50110000u && (a) < 0x50114000u))
static void usb_irq_update (rp2350_t *rp)
{
	int l = usb_irq ();
	if (l || usb_level) irq_line (rp, 14, l);                                /* (USBCTRL_IRQ: level, as long as it is due) */
	usb_level = l;
}
static uint32_t bus_read (rp2350_t *rp, cm33_t *c, uint32_t a, int n)
{
	uint32_t v;
	if (IS_USB (a) && usb_on ()) {
		v = usb_read (a & ~3u);
		if (rp->log && c) { static unsigned nl; if (nl++ < 60) fprintf (stderr, "usb pc: core %d %08X rd %08X t %.3f s%c", c->core, c->last_pc, a, (double) rp->now / 150e6, 10); }
		if (n < 4) v = (v >> ((a & 3) * 8)) & (n == 1 ? 0xff : 0xffff);
		usb_irq_update (rp);
		return v;
	}
	if (a < 0x8000u) {
		const uint8_t *p = rp->rom + a;
		v = n == 4 ? (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24)) : n == 2 ? (uint32_t) (p[0] | (p[1] << 8)) : p[0];
		return v;
	}
	if (a - SRAM_BASE < SRAM_SIZE) {
		const uint8_t *p = rp->sram + (a - SRAM_BASE);
		uint32_t w = 0; memcpy (&w, p, (size_t) n); return w;
	}
	if (a >= 0x10000000u && a < 0x20000000u) {                                /* XIP: flash (empty) / PSRAM (M1) */
		uint32_t o = a & 0x00ffffffu;
		if ((a & 0x01000000u) && o < PSRAM_SIZE) { uint32_t w = 0; memcpy (&w, rp->psram + o, (size_t) n); return w; }
		return n == 4 ? 0xffffffffu : n == 2 ? 0xffff : 0xff;
	}
	if ((a >> 12) == 0xe0000u) return 1;                                     /* ITM stimulus ports: ready */
	if ((a >> 12) == 0xe0001u) {                                              /* DWT: the cycle counter */
		if ((a & 0xfff) == 4) return c ? (uint32_t) c->cycles : (uint32_t) rp->now;
		return greg_rd (rp, a);
	}
	if (a >= 0xd0000000u && a < 0xd0040000u) {
		v = sio_read (rp, c, a & 0x1ffu);
		if (n < 4) v = (v >> ((a & 3) * 8)) & (n == 1 ? 0xff : 0xffff);
		poll_note (rp, c, a, v);
		return v;
	}
	if (is_mmio (a)) {
		v = periph_read (rp, c, a & ~3u);
		if (n < 4) v = (v >> ((a & 3) * 8)) & (n == 1 ? 0xff : 0xffff);
		poll_note (rp, c, a, v);
		return v;
	}
	return 0;
}
static void bus_write (rp2350_t *rp, cm33_t *c, uint32_t a, uint32_t v, int n)
{
	if (IS_USB (a) && usb_on ()) {
		if (a < 0x50101000u) usb_write (a, v, n);
		else {
			if (n == 1) v = (v & 0xff) * 0x01010101u; else if (n == 2) v = (v & 0xffff) * 0x00010001u;
			usb_write (a & ~3u, v, 4);
		}
		usb_irq_update (rp);
		return;
	}
	if (a - SRAM_BASE < SRAM_SIZE) { memcpy (rp->sram + (a - SRAM_BASE), &v, (size_t) n); cm33_code_written (&rp->sys, a, (uint32_t) n); return; }
	if (a >= 0x10000000u && a < 0x20000000u) {
		uint32_t o = a & 0x00ffffffu;
		if ((a & 0x01000000u) && o < PSRAM_SIZE) memcpy (rp->psram + o, &v, (size_t) n);
		return;
	}
	if (a >= 0xd0000000u && a < 0xd0040000u) {
		if (c) sio_write (rp, c, a & 0x1ffu, v);
		return;
	}
	if ((a >> 12) == 0xe0000u) {                                              /* ITM: debug text */
		if (a < 0xe0000080u) {
			for (int i = 0; i < n; i++) {
				char ch = (char) (v >> (8 * i));
				if (ch == '\n' || rp->uart_n >= 250) { rp->uart_line[rp->uart_n] = 0; if (rp->log) fprintf (stderr, "uvm2 itm: %s\n", rp->uart_line); rp->uart_n = 0; }
				else if (ch != '\r' && ch) rp->uart_line[rp->uart_n++] = ch;
			}
		}
		return;
	}
	if (is_mmio (a)) {
		if (n == 1) v = (v & 0xff) * 0x01010101u; else if (n == 2) v = (v & 0xffff) * 0x00010001u;   /* (narrow writes replicate) */
		periph_write (rp, c, a & ~3u, v);
	}
}

/* ------------------------------------------------------------------ the DCP (double precision coprocessor)
 * The pico-sdk drives it only through its canned sequences (hardware_dcp/dcp_canned.inc.S): the operands
 * go in (WXUP / WYUP doubles, WXYU two floats, WXIC / WXUC an int, WXDC / WXFC for a conversion), the
 * software refines mantissas in between, and the final read names the operation (RDDA add, RDDS subtract,
 * RDDM multiply, RDDD divide, RDDQ square root, RDDG the operand; RDF* the same as floats; RDIC / RDUC a
 * conversion to int; RCMP a compare as NZCV; RXVD a classification). So the result is computed here with
 * IEEE arithmetic from the operands, and the refinement steps are accepted and ignored. */
static double dcp_x, dcp_y; static int dcp_trunc;
static double d_of (uint32_t lo, uint32_t hi) { uint64_t b = ((uint64_t) hi << 32) | lo; double d; memcpy (&d, &b, 8); return d; }
static double f_of (uint32_t v) { float f; memcpy (&f, &v, 4); return f; }
static uint32_t f_bits (double d) { float f = (float) d; uint32_t b; memcpy (&b, &f, 4); return b; }
static void d_bits (double d, uint32_t *lo, uint32_t *hi) { uint64_t b; memcpy (&b, &d, 8); *lo = (uint32_t) b; *hi = (uint32_t) (b >> 32); }
static uint32_t nzcv_cmp (double a, double b)
{
	if (a != a || b != b) return 0x3u << 28;
	if (a == b) return 0x6u << 28;
	return a < b ? 0x8u << 28 : 0x2u << 28;
}
static int dcp (rp2350_t *rp, int kind, int opc1, int crm, int opc2, uint32_t *rt, uint32_t *rt2)
{
	(void) rp;
	switch (kind) {
	case CM33_CP_MCRR:
		if (opc1 == 1 && crm == 0) dcp_x = d_of (*rt, *rt2);
		else if (opc1 == 1 && crm == 1) dcp_y = d_of (*rt, *rt2);
		else if (opc1 == 1 && crm == 2) { dcp_x = f_of (*rt); dcp_y = f_of (*rt2); }
		else if (opc1 == 6) { dcp_x = (double) *rt; dcp_y = 0; }
		else if (opc1 == 7) { dcp_x = (double) (int32_t) *rt; dcp_y = 0; }
		else if (opc1 == 8) { dcp_x = d_of (*rt, *rt2); dcp_y = 0; }
		else if (opc1 == 9) { dcp_x = f_of (*rt); dcp_y = 0; }
		return 0;
	case CM33_CP_CDP:
		if (opc1 == 8 && crm == 0 && opc2 == 2) dcp_trunc = 1;              /* NTDC */
		else if (opc1 == 8 && crm == 0 && opc2 == 3) dcp_trunc = 0;         /* NRDC */
		return 0;
	case CM33_CP_MRC: {
		double x = dcp_x, y = dcp_y, r;
		if (crm == 0 && opc2 == 1) { *rt = nzcv_cmp (x, y); return 0; }
		if (crm == 0 && opc2 == 0) {                                        /* classify: N neg, Z zero, C inf, V NaN */
			uint32_t f = 0;
			if (x != x) f |= 1u << 28; else { if (x == 0) f |= 1u << 30; if (x - x != x - x) f |= 1u << 29; }
			if (signbit (x)) f |= 1u << 31;
			*rt = f; return 0;
		}
		if (crm == 2) {
			switch (opc2) {
			case 0: r = x + y; break;
			case 1: r = x - y; break;
			case 2: r = x * y; break;
			case 3: r = x / y; break;
			case 4: r = sqrt (x); break;
			default: r = x; break;
			}
			*rt = f_bits (r); return 0;
		}
		if (crm == 3) {
			double v = dcp_trunc ? trunc (x) : nearbyint (x);
			if (opc2 == 0) *rt = v != v ? 0 : v >= 2147483647.0 ? 0x7fffffffu : v <= -2147483648.0 ? 0x80000000u : (uint32_t) (int32_t) v;
			else *rt = v != v ? 0 : v >= 4294967295.0 ? 0xffffffffu : v <= 0 ? 0 : (uint32_t) v;
			return 0;
		}
		*rt = 0; return 0;
	}
	case CM33_CP_MRRC: {
		double x = dcp_x, y = dcp_y, r;
		if (crm == 0) {
			switch (opc1) {
			case 1: r = x + y; break;
			case 3: r = x - y; break;
			case 5: r = x * y; break;
			case 7: r = x / y; break;
			case 9: r = sqrt (x); break;
			default: r = x; break;
			}
			d_bits (r, rt, rt2); return 0;
		}
		*rt = 0; *rt2 = 0x00100000u;                                           /* (refinement reads: a plain 1.0 mantissa) */
		return 0;
	}
	}
	return 0;
}

/* the cores' memory callbacks (RAM is the fast path in cm33.c) */
static unsigned long long rp_nio[2][3];                                  /* (debug: peripheral reads / writes / coprocessor, per core) */
static unsigned long long rp_nreg[2][64]; static uint32_t rp_nreg_a[2][64];
static uint32_t cb_rd_x (cm33_t *c, uint32_t a, int n)
{
	rp_nio[c->core & 1][0]++;
	if (((rp2350_t *) c->user)->log) { uint32_t key = (a >> 16); int k = c->core & 1, i; for (i = 0; i < 64 && rp_nreg[k][i] && rp_nreg_a[k][i] != key; i++) ; if (i < 64) { rp_nreg_a[k][i] = key; rp_nreg[k][i]++; } }
	return bus_read ((rp2350_t *) c->user, c, a, n);
}
static void cb_wr_x (cm33_t *c, uint32_t a, uint32_t v, int n)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	rp_nio[c->core & 1][1]++;
	bus_write (rp, c, a, v, n);
	cm33_excl_clear_addr (&rp->cpu[c->core ^ 1], a);
}
static int cb_cp_x (cm33_t *c, int kind, int cp, int opc1, int crn, int crm, int opc2, uint32_t *rt, uint32_t *rt2)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	(void) crn; (void) opc2;
	rp_nio[c->core & 1][2]++;
	if (cp == 0) {                                                            /* the GPIO coprocessor */
		uint64_t *reg = (crm & 4) ? &rp->gpio_oe : &rp->gpio_out;
		if (kind == CM33_CP_MRC) {
			if (crm == 8) *rt = (uint32_t) pin_levels (rp);
			else if (crm == 9) *rt = (uint32_t) (pin_levels (rp) >> 32);
			else *rt = (crm & 1) ? (uint32_t) (*reg >> 32) : (uint32_t) *reg;
			poll_note (rp, c, 0xd0000004u + (uint32_t) crm, *rt);
			return 0;
		}
		if (kind == CM33_CP_MRRC) {
			uint64_t v = crm == 8 ? pin_levels (rp) : *reg;
			*rt = (uint32_t) v; *rt2 = (uint32_t) (v >> 32);
			poll_note (rp, c, 0xd0000004u, *rt);
			return 0;
		}
		if (kind == CM33_CP_MCR) {
			if (opc1 <= 3) {
				uint64_t m = (crm & 1) ? (uint64_t) *rt << 32 : *rt;
				uint64_t keep = (crm & 1) ? 0xffffffffull : 0xffffffff00000000ull;
				switch (opc1) { case 0: *reg = (*reg & keep) | m; break; case 1: *reg ^= m; break; case 2: *reg |= m; break; case 3: *reg &= ~m; break; }
			} else {                                                          /* single pin: xor / set / clr */
				uint64_t b = 1ull << (*rt & 63);
				switch (opc1) { case 5: *reg ^= b; break; case 6: *reg |= b; break; case 7: *reg &= ~b; break; }
			}
			return 0;
		}
		if (kind == CM33_CP_MCRR) {
			if (opc1 <= 3) {
				uint64_t m = (uint64_t) *rt | ((uint64_t) *rt2 << 32);
				switch (opc1) { case 0: *reg = m; break; case 1: *reg ^= m; break; case 2: *reg |= m; break; case 3: *reg &= ~m; break; }
			} else if (opc1 <= 7) {
				uint64_t b = 1ull << (*rt & 63); int val = *rt2 != 0;
				switch (opc1) {
				case 4: if (val) *reg |= b; else *reg &= ~b; break;
				case 5: if (val) *reg ^= b; break;
				case 6: if (val) *reg |= b; break;
				case 7: if (val) *reg &= ~b; break;
				}
			} else {                                                          /* indexed: rt = value, rt2 = index */
				int hi = *rt2 & 1; uint64_t m = hi ? (uint64_t) *rt << 32 : *rt, keep = hi ? 0xffffffffull : 0xffffffff00000000ull;
				switch (opc1 - 8) { case 0: *reg = (*reg & keep) | m; break; case 1: *reg ^= m; break; case 2: *reg |= m; break; case 3: *reg &= ~m; break; }
			}
			return 0;
		}
		return 0;
	}
	if (cp == 7) { if (kind == CM33_CP_MRC || kind == CM33_CP_MRRC) { *rt = 0; *rt2 = 0; } return 0; }   /* RCP: canaries pass */
	if (cp == 4 || cp == 5) return dcp (rp, kind, opc1, crm, opc2, rt, rt2);
	return -1;
}
static void cb_event_x (cm33_t *c, int what)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	if (what == CM33_EV_SEV) { rp->cpu[0].event = 1; rp->cpu[1].event = 1; rp->cpu[0].attn = 1; rp->cpu[1].attn = 1; }
	else if (what == CM33_EV_RESET) snprintf (rp->fault, sizeof rp->fault, "the game reset the chip");
}

/* ------------------------------------------------------------------ core 0 on a host thread of its own
 * (rp_threads / VT_THREADS=1). Core 0 runs in epochs of TH_Q cycles beside the rest (core 1, DMA, PIO, the
 * timers), at most one epoch apart. Memory is shared as is (the cores' atomics are compare-and-swaps, cm33.c);
 * everything else - the peripherals - is behind a ticket lock the main thread holds, handing it over at the
 * end of each of its pieces when core 0 waits for it. It is on only while both cores run and core 0 seldom
 * touches the peripherals (a game whose core 0 drives the bus runs better in step: it turns itself off). */
static uint32_t TH_Q = 1024;                                          /* (VT_TQ: the epoch, cycles) */
#if defined(_MSC_VER)
#include <intrin.h>
#include <process.h>
__declspec(dllimport) void __stdcall Sleep (unsigned long);
__declspec(dllimport) int __stdcall CloseHandle (void *);
#define TH_INC(p) ((uint32_t) _InterlockedIncrement ((volatile long *) (p)) - 1u)
#define TH_PAUSE() _mm_pause ()
#define TH_SLEEP() Sleep (1)
#define TH_LOAD(p) (*(p))
#define TH_STORE(p, v) (*(p) = (v))
#else
#include <pthread.h>
#include <unistd.h>
#define TH_INC(p) __atomic_fetch_add ((p), 1u, __ATOMIC_SEQ_CST)
#if defined(__x86_64__) || defined(__i386__)
#define TH_PAUSE() __builtin_ia32_pause ()
#elif defined(__aarch64__) || defined(__arm__)
#define TH_PAUSE() __asm__ __volatile__ ("yield")
#else
#define TH_PAUSE() ((void) 0)
#endif
#define TH_SLEEP() usleep (1000)
#define TH_LOAD(p) __atomic_load_n ((p), __ATOMIC_ACQUIRE)
#define TH_STORE(p, v) __atomic_store_n ((p), (v), __ATOMIC_RELEASE)
#endif
static void lk_take (rp2350_t *rp)
{
	uint32_t me = TH_INC (&rp->lk_next);
	unsigned spins = 0;
	while (TH_LOAD (&rp->lk_serve) != me) { TH_PAUSE (); if (++spins > 4000000u) { TH_SLEEP (); spins = 0; } }
}
static void lk_give (rp2350_t *rp) { TH_STORE (&rp->lk_serve, rp->lk_serve + 1u); }
/* the main thread, between its pieces: core 0 waits for the peripherals - its turn */
static void lk_yield (rp2350_t *rp) { if (TH_LOAD (&rp->lk_want)) { rp->th_n_yield++; lk_give (rp); lk_take (rp); } }

/* the callbacks: core 0 on its thread takes the lock for each peripheral access */
#define TH_MINE(c, rp) ((c)->core == 0 && (rp)->th_on)
static void th_enter (rp2350_t *rp)
{
	uint32_t me = TH_INC (&rp->lk_next);
	TH_STORE (&rp->lk_want, 1);
	unsigned spins = 0;
	while (TH_LOAD (&rp->lk_serve) != me) { TH_PAUSE (); rp->th_spin_lk++; if (++spins > 4000000u) { TH_SLEEP (); spins = 0; } }
	TH_STORE (&rp->lk_want, 0);
	rp->th_io++;
}
static uint32_t cb_rd (cm33_t *c, uint32_t a, int n)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	if (!TH_MINE (c, rp)) return cb_rd_x (c, a, n);
	/* (the timers' raw counts: no side effects - read as they are, without waiting for the lock) */
	if ((a & ~0x8000u) == 0x400b0024u || (a & ~0x8000u) == 0x400b0028u) return cb_rd_x (c, a, n);
	th_enter (rp); uint32_t v = cb_rd_x (c, a, n); lk_give (rp); return v;
}
static void cb_wr (cm33_t *c, uint32_t a, uint32_t v, int n)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	if (!TH_MINE (c, rp)) { cb_wr_x (c, a, v, n); return; }
	th_enter (rp); cb_wr_x (c, a, v, n); lk_give (rp);
}
static int cb_cp (cm33_t *c, int kind, int cp, int opc1, int crn, int crm, int opc2, uint32_t *rt, uint32_t *rt2)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	if (!TH_MINE (c, rp)) return cb_cp_x (c, kind, cp, opc1, crn, crm, opc2, rt, rt2);
	th_enter (rp); int r = cb_cp_x (c, kind, cp, opc1, crn, crm, opc2, rt, rt2); lk_give (rp); return r;
}
static void cb_event (cm33_t *c, int what)
{
	rp2350_t *rp = (rp2350_t *) c->user;
	if (!TH_MINE (c, rp)) { cb_event_x (c, what); return; }
	th_enter (rp); cb_event_x (c, what); lk_give (rp);
}
static int peek (rp2350_t *rp, cm33_t *c, uint32_t a, int n, uint32_t *v);
/* core 0's slice from its own time to t (as rp_run does for each core): asleep on a value, or running */
static void core_slice (rp2350_t *rp, cm33_t *c, uint64_t t, int threaded)
{
	if (c->wait_on) {                                                   /* spinning: until the value changes */
		/* (the peeks have no side effects: a snapshot, without the lock even from core 0's own thread) */
		uint32_t v; int ok;
		(void) threaded;
		if (c->wait_on == 3) {                                          /* a few values */
			ok = 1;
			for (int i = 0; i < c->wl_cnt && ok; i++) ok = peek (rp, c, c->wl_a[i], c->wl_n[i], &v) && v == c->wl_v[i];
			v = c->wait_val;
		} else ok = peek (rp, c, c->wait_addr, c->wait_n, &v);
		if (!c->halted && ok && v == c->wait_val && !cm33_irq_pending (c)
		    && (c->wait_on != 2 || c->spin_skipped + (t - c->cycles) < c->spin_cap)) {
			if (c->wait_on == 2) c->spin_skipped += t - c->cycles;
			c->cycles = t; return;
		}
		if (c->wait_on == 2) cm33_spin_wake (c);
		c->wait_on = 0;
	}
	if (c->cycles < t) cm33_run (c, t);
}
#if defined(_MSC_VER)
static unsigned __stdcall th_main (void *p)
#else
static void *th_main (void *p)
#endif
{
	rp2350_t *rp = (rp2350_t *) p;
	cm33_t *c = &rp->cpu[0];
	unsigned idle = 0;
	host_pin (1);                                                       /* (a physical core of its own) */
	while (!TH_LOAD (&rp->th_quit)) {
		uint64_t tgt = TH_LOAD (&rp->th_target);
		if (!TH_LOAD (&rp->th_on) || c->cycles >= tgt) {
			TH_PAUSE (); rp->th_spin_idle++;
			if (++idle > 2000000u) { TH_SLEEP (); idle = 0; }            /* (the emulation paused: no busy core) */
			continue;
		}
		idle = 0;
		while (c->cycles < tgt) {
			uint64_t t = c->cycles + 64; if (t > tgt) t = tgt;
			if (c->halted) { c->cycles = tgt; break; }
			core_slice (rp, c, t, 1);
		}
		TH_STORE (&rp->th_done, c->cycles);
	}
	TH_STORE (&rp->th_exited, 1);
	return 0;
}
static void th_start (rp2350_t *rp)
{
	if (!rp->th_made) {
#if defined(_MSC_VER)
		rp->th_handle = (void *) _beginthreadex (NULL, 0, th_main, rp, 0, NULL);
		if (!rp->th_handle) { rp->th_enabled = 0; return; }
#else
		pthread_t th;
		if (pthread_create (&th, NULL, th_main, rp)) { rp->th_enabled = 0; return; }
		pthread_detach (th);
#endif
		rp->th_made = 1;
	}
	host_pin (0);                                                      /* (the main thread on a physical core of its own) */
	lk_take (rp);                                                      /* (the main thread holds the peripherals) */
	rp->th_done = rp->cpu[0].cycles;
	TH_STORE (&rp->th_target, rp->cpu[0].cycles + TH_Q);
	rp->th_io = 0; rp->th_epochs = 0;
	TH_STORE (&rp->th_on, 1); rp->th_n_on++;
	fprintf (stderr, "STAT: threads 2\n"); fflush (stderr);              /* (the window's OSD) */
}
/* the main thread: until core 0 has done its epoch (it may want the peripherals meanwhile) */
static void th_wait_epoch (rp2350_t *rp)
{
	unsigned spins = 0;
	while (TH_LOAD (&rp->th_done) < rp->th_target && !rp->cpu[0].halted) {
		lk_yield (rp); TH_PAUSE (); rp->th_spin_main++;
		if (++spins > 4000000u) { TH_SLEEP (); spins = 0; }
	}
}
static void th_stop (rp2350_t *rp)
{
	if (!rp->th_on) return;
	th_wait_epoch (rp);
	TH_STORE (&rp->th_on, 0);
	lk_give (rp);
	fprintf (stderr, "STAT: threads 1\n"); fflush (stderr);
}
static void th_quit (rp2350_t *rp)
{
	th_stop (rp);
	if (!rp->th_made) return;
	TH_STORE (&rp->th_quit, 1);
	while (!TH_LOAD (&rp->th_exited)) TH_PAUSE ();
#if defined(_MSC_VER)
	CloseHandle (rp->th_handle);
#endif
	rp->th_made = 0;
}
extern int cm33_excl_sticky;
void rp_threads (rp2350_t *rp, int on) { rp->th_enabled = on; if (on) cm33_excl_sticky = 1; if (!on) th_stop (rp); }

/* ------------------------------------------------------------------ setup, run */
rp2350_t *rp_create (void)
{
	rp2350_t *rp = (rp2350_t *) calloc (1, sizeof *rp);
	if (!rp) return NULL;
	rp->psram = (uint8_t *) calloc (1, PSRAM_SIZE);
	rp->log = getenv ("VT_RPLOG") != NULL;
	usb_init ();
	{ extern int cm33_spin_mode, cm33_spin_log; const char *z = getenv ("VT_SPIN"); if (z) cm33_spin_mode = atoi (z); cm33_spin_log = getenv ("VT_SPINLOG") != NULL; }
	{ const char *z = getenv ("VT_THREADS"); rp->th_enabled = z && atoi (z) > 0; if (rp->th_enabled) cm33_excl_sticky = 1; }
	{ const char *z = getenv ("VT_TQ"); if (z && atoi (z) >= 64) TH_Q = (uint32_t) atoi (z); }
	{ extern int cm33_specialise; const char *z = getenv ("VT_SPEC"); if (z) cm33_specialise = atoi (z); }   /* (core 0 on a host thread of its own) */
	rp->sys.rd = cb_rd; rp->sys.wr = cb_wr; rp->sys.cp = cb_cp; rp->sys.event = cb_event;
	rp->sys.ram = rp->sram; rp->sys.ram_base = SRAM_BASE; rp->sys.ram_size = SRAM_SIZE; rp->sys.xram = rp->psram; rp->sys.xram_size = PSRAM_SIZE;
	extern int (*cm33_bkpt_hle) (cm33_t *, int);
	cm33_bkpt_hle = rom_hle;
	for (int k = 0; k < 2; k++) { rp->cpu[k].sys = &rp->sys; rp->cpu[k].user = rp; rp->cpu[k].core = k; }
	for (int i = 0; i < NPINS + 2; i++) rp->pads[i] = 0x116;               /* (reset: pull-down, input enable) */
	for (int i = 0; i < NPINS; i++) { rp->funcsel[i] = 31; rp->gpio_ctrl[i] = 31; }
	for (int k = 0; k < 3; k++) for (int i = 0; i < 4; i++) { rp->pio[k].sm[i].clkdiv = 1u << 16; rp->pio[k].sm[i].execctrl = 31u << 12; rp->pio[k].sm[i].shiftctrl = 0x000c0000u; pio_sm_restart (&rp->pio[k].sm[i]); }
	rom_build (rp);
	fmasks (rp);
	/* the clocks as the multicart's firmware leaves them: clk_sys 150 MHz from the PLL */
	greg_wr (rp, 0x4001003c, 1); greg_wr (rp, 0x40010040, 1u << 16);
	greg_wr (rp, 0x40050008, 125); greg_wr (rp, 0x4005000c, (5u << 16) | (2u << 12));
	recompute_sys_hz (rp);
	return rp;
}
void rp_destroy (rp2350_t *rp) { if (!rp) return; th_quit (rp); if (rp->sd) fclose (rp->sd); free (rp->psram); free (rp->sys.dcache); free (rp->sys.dcache1); free (rp->sys.code_pages); free (rp); }

int rp_load_um2 (rp2350_t *rp, const uint8_t *d, size_t n, char *err, size_t errn)
{
	if (n < 28 || memcmp (d, "2CMU", 4)) { snprintf (err, errn, "not a .um2 file"); return -1; }
	uint32_t ver = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t) d[7] << 24);
	uint32_t load = d[12] | (d[13] << 8) | (d[14] << 16) | ((uint32_t) d[15] << 24);
	uint32_t words = d[16] | (d[17] << 8) | (d[18] << 16) | ((uint32_t) d[19] << 24);
	size_t len = (size_t) words * 4;
	(void) ver;
	/* (the length is in words; some images give it in bytes - the loader copies what the file holds) */
	if (20 + len > n) len = n - 20;
	if (load < SRAM_BASE || load - SRAM_BASE + len > SRAM_SIZE) { snprintf (err, errn, "a .um2 image that does not fit in SRAM"); return -1; }
	memcpy (rp->sram + (load - SRAM_BASE), d + 20, len);
	cm33_reset (&rp->cpu[0], load);
	rp->cpu[0].cycles = 0;
	rp->core1_parked = 1; rp->launch_i = 0;
	rp->cpu[1].halted = 0;
	rp->now = 0;
	return 0;
}

static uint32_t prof_pc[2][65536]; static int prof_on = -1;
void rp_debug (rp2350_t *rp)
{
	fprintf (stderr, "sd blocks read %lu written %lu\n", rp->sd_nread, rp->sd_nwrite);
	fprintf (stderr, "threads: on %d starts %llu stops %llu epochs %llu cycles %llu main-spins %llu yields %llu core0-idle %llu core0-lockwait %llu\n", rp->th_on, rp->th_n_on, rp->th_n_off, rp->th_n_ep, rp->th_cyc_on, rp->th_spin_main, rp->th_n_yield, rp->th_spin_idle, rp->th_spin_lk);
	fprintf (stderr, "io core0 rd %llu wr %llu cp %llu | core1 rd %llu wr %llu cp %llu\n", rp_nio[0][0], rp_nio[0][1], rp_nio[0][2], rp_nio[1][0], rp_nio[1][1], rp_nio[1][2]);
	for (int k = 0; k < 2; k++) { fprintf (stderr, "io core%d pages:", k); for (int i = 0; i < 64 && rp_nreg[k][i]; i++) if (rp_nreg[k][i] > 100000) fprintf (stderr, " %04X0000:%llu", rp_nreg_a[k][i], rp_nreg[k][i]); fputc (10, stderr); }
	{ extern unsigned long long cm33_gen_count; extern int cm33_gen_log; extern uint32_t cm33_gen_hist[4096];
	  { extern unsigned long long cm33_gen_op[2][4096];
	    for (int k = 0; k < 2; k++) {
	      fprintf (stderr, "generic ops core%d:", k);
	      for (int t = 0; t < 16; t++) { int b = 0; for (int i = 1; i < 4096; i++) if (cm33_gen_op[k][i] > cm33_gen_op[k][b]) b = i;
	        if (!cm33_gen_op[k][b]) break; fprintf (stderr, " %03Xx:%llu", b, cm33_gen_op[k][b]); cm33_gen_op[k][b] = 0; }
	      fputc (10, stderr); } }
	  cm33_gen_log = 1; fprintf (stderr, "generic %llu:", cm33_gen_count);
	  static uint32_t seen[64]; int ns = 0;
	  for (int i = 0; i < 4096 && ns < 24; i++) if (cm33_gen_hist[i]) { uint32_t a = cm33_gen_hist[i]; int k; for (k = 0; k < ns; k++) if (seen[k] == a) break; if (k == ns) { seen[ns++] = a; fprintf (stderr, " %08X:%04X", a, *(uint16_t *) (rp->sram + (a - 0x20000000u))); } }
	  fputc (10, stderr); memset (cm33_gen_hist, 0, sizeof cm33_gen_hist); }
	fprintf (stderr, "pio instructions: %llu %llu %llu", pio_count[0], pio_count[1], pio_count[2]); fputc (10, stderr);
	for (int t = 0; t < 2; t++) fprintf (stderr, "timer%d us %llu armed %X inte %X intr %X alarms %X %X %X %X  | ", t, (unsigned long long) us_now (rp, t), rp->armed[t], rp->tinte[t], rp->tintr[t], rp->alarm[t][0], rp->alarm[t][1], rp->alarm[t][2], rp->alarm[t][3]);
	fprintf (stderr, "nvic0 en %X pend %X act %X pri0-3 %X %X %X %X primask %u", rp->cpu[0].nvic_en[0], rp->cpu[0].nvic_pend[0], rp->cpu[0].nvic_act[0], rp->cpu[0].nvic_pri[0], rp->cpu[0].nvic_pri[1], rp->cpu[0].nvic_pri[2], rp->cpu[0].nvic_pri[3], rp->cpu[0].primask); fputc (10, stderr);
	fprintf (stderr, "audio peak %d pops %lu nonzero %lu kept %lu rate %d  pio2 sm0 pinctrl %08X gpiobase %u", aud_peak, aud_pops, aud_nonzero, aud_kept, rp->aud_rate, rp->pio[2].sm[0].pinctrl, rp->pio[2].gpiobase); fputc (10, stderr);
	{ extern unsigned long long cm33_steps[16]; fprintf (stderr, "steps: core0 %llu core1 %llu  now %llu", cm33_steps[0], cm33_steps[8], (unsigned long long) rp->now); fputc (10, stderr); }
	if (prof_on > 0) {
		for (int k = 0; k < 2; k++) {
			fprintf (stderr, "core %d hot:", k);
			for (int t = 0; t < 12; t++) {
				int best = 0; for (int i = 1; i < 65536; i++) if (prof_pc[k][i] > prof_pc[k][best]) best = i;
				if (!prof_pc[k][best]) break;
				fprintf (stderr, " %08X:%u", 0x20000000u + (uint32_t) best * 2, prof_pc[k][best]); prof_pc[k][best] = 0;
			}
			fprintf (stderr, "%s", "  |  ");
		}
		memset (prof_pc, 0, sizeof prof_pc);
	}
	for (int k = 0; k < 2; k++) {
		cm33_t *c = &rp->cpu[k];
		fprintf (stderr, "core %d: %s pc=%08X lr=%08X sp=%08X r0=%08X r1=%08X r2=%08X r3=%08X ipsr=%u sleep=%d scr=%X en=%X pend=%X%s\n", k,
		         k == 1 && rp->core1_parked ? "parked" : "run", c->r[15], c->r[14], c->r[13], c->r[0], c->r[1], c->r[2], c->r[3], c->ipsr, c->sleeping, c->scr, c->nvic_en[0], c->nvic_pend[0], c->halted ? " HALTED" : "");
		if (getenv ("VT_STACKLOG") && c->r[13] - SRAM_BASE < SRAM_SIZE - 128) {
			fprintf (stderr, "  stack:");
			for (int w = 0; w < 32; w++) { uint32_t x; memcpy (&x, rp->sram + (c->r[13] - SRAM_BASE) + 4 * w, 4); fprintf (stderr, " %08X", x); }
			fputc (10, stderr);
		}
	}
	uint64_t oe, v = rp_pins (rp, &oe);
	fprintf (stderr, "pins %012llX oe %012llX  pio ctrl %X %X %X  dmatimeouts %u  dma busy", (unsigned long long) v, (unsigned long long) oe, rp->pio[0].ctrl, rp->pio[1].ctrl, rp->pio[2].ctrl, *(uint32_t *) (rp->sram + 0x5e3ac));
	for (int i = 0; i < 16; i++) if (rp->dma[i].busy) fprintf (stderr, " %d", i);
	fprintf (stderr, "  hz %u\n", rp->sys_hz);
}

const char *rp_fault (rp2350_t *rp)
{
	if (rp->fault[0]) return rp->fault;
	if (rp->cpu[0].halted) return rp->cpu[0].fault;
	{ cm33_t *c = &rp->cpu[0];
	  if (c->sleeping == 1 && c->ipsr == 11)
		return "the program calls a cartridge service (SVC) that a UVMC2 does not provide: it was built for another cartridge (the Vectrex Studio debug cartridge)";
	  if (c->sleeping == 1 && (c->ipsr == 3 || c->ipsr == 4 || c->ipsr == 5 || c->ipsr == 6)) return "the program stopped on a fault (HardFault)"; }
	if (!rp->core1_parked && rp->cpu[1].halted) return rp->cpu[1].fault;
	return NULL;
}

/* a value a suspended core polls, read again without side effects (0: cannot be read safely) */
static int peek (rp2350_t *rp, cm33_t *c, uint32_t a, int n, uint32_t *v)
{
	if (a - SRAM_BASE < SRAM_SIZE || (a >= 0x10000000u && a < 0x20000000u)) { *v = bus_read (rp, c, a, n); return 1; }
	int safe = 0;
	if (a >= 0x50000000u && a < 0x50001000u) safe = 1;                                 /* DMA */
	else if (a >= 0x50200000u && a < 0x50500000u) { uint32_t o = a & 0xfff; safe = o < 0x20 || (o >= 0x30 && o < 0x128) || o >= 0x168; }   /* PIO, not the RX FIFOs */
	else if (a >= 0xd0000000u && a < 0xd0000200u) { uint32_t o = a & 0x1ff; safe = o != 0x058 && !(o >= 0x100 && o < 0x180); }   /* SIO, not FIFO_RD / spinlocks */
	else if ((a & 0xffff0000u) == 0x400b0000u) safe = 1;                                /* timers */
	if (!safe) return 0;
	uint32_t ph = rp->poll_hits[c->core];
	*v = bus_read (rp, NULL, a, n);
	rp->poll_hits[c->core] = (int) ph;
	return 1;
}

/* the main thread, each piece: core 0's thread on or off, its next epoch */
static void th_manage (rp2350_t *rp, uint64_t t)
{
	int want = !rp->core1_parked && !rp->cpu[1].halted && !rp->cpu[0].halted && rp->now >= rp->th_off_until;
	if (!rp->th_on) { if (want && rp->cpu[0].cycles >= rp->now && !rp->cpu[0].wait_on) th_start (rp); return; }
	if (t <= rp->th_target) return;
	th_wait_epoch (rp);
	rp->th_epochs++;
	/* (core 0 at the peripherals often - a few times an epoch: in step is better, for a while) */
	if (!want || (rp->th_epochs >= 64 && rp->th_io > (uint64_t) rp->th_epochs * TH_Q / 16)) {
		th_stop (rp); rp->th_n_off++;
		if (want) rp->th_off_until = rp->now + 15000000u;
		return;
	}
	if (rp->th_epochs >= 64) { rp->th_epochs = 0; rp->th_io = 0; }
	uint64_t nt = rp->th_target + TH_Q; if (nt < t) nt = t + TH_Q;
	rp->th_n_ep++; rp->th_cyc_on += nt - rp->th_target;
	TH_STORE (&rp->th_target, nt);
}

#if defined(_MSC_VER)
#include <intrin.h>
#define RP_TSC() __rdtsc ()
#elif defined(__x86_64__)
#include <x86intrin.h>
#define RP_TSC() __rdtsc ()
#else
#define RP_TSC() 0ull
#endif
unsigned long long rp_t_core[2], rp_t_dma, rp_t_pio, rp_t_rest, rp_t_all, rp_n_slices; static int rp_timing = -1;
static void rp_timing_print (void)
{
	double a = (double) (rp_t_all ? rp_t_all : 1);
	extern unsigned long long cm33_steps[16];
	fprintf (stderr, "RP timing (VT_RPTIME): %llu slices; core0 %.1f%%, core1 %.1f%%, PIO + DMA %.1f%%, DMA + timers %.1f%% (of rp_run's time); instructions core0 %llu core1 %llu (interpreter only); %.0f TSC ticks per slice%c",
	         rp_n_slices, 100.0 * rp_t_core[0] / a, 100.0 * rp_t_core[1] / a, 100.0 * rp_t_pio / a, 100.0 * rp_t_dma / a, cm33_steps[0], cm33_steps[8], a / (double) (rp_n_slices ? rp_n_slices : 1), 10);
}
void rp_run (rp2350_t *rp, uint32_t cycles)
{
	if (rp_timing < 0) { rp_timing = getenv ("VT_RPTIME") != NULL; if (rp_timing) atexit (rp_timing_print); }
	if (rp_timing) {                                             /* (the same, timed: VT_RPTIME) */
		uint64_t end = rp->now + cycles;
		while (rp->now < end) {
			unsigned long long t0 = RP_TSC (), t1, t2, t3;
			uint64_t t = rp->now + 64; if (t > end) t = end;
			uint32_t dt = (uint32_t) (t - rp->now);
			if (rp->th_enabled) th_manage (rp, t);
			for (int k = 0; k < 2; k++) {
				cm33_t *c = &rp->cpu[k];
				unsigned long long c0 = RP_TSC ();
				if (k == 0 && rp->th_on) continue;
				if (k == 1 && rp->core1_parked) { c->cycles = t; continue; }
				if (c->cycles < rp->now) c->cycles = rp->now;
				core_slice (rp, c, t, 0);
				rp_t_core[k] += RP_TSC () - c0;
			}
			t1 = RP_TSC ();
			dma_service (rp, 64);
			pl_on = 1; pl_ok = 0;
			for (int k = 0; k < 3; k++)
				for (int i = 0; i < 4; i++)
					if ((rp->pio[k].ctrl >> i) & 1) pio_run_sm (rp, k, i, dt);
			pl_on = 0; pl_ok = 0;
			t2 = RP_TSC ();
			dma_service (rp, 64);
			rp->now = t;
			timers_update (rp);
			if (usb_on ()) { usb_tick (rp->now, rp->sys_hz); usb_irq_update (rp); }
			pio_irq_update (rp);
			if (rp->th_on) lk_yield (rp);
			t3 = RP_TSC ();
			rp_t_pio += t2 - t1; rp_t_dma += t3 - t2; rp_t_all += t3 - t0; rp_n_slices++;
			rp_t_rest += (t3 - t0) - (t3 - t2) - (t2 - t1);
		}
		rp_t_rest -= 0;
		return;
	}
	uint64_t end = rp->now + cycles;
	/* in pieces of at most 64 cycles: the cores, then the DMA and the PIO see what they did */
	while (rp->now < end) {
		uint64_t t = rp->now + 64; if (t > end) t = end;
		uint32_t dt = (uint32_t) (t - rp->now);
		if (rp->th_enabled) th_manage (rp, t);
		for (int k = 0; k < 2; k++) {
			cm33_t *c = &rp->cpu[k];
			if (k == 0 && rp->th_on) continue;                         /* (core 0: on its own thread) */
			if (k == 1 && rp->core1_parked) { c->cycles = t; continue; }
			if (c->cycles < rp->now) c->cycles = rp->now;
			if (c->wait_on && rp->log) {
				static uint32_t seen[64]; static int ns; int k2; for (k2 = 0; k2 < ns; k2++) if (seen[k2] == c->r[15]) break;
				if (k2 == ns && ns < 64) { seen[ns++] = c->r[15]; fprintf (stderr, "suspend core %d pc %08X on %08X = %08X\n", k, c->r[15], c->wait_addr, c->wait_val); }
			}
			core_slice (rp, c, t, 0);
			if (prof_on < 0) prof_on = getenv ("VT_RPPROF") != NULL;
			if (prof_on && c->r[15] >= 0x20000000u && c->r[15] < 0x20020000u) prof_pc[k][(c->r[15] - 0x20000000u) >> 1]++;
		}
		dma_service (rp, 64);
		pl_on = 1; pl_ok = 0;                                    /* (the pin levels kept while the PIO runs: pin_levels) */
		for (int k = 0; k < 3; k++)
			for (int i = 0; i < 4; i++)
				if ((rp->pio[k].ctrl >> i) & 1) pio_run_sm (rp, k, i, dt);
		pl_on = 0; pl_ok = 0;
		dma_service (rp, 64);
		rp->now = t;
		timers_update (rp);
		if (usb_on ()) { usb_tick (rp->now, rp->sys_hz); usb_irq_update (rp); }
		pio_irq_update (rp);
		if (rp->th_on) lk_yield (rp);
	}
}
uint64_t rp_sio_mask (rp2350_t *rp) { return rp->m_sio; }
uint64_t rp_gpio_out (rp2350_t *rp) { return rp->gpio_out; }
uint32_t rp_gpio_ctrl (rp2350_t *rp, int pin) { return rp->gpio_ctrl[pin]; }

/* the chip's state in one number (online play: both sides compare it now and then - live.c): the cores' cycle counts
 * and registers, which part from the other side's at the first instruction that runs differently */
uint32_t rp2350_sig (rp2350_t *rp)
{
	uint32_t h = 2166136261u;
	for (int k = 0; k < 2; k++) {
		const cm33_t *c = &rp->cpu[k];
		h = (h ^ (uint32_t) c->cycles) * 16777619u; h = (h ^ (uint32_t) (c->cycles >> 32)) * 16777619u;
		for (int i = 0; i < 16; i++) h = (h ^ c->r[i]) * 16777619u;
	}
	return h;
}
