/* cm33.c - an ARM Cortex-M33 interpreter (ARMv8-M Mainline: Thumb-2 with the DSP extension and the
 * single precision FPU, the M-profile exception model, Secure state only), for the RP2350 of the
 * add-on cartridges (rp2350.c). Written from the ARMv7-M / ARMv8-M Architecture Reference Manuals.
 *
 * Time: one cycle per instruction plus the usual extras (loads, taken branches, LDM / STM, divides),
 * close to a Cortex-M33 for timing purposes; the system runs the cores in slices (cm33_run). */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "cm33.h"

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#define NOINLINE __declspec(noinline)
#else
#define FORCE_INLINE inline __attribute__ ((always_inline))
#define NOINLINE __attribute__ ((noinline))
#endif
/* atomic operations on the host: the cores may run on two host threads (rp2350.c), so what one raises in the
 * other (pending interrupts) and the exclusive stores (STREX: a compare-and-swap) must be atomic */
#if defined(_MSC_VER)
#include <intrin.h>
#define AT_OR32(p, v)  _InterlockedOr ((volatile long *) (p), (long) (v))
#define AT_AND32(p, v) _InterlockedAnd ((volatile long *) (p), (long) (v))
static int at_cas (void *p, uint32_t expect, uint32_t v, int n)
{
	if (n == 4) return (uint32_t) _InterlockedCompareExchange ((volatile long *) p, (long) v, (long) expect) == expect;
	if (n == 2) return (uint16_t) _InterlockedCompareExchange16 ((volatile short *) p, (short) v, (short) expect) == (uint16_t) expect;
	return (uint8_t) _InterlockedCompareExchange8 ((volatile char *) p, (char) v, (char) expect) == (uint8_t) expect;
}
#else
#define AT_OR32(p, v)  __atomic_fetch_or ((p), (v), __ATOMIC_SEQ_CST)
#define AT_AND32(p, v) __atomic_fetch_and ((p), (v), __ATOMIC_SEQ_CST)
static int at_cas (void *p, uint32_t expect, uint32_t v, int n)
{
	if (n == 4) return __sync_bool_compare_and_swap ((uint32_t *) p, expect, v);
	if (n == 2) return __sync_bool_compare_and_swap ((uint16_t *) p, (uint16_t) expect, (uint16_t) v);
	return __sync_bool_compare_and_swap ((uint8_t *) p, (uint8_t) expect, (uint8_t) v);
}
#endif
/* the second plain memory (the RP2350's PSRAM): any 0x1x000000 window with bit 24 set, offset in its low 24 bits */
#define XRAM(s, a, n) (((a) >> 28) == 1 && ((a) & 0x01000000u) && ((a) & 0xffffffu) + (uint32_t) (n) <= (s)->xram_size)

#define RD(a, n)     mrd (c, (a), (n))
#define WR(a, v, n)  mwr (c, (a), (v), (n))

/* ---------------------------------------------------------------- memory */
static FORCE_INLINE uint32_t mrd (cm33_t *c, uint32_t a, int n)
{
	cm33_sys_t *s = c->sys;
	uint32_t o = a - s->ram_base, v;
	if (o < s->ram_size && o + n <= s->ram_size) {
		const uint8_t *p = s->ram + o;
		if (n == 4) memcpy (&v, p, 4);
		else if (n == 2) { uint16_t h; memcpy (&h, p, 2); v = h; }
		else v = *p;
	} else if (XRAM (s, a, n)) {
		const uint8_t *p = s->xram + (a & 0xffffffu);
		if (n == 4) memcpy (&v, p, 4);
		else if (n == 2) { uint16_t h; memcpy (&h, p, 2); v = h; }
		else v = *p;
	} else { v = s->rd (c, a, n); c->attn = 1; }                        /* (a peripheral: it may have changed things) */
	c->loop_loads++; c->ld_addr = a; c->ld_val = v; c->ld_n = n; c->ld_hash = (c->ld_hash ^ a) * 0x9E3779B1u + v;
	if (c->ldl_on) { int k = c->ldl_cnt++; if (k < CM33_WL) { c->ldl_a[k] = a; c->ldl_v[k] = v; c->ldl_n[k] = (uint8_t) n; } }
	return v;
}
#define FETCH(a) fetch16 (c, (a))
static inline uint32_t fetch16 (cm33_t *c, uint32_t a)
{
	cm33_sys_t *s = c->sys;
	uint32_t o = a - s->ram_base;
	if (o + 2 <= s->ram_size) { uint16_t h; memcpy (&h, s->ram + o, 2); return h; }
	return s->rd (c, a, 2);
}
void cm33_code_written (cm33_sys_t *s, uint32_t addr, uint32_t n);
int (*cm33_bkpt_hle) (cm33_t *c, int imm);
int cm33_specialise = 1;                         /* (VT_SPEC=0: the general handlers only - for comparing) */
unsigned long long cm33_gen_op[2][4096];          /* (debug, VT_RPLOG: the full decoder's work by the first halfword's top 12 bits) */
unsigned long long cm33_steps[16], cm33_gen_count; int cm33_gen_log; uint32_t cm33_gen_hist[4096];   /* (steps: core k at [8 k] - apart, on cache lines of their own) */
int cm33_spin_log = 0;
int cm33_spin_mode = 2;                  /* spin handling: 0 off, 1 steady counting loops skipped ahead, 2 pure spins suspended too */
static int excl_any;                     /* (any core holds an exclusive monitor: stores check it) */
static cm33_t *excl_cores[2];
static FORCE_INLINE void mwr (cm33_t *c, uint32_t a, uint32_t v, int n)
{
	cm33_sys_t *s = c->sys;
	c->loop_stores++;
	if (excl_any) {
		cm33_t *o = excl_cores[c->core ^ 1];
		if (o && o->excl && ((o->excl_addr ^ a) & ~7u) == 0) o->excl = 0;
	}
	uint32_t off = a - s->ram_base;
	if (off < s->ram_size && off + n <= s->ram_size) {
		uint8_t *p = s->ram + off;
		if (n == 4) memcpy (p, &v, 4);
		else if (n == 2) { uint16_t h = (uint16_t) v; memcpy (p, &h, 2); }
		else *p = (uint8_t) v;
		if (s->code_pages && (s->code_pages[off >> 12] | (off >= 2 ? s->code_pages[(off - 2) >> 12] : 0)))
			cm33_code_written (s, a, (uint32_t) n);              /* (code changed: its decoded entries go) */
		return;
	}
	if (XRAM (s, a, n)) {
		uint8_t *p = s->xram + (a & 0xffffffu);
		if (n == 4) memcpy (p, &v, 4);
		else if (n == 2) { uint16_t h = (uint16_t) v; memcpy (p, &h, 2); }
		else *p = (uint8_t) v;
		return;
	}
	c->attn = 1;                                                    /* (a peripheral or the SCS: interrupts may change) */
	if ((a & 0xfffff000u) == 0xe000e000u) { if (n == 4) cm33_scs_write (c, a, v); return; }
	s->wr (c, a, v, n);
}
void cm33_excl_clear_addr (cm33_t *c, uint32_t a) { if (c->excl && ((c->excl_addr ^ a) & ~7u) == 0) c->excl = 0; }

/* STREX / STREXB / STREXH: 0 stored, 1 failed. In memory the store is a compare-and-swap against what LDREX
 * read, so it is atomic against the other core even when that one runs on another host thread */
static int strex (cm33_t *c, uint32_t a, uint32_t v, int n)
{
	int mine = c->excl && c->excl_addr == a;
	c->excl = 0;
	if (!mine) return 1;
	cm33_sys_t *s = c->sys; uint8_t *p = NULL;
	uint32_t o = a - s->ram_base;
	if (o < s->ram_size && o + n <= s->ram_size) p = s->ram + o;
	else if (XRAM (s, a, n)) p = s->xram + (a & 0xffffffu);
	if (!p || (a & (uint32_t) (n - 1))) { mwr (c, a, v, n); return 0; }
	uint32_t m = n == 4 ? 0xffffffffu : n == 2 ? 0xffffu : 0xffu;
	if (!at_cas (p, c->excl_val & m, v & m, n)) return 1;
	/* (the rest of what a store does: the other core's monitor, decoded code there) */
	c->loop_stores++;
	cm33_t *oc = excl_cores[c->core ^ 1];
	if (oc && oc->excl && ((oc->excl_addr ^ a) & ~7u) == 0) oc->excl = 0;
	if (p != s->xram + (a & 0xffffffu) && s->code_pages && (s->code_pages[o >> 12] | (o >= 2 ? s->code_pages[(o - 2) >> 12] : 0)))
		cm33_code_written (s, a, (uint32_t) n);
	return 0;
}

static FORCE_INLINE uint32_t rd_mem (cm33_t *c, uint32_t a, int n)
{
	uint32_t o = a - c->sys->ram_base;
	if (o < c->sys->ram_size && o + n <= c->sys->ram_size) return mrd (c, a, n);   /* (RAM first: by far the most) */
	if ((a & 0xfffff000u) == 0xe000e000u) return n == 4 ? cm33_scs_read (c, a) : (cm33_scs_read (c, a & ~3u) >> ((a & 3) * 8)) & (n == 1 ? 0xff : 0xffff);
	return mrd (c, a, n);
}

/* ---------------------------------------------------------------- helpers */
static void fault (cm33_t *c, const char *why, uint32_t pc, uint32_t ins)
{
	if (!c->halted) {
		snprintf (c->fault, sizeof c->fault, "core %d: %s at %08X (%08X)", c->core, why, pc, ins);
		c->halted = 1; c->attn = 1;
	}
}
static inline uint32_t ror32 (uint32_t x, unsigned n) { n &= 31; return n ? (x >> n) | (x << (32 - n)) : x; }

static inline uint32_t add_c (uint32_t x, uint32_t y, uint32_t cin, uint32_t *co, uint32_t *ov)
{
	uint64_t u = (uint64_t) x + y + cin;
	uint32_t r = (uint32_t) u;
	*co = (uint32_t) (u >> 32);
	*ov = ((~(x ^ y) & (x ^ r)) >> 31) & 1;
	return r;
}
static inline void nz (cm33_t *c, uint32_t r) { c->n = r >> 31; c->z = r == 0; }

/* shift with carry out: type 0 LSL, 1 LSR, 2 ASR, 3 ROR (amount 0 with ROR = RRX in the immediate forms,
 * decoded by the caller) */
static inline uint32_t shift_c (uint32_t v, int type, unsigned amt, uint32_t cin, uint32_t *co)
{
	*co = cin;
	if (amt == 0) return v;
	switch (type) {
	case 0: if (amt < 32) { *co = (v >> (32 - amt)) & 1; return v << amt; }
	        *co = amt == 32 ? v & 1 : 0; return 0;
	case 1: if (amt < 32) { *co = (v >> (amt - 1)) & 1; return v >> amt; }
	        *co = amt == 32 ? v >> 31 : 0; return 0;
	case 2: if (amt < 32) { *co = (v >> (amt - 1)) & 1; return (uint32_t) ((int32_t) v >> amt); }
	        *co = v >> 31; return (uint32_t) ((int32_t) v >> 31);
	default: { unsigned m = amt & 31; uint32_t r = ror32 (v, m); *co = r >> 31; return r; }
	case 4: *co = v & 1; return (cin << 31) | (v >> 1);          /* RRX */
	}
}
/* the immediate shift (type, imm5) -> (type, amount) */
static inline void dec_imm_shift (int type, unsigned imm5, int *t, unsigned *amt)
{
	switch (type) {
	case 0: *t = 0; *amt = imm5; break;
	case 1: *t = 1; *amt = imm5 ? imm5 : 32; break;
	case 2: *t = 2; *amt = imm5 ? imm5 : 32; break;
	default: if (imm5 == 0) { *t = 4; *amt = 1; } else { *t = 3; *amt = imm5; }
	}
}
static inline uint32_t expand_imm_c (uint32_t imm12, uint32_t cin, uint32_t *co)
{
	uint32_t v = imm12 & 0xff;
	*co = cin;
	if ((imm12 >> 10) == 0) {
		switch ((imm12 >> 8) & 3) {
		case 0: return v;
		case 1: return v | (v << 16);
		case 2: return (v << 8) | (v << 24);
		default: return v | (v << 8) | (v << 16) | (v << 24);
		}
	}
	uint32_t r = ror32 (0x80 | (imm12 & 0x7f), (imm12 >> 7) & 31);
	*co = r >> 31;
	return r;
}
static FORCE_INLINE int cond_pass (cm33_t *c, unsigned cond)
{
	int r;
	switch (cond >> 1) {
	case 0: r = c->z; break;
	case 1: r = c->c; break;
	case 2: r = c->n; break;
	case 3: r = c->v; break;
	case 4: r = c->c && !c->z; break;
	case 5: r = c->n == c->v; break;
	case 6: r = c->n == c->v && !c->z; break;
	default: return 1;
	}
	return (cond & 1) ? !r : r;
}
static inline int32_t sat_s (int64_t v, unsigned bits, int *sat)
{
	int64_t hi = ((int64_t) 1 << (bits - 1)) - 1, lo = -((int64_t) 1 << (bits - 1));
	if (v > hi) { *sat = 1; return (int32_t) hi; }
	if (v < lo) { *sat = 1; return (int32_t) lo; }
	return (int32_t) v;
}
static inline uint32_t sat_u (int64_t v, unsigned bits, int *sat)
{
	int64_t hi = ((int64_t) 1 << bits) - 1;
	if (v > hi) { *sat = 1; return (uint32_t) hi; }
	if (v < 0) { *sat = 1; return 0; }
	return (uint32_t) v;
}

/* ---------------------------------------------------------------- xPSR, stacks, exceptions */
static uint32_t xpsr_get (cm33_t *c)
{
	return (c->n << 31) | (c->z << 30) | (c->c << 29) | (c->v << 28) | (c->q << 27) | ((c->it & 3) << 25)
	       | (1u << 24) | (c->ge << 16) | ((c->it & 0xfc) << 8) | (c->ipsr & 0x1ff);
}
static void apsr_set (cm33_t *c, uint32_t x, int nzcvq, int g)
{
	if (nzcvq) { c->n = x >> 31; c->z = (x >> 30) & 1; c->c = (x >> 29) & 1; c->v = (x >> 28) & 1; c->q = (x >> 27) & 1; }
	if (g) c->ge = (x >> 16) & 15;
}
/* the stack pointers: r[13] is the one in use; switching moves it */
static inline int sp_is_psp (cm33_t *c) { return c->ipsr == 0 && (c->control & 2); }
static void sp_bank (cm33_t *c, int use_psp)
{
	int now = sp_is_psp (c);
	if (now == use_psp) return;
	if (now) { c->psp = c->r[13]; c->r[13] = c->msp; } else { c->msp = c->r[13]; c->r[13] = c->psp; }
}
static void set_control_spsel (cm33_t *c, int spsel)
{
	int was = sp_is_psp (c);
	uint32_t cur = c->r[13];
	c->control = (c->control & ~2u) | (spsel ? 2 : 0);
	int now = sp_is_psp (c);
	if (was != now) {
		if (was) { c->psp = cur; c->r[13] = c->msp; } else { c->msp = cur; c->r[13] = c->psp; }
	}
}

static int exc_prio (cm33_t *c, int e)            /* group priority of exception e (lower = more urgent) */
{
	if (e == 2) return -2;
	if (e == 3) return -1;
	if (e >= 4 && e < 16) return (int) ((c->shpr[(e - 4) >> 2] >> (((e - 4) & 3) * 8)) & 0xff);
	if (e >= 16 && e < 16 + CM33_NIRQ) return c->nvic_pri[e - 16];
	return 256;
}
static int exc_active (cm33_t *c, int e)
{
	if (e < 16) return c->active_exc[e] != 0;
	e -= 16; return (c->nvic_act[e >> 5] >> (e & 31)) & 1;
}
static void exc_set_active (cm33_t *c, int e, int on)
{
	if (e < 16) { c->active_exc[e] = on; return; }
	e -= 16;
	if (on) c->nvic_act[e >> 5] |= 1u << (e & 31); else c->nvic_act[e >> 5] &= ~(1u << (e & 31));
}
static int exec_prio (cm33_t *c)                  /* the current execution priority */
{
	int p = 256;
	for (int e = 2; e < 16; e++) if (c->active_exc[e]) { int q = exc_prio (c, e); if (q < p) p = q; }
	for (int i = 0; i < 2; i++) if (c->nvic_act[i])
		for (int b = 0; b < 32; b++) if ((c->nvic_act[i] >> b) & 1) { int q = c->nvic_pri[i * 32 + b]; if (q < p) p = q; }
	if (c->basepri && (int) (c->basepri & 0xff) < p) p = c->basepri & 0xff;
	if (c->primask & 1) p = p < 0 ? p : 0;
	if (c->faultmask & 1) p = -1;
	return p;
}
/* the most urgent pending exception that may preempt now, or 0 */
static int exc_pending (cm33_t *c)
{
	if (!c->icsr_pend && !(c->nvic_pend[0] & c->nvic_en[0]) && !(c->nvic_pend[1] & c->nvic_en[1])) return 0;
	int best = 0, bp = exec_prio (c);
	if (c->icsr_pend & (1u << 28)) { int q = exc_prio (c, 14); if (q < bp) { bp = q; best = 14; } }
	if (c->icsr_pend & (1u << 26)) { int q = exc_prio (c, 15); if (q < bp) { bp = q; best = 15; } }
	for (int i = 0; i < 2; i++) {
		uint32_t m = c->nvic_pend[i] & c->nvic_en[i];
		while (m) {
			int b = 0; while (!((m >> b) & 1)) b++;
			m &= ~(1u << b);
			int irq = i * 32 + b, q = c->nvic_pri[irq];
			if (q < bp) { bp = q; best = 16 + irq; }
		}
	}
	return best;
}
/* (VT_WAKELOG: where a core sleeps on WFE, and when) */
static void WFE_LOG (cm33_t *c)
{
	static int on = -1; static unsigned n;
	if (on < 0) on = getenv ("VT_WAKELOG") != NULL;
	if (on && n++ < 400) fprintf (stderr, "wfe: core %d pc %08X t %.4f%c", c->core, c->r[15], (double) c->cycles / 150e6, 10);
}
static int any_wake (cm33_t *c)                   /* an interrupt pending at all (WFI wakes even when masked) */
{
	if (c->icsr_pend & ((1u << 28) | (1u << 26))) return 1;
	return ((c->nvic_pend[0] & c->nvic_en[0]) | (c->nvic_pend[1] & c->nvic_en[1])) != 0;
}

static void exc_entry (cm33_t *c, int e, uint32_t ret)
{
	int fp = (c->control & 4) != 0;
	uint32_t frame = fp ? 0x68 : 0x20;
	uint32_t sp = c->r[13];
	uint32_t align = (sp - frame) & 4;
	sp = (sp - frame) & ~7u;
	uint32_t x = xpsr_get (c);
	if (align) x |= 1u << 9; else x &= ~(1u << 9);
	WR (sp + 0, c->r[0], 4); WR (sp + 4, c->r[1], 4); WR (sp + 8, c->r[2], 4); WR (sp + 12, c->r[3], 4);
	WR (sp + 16, c->r[12], 4); WR (sp + 20, c->r[14], 4); WR (sp + 24, ret, 4); WR (sp + 28, x, 4);
	if (fp) {
		for (int i = 0; i < 16; i++) WR (sp + 32 + 4 * i, c->s[i], 4);
		WR (sp + 96, c->fpscr, 4); WR (sp + 100, 0, 4);
	}
	c->r[13] = sp;
	int thread = c->ipsr == 0;
	uint32_t lr = 0xffffff80u | 0x40 | 0x20 | (fp ? 0 : 0x10) | (thread ? 8 : 0) | ((thread && (c->control & 2)) ? 4 : 0) | 1;
	/* now handler mode on the main stack */
	if (sp_is_psp (c)) { c->psp = sp; c->r[13] = c->msp; }
	c->control &= ~4u;                              /* FPCA */
	c->ipsr = (uint32_t) e;
	c->r[14] = lr;
	exc_set_active (c, e, 1);
	if (e == 14) c->icsr_pend &= ~(1u << 28);
	if (e == 15) c->icsr_pend &= ~(1u << 26);
	if (e >= 16) { int i = e - 16; AT_AND32 (&c->nvic_pend[i >> 5], ~(1u << (i & 31))); }
	c->it = 0;
	uint32_t pc = RD (c->vtor + 4 * (uint32_t) e, 4);
	c->r[15] = pc & ~1u;
	c->excl = 0;
	c->sleeping = 0;
	c->wait_on = 0; c->spin_slow = 0;
	c->cycles += 12;
	c->attn = 1;
}
static void exc_return (cm33_t *c, uint32_t er)
{
	int e = (int) c->ipsr;
	c->attn = 1;                                                    /* (a pending one may be taken now) */
	exc_set_active (c, e, 0);
	int thread = (er >> 3) & 1, psp = (er >> 2) & 1, basic = (er >> 4) & 1;
	/* the stack to pop from */
	uint32_t sp;
	if (thread && psp) { c->msp = c->r[13]; sp = c->psp; } else sp = c->r[13];
	uint32_t x;
	c->r[0] = RD (sp, 4); c->r[1] = RD (sp + 4, 4); c->r[2] = RD (sp + 8, 4); c->r[3] = RD (sp + 12, 4);
	c->r[12] = RD (sp + 16, 4); c->r[14] = RD (sp + 20, 4);
	uint32_t pc = RD (sp + 24, 4); x = RD (sp + 28, 4);
	uint32_t frame = 0x20;
	if (!basic) {
		for (int i = 0; i < 16; i++) c->s[i] = RD (sp + 32 + 4 * i, 4);
		c->fpscr = RD (sp + 96, 4);
		frame = 0x68;
	}
	sp += frame;
	if (x & (1u << 9)) sp |= 4;
	c->ipsr = x & 0x1ff;
	if (!thread) c->ipsr = c->ipsr ? c->ipsr : 1;   /* (back to a handler: it keeps its number) */
	c->control = (c->control & ~4u) | (basic ? 0 : 4);
	if (thread) {
		c->ipsr = 0;
		c->control = (c->control & ~2u) | (psp ? 2 : 0);
		if (psp) { c->psp = sp; c->r[13] = sp; } else { c->msp = sp; c->r[13] = sp; }
		if (psp) c->r[13] = c->psp;
	} else {
		c->r[13] = sp; c->msp = sp;
	}
	if (thread && psp) { /* r13 is the PSP now; the MSP kept */ }
	apsr_set (c, x, 1, 1);
	c->it = ((x >> 8) & 0xfc) | ((x >> 25) & 3);
	c->r[15] = pc & ~1u;
	c->excl = 0;
	c->cycles += 10;
}

/* a branch through a register (BX, BLX, a load to PC): interworking, exception return */
static inline void bx_write_pc (cm33_t *c, uint32_t a, uint32_t ip)
{
	if (c->ipsr && (a >> 24) == 0xff) { exc_return (c, a); return; }
	if ((a >> 24) == 0xfe) { c->r[15] = a & ~1u; return; }   /* (FNC_RETURN: not used here) */
	if (!(a & 1)) { fault (c, "branch to ARM state", ip, a); return; }
	c->r[15] = a & ~1u;
}

void cm33_reset (cm33_t *c, uint32_t vtor)
{
	cm33_sys_t *s = c->sys; void *u = c->user; int core = c->core;
	memset (c, 0, sizeof *c);
	c->sys = s; c->user = u; c->core = core;
	c->vtor = vtor;
	c->msp = RD (vtor, 4);
	c->r[13] = c->msp;
	c->r[15] = RD (vtor + 4, 4) & ~1u;
	c->r[14] = 0xffffffffu;
	c->ccr = 0x201;
	c->bkpt_stop = 1;
	excl_cores[core & 1] = c;
}

void cm33_irq (cm33_t *c, int n, int level)
{
	if (n < 0 || n >= CM33_NIRQ) return;
	if (level) {
		/* SCR.SEVONPEND: an interrupt that becomes pending - even a disabled one - is an event (wakes a WFE) */
		if ((c->scr & 0x10) && !(c->nvic_pend[n >> 5] & (1u << (n & 31)))) c->event = 1;
		AT_OR32 (&c->nvic_pend[n >> 5], 1u << (n & 31)); c->attn = 1;
	}
	/* (level-sensitive lines stay pending while asserted: the system calls again; a falling line leaves
	 * the pending bit as the NVIC would until it is taken or cleared) */
}

/* ---------------------------------------------------------------- the system control space */
uint32_t cm33_scs_read (cm33_t *c, uint32_t a)
{
	switch (a) {
	case 0xe000e004: return 1;                                   /* ICTR */
	case 0xe000e010: { uint32_t v = c->syst_csr; c->syst_csr &= ~0x10000u; return v; }
	case 0xe000e014: return c->syst_rvr;
	case 0xe000e018: return c->syst_cvr;
	case 0xe000e01c: return 0x40000000u | 0x2710;                /* CALIB: no reference skew */
	case 0xe000ed00: return 0x411fd210u;                         /* CPUID: Cortex-M33 r1p0 */
	case 0xe000ed04: { int e = exc_pending (c);
	                   return (c->icsr_pend & ((1u << 28) | (1u << 26))) | (c->ipsr & 0x1ff) | ((uint32_t) e << 12)
	                          | (any_wake (c) ? (1u << 22) : 0); }
	case 0xe000ed08: return c->vtor;
	case 0xe000ed0c: return 0xfa050000u | (c->aircr & 0x700);
	case 0xe000ed10: return c->scr;
	case 0xe000ed14: return c->ccr;
	case 0xe000ed18: return c->shpr[0];
	case 0xe000ed1c: return c->shpr[1];
	case 0xe000ed20: return c->shpr[2];
	case 0xe000ed24: { uint32_t v = 0;                           /* SHCSR */
	                   if (c->active_exc[11]) v |= 1u << 7; if (c->active_exc[14]) v |= 1u << 10;
	                   if (c->active_exc[15]) v |= 1u << 11; return v; }
	case 0xe000ed88: return c->cpacr;
	case 0xe000ef34: return c->fpccr;
	case 0xe000ef38: return c->fpcar;
	case 0xe000ef3c: return 0;                                   /* FPDSCR */
	case 0xe000ef40: return 0x10110021u;                         /* MVFR0: single precision */
	case 0xe000ef44: return 0x11000011u;
	case 0xe000ef48: return 0x00000040u;
	}
	if (a >= 0xe000e100 && a < 0xe000e108) return c->nvic_en[(a - 0xe000e100) >> 2];
	if (a >= 0xe000e180 && a < 0xe000e188) return c->nvic_en[(a - 0xe000e180) >> 2];
	if (a >= 0xe000e200 && a < 0xe000e208) return c->nvic_pend[(a - 0xe000e200) >> 2];
	if (a >= 0xe000e280 && a < 0xe000e288) return c->nvic_pend[(a - 0xe000e280) >> 2];
	if (a >= 0xe000e300 && a < 0xe000e308) return c->nvic_act[(a - 0xe000e300) >> 2];
	if (a >= 0xe000e400 && a < 0xe000e400 + CM33_NIRQ) {
		unsigned i = a - 0xe000e400;
		return c->nvic_pri[i] | (c->nvic_pri[i + 1] << 8) | (c->nvic_pri[i + 2] << 16) | ((uint32_t) c->nvic_pri[i + 3] << 24);
	}
	return 0;
}
void cm33_scs_write (cm33_t *c, uint32_t a, uint32_t v)
{
	switch (a) {
	case 0xe000e010: c->syst_csr = (c->syst_csr & 0x10000u) | (v & 7); return;
	case 0xe000e014: c->syst_rvr = v & 0xffffff; return;
	case 0xe000e018: c->syst_cvr = 0; c->syst_csr &= ~0x10000u; return;
	case 0xe000ed04:
		if (v & (1u << 28)) c->icsr_pend |= 1u << 28;
		if (v & (1u << 27)) c->icsr_pend &= ~(1u << 28);
		if (v & (1u << 26)) c->icsr_pend |= 1u << 26;
		if (v & (1u << 25)) c->icsr_pend &= ~(1u << 26);
		return;
	case 0xe000ed08: c->vtor = v & ~0x7fu; return;
	case 0xe000ed0c:
		if ((v >> 16) == 0x05fa) {
			c->aircr = v & 0x700;
			if (v & 4) { if (c->sys->event) c->sys->event (c, CM33_EV_RESET); }
		}
		return;
	case 0xe000ed10: c->scr = v & 0x1e; return;
	case 0xe000ed14: c->ccr = v; return;
	case 0xe000ed18: c->shpr[0] = v & 0xf0f0f0f0u; return;
	case 0xe000ed1c: c->shpr[1] = v & 0xf0000000u; return;
	case 0xe000ed20: c->shpr[2] = v & 0xf0f00000u; return;
	case 0xe000ed88: c->cpacr = v; return;
	case 0xe000ef34: c->fpccr = v; return;
	case 0xe000ef38: c->fpcar = v; return;
	}
	if (getenv ("VT_NVICLOG") && a >= 0xe000e100 && a < 0xe000e188) fprintf (stderr, "nvic: core %d %s %08X = %08X pc %08X t %.3f s%c", c->core, a < 0xe000e180 ? "ISER" : "ICER", a, v, c->last_pc, (double) c->cycles / 150e6, 10);
	if (a >= 0xe000e100 && a < 0xe000e108) { c->nvic_en[(a - 0xe000e100) >> 2] |= v; return; }
	if (a >= 0xe000e180 && a < 0xe000e188) { c->nvic_en[(a - 0xe000e180) >> 2] &= ~v; return; }
	if (a >= 0xe000e200 && a < 0xe000e208) { AT_OR32 (&c->nvic_pend[(a - 0xe000e200) >> 2], v); return; }
	if (a >= 0xe000e280 && a < 0xe000e288) { AT_AND32 (&c->nvic_pend[(a - 0xe000e280) >> 2], ~v); return; }
	if (a >= 0xe000e400 && a < 0xe000e400 + CM33_NIRQ) {
		unsigned i = a - 0xe000e400;
		for (int k = 0; k < 4; k++) if (i + k < CM33_NIRQ) c->nvic_pri[i + k] = (uint8_t) ((v >> (8 * k)) & 0xf0);
		return;
	}
	if (a == 0xe000ef00) { int irq = (int) (v & 0x1ff); if (irq < CM33_NIRQ) AT_OR32 (&c->nvic_pend[irq >> 5], 1u << (irq & 31)); }  /* STIR */
}

/* ---------------------------------------------------------------- floating point */
static inline float fs (uint32_t b) { float f; memcpy (&f, &b, 4); return f; }
static inline uint32_t fb (float f) { uint32_t b; memcpy (&b, &f, 4); return b; }
static inline uint32_t fp_default_nan (uint32_t r, float f) { (void) f; return r; }
static void fp_cmp (cm33_t *c, float a, float b)
{
	uint32_t f;
	if (isnan (a) || isnan (b)) f = 0x3; else if (a == b) f = 0x6; else if (a < b) f = 0x8; else f = 0x2;
	c->fpscr = (c->fpscr & 0x0fffffffu) | (f << 28);
}
static float fp_round (float x, int mode)          /* 0 nearest even, 1 +inf, 2 -inf, 3 zero, 4 nearest away */
{
	switch (mode) {
	case 0: return nearbyintf (x);
	case 1: return ceilf (x);
	case 2: return floorf (x);
	case 3: return truncf (x);
	default: return roundf (x);
	}
}
static uint32_t fp_to_int (float x, int mode, int sgn)
{
	if (isnan (x)) return 0;
	double r = fp_round (x, mode);
	if (sgn) { if (r >= 2147483647.0) return 0x7fffffffu; if (r <= -2147483648.0) return 0x80000000u; return (uint32_t) (int32_t) r; }
	if (r >= 4294967295.0) return 0xffffffffu; if (r <= 0) return 0; return (uint32_t) r;
}
static float vfp_expand_imm (uint32_t imm8)
{
	uint32_t sign = (imm8 >> 7) & 1, b6 = (imm8 >> 6) & 1;
	uint32_t exp = ((b6 ^ 1) << 7) | (b6 ? 0x7c : 0) | ((imm8 >> 4) & 3);
	uint32_t bits = (sign << 31) | (exp << 23) | ((imm8 & 15) << 19);
	return fs (bits);
}
static inline uint16_t f2h (float f)
{
	uint32_t x = fb (f), s = (x >> 16) & 0x8000; int e = (int) ((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff;
	if (((x >> 23) & 0xff) == 0xff) return (uint16_t) (s | 0x7c00 | (m ? 0x200 : 0));
	if (e >= 31) return (uint16_t) (s | 0x7c00);
	if (e <= 0) { if (e < -10) return (uint16_t) s; m |= 0x800000; return (uint16_t) (s | (m >> (14 - e))); }
	return (uint16_t) (s | (e << 10) | (m >> 13));
}
static inline float h2f (uint16_t h)
{
	uint32_t s = (uint32_t) (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3ff;
	if (e == 0) { float f = ldexpf ((float) m, -24); return s ? -f : f; }
	if (e == 31) return fs (s | 0x7f800000u | (m << 13));
	return fs (s | ((e - 15 + 127) << 23) | (m << 13));
}

/* VFP data processing (single precision); ins = hw1 << 16 | hw2; returns 0 when undefined */
static int vfp_dp (cm33_t *c, uint32_t ins)
{
	if (((ins >> 8) & 15) != 10) return 0;             /* only single precision on this FPU */
	unsigned D = (ins >> 22) & 1, N = (ins >> 7) & 1, M = (ins >> 5) & 1;
	unsigned d = ((ins >> 12) & 15) << 1 | D, n = ((ins >> 16) & 15) << 1 | N, m = (ins & 15) << 1 | M;
	unsigned opc1 = (ins >> 20) & 0xb, opc2 = (ins >> 16) & 15, opc3 = (ins >> 6) & 3;
	float a = fs (c->s[n]), b = fs (c->s[m]), dd = fs (c->s[d]), r;
	int op = (ins >> 6) & 1;
	switch (opc1) {
	case 0x0: r = a * b; c->s[d] = fb (op ? dd - r : dd + r); return 1;           /* VMLA / VMLS */
	case 0x1: r = a * b; c->s[d] = fb (op ? -dd - r : -dd + r); return 1;          /* VNMLA / VNMLS */
	case 0x2: r = a * b; c->s[d] = fb (op ? -r : r); return 1;                     /* VMUL / VNMUL */
	case 0x3: c->s[d] = fb (op ? a - b : a + b); return 1;                         /* VADD / VSUB */
	case 0x8: if (op) return 0; c->s[d] = fb (a / b); return 1;                    /* VDIV */
	case 0x9: c->s[d] = fb (op ? fmaf (-a, b, -dd) : fmaf (a, b, -dd)); return 1;  /* VFNMA / VFNMS */
	case 0xa: c->s[d] = fb (op ? fmaf (-a, b, dd) : fmaf (a, b, dd)); return 1;    /* VFMS / VFMA */
	case 0xb: break;
	default: return 0;
	}
	if ((opc3 & 1) == 0) { c->s[d] = fb (vfp_expand_imm (((ins >> 12) & 0xf0) | (ins & 15))); return 1; }   /* VMOV imm */
	switch (opc2) {
	case 0: c->s[d] = opc3 == 1 ? c->s[m] : c->s[m] & 0x7fffffffu; return 1;      /* VMOV / VABS */
	case 1: c->s[d] = opc3 == 1 ? c->s[m] ^ 0x80000000u : fb (sqrtf (b)); return 1;   /* VNEG / VSQRT */
	case 2: case 3: {                                                                  /* VCVTB / VCVTT (half) */
		int top = (ins >> 7) & 1;
		if (opc2 == 2) c->s[d] = fb (h2f ((uint16_t) (top ? c->s[m] >> 16 : c->s[m])));
		else { uint16_t h = f2h (b); c->s[d] = top ? (c->s[d] & 0xffff) | ((uint32_t) h << 16) : (c->s[d] & 0xffff0000u) | h; }
		return 1;
	}
	case 4: case 5: fp_cmp (c, dd, opc2 == 5 ? 0.0f : b); return 1;              /* VCMP(E) */
	case 6: { int mode = (c->fpscr >> 22) & 3; static const int mm[4] = { 0, 1, 2, 3 };   /* VRINTR / VRINTZ */
	          c->s[d] = fb (fp_round (b, (ins >> 7) & 1 ? 3 : mm[mode])); return 1; }
	case 7: { int mode = (c->fpscr >> 22) & 3; c->s[d] = fb (fp_round (b, mode)); return 1; }  /* VRINTX */
	case 8: c->s[d] = fb ((ins >> 7) & 1 ? (float) (int32_t) c->s[m] : (float) c->s[m]); return 1;  /* VCVT int -> f */
	case 10: case 11: case 14: case 15: {                                              /* VCVT fixed point */
		int tofix = (opc2 & 4) != 0, uns = opc2 & 1, sx = (ins >> 7) & 1;
		unsigned size = sx ? 32 : 16, imm = ((ins & 15) << 1) | ((ins >> 5) & 1), fbits = size - imm;
		if (tofix) {
			float x = ldexpf (dd, (int) fbits);
			uint32_t v = fp_to_int (x, 3, !uns);
			if (size == 16) { int sat = 0; v = uns ? sat_u ((int64_t) v, 16, &sat) : (uint32_t) sat_s ((int32_t) v, 16, &sat); }
			c->s[d] = v;
		} else {
			uint32_t v = c->s[d];
			double x = uns ? (double) (size == 16 ? (v & 0xffff) : v) : (double) (size == 16 ? (int16_t) v : (int32_t) v);
			c->s[d] = fb ((float) ldexp (x, -(int) fbits));
		}
		return 1;
	}
	case 12: case 13: {                                                                /* VCVT(R) f -> int */
		int mode = (ins >> 7) & 1 ? 3 : (int) ((c->fpscr >> 22) & 3);
		c->s[d] = fp_to_int (b, mode, opc2 & 1);
		return 1;
	}
	}
	return 0;
}
/* the unconditional FP encodings (v8): VSEL, VMAXNM / VMINNM, VRINTA/N/P/M, VCVTA/N/P/M */
static int vfp_v8 (cm33_t *c, uint32_t ins)
{
	if (((ins >> 8) & 15) != 10) return 0;
	unsigned D = (ins >> 22) & 1, N = (ins >> 7) & 1, M = (ins >> 5) & 1;
	unsigned d = ((ins >> 12) & 15) << 1 | D, n = ((ins >> 16) & 15) << 1 | N, m = (ins & 15) << 1 | M;
	if ((ins & 0xff800f50u) == 0xfe000a00u) {                                          /* VSEL */
		static const unsigned cc[4] = { 0, 6, 10, 12 };   /* EQ VS GE GT */
		c->s[d] = cond_pass (c, cc[(ins >> 20) & 3]) ? c->s[n] : c->s[m];
		return 1;
	}
	if ((ins & 0xffb00f10u) == 0xfe800a00u) {                                          /* VMAXNM / VMINNM */
		float a = fs (c->s[n]), b = fs (c->s[m]);
		float r = (ins >> 6) & 1 ? fminf (a, b) : fmaxf (a, b);
		c->s[d] = fb (r);
		return 1;
	}
	if ((ins & 0xffbc0fd0u) == 0xfeb80a40u) {                                          /* VRINTA/N/P/M */
		static const int mode[4] = { 4, 0, 1, 2 };
		c->s[d] = fb (fp_round (fs (c->s[m]), mode[(ins >> 16) & 3]));
		return 1;
	}
	if ((ins & 0xffbc0f50u) == 0xfebc0a40u) {                                          /* VCVTA/N/P/M */
		static const int mode[4] = { 4, 0, 1, 2 };
		c->s[d] = fp_to_int (fs (c->s[m]), mode[(ins >> 16) & 3], (ins >> 7) & 1);
		return 1;
	}
	return 0;
}

/* coprocessor space (hw1 = 111x 11xx xxxx xxxx); returns 0 when undefined */
static int copro (cm33_t *c, uint32_t hw1, uint32_t hw2, uint32_t ip)
{
	uint32_t ins = (hw1 << 16) | hw2;
	unsigned cp = (hw2 >> 8) & 15, op1 = (hw1 >> 4) & 0x3f, Rn = hw1 & 15, Rt = hw2 >> 12;
	if ((hw1 & 0xff00) == 0xfe00 && (cp & 14) == 10 && !(hw2 & 0x10)) return vfp_v8 (c, ins);
	if ((cp & 14) == 10) {
		/* FP: extension register load / store, transfers, data processing */
		if ((op1 & 0x3e) == 0x04) {                                               /* 64-bit transfers */
			unsigned m = ((hw2 & 15) << 1) | ((hw2 >> 5) & 1), Rt2 = hw1 & 15;
			if (cp == 11) m = (((hw2 >> 5) & 1) << 4 | (hw2 & 15)) * 2;
			if (op1 & 1) { c->r[Rt] = c->s[m]; c->r[Rt2] = c->s[m + 1]; }
			else { c->s[m] = c->r[Rt]; c->s[m + 1] = c->r[Rt2]; }
			return 1;
		}
		if ((op1 & 0x20) == 0) {                                                   /* load / store */
			unsigned P = (op1 >> 4) & 1, U = (op1 >> 3) & 1, D = (op1 >> 2) & 1, W = (op1 >> 1) & 1, L = op1 & 1;
			unsigned Vd = (hw2 >> 12) & 15, imm8 = hw2 & 0xff;
			unsigned d = cp == 11 ? ((D << 4) | Vd) * 2 : (Vd << 1) | D;
			uint32_t base = Rn == 15 ? ((ip + 4) & ~3u) : c->r[Rn];
			if (P && !W) {                                                         /* VLDR / VSTR */
				uint32_t a = U ? base + imm8 * 4 : base - imm8 * 4;
				int words = cp == 11 ? 2 : 1;
				for (int i = 0; i < words; i++)
					if (L) c->s[d + i] = RD (a + 4 * i, 4); else WR (a + 4 * i, c->s[d + i], 4);
				c->cycles += words;
				return 1;
			}
			/* VLDM / VSTM (VPUSH / VPOP) */
			unsigned words = imm8;
			if (cp == 11 && (imm8 & 1)) words = imm8 - 1;                          /* (FLDMX: one extra word) */
			uint32_t a = U ? base : base - imm8 * 4;
			for (unsigned i = 0; i < words; i++)
				if (L) c->s[(d + i) & 31] = RD (a + 4 * i, 4); else WR (a + 4 * i, c->s[(d + i) & 31], 4);
			if (W) c->r[Rn] = U ? base + imm8 * 4 : base - imm8 * 4;
			c->cycles += words;
			return 1;
		}
		if ((op1 & 0x30) == 0x20 && (hw2 & 0x10)) {                                   /* 32-bit transfers */
			unsigned L = op1 & 1, A = (op1 >> 1) & 7, C = (hw2 >> 8) & 1;
			if (C == 0) {
				unsigned n = ((hw1 & 15) << 1) | ((hw2 >> 7) & 1);
				if (A == 0) { if (L) c->r[Rt] = c->s[n]; else c->s[n] = c->r[Rt]; return 1; }
				if (A == 7) {
					if (L) {
						if (Rt == 15) { c->n = c->fpscr >> 31; c->z = (c->fpscr >> 30) & 1; c->c = (c->fpscr >> 29) & 1; c->v = (c->fpscr >> 28) & 1; }
						else c->r[Rt] = (hw1 & 15) == 1 ? c->fpscr : 0;
					} else if ((hw1 & 15) == 1) c->fpscr = c->r[Rt];
					return 1;
				}
				return 0;
			}
			/* VMOV between a core register and half of a D register */
			unsigned dreg = (((hw2 >> 7) & 1) << 4) | (hw1 & 15), h = (hw1 >> 5) & 1;
			if (L) c->r[Rt] = c->s[dreg * 2 + h]; else c->s[dreg * 2 + h] = c->r[Rt];
			return 1;
		}
		if ((op1 & 0x30) == 0x20 && !(hw2 & 0x10)) return vfp_dp (c, ins);
		return 0;
	}
	/* other coprocessors: the system's */
	if (!c->sys->cp) return 0;
	if ((op1 & 0x3e) == 0x04) {                                                   /* MCRR / MRRC */
		uint32_t rt = c->r[Rt], rt2 = c->r[Rn];
		int kind = (op1 & 1) ? CM33_CP_MRRC : CM33_CP_MCRR;
		if (c->sys->cp (c, kind, (int) cp, (hw2 >> 4) & 15, 0, hw2 & 15, 0, &rt, &rt2) < 0) return 0;
		if (kind == CM33_CP_MRRC) { c->r[Rt] = rt; c->r[Rn] = rt2; }
		return 1;
	}
	if ((op1 & 0x30) == 0x20) {
		int opc1 = (hw1 >> 5) & 7, crn = hw1 & 15, crm = hw2 & 15, opc2 = (hw2 >> 5) & 7;
		if (hw2 & 0x10) {
			int kind = (hw1 >> 4) & 1 ? CM33_CP_MRC : CM33_CP_MCR;
			uint32_t rt = Rt == 15 ? 0 : c->r[Rt], rt2 = 0;
			if (c->sys->cp (c, kind, (int) cp, opc1, crn, crm, opc2, &rt, &rt2) < 0) return 0;
			if (kind == CM33_CP_MRC) {
				if (Rt == 15) { c->n = rt >> 31; c->z = (rt >> 30) & 1; c->c = (rt >> 29) & 1; c->v = (rt >> 28) & 1; }
				else c->r[Rt] = rt;
			}
			return 1;
		}
		uint32_t rt = 0, rt2 = 0;
		return c->sys->cp (c, CM33_CP_CDP, (int) cp, (hw1 >> 4) & 15, crn, crm, opc2, &rt, &rt2) >= 0;
	}
	return 0;
}

/* ---------------------------------------------------------------- DSP helpers */
static uint32_t par_addsub (cm33_t *c, unsigned op1, unsigned op2, uint32_t a, uint32_t b)
{
	/* op1 (hw1[6:4]): 000 ADD8 / 001 ADD16 / 010 ASX / 100 SUB8 / 101 SUB16 / 110 SAX;
	 * op2 (hw2[6:4]): 000 S, 001 Q, 010 SH, 100 U, 101 UQ, 110 UH */
	int uns = (op2 >> 2) & 1, kind = op2 & 3;   /* 0 plain (GE), 1 saturating, 2 halving */
	uint32_t r = 0, ge = 0;
	int is8 = (op1 & 3) == 0;
	int lanes = is8 ? 4 : 2, w = is8 ? 8 : 16;
	for (int i = 0; i < lanes; i++) {
		int64_t x, y, s;
		uint32_t m = (1u << w) - 1;
		uint32_t ax = (a >> (i * w)) & m, by = (b >> (i * w)) & m;
		if (!is8 && (op1 & 3) != 1) {                      /* the exchanging forms: ASX / SAX */
			by = (b >> ((1 - i) * 16)) & 0xffff;
		}
		if (uns) { x = ax; y = by; } else { x = is8 ? (int8_t) ax : (int16_t) ax; y = is8 ? (int8_t) by : (int16_t) by; }
		int sub;
		if (is8) sub = (op1 >> 2) & 1;
		else if ((op1 & 3) == 1) sub = (op1 >> 2) & 1;       /* ADD16 / SUB16 */
		else sub = (op1 == 2) ? (i == 0) : (i == 1);         /* ASX: low lane subtracts; SAX: high lane */
		s = sub ? x - y : x + y;
		int64_t outv;
		if (kind == 1) { int sat = 0; outv = uns ? (int64_t) sat_u (s, (unsigned) w, &sat) : (int64_t) sat_s (s, (unsigned) w, &sat); }
		else if (kind == 2) outv = s >> 1;
		else {
			outv = s;
			if (uns) { if (sub ? s >= 0 : s >= (1 << w)) ge |= (is8 ? 1u : 3u) << (i * (is8 ? 1 : 2)); }
			else if (s >= 0) ge |= (is8 ? 1u : 3u) << (i * (is8 ? 1 : 2));
		}
		r |= ((uint32_t) outv & m) << (i * w);
	}
	if (kind == 0) c->ge = ge;
	return r;
}

/* ---------------------------------------------------------------- one instruction */
#define REG(n) ((n) == 15 ? ip + 4 : c->r[n])

static void ldm_write_pc (cm33_t *c, uint32_t v, uint32_t ip) { bx_write_pc (c, v, ip); }

/* after each instruction: the IT state advances, short loops are watched (spins) */
/* a counting loop's compares with constants (CMP / CMN, 16 and 32 bit): values its counters must not be moved past */
static void spin_bounds (cm33_t *c)
{
	c->spin_nb = 0;
	for (uint32_t a = c->spin_lo; a <= c->spin_hi && c->spin_nb < 4; ) {
		uint32_t h = fetch16 (c, a);
		if ((h >> 11) >= 0x1d) {
			uint32_t h2 = fetch16 (c, a + 2), co;
			if ((h2 & 0x8f00) == 0x0f00 && ((h & 0xfbf0) == 0xf1b0 || (h & 0xfbf0) == 0xf110)) {
				uint32_t v = expand_imm_c (((h >> 10) & 1) << 11 | ((h2 >> 12) & 7) << 8 | (h2 & 0xff), 2, &co);
				c->spin_breg[c->spin_nb] = (uint8_t) (h & 15); c->spin_bval[c->spin_nb++] = (h & 0xfbf0) == 0xf110 ? 0u - v : v;
			}
			a += 4;
		} else {
			if ((h & 0xf800) == 0x2800) { c->spin_breg[c->spin_nb] = (uint8_t) ((h >> 8) & 7); c->spin_bval[c->spin_nb++] = h & 0xff; }
			a += 2;
		}
	}
}
/* a short backward branch: a round of a loop (perhaps a spin) went round */
static NOINLINE void loop_round (cm33_t *c, uint32_t ip)
{
	if (c->spin_slow) {                                       /* a counting spin: each round measured */
		if (c->r[15] != c->spin_lo || ip != c->spin_hi) { c->spin_slow = 0; c->spin_last = 0; c->spin_stable = 0; c->spin_run = 0; }   /* (out of it) */
		else {
			/* one round done: its register changes and its length, for skipping the rounds of the rest of the slice */
			uint32_t dl[13]; for (int i = 0; i < 13; i++) dl[i] = c->r[i] - c->spin_prev[i];
			uint64_t it = c->cycles - c->spin_last;
			int steady = c->spin_last && it == c->spin_iter && !memcmp (dl, c->spin_delta, sizeof dl)
			             && c->ld_hash == c->prev_ld_hash && c->ld_addr == c->prev_ld_addr && c->ld_val == c->prev_ld_val;
			c->spin_run = steady ? c->spin_run + 1 : 0;
			/* (polling a peripheral's register: steady at once; reading RAM: many rounds alike first, since a scan
			 *  through data, a bitmap say, reads the same word for up to 32 rounds) */
			c->spin_stable = c->spin_run >= (c->ld_addr >= 0x40000000u ? 1 : 40);
			memcpy (c->spin_delta, dl, sizeof dl); memcpy (c->spin_prev, c->r, sizeof c->spin_prev);
			c->spin_iter = it; c->spin_last = c->cycles;
			if (c->spin_stable) c->poll_skip = 1, c->attn = 1;      /* (steady: it may sleep, its rounds counted on) */
		}
	}
	{
		if (c->r[15] == c->loop_target && !c->loop_stores && c->loop_loads && c->ld_addr == c->prev_ld_addr && c->ld_val == c->prev_ld_val
		    && c->ld_hash == c->prev_ld_hash) {                       /* (all its loads the same as last round: not work on data) */
			int one = c->loop_loads == 1;                              /* (a single value read: it alone can end the loop) */
			int same = !memcmp (c->loop_regs, c->r, sizeof c->loop_regs);
			if (++c->loop_iters >= 4) {
				if (same && one && cm33_spin_mode >= 2) {             /* a pure spin on one value: wait until it changes */
					c->wait_on = 1; c->wait_addr = c->ld_addr; c->wait_val = c->ld_val; c->wait_n = c->ld_n;
				} else if (same && cm33_spin_mode >= 2 && c->ldl_on && c->ldl_cnt == (int) c->loop_loads && c->ldl_cnt <= CM33_WL) {
					/* a pure spin on a few values (no register changes, no stores: what it does next depends on
					 * what it reads alone) - wait until one of them changes */
					c->wait_on = 3; c->wl_cnt = c->ldl_cnt;
					memcpy (c->wl_a, c->ldl_a, sizeof c->wl_a); memcpy (c->wl_v, c->ldl_v, sizeof c->wl_v); memcpy (c->wl_n, c->ldl_n, sizeof c->wl_n);
					c->wait_addr = c->ld_addr; c->wait_val = c->ld_val; c->wait_n = c->ld_n;
				} else if (!c->spin_slow || c->spin_lo != c->r[15] || c->spin_hi != ip) {   /* (a new counting loop) */
				         c->spin_slow = 1; c->spin_lo = c->r[15]; c->spin_hi = ip; c->spin_run = 0; spin_bounds (c);
				         if (cm33_spin_log) { static uint32_t seen[256]; static int ns; int k; for (k = 0; k < ns; k++) if (seen[k] == ip) break;
				           if (k == ns && ns < 256) { seen[ns++] = ip; fprintf (stderr, "slow spin core %d %08X-%08X ld %08X=%08X n%d\n", c->core, c->r[15], ip, c->ld_addr, c->ld_val, c->loop_loads); } } }
				c->poll_skip = 1; c->attn = 1; c->loop_iters = 0;
			}
		} else c->loop_iters = 0;
		if (c->spin_slow && c->spin_last == 0) { memcpy (c->spin_prev, c->r, sizeof c->spin_prev); c->spin_last = c->cycles; c->spin_stable = 0; }
		memcpy (c->loop_regs, c->r, sizeof c->loop_regs);
		c->loop_target = c->r[15]; c->prev_ld_addr = c->ld_addr; c->prev_ld_val = c->ld_val;
		c->prev_ld_hash = c->ld_hash; c->ld_hash = 0; c->prev_loop_loads = c->loop_loads;
		c->loop_loads = c->loop_stores = 0;
		c->ldl_on = c->loop_iters >= 3; c->ldl_cnt = 0;                /* (the next round's loads, one by one: it may wait on them) */
	}
}

/* after each instruction: the IT state advances; a short backward branch: loop_round */
static FORCE_INLINE void post (cm33_t *c, uint32_t ip, int in_it)
{
	if (in_it) {
		if ((c->it & 7) == 0) c->it = 0;
		else c->it = (c->it & 0xe0) | ((c->it << 1) & 0x1f);
	}
	if (c->r[15] < ip && ip - c->r[15] <= 64 && cm33_spin_mode) loop_round (c, ip);
}

static FORCE_INLINE void step (cm33_t *c)
{
	uint32_t ip = c->r[15];
	uint32_t hw1 = FETCH (ip);
	c->last_pc = ip;
	int in_it = c->it != 0;
	if (in_it && !cond_pass (c, (c->it >> 4) & 15)) {
		/* skip: but the length of the instruction still counts */
		c->r[15] = ip + (((hw1 >> 11) >= 0x1d) ? 4 : 2);
		c->cycles++;
		goto it_advance;
	}
	c->cycles++;
	if ((hw1 >> 11) < 0x1d) {
		/* ======================== 16-bit */
		c->r[15] = ip + 2;
		uint32_t co, ov, r;
		int sf = !in_it;
		switch (hw1 >> 10) {
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: {  /* LSL / LSR / ASR imm */
			unsigned rd = hw1 & 7, rm = (hw1 >> 3) & 7, imm5 = (hw1 >> 6) & 31, op = (hw1 >> 11) & 3;
			int t; unsigned amt; dec_imm_shift ((int) op, imm5, &t, &amt);
			r = shift_c (c->r[rm], t, amt, c->c, &co);
			c->r[rd] = r;
			if (sf) { nz (c, r); c->c = co; }
			break;
		}
		case 0x06: case 0x07: {                                       /* ADD / SUB reg / imm3 */
			unsigned rd = hw1 & 7, rn = (hw1 >> 3) & 7, x = (hw1 >> 6) & 7;
			uint32_t b = (hw1 & 0x400) ? x : c->r[x];
			if (hw1 & 0x200) r = add_c (c->r[rn], ~b, 1, &co, &ov);
			else r = add_c (c->r[rn], b, 0, &co, &ov);
			c->r[rd] = r;
			if (sf) { nz (c, r); c->c = co; c->v = ov; }
			break;
		}
		case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f: {   /* MOV/CMP/ADD/SUB imm8 */
			unsigned rd = (hw1 >> 8) & 7, imm = hw1 & 0xff;
			switch ((hw1 >> 11) & 3) {
			case 0: c->r[rd] = imm; if (sf) nz (c, imm); break;
			case 1: r = add_c (c->r[rd], ~imm, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov; break;
			case 2: r = add_c (c->r[rd], imm, 0, &co, &ov); c->r[rd] = r; if (sf) { nz (c, r); c->c = co; c->v = ov; } break;
			case 3: r = add_c (c->r[rd], ~imm, 1, &co, &ov); c->r[rd] = r; if (sf) { nz (c, r); c->c = co; c->v = ov; } break;
			}
			break;
		}
		case 0x10: {                                                  /* data processing */
			unsigned rdn = hw1 & 7, rm = (hw1 >> 3) & 7;
			uint32_t a = c->r[rdn], b = c->r[rm];
			co = c->c; ov = c->v;
			switch ((hw1 >> 6) & 15) {
			case 0: r = a & b; c->r[rdn] = r; break;
			case 1: r = a ^ b; c->r[rdn] = r; break;
			case 2: r = shift_c (a, 0, b & 0xff, c->c, &co); c->r[rdn] = r; break;
			case 3: r = shift_c (a, 1, b & 0xff, c->c, &co); c->r[rdn] = r; break;
			case 4: r = shift_c (a, 2, b & 0xff, c->c, &co); c->r[rdn] = r; break;
			case 5: r = add_c (a, b, c->c, &co, &ov); c->r[rdn] = r; break;
			case 6: r = add_c (a, ~b, c->c, &co, &ov); c->r[rdn] = r; break;
			case 7: r = shift_c (a, 3, b & 0xff, c->c, &co); c->r[rdn] = r; break;
			case 8: r = a & b; nz (c, r); c->c = co; goto dp_done;
			case 9: r = add_c (0, ~b, 1, &co, &ov); c->r[rdn] = r; break;   /* RSB #0 */
			case 10: r = add_c (a, ~b, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov; goto dp_done;
			case 11: r = add_c (a, b, 0, &co, &ov); nz (c, r); c->c = co; c->v = ov; goto dp_done;
			case 12: r = a | b; c->r[rdn] = r; break;
			case 13: r = a * b; c->r[rdn] = r; if (sf) nz (c, r); goto dp_done;
			case 14: r = a & ~b; c->r[rdn] = r; break;
			default: r = ~b; c->r[rdn] = r; break;
			}
			if (sf) { nz (c, r); c->c = co; c->v = ov; }
		dp_done:
			break;
		}
		case 0x11: {                                                  /* special data, BX, BLX */
			unsigned op = (hw1 >> 8) & 3, rd = ((hw1 >> 4) & 8) | (hw1 & 7), rm = (hw1 >> 3) & 15;
			if (op == 0) {
				r = REG (rd) + REG (rm);
				if (rd == 15) { c->r[15] = r & ~1u; c->cycles++; } else c->r[rd] = r;
			} else if (op == 1) {
				r = add_c (REG (rd), ~REG (rm), 1, &co, &ov); nz (c, r); c->c = co; c->v = ov;
			} else if (op == 2) {
				r = REG (rm);
				if (rd == 15) { c->r[15] = r & ~1u; c->cycles++; } else c->r[rd] = r;
			} else {
				uint32_t t = REG (rm);
				if (hw1 & 0x80) c->r[14] = (ip + 2) | 1;
				bx_write_pc (c, t, ip);
				c->cycles++;
			}
			break;
		}
		case 0x12: case 0x13: {                                       /* LDR literal */
			unsigned rt = (hw1 >> 8) & 7;
			c->r[rt] = rd_mem (c, ((ip + 4) & ~3u) + (hw1 & 0xff) * 4, 4);
			c->cycles++;
			break;
		}
		case 0x14: case 0x15: case 0x16: case 0x17: {                 /* load / store register offset */
			unsigned rt = hw1 & 7, rn = (hw1 >> 3) & 7, rm = (hw1 >> 6) & 7;
			uint32_t a = c->r[rn] + c->r[rm];
			switch ((hw1 >> 9) & 7) {
			case 0: WR (a, c->r[rt], 4); break;
			case 1: WR (a, c->r[rt], 2); break;
			case 2: WR (a, c->r[rt], 1); break;
			case 3: c->r[rt] = (uint32_t) (int8_t) rd_mem (c, a, 1); break;
			case 4: c->r[rt] = rd_mem (c, a, 4); break;
			case 5: c->r[rt] = rd_mem (c, a, 2); break;
			case 6: c->r[rt] = rd_mem (c, a, 1); break;
			default: c->r[rt] = (uint32_t) (int16_t) rd_mem (c, a, 2); break;
			}
			c->cycles++;
			break;
		}
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: {   /* imm5 word / byte */
			unsigned rt = hw1 & 7, rn = (hw1 >> 3) & 7, imm5 = (hw1 >> 6) & 31;
			int byte = (hw1 >> 12) & 1, load = (hw1 >> 11) & 1;
			uint32_t a = c->r[rn] + (byte ? imm5 : imm5 * 4);
			if (load) c->r[rt] = rd_mem (c, a, byte ? 1 : 4); else WR (a, c->r[rt], byte ? 1 : 4);
			c->cycles++;
			break;
		}
		case 0x20: case 0x21: case 0x22: case 0x23: {                 /* STRH / LDRH imm5 */
			unsigned rt = hw1 & 7, rn = (hw1 >> 3) & 7;
			uint32_t a = c->r[rn] + ((hw1 >> 6) & 31) * 2;
			if (hw1 & 0x800) c->r[rt] = rd_mem (c, a, 2); else WR (a, c->r[rt], 2);
			c->cycles++;
			break;
		}
		case 0x24: case 0x25: case 0x26: case 0x27: {                 /* STR / LDR SP-relative */
			unsigned rt = (hw1 >> 8) & 7;
			uint32_t a = c->r[13] + (hw1 & 0xff) * 4;
			if (hw1 & 0x800) c->r[rt] = rd_mem (c, a, 4); else WR (a, c->r[rt], 4);
			c->cycles++;
			break;
		}
		case 0x28: case 0x29:                                         /* ADR */
			c->r[(hw1 >> 8) & 7] = ((ip + 4) & ~3u) + (hw1 & 0xff) * 4; break;
		case 0x2a: case 0x2b:                                         /* ADD Rd, SP, #imm */
			c->r[(hw1 >> 8) & 7] = c->r[13] + (hw1 & 0xff) * 4; break;
		case 0x2c: case 0x2d: case 0x2e: case 0x2f: {                 /* misc */
			unsigned op = (hw1 >> 5) & 0x7f;
			if ((op & 0x7c) == 0x00) { c->r[13] += (hw1 & 0x7f) * 4; break; }
			if ((op & 0x7c) == 0x04) { c->r[13] -= (hw1 & 0x7f) * 4; break; }
			if ((op & 0x28) == 0x08) {                                /* CBZ / CBNZ */
				unsigned rn = hw1 & 7;
				uint32_t off = (((hw1 >> 9) & 1) << 6) | (((hw1 >> 3) & 31) << 1);
				int nzr = (hw1 >> 11) & 1;
				if ((c->r[rn] != 0) == nzr) { c->r[15] = ip + 4 + off; c->cycles++; }
				break;
			}
			if ((op & 0x78) == 0x10) {                                /* SXTH SXTB UXTH UXTB */
				unsigned rd = hw1 & 7; uint32_t m = c->r[(hw1 >> 3) & 7];
				switch ((hw1 >> 6) & 3) {
				case 0: c->r[rd] = (uint32_t) (int16_t) m; break;
				case 1: c->r[rd] = (uint32_t) (int8_t) m; break;
				case 2: c->r[rd] = m & 0xffff; break;
				default: c->r[rd] = m & 0xff; break;
				}
				break;
			}
			if ((op & 0x70) == 0x20) {                                /* PUSH */
				uint32_t list = (hw1 & 0xff) | ((hw1 & 0x100) ? 0x4000 : 0);
				unsigned cnt = 0; for (int i = 0; i < 16; i++) cnt += (list >> i) & 1;
				uint32_t a = c->r[13] - 4 * cnt;
				c->r[13] = a;
				for (int i = 0; i < 16; i++) if ((list >> i) & 1) { WR (a, c->r[i], 4); a += 4; }
				c->cycles += cnt;
				break;
			}
			if (op == 0x33) {                                        /* CPS */
				int dis = (hw1 >> 4) & 1;
				if (hw1 & 2) c->primask = dis;
				if (hw1 & 1) c->faultmask = dis;
				break;
			}
			if ((op & 0x78) == 0x50) {                                /* REV REV16 - REVSH */
				unsigned rd = hw1 & 7; uint32_t m = c->r[(hw1 >> 3) & 7];
				switch ((hw1 >> 6) & 3) {
				case 0: c->r[rd] = (m >> 24) | ((m >> 8) & 0xff00) | ((m << 8) & 0xff0000) | (m << 24); break;
				case 1: c->r[rd] = ((m >> 8) & 0x00ff00ffu) | ((m << 8) & 0xff00ff00u); break;
				case 3: c->r[rd] = (uint32_t) (int16_t) (((m >> 8) & 0xff) | ((m & 0xff) << 8)); break;
				default: fault (c, "undefined (HLT)", ip, hw1); return;
				}
				break;
			}
			if ((op & 0x70) == 0x60) {                                /* POP */
				uint32_t list = (hw1 & 0xff) | ((hw1 & 0x100) ? 0x8000 : 0);
				uint32_t a = c->r[13];
				unsigned cnt = 0;
				for (int i = 0; i < 15; i++) if ((list >> i) & 1) { c->r[i] = RD (a, 4); a += 4; cnt++; }
				if (list & 0x8000) { uint32_t v = RD (a, 4); a += 4; c->r[13] = a; ldm_write_pc (c, v, ip); cnt += 2; }
				else c->r[13] = a;
				c->cycles += cnt;
				break;
			}
			if ((op & 0x78) == 0x70) {                                /* BKPT */
				if (ip < 0x8000 && cm33_bkpt_hle && cm33_bkpt_hle (c, hw1 & 0xff)) break;   /* (the bootrom's services) */
				if (c->bkpt_stop) { fault (c, "BKPT", ip, hw1); c->r[15] = ip; return; }
				break;
			}
			if ((op & 0x78) == 0x78) {                                /* IT, hints */
				if (hw1 & 15) { c->it = hw1 & 0xff; return; }      /* (the IT itself does not advance the state) */
				switch ((hw1 >> 4) & 15) {
				case 2: if (c->event) c->event = 0; else { c->sleeping = 2; WFE_LOG (c); } break;   /* WFE */
				case 3: c->sleeping = 1; break;                                     /* WFI */
				case 4: if (c->sys->event) c->sys->event (c, CM33_EV_SEV); break;
				}
				break;
			}
			fault (c, "undefined 16-bit", ip, hw1);
			return;
		}
		case 0x30: case 0x31: case 0x32: case 0x33: {                 /* STM / LDM */
			unsigned rn = (hw1 >> 8) & 7; uint32_t list = hw1 & 0xff;
			uint32_t a = c->r[rn]; unsigned cnt = 0;
			if (hw1 & 0x800) {
				for (int i = 0; i < 8; i++) if ((list >> i) & 1) { c->r[i] = RD (a, 4); a += 4; cnt++; }
				if (!((list >> rn) & 1)) c->r[rn] = a;
			} else {
				for (int i = 0; i < 8; i++) if ((list >> i) & 1) { WR (a, c->r[i], 4); a += 4; cnt++; }
				c->r[rn] = a;
			}
			c->cycles += cnt;
			break;
		}
		case 0x34: case 0x35: case 0x36: case 0x37: {                 /* B cond, UDF, SVC */
			unsigned cond = (hw1 >> 8) & 15;
			if (cond == 14) { fault (c, "UDF", ip, hw1); c->r[15] = ip; return; }
			if (cond == 15) { exc_entry (c, 11, ip + 2); return; }
			if (cond_pass (c, cond)) { c->r[15] = ip + 4 + (uint32_t) ((int32_t) (int8_t) (hw1 & 0xff) * 2); c->cycles++; }
			break;
		}
		case 0x38: case 0x39:                                         /* B */
			c->r[15] = ip + 4 + (uint32_t) (((int32_t) (hw1 << 21)) >> 20);
			c->cycles++;
			if (c->r[15] == ip) c->sleeping = 1;                     /* (b . : stuck - asleep until an interrupt) */
			break;
		default:
			fault (c, "undefined 16-bit", ip, hw1); return;
		}
		goto it_advance;
	}

	/* ======================== 32-bit */
	{
	uint32_t hw2 = FETCH (ip + 2);
	c->r[15] = ip + 4;
	unsigned op1 = (hw1 >> 11) & 3, op2 = (hw1 >> 4) & 0x7f;
	uint32_t co, ov, r;
	if (op1 == 1) {
		if ((op2 & 0x64) == 0x00) {                                   /* load / store multiple */
			unsigned op = (hw1 >> 7) & 3, L = (hw1 >> 4) & 1, W = (hw1 >> 5) & 1, rn = hw1 & 15;
			uint32_t list = hw2, a = c->r[rn];
			unsigned cnt = 0; for (int i = 0; i < 16; i++) cnt += (list >> i) & 1;
			if (op == 1 || op == 2) {
				uint32_t start = op == 1 ? a : a - 4 * cnt, p = start;
				uint32_t pcv = 0; int pcl = 0;
				if (L) {
					for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
						uint32_t v = RD (p, 4); p += 4;
						if (i == 15) { pcv = v; pcl = 1; } else c->r[i] = v;
					}
					if (W && !((list >> rn) & 1)) c->r[rn] = op == 1 ? a + 4 * cnt : a - 4 * cnt;
					if (pcl) ldm_write_pc (c, pcv, ip);
				} else {
					for (int i = 0; i < 16; i++) if ((list >> i) & 1) { WR (p, REG (i), 4); p += 4; }
					if (W) c->r[rn] = op == 1 ? a + 4 * cnt : a - 4 * cnt;
				}
				c->cycles += cnt;
				goto it_advance;
			}
			fault (c, "undefined (SRS/RFE)", ip, (hw1 << 16) | hw2); return;
		}
		if ((op2 & 0x64) == 0x04) {                                   /* dual, exclusive, table branch */
			unsigned o1 = (hw1 >> 7) & 3, o2 = (hw1 >> 4) & 3, o3 = (hw2 >> 4) & 15, rn = hw1 & 15, rt = hw2 >> 12, rd = (hw2 >> 8) & 15;
			if (o1 == 0 && o2 == 0) {                                 /* STREX / TT */
				if (rt == 15 && (hw2 & 0x3f) == 0) { c->r[rd] = 1u << 22; goto it_advance; }   /* TT: Secure, no regions */
				uint32_t a = c->r[rn] + (hw2 & 0xff) * 4;
				c->r[rd] = (uint32_t) strex (c, a, c->r[rt], 4);
				goto it_advance;
			}
			if (o1 == 0 && o2 == 1) {                                 /* LDREX */
				uint32_t a = c->r[rn] + (hw2 & 0xff) * 4;
				c->r[rt] = rd_mem (c, a, 4); c->excl = 1; c->excl_addr = a; c->excl_val = c->r[rt]; excl_any = 1;
				goto it_advance;
			}
			if ((o1 & 2) || (o2 & 2)) {                               /* LDRD / STRD */
				unsigned P = (hw1 >> 8) & 1, U = (hw1 >> 7) & 1, W = (hw1 >> 5) & 1, L = (hw1 >> 4) & 1;
				uint32_t imm = (hw2 & 0xff) * 4;
				uint32_t base = rn == 15 ? ((ip + 4) & ~3u) : c->r[rn];
				uint32_t off = U ? base + imm : base - imm, a = P ? off : base;
				if (L) { uint32_t v0 = RD (a, 4), v1 = RD (a + 4, 4); c->r[rt] = v0; c->r[rd] = v1; }
				else { WR (a, c->r[rt], 4); WR (a + 4, c->r[rd], 4); }
				if (W) c->r[rn] = off;
				c->cycles += 2;
				goto it_advance;
			}
			if (o1 == 1 && o2 == 0) {                                 /* STREXB/H, STL*, STLEX* */
				unsigned rd2 = hw2 & 15;
				uint32_t a = c->r[rn];
				int sz = (o3 & 3) == 0 ? 1 : (o3 & 3) == 1 ? 2 : 4;
				if (o3 == 4 || o3 == 5 || (o3 & 12) == 12) {           /* exclusive */
					c->r[rd2] = (uint32_t) strex (c, a, c->r[rt], sz);
				} else WR (a, c->r[rt], sz);                           /* STL / STLB / STLH */
				goto it_advance;
			}
			if (o1 == 1 && o2 == 1) {                                 /* TBB/TBH, LDREXB/H, LDA* */
				if (o3 == 0 || o3 == 1) {
					unsigned rm = hw2 & 15;
					uint32_t base = REG (rn), idx = c->r[rm];
					uint32_t h = o3 ? rd_mem (c, base + idx * 2, 2) : rd_mem (c, base + idx, 1);
					c->r[15] = ip + 4 + h * 2;
					c->cycles += 2;
					goto it_advance;
				}
				uint32_t a = c->r[rn];
				int sz = (o3 & 3) == 0 ? 1 : (o3 & 3) == 1 ? 2 : 4;
				c->r[rt] = rd_mem (c, a, sz);
				if (o3 == 4 || o3 == 5 || (o3 & 12) == 12) { c->excl = 1; c->excl_addr = a; c->excl_val = c->r[rt]; excl_any = 1; }
				goto it_advance;
			}
			fault (c, "undefined (dual/excl)", ip, (hw1 << 16) | hw2); return;
		}
		if ((op2 & 0x60) == 0x20) {                                   /* data processing, shifted register */
			unsigned op = (hw1 >> 5) & 15, S = (hw1 >> 4) & 1, rn = hw1 & 15, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
			unsigned imm5 = ((hw2 >> 10) & 0x1c) | ((hw2 >> 6) & 3);
			int t; unsigned amt; dec_imm_shift ((hw2 >> 4) & 3, imm5, &t, &amt);
			uint32_t sc;
			uint32_t b = shift_c (c->r[rm], t, amt, c->c, &sc);
			uint32_t a = REG (rn);
			co = sc; ov = c->v;
			switch (op) {
			case 0: r = a & b; if (rd == 15 && S) { nz (c, r); c->c = co; goto it_advance; } break;
			case 1: r = a & ~b; break;
			case 2: r = rn == 15 ? b : a | b; break;
			case 3: r = rn == 15 ? ~b : a | ~b; break;
			case 4: r = a ^ b; if (rd == 15 && S) { nz (c, r); c->c = co; goto it_advance; } break;
			case 6: {                                                 /* PKHBT / PKHTB */
				uint32_t m = c->r[rm];
				if ((hw2 >> 5) & 1) { uint32_t sh = (uint32_t) ((int32_t) m >> (amt == 32 ? 31 : amt)); r = (a & 0xffff0000u) | (sh & 0xffff); }
				else r = (a & 0xffff) | ((m << imm5) & 0xffff0000u);
				c->r[rd] = r; goto it_advance;
			}
			case 8: r = add_c (a, b, 0, &co, &ov); if (rd == 15 && S) { nz (c, r); c->c = co; c->v = ov; goto it_advance; } break;
			case 10: r = add_c (a, b, c->c, &co, &ov); break;
			case 11: r = add_c (a, ~b, c->c, &co, &ov); break;
			case 13: r = add_c (a, ~b, 1, &co, &ov); if (rd == 15 && S) { nz (c, r); c->c = co; c->v = ov; goto it_advance; } break;
			case 14: r = add_c (b, ~a, 1, &co, &ov); break;
			default: fault (c, "undefined (dp shifted)", ip, (hw1 << 16) | hw2); return;
			}
			if (rd == 15) { c->r[15] = r & ~1u; c->cycles++; goto it_advance; }
			c->r[rd] = r;
			if (S) { nz (c, r); c->c = co; c->v = ov; }
			goto it_advance;
		}
		if (op2 & 0x40) { if (!copro (c, hw1, hw2, ip)) { fault (c, "undefined (coprocessor)", ip, (hw1 << 16) | hw2); return; } c->cycles++; goto it_advance; }
		fault (c, "undefined 32-bit", ip, (hw1 << 16) | hw2); return;
	}
	if (op1 == 2) {
		if (!(hw2 & 0x8000)) {
			if (!(op2 & 0x20)) {                                      /* data processing, modified immediate */
				unsigned op = (hw1 >> 5) & 15, S = (hw1 >> 4) & 1, rn = hw1 & 15, rd = (hw2 >> 8) & 15;
				uint32_t imm12 = ((hw1 & 0x400) << 1) | ((hw2 >> 4) & 0x700) | (hw2 & 0xff);
				uint32_t b = expand_imm_c (imm12, c->c, &co);
				uint32_t a = REG (rn);
				ov = c->v;
				switch (op) {
				case 0: r = a & b; if (rd == 15 && S) { nz (c, r); c->c = co; goto it_advance; } break;
				case 1: r = a & ~b; break;
				case 2: r = rn == 15 ? b : a | b; break;
				case 3: r = rn == 15 ? ~b : a | ~b; break;
				case 4: r = a ^ b; if (rd == 15 && S) { nz (c, r); c->c = co; goto it_advance; } break;
				case 8: r = add_c (a, b, 0, &co, &ov); if (rd == 15 && S) { nz (c, r); c->c = co; c->v = ov; goto it_advance; } break;
				case 10: r = add_c (a, b, c->c, &co, &ov); break;
				case 11: r = add_c (a, ~b, c->c, &co, &ov); break;
				case 13: r = add_c (a, ~b, 1, &co, &ov); if (rd == 15 && S) { nz (c, r); c->c = co; c->v = ov; goto it_advance; } break;
				case 14: r = add_c (b, ~a, 1, &co, &ov); break;
				default: fault (c, "undefined (dp imm)", ip, (hw1 << 16) | hw2); return;
				}
				if (rd == 15) { c->r[15] = r & ~1u; c->cycles++; goto it_advance; }
				c->r[rd] = r;
				if (S) { nz (c, r); c->c = co; c->v = ov; }
				goto it_advance;
			}
			{                                                         /* plain binary immediate */
				unsigned op = (hw1 >> 4) & 31, rn = hw1 & 15, rd = (hw2 >> 8) & 15;
				uint32_t imm12 = ((hw1 & 0x400) << 1) | ((hw2 >> 4) & 0x700) | (hw2 & 0xff);
				unsigned lsb = ((hw2 >> 10) & 0x1c) | ((hw2 >> 6) & 3), wm1 = hw2 & 31;
				int sat = 0;
				switch (op) {
				case 0: c->r[rd] = rn == 15 ? ((ip + 4) & ~3u) + imm12 : c->r[rn] + imm12; break;
				case 10: c->r[rd] = rn == 15 ? ((ip + 4) & ~3u) - imm12 : c->r[rn] - imm12; break;
				case 4: c->r[rd] = ((hw1 & 15) << 12) | imm12; break;
				case 12: c->r[rd] = (c->r[rd] & 0xffff) | ((((hw1 & 15) << 12) | imm12) << 16); break;
				case 16: case 18: case 24: case 26: {                  /* SSAT / USAT (and the 16-bit forms) */
					int uns = op >= 24, sh = (hw1 >> 5) & 1;
					unsigned bits = wm1 + (uns ? 0 : 1);
					if (sh == 1 && lsb == 0) {                         /* SSAT16 / USAT16 */
						uint32_t x = c->r[rn]; int32_t lo = (int16_t) x, hi = (int16_t) (x >> 16);
						uint32_t a = uns ? sat_u (lo, wm1 & 15, &sat) : (uint32_t) sat_s (lo, (wm1 & 15) + 1, &sat);
						uint32_t b = uns ? sat_u (hi, wm1 & 15, &sat) : (uint32_t) sat_s (hi, (wm1 & 15) + 1, &sat);
						c->r[rd] = (a & 0xffff) | (b << 16);
					} else {
						int32_t x = (int32_t) c->r[rn];
						int64_t v = sh ? (int64_t) (x >> (lsb ? lsb : 31)) : (int64_t) x << lsb;
						if (sh && lsb == 0) v = x >> 31;
						c->r[rd] = uns ? sat_u (v, bits, &sat) : (uint32_t) sat_s (v, bits, &sat);
					}
					if (sat) c->q = 1;
					break;
				}
				case 20: { uint32_t x = c->r[rn] >> lsb; unsigned w = wm1 + 1;    /* SBFX */
				           c->r[rd] = w >= 32 ? x : (uint32_t) (((int32_t) (x << (32 - w))) >> (32 - w)); break; }
				case 28: { uint32_t x = c->r[rn] >> lsb; unsigned w = wm1 + 1;    /* UBFX */
				           c->r[rd] = w >= 32 ? x : x & ((1u << w) - 1); break; }
				case 22: {                                             /* BFI / BFC */
					unsigned msb = wm1;
					if (msb < lsb) { fault (c, "BFI", ip, 0); return; }
					uint32_t mask = (msb - lsb + 1 >= 32 ? 0xffffffffu : ((1u << (msb - lsb + 1)) - 1)) << lsb;
					uint32_t src = rn == 15 ? 0 : c->r[rn] << lsb;
					c->r[rd] = (c->r[rd] & ~mask) | (src & mask);
					break;
				}
				default: fault (c, "undefined (plain imm)", ip, (hw1 << 16) | hw2); return;
				}
				goto it_advance;
			}
		}
		/* branches and miscellaneous control */
		{
			unsigned o1 = (hw2 >> 12) & 7, op = (hw1 >> 4) & 0x7f;
			if ((o1 & 5) == 0) {
				if ((op & 0x38) != 0x38) {                            /* B<c>.W */
					uint32_t S = (hw1 >> 10) & 1, J1 = (hw2 >> 13) & 1, J2 = (hw2 >> 11) & 1;
					int32_t off = (int32_t) ((S << 20) | (J2 << 19) | (J1 << 18) | ((hw1 & 0x3f) << 12) | ((hw2 & 0x7ff) << 1));
					off = (off << 11) >> 11;
					if (cond_pass (c, (hw1 >> 6) & 15)) { c->r[15] = ip + 4 + (uint32_t) off; c->cycles++; }
					goto it_advance;
				}
				if ((op & 0x7e) == 0x38) {                            /* MSR */
					unsigned sysm = hw2 & 0xff, mask = (hw2 >> 10) & 3; uint32_t v = c->r[hw1 & 15];
					switch (sysm) {
					case 0: case 1: case 2: case 3: apsr_set (c, v, mask & 2, mask & 1); break;
					case 8: if (c->ipsr == 0 && (c->control & 2)) c->msp = v & ~3u; else c->r[13] = v & ~3u; break;
					case 9: if (c->ipsr == 0 && (c->control & 2)) c->r[13] = v & ~3u; else c->psp = v & ~3u; break;
					case 10: c->msplim = v; break;
					case 11: c->psplim = v; break;
					case 16: c->primask = v & 1; break;
					case 17: c->basepri = v & 0xf0; break;
					case 18: if ((v & 0xf0) && (!c->basepri || (v & 0xf0) < c->basepri)) c->basepri = v & 0xf0; break;
					case 19: c->faultmask = v & 1; break;
					case 20: {
						c->control = (c->control & ~5u) | (v & 5);
						if (c->ipsr == 0) set_control_spsel (c, (v >> 1) & 1);
						break;
					}
					}
					goto it_advance;
				}
				if (op == 0x3a) {                                     /* hints */
					switch (hw2 & 0xff) {
					case 2: if (c->event) c->event = 0; else { c->sleeping = 2; WFE_LOG (c); } break;
					case 3: c->sleeping = 1; break;
					case 4: if (c->sys->event) c->sys->event (c, CM33_EV_SEV); break;
					}
					goto it_advance;
				}
				if (op == 0x3b) {                                     /* CLREX, DSB, DMB, ISB */
					if (((hw2 >> 4) & 15) == 2) c->excl = 0;
					goto it_advance;
				}
				if ((op & 0x7e) == 0x3e) {                            /* MRS */
					unsigned sysm = hw2 & 0xff, rd = (hw2 >> 8) & 15; uint32_t v = 0;
					switch (sysm) {
					case 0: case 1: case 2: case 3: case 5: case 6: case 7: {
						uint32_t x = xpsr_get (c);
						v = 0;
						if (!(sysm & 4)) v |= x & 0xf80f0000u;   /* (APSR part) */
						if (sysm & 1) v |= x & 0x1ff;
						break;
					}
					case 8: v = (c->ipsr == 0 && (c->control & 2)) ? c->msp : c->r[13]; break;
					case 9: v = (c->ipsr == 0 && (c->control & 2)) ? c->r[13] : c->psp; break;
					case 10: v = c->msplim; break;
					case 11: v = c->psplim; break;
					case 16: v = c->primask; break;
					case 17: case 18: v = c->basepri; break;
					case 19: v = c->faultmask; break;
					case 20: v = c->control; break;
					}
					c->r[rd] = v;
					goto it_advance;
				}
				if (op == 0x7f && o1 == 2) { fault (c, "UDF.W", ip, (hw1 << 16) | hw2); c->r[15] = ip; return; }
				fault (c, "undefined (misc control)", ip, (hw1 << 16) | hw2); return;
			}
			{                                                         /* B.W, BL */
				uint32_t S = (hw1 >> 10) & 1, J1 = (hw2 >> 13) & 1, J2 = (hw2 >> 11) & 1;
				uint32_t I1 = !(J1 ^ S), I2 = !(J2 ^ S);
				int32_t off = (int32_t) ((S << 24) | (I1 << 23) | (I2 << 22) | ((hw1 & 0x3ff) << 12) | ((hw2 & 0x7ff) << 1));
				off = (off << 7) >> 7;
				if (o1 & 4) {
					if (!(o1 & 1)) { fault (c, "BLX imm", ip, (hw1 << 16) | hw2); return; }
					c->r[14] = (ip + 4) | 1;
				}
				c->r[15] = ip + 4 + (uint32_t) off;
				c->cycles++;
				if (c->r[15] == ip) c->sleeping = 1;                     /* (b.w . : stuck) */
				goto it_advance;
			}
		}
	}
	/* op1 == 3 */
	if ((op2 & 0x71) == 0x00 || (op2 & 0x67) == 0x01 || (op2 & 0x67) == 0x03 || (op2 & 0x67) == 0x05) {   /* load / store single */
		int load = (op2 & 1) != 0;
		unsigned size = (hw1 >> 5) & 3, sgn = (hw1 >> 8) & 1, rn = hw1 & 15, rt = hw2 >> 12;
		int n = size == 0 ? 1 : size == 1 ? 2 : 4;
		if (!load) sgn = 0;
		uint32_t a, wb = 0; int dowb = 0;
		if (rn == 15 && load) {                                       /* literal */
			uint32_t imm = hw2 & 0xfff, base = (ip + 4) & ~3u;
			a = (hw1 & 0x80) ? base + imm : base - imm;
		} else if (hw1 & 0x80) {                                      /* imm12 */
			a = c->r[rn] + (hw2 & 0xfff);
		} else if ((hw2 & 0x800) == 0) {                              /* register */
			if (hw2 & 0x7c0) { fault (c, "undefined (ld/st)", ip, (hw1 << 16) | hw2); return; }
			a = c->r[rn] + (c->r[hw2 & 15] << ((hw2 >> 4) & 3));
		} else {                                                      /* imm8, P U W */
			unsigned P = (hw2 >> 10) & 1, U = (hw2 >> 9) & 1, W = (hw2 >> 8) & 1, imm = hw2 & 0xff;
			uint32_t off = U ? c->r[rn] + imm : c->r[rn] - imm;
			a = P ? off : c->r[rn];
			if (W || !P) { dowb = 1; wb = off; }
		}
		if (load) {
			if (rt == 15 && n < 4) { /* PLD / PLI */ }
			else {
				uint32_t v = rd_mem (c, a, n);
				if (sgn) v = n == 1 ? (uint32_t) (int8_t) v : (uint32_t) (int16_t) v;
				if (dowb) c->r[rn] = wb;
				if (rt == 15) { ldm_write_pc (c, v, ip); c->cycles += 2; }
				else c->r[rt] = v;
				c->cycles++;
				goto it_advance;
			}
		} else {
			WR (a, c->r[rt], n);
		}
		if (dowb) c->r[rn] = wb;
		c->cycles++;
		goto it_advance;
	}
	if ((op2 & 0x70) == 0x20) {                                       /* data processing (register) */
		unsigned o1 = (hw1 >> 4) & 15, o2 = (hw2 >> 4) & 15, rn = hw1 & 15, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
		if ((o1 & 8) == 0 && o2 == 0) {                               /* LSL/LSR/ASR/ROR register */
			r = shift_c (c->r[rn], (int) ((o1 >> 1) & 3), c->r[rm] & 0xff, c->c, &co);
			c->r[rd] = r;
			if (o1 & 1) { nz (c, r); c->c = co; }
			goto it_advance;
		}
		if ((o1 & 8) == 0 && (o2 & 8)) {                              /* extend (and add) */
			uint32_t m = ror32 (c->r[rm], ((hw2 >> 4) & 3) * 8), x;
			switch (o1 & 7) {
			case 0: x = (uint32_t) (int16_t) m; break;
			case 1: x = m & 0xffff; break;
			case 2: x = ((uint32_t) (int8_t) m & 0xffff) | (((uint32_t) (int8_t) (m >> 16)) << 16); break;
			case 3: x = m & 0x00ff00ffu; break;
			case 4: x = (uint32_t) (int8_t) m; break;
			case 5: x = m & 0xff; break;
			default: fault (c, "undefined (extend)", ip, (hw1 << 16) | hw2); return;
			}
			if (rn != 15) {
				if ((o1 & 7) == 2 || (o1 & 7) == 3) x = ((c->r[rn] + x) & 0xffff) | ((c->r[rn] & 0xffff0000u) + (x & 0xffff0000u));
				else x += c->r[rn];
			}
			c->r[rd] = x;
			goto it_advance;
		}
		if ((o1 & 8) && (o2 & 8) == 0) {                              /* parallel add / subtract */
			c->r[rd] = par_addsub (c, o1 & 7, o2 & 7, c->r[rn], c->r[rm]);
			goto it_advance;
		}
		if ((o1 & 0xc) == 8 && (o2 & 0xc) == 8) {                     /* misc */
			uint32_t m = c->r[rm], a = c->r[rn]; int sat = 0;
			switch (((o1 & 3) << 2) | (o2 & 3)) {
			case 0: c->r[rd] = (uint32_t) sat_s ((int64_t) (int32_t) m + (int32_t) a, 32, &sat); break;   /* QADD Rd,Rm,Rn */
			case 1: { int32_t d2 = sat_s ((int64_t) (int32_t) a * 2, 32, &sat); c->r[rd] = (uint32_t) sat_s ((int64_t) (int32_t) m + d2, 32, &sat); break; }
			case 2: c->r[rd] = (uint32_t) sat_s ((int64_t) (int32_t) m - (int32_t) a, 32, &sat); break;
			case 3: { int32_t d2 = sat_s ((int64_t) (int32_t) a * 2, 32, &sat); c->r[rd] = (uint32_t) sat_s ((int64_t) (int32_t) m - d2, 32, &sat); break; }
			case 4: c->r[rd] = (m >> 24) | ((m >> 8) & 0xff00) | ((m << 8) & 0xff0000) | (m << 24); break;
			case 5: c->r[rd] = ((m >> 8) & 0x00ff00ffu) | ((m << 8) & 0xff00ff00u); break;
			case 6: { uint32_t x = 0; for (int i = 0; i < 32; i++) if ((m >> i) & 1) x |= 1u << (31 - i); c->r[rd] = x; break; }
			case 7: c->r[rd] = (uint32_t) (int16_t) (((m >> 8) & 0xff) | ((m & 0xff) << 8)); break;
			case 8: { uint32_t x = 0; for (int i = 0; i < 4; i++) x |= (((c->ge >> i) & 1) ? a : m) & (0xffu << (8 * i)); c->r[rd] = x; break; }  /* SEL */
			case 12: { unsigned k = 0; while (k < 32 && !((m >> (31 - k)) & 1)) k++; c->r[rd] = k; break; }   /* CLZ */
			default: fault (c, "undefined (misc dp)", ip, (hw1 << 16) | hw2); return;
			}
			if (sat) c->q = 1;
			goto it_advance;
		}
		fault (c, "undefined (dp register)", ip, (hw1 << 16) | hw2); return;
	}
	if ((op2 & 0x78) == 0x30) {                                       /* multiply, multiply accumulate */
		unsigned o1 = (hw1 >> 4) & 7, o2 = (hw2 >> 4) & 3, rn = hw1 & 15, ra = hw2 >> 12, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
		uint32_t a = c->r[rn], b = c->r[rm], acc = ra == 15 ? 0 : c->r[ra];
		switch (o1) {
		case 0:
			if (o2 == 0) c->r[rd] = a * b + acc;
			else if (o2 == 1) c->r[rd] = acc - a * b;
			else goto mul_undef;
			break;
		case 1: {                                                     /* SMLAxy / SMULxy */
			int32_t x = (hw2 & 0x20) ? (int16_t) (a >> 16) : (int16_t) a, y = (hw2 & 0x10) ? (int16_t) (b >> 16) : (int16_t) b;
			int64_t p = (int64_t) x * y + (ra == 15 ? 0 : (int32_t) acc);
			if (p != (int32_t) p) c->q = 1;
			c->r[rd] = (uint32_t) p;
			break;
		}
		case 2: case 4: {                                             /* SMLAD / SMUAD, SMLSD / SMUSD */
			uint32_t bb = (hw2 & 0x10) ? ror32 (b, 16) : b;
			int64_t p1 = (int64_t) (int16_t) a * (int16_t) bb, p2 = (int64_t) (int16_t) (a >> 16) * (int16_t) (bb >> 16);
			int64_t s = (o1 == 2 ? p1 + p2 : p1 - p2) + (ra == 15 ? 0 : (int32_t) acc);
			if (s != (int32_t) s) c->q = 1;
			c->r[rd] = (uint32_t) s;
			break;
		}
		case 3: {                                                     /* SMLAWy / SMULWy */
			int32_t y = (hw2 & 0x10) ? (int16_t) (b >> 16) : (int16_t) b;
			int64_t p = ((int64_t) (int32_t) a * y) >> 16;
			int64_t s = p + (ra == 15 ? 0 : (int32_t) acc);
			if (s != (int32_t) s) c->q = 1;
			c->r[rd] = (uint32_t) s;
			break;
		}
		case 5: case 6: {                                             /* SMMLA / SMMUL, SMMLS */
			int64_t p = (int64_t) (int32_t) a * (int32_t) b;
			int64_t s = o1 == 5 ? ((int64_t) (ra == 15 ? 0 : acc) << 32) + p : ((int64_t) acc << 32) - p;
			if (hw2 & 0x10) s += 0x80000000ll;
			c->r[rd] = (uint32_t) (s >> 32);
			break;
		}
		case 7: {                                                     /* USAD8 / USADA8 */
			uint32_t s = 0;
			for (int i = 0; i < 4; i++) { int x = (a >> (8 * i)) & 0xff, y = (b >> (8 * i)) & 0xff; s += (uint32_t) (x > y ? x - y : y - x); }
			c->r[rd] = s + acc;
			break;
		}
		default: mul_undef: fault (c, "undefined (multiply)", ip, (hw1 << 16) | hw2); return;
		}
		goto it_advance;
	}
	if ((op2 & 0x78) == 0x38) {                                       /* long multiply, divide */
		unsigned o1 = (hw1 >> 4) & 7, o2 = (hw2 >> 4) & 15, rn = hw1 & 15, rlo = hw2 >> 12, rhi = (hw2 >> 8) & 15, rm = hw2 & 15;
		uint32_t a = c->r[rn], b = c->r[rm];
		uint64_t acc = ((uint64_t) c->r[rhi] << 32) | c->r[rlo];
		switch (o1) {
		case 0: { int64_t p = (int64_t) (int32_t) a * (int32_t) b; c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) ((uint64_t) p >> 32); break; }
		case 1: { int32_t x = (int32_t) a, y = (int32_t) b;
		          c->r[rhi] = y == 0 ? 0 : (x == (int32_t) 0x80000000 && y == -1) ? (uint32_t) x : (uint32_t) (x / y); c->cycles += 6; break; }
		case 2: { uint64_t p = (uint64_t) a * b; c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) (p >> 32); break; }
		case 3: c->r[rhi] = b ? a / b : 0; c->cycles += 6; break;
		case 4:
			if (o2 == 0) { int64_t p = (int64_t) (int32_t) a * (int32_t) b + (int64_t) acc; c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) ((uint64_t) p >> 32); }
			else if ((o2 & 12) == 8) {
				int32_t x = (o2 & 2) ? (int16_t) (a >> 16) : (int16_t) a, y = (o2 & 1) ? (int16_t) (b >> 16) : (int16_t) b;
				int64_t p = (int64_t) x * y + (int64_t) acc; c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) ((uint64_t) p >> 32);
			} else if ((o2 & 14) == 12) {
				uint32_t bb = (o2 & 1) ? ror32 (b, 16) : b;
				int64_t p = (int64_t) (int16_t) a * (int16_t) bb + (int64_t) (int16_t) (a >> 16) * (int16_t) (bb >> 16) + (int64_t) acc;
				c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) ((uint64_t) p >> 32);
			} else goto lmul_undef;
			break;
		case 5:
			if ((o2 & 14) == 12) {
				uint32_t bb = (o2 & 1) ? ror32 (b, 16) : b;
				int64_t p = (int64_t) (int16_t) a * (int16_t) bb - (int64_t) (int16_t) (a >> 16) * (int16_t) (bb >> 16) + (int64_t) acc;
				c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) ((uint64_t) p >> 32);
			} else goto lmul_undef;
			break;
		case 6:
			if (o2 == 0) { uint64_t p = (uint64_t) a * b + acc; c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) (p >> 32); }
			else if (o2 == 6) { uint64_t p = (uint64_t) a * b + c->r[rlo] + c->r[rhi]; c->r[rlo] = (uint32_t) p; c->r[rhi] = (uint32_t) (p >> 32); }
			else goto lmul_undef;
			break;
		default: lmul_undef: fault (c, "undefined (long multiply)", ip, (hw1 << 16) | hw2); return;
		}
		goto it_advance;
	}
	if (op2 & 0x40) { if (!copro (c, hw1, hw2, ip)) { fault (c, "undefined (coprocessor)", ip, (hw1 << 16) | hw2); return; } c->cycles++; goto it_advance; }
	fault (c, "undefined 32-bit", ip, (hw1 << 16) | hw2); return;
	}

it_advance:
	post (c, ip, in_it);
}
int cm33_irq_pending (cm33_t *c) { return exc_pending (c) != 0; }

/* ---------------------------------------------------------------- the decoded-instruction cache
 * Code in RAM is decoded once: each halfword address gets an entry with its handler and its fields
 * (registers, immediates, branch targets worked out). Running it again is a call through the entry.
 * The common instructions have handlers of their own; the rest are marked GENERIC and go through step()
 * (the full decoder). A write to RAM in a page that holds decoded code clears the entries it touches. */
typedef struct di di_t;
typedef void (*hfn) (cm33_t *c, const di_t *d, uint32_t ip);
struct di { hfn fn; uint32_t imm; uint8_t a, b, e, op, len, f1, f2, f3; };

/* the handlers of the rest: GENERIC (through the full decoder) and h_decode (not decoded yet - a fresh entry) */
static void h_generic (cm33_t *c, const di_t *d, uint32_t ip);
static void h_decode (cm33_t *c, const di_t *d, uint32_t ip);
static void step_out (cm33_t *c);
#define GENERIC h_generic

void cm33_code_written (cm33_sys_t *s, uint32_t addr, uint32_t n)
{
	if ((!s->dcache && !s->dcache1) || !s->code_pages) return;
	uint32_t o = addr - s->ram_base;
	if (o >= s->ram_size) return;
	uint32_t p0 = (o >= 2 ? o - 2 : 0) >> 12, p1 = (o + n) >> 12;
	int any = 0;
	for (uint32_t p = p0; p <= p1 && p < (s->ram_size >> 12) + 1; p++) any |= s->code_pages[p];
	if (!any) return;
	uint32_t k0 = o >= 2 ? (o - 2) >> 1 : 0, k1 = (o + n - 1) >> 1;
	for (int i = 0; i < 2; i++) {
		di_t *dc = (di_t *) (i ? s->dcache1 : s->dcache);
		if (dc) for (uint32_t k = k0; k <= k1 && k < (s->ram_size >> 1); k++) dc[k].fn = h_decode;
	}
}

/* ----- 16-bit handlers */
static void h_shift_imm (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t co;
	uint32_t r = shift_c (c->r[d->b], d->op, d->imm, c->c, &co);
	c->r[d->a] = r;
	if (!c->it) { nz (c, r); c->c = co; }
}
static void h_addsub3 (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t co, ov, r;
	uint32_t b = (d->op & 2) ? d->imm : c->r[d->e];
	if (d->op & 1) r = add_c (c->r[d->b], ~b, 1, &co, &ov); else r = add_c (c->r[d->b], b, 0, &co, &ov);
	c->r[d->a] = r;
	if (!c->it) { nz (c, r); c->c = co; c->v = ov; }
}
static void h_imm8 (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t co, ov, r; unsigned rd = d->a; uint32_t imm = d->imm; int sf = !c->it;
	switch (d->op) {
	case 0: c->r[rd] = imm; if (sf) nz (c, imm); break;
	case 1: r = add_c (c->r[rd], ~imm, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov; break;
	case 2: r = add_c (c->r[rd], imm, 0, &co, &ov); c->r[rd] = r; if (sf) { nz (c, r); c->c = co; c->v = ov; } break;
	default: r = add_c (c->r[rd], ~imm, 1, &co, &ov); c->r[rd] = r; if (sf) { nz (c, r); c->c = co; c->v = ov; } break;
	}
}
static void h_dp16 (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	unsigned rdn = d->a;
	uint32_t a = c->r[rdn], b = c->r[d->b], co = c->c, ov = c->v, r;
	int sf = !c->it;
	switch (d->op) {
	case 0: r = a & b; c->r[rdn] = r; break;
	case 1: r = a ^ b; c->r[rdn] = r; break;
	case 2: r = shift_c (a, 0, b & 0xff, c->c, &co); c->r[rdn] = r; break;
	case 3: r = shift_c (a, 1, b & 0xff, c->c, &co); c->r[rdn] = r; break;
	case 4: r = shift_c (a, 2, b & 0xff, c->c, &co); c->r[rdn] = r; break;
	case 5: r = add_c (a, b, c->c, &co, &ov); c->r[rdn] = r; break;
	case 6: r = add_c (a, ~b, c->c, &co, &ov); c->r[rdn] = r; break;
	case 7: r = shift_c (a, 3, b & 0xff, c->c, &co); c->r[rdn] = r; break;
	case 8: r = a & b; nz (c, r); c->c = co; return;
	case 9: r = add_c (0, ~b, 1, &co, &ov); c->r[rdn] = r; break;
	case 10: r = add_c (a, ~b, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov; return;
	case 11: r = add_c (a, b, 0, &co, &ov); nz (c, r); c->c = co; c->v = ov; return;
	case 12: r = a | b; c->r[rdn] = r; break;
	case 13: r = a * b; c->r[rdn] = r; if (sf) nz (c, r); return;
	case 14: r = a & ~b; c->r[rdn] = r; break;
	default: r = ~b; c->r[rdn] = r; break;
	}
	if (sf) { nz (c, r); c->c = co; c->v = ov; }
}
static void h_hi_add (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] += c->r[d->b]; }
static void h_hi_mov (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = c->r[d->b]; }
static void h_hi_cmp (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t co, ov, r = add_c (c->r[d->a], ~c->r[d->b], 1, &co, &ov); nz (c, r); c->c = co; c->v = ov;
}
static void h_bx (cm33_t *c, const di_t *d, uint32_t ip)
{
	uint32_t t = c->r[d->a];
	if (d->op) c->r[14] = (ip + 2) | 1;
	bx_write_pc (c, t, ip);
	c->cycles++;
}
static void h_ld_lit (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t v = rd_mem (c, d->imm, d->e);
	if (d->f1) v = d->e == 1 ? (uint32_t) (int8_t) v : (uint32_t) (int16_t) v;
	c->r[d->a] = v; c->cycles++;
}
static void h_movi (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = d->imm; }
static void h_ldst_reg (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t a = c->r[d->b] + c->r[d->e]; unsigned rt = d->a;
	switch (d->op) {
	case 0: WR (a, c->r[rt], 4); break;
	case 1: WR (a, c->r[rt], 2); break;
	case 2: WR (a, c->r[rt], 1); break;
	case 3: c->r[rt] = (uint32_t) (int8_t) rd_mem (c, a, 1); break;
	case 4: c->r[rt] = rd_mem (c, a, 4); break;
	case 5: c->r[rt] = rd_mem (c, a, 2); break;
	case 6: c->r[rt] = rd_mem (c, a, 1); break;
	default: c->r[rt] = (uint32_t) (int16_t) rd_mem (c, a, 2); break;
	}
	c->cycles++;
}
/* load / store, [rn, #imm] (any size, signed loads): e = size, f1 = signed, f2 = load */
static void h_ld_imm (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t v = rd_mem (c, c->r[d->b] + d->imm, d->e);
	if (d->f1) v = d->e == 1 ? (uint32_t) (int8_t) v : (uint32_t) (int16_t) v;
	c->r[d->a] = v; c->cycles++;
}
static void h_st_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; WR (c->r[d->b] + d->imm, c->r[d->a], d->e); c->cycles++; }
/* imm8 with pre / post index and writeback: f3 bit0 pre-index, bit1 writeback */
static void h_ldst_idx (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t base = c->r[d->b], off = base + d->imm, a = (d->f3 & 1) ? off : base;
	if (d->f2) {
		uint32_t v = rd_mem (c, a, d->e);
		if (d->f1) v = d->e == 1 ? (uint32_t) (int8_t) v : (uint32_t) (int16_t) v;
		if (d->f3 & 2) c->r[d->b] = off;
		c->r[d->a] = v;
	} else {
		WR (a, c->r[d->a], d->e);
		if (d->f3 & 2) c->r[d->b] = off;
	}
	c->cycles++;
}
/* [rn, rm, LSL #imm] */
static void h_ldst_rs (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t a = c->r[d->b] + (c->r[d->f3] << d->imm);
	if (d->f2) {
		uint32_t v = rd_mem (c, a, d->e);
		if (d->f1) v = d->e == 1 ? (uint32_t) (int8_t) v : (uint32_t) (int16_t) v;
		c->r[d->a] = v;
	} else WR (a, c->r[d->a], d->e);
	c->cycles++;
}
static void h_addspi (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = c->r[13] + d->imm; }
static void h_spadj (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[13] += d->imm; }
static void h_cbz (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	if ((c->r[d->a] != 0) == d->op) { c->r[15] = d->imm; c->cycles++; }
}
static void h_ext16 (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t m = c->r[d->b];
	switch (d->op) {
	case 0: c->r[d->a] = (uint32_t) (int16_t) m; break;
	case 1: c->r[d->a] = (uint32_t) (int8_t) m; break;
	case 2: c->r[d->a] = m & 0xffff; break;
	default: c->r[d->a] = m & 0xff; break;
	}
}
static void h_push (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t list = d->imm; unsigned cnt = d->e;
	uint32_t a = c->r[13] - 4 * cnt;
	if (d->op) {                                          /* (STMDB rn!: f1 = rn) */
		unsigned rn = d->f1; a = c->r[rn] - 4 * cnt;
		uint32_t p = a;
		for (int i = 0; i < 16; i++) if ((list >> i) & 1) { WR (p, c->r[i], 4); p += 4; }
		c->r[rn] = a;
	} else {
		c->r[13] = a;
		for (int i = 0; i < 16; i++) if ((list >> i) & 1) { WR (a, c->r[i], 4); a += 4; }
	}
	c->cycles += cnt;
}
static void h_pop (cm33_t *c, const di_t *d, uint32_t ip)
{
	uint32_t list = d->imm, a = c->r[13];
	unsigned cnt = 0;
	for (int i = 0; i < 15; i++) if ((list >> i) & 1) { c->r[i] = RD (a, 4); a += 4; cnt++; }
	if (list & 0x8000) { uint32_t v = RD (a, 4); a += 4; c->r[13] = a; bx_write_pc (c, v, ip); cnt += 2; }
	else c->r[13] = a;
	c->cycles += cnt;
}
/* LDM / STM increment after (16 and 32 bit, no PC): a = rn, f1 = load, f2 = writeback rule (1: unless in list) */
static void h_ldmia (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	unsigned rn = d->a; uint32_t list = d->imm, a = c->r[rn];
	if (d->f1) {
		for (int i = 0; i < 15; i++) if ((list >> i) & 1) { c->r[i] = RD (a, 4); a += 4; }
		if (d->f2 && !((list >> rn) & 1)) c->r[rn] = a;
	} else {
		for (int i = 0; i < 15; i++) if ((list >> i) & 1) { WR (a, c->r[i], 4); a += 4; }
		if (d->f2) c->r[rn] = a;
	}
	c->cycles += d->e;
}
static void h_rev (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t m = c->r[d->b];
	switch (d->op) {
	case 0: c->r[d->a] = (m >> 24) | ((m >> 8) & 0xff00) | ((m << 8) & 0xff0000) | (m << 24); break;
	case 1: c->r[d->a] = ((m >> 8) & 0x00ff00ffu) | ((m << 8) & 0xff00ff00u); break;
	default: c->r[d->a] = (uint32_t) (int16_t) (((m >> 8) & 0xff) | ((m & 0xff) << 8)); break;
	}
}
static void h_it (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->it = d->imm; }
static void h_nop (cm33_t *c, const di_t *d, uint32_t ip) { (void) c; (void) d; (void) ip; }
static void h_bcond (cm33_t *c, const di_t *d, uint32_t ip)
{
	if (cond_pass (c, d->op)) { c->r[15] = d->imm; c->cycles++; if (d->imm == ip) c->sleeping = 1, c->attn = 1; }
}
static void h_b (cm33_t *c, const di_t *d, uint32_t ip) { c->r[15] = d->imm; c->cycles++; if (d->imm == ip) c->sleeping = 1, c->attn = 1; }
static void h_bl (cm33_t *c, const di_t *d, uint32_t ip) { c->r[14] = (ip + 4) | 1; c->r[15] = d->imm; c->cycles++; }

/* ----- 32-bit handlers */
/* data processing with a (pre-expanded) immediate: op = the opcode, f1 = S, f2 = the carry from the
 * expansion (0 unchanged, 1 clear, 2 set), f3 = test form (no result) */
static void h_dpimm (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t a = c->r[d->b], b = d->imm, r, co = d->f2 ? (uint32_t) (d->f2 - 1) : c->c, ov = c->v;
	switch (d->op) {
	case 0: r = a & b; break;
	case 1: r = a & ~b; break;
	case 2: r = a | b; break;
	case 3: r = a | ~b; break;
	case 4: r = a ^ b; break;
	case 8: r = add_c (a, b, 0, &co, &ov); break;
	case 10: r = add_c (a, b, c->c, &co, &ov); break;
	case 11: r = add_c (a, ~b, c->c, &co, &ov); break;
	case 13: r = add_c (a, ~b, 1, &co, &ov); break;
	case 14: r = add_c (b, ~a, 1, &co, &ov); break;
	case 16: r = b; break;                                  /* MOV */
	default: r = ~b; break;                                 /* MVN */
	}
	if (!d->f3) c->r[d->a] = r;
	if (d->f1) { nz (c, r); c->c = co; c->v = ov; }
}
/* data processing with a shifted register: e = rm, f2 = shift type (4 RRX), imm = amount */
static void h_dpreg (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t sc, b = shift_c (c->r[d->e], d->f2, d->imm, c->c, &sc);
	uint32_t a = c->r[d->b], r, co = sc, ov = c->v;
	switch (d->op) {
	case 0: r = a & b; break;
	case 1: r = a & ~b; break;
	case 2: r = a | b; break;
	case 3: r = a | ~b; break;
	case 4: r = a ^ b; break;
	case 8: r = add_c (a, b, 0, &co, &ov); break;
	case 10: r = add_c (a, b, c->c, &co, &ov); break;
	case 11: r = add_c (a, ~b, c->c, &co, &ov); break;
	case 13: r = add_c (a, ~b, 1, &co, &ov); break;
	case 14: r = add_c (b, ~a, 1, &co, &ov); break;
	case 16: r = b; break;
	default: r = ~b; break;
	}
	if (!d->f3) c->r[d->a] = r;
	if (d->f1) { nz (c, r); c->c = co; c->v = ov; }
}
static void h_addw (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = c->r[d->b] + d->imm; }
static void h_movt (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = (c->r[d->a] & 0xffff) | (d->imm << 16); }
static void h_ubfx (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t x = c->r[d->b] >> d->imm; unsigned w = d->e;
	if (d->op) c->r[d->a] = w >= 32 ? x : (uint32_t) (((int32_t) (x << (32 - w))) >> (32 - w));
	else c->r[d->a] = w >= 32 ? x : x & ((1u << w) - 1);
}
static void h_bfi (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t mask = d->imm;
	uint32_t src = d->op ? 0 : c->r[d->b] << d->e;
	c->r[d->a] = (c->r[d->a] & ~mask) | (src & mask);
}
static void h_mul32 (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t p = c->r[d->b] * c->r[d->e];
	switch (d->op) { case 0: c->r[d->a] = p; break; case 1: c->r[d->a] = p + c->r[d->f1]; break; default: c->r[d->a] = c->r[d->f1] - p; break; }
}
static void h_lmul (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t a = c->r[d->b], b = c->r[d->e]; unsigned lo = d->a, hi = d->f1;
	switch (d->op) {
	case 0: { int64_t p = (int64_t) (int32_t) a * (int32_t) b; c->r[lo] = (uint32_t) p; c->r[hi] = (uint32_t) ((uint64_t) p >> 32); break; }
	case 1: { uint64_t p = (uint64_t) a * b; c->r[lo] = (uint32_t) p; c->r[hi] = (uint32_t) (p >> 32); break; }
	case 2: { uint64_t acc = ((uint64_t) c->r[hi] << 32) | c->r[lo]; int64_t p = (int64_t) (int32_t) a * (int32_t) b + (int64_t) acc; c->r[lo] = (uint32_t) p; c->r[hi] = (uint32_t) ((uint64_t) p >> 32); break; }
	default: { uint64_t acc = ((uint64_t) c->r[hi] << 32) | c->r[lo]; uint64_t p = (uint64_t) a * b + acc; c->r[lo] = (uint32_t) p; c->r[hi] = (uint32_t) (p >> 32); break; }
	}
}
static void h_div (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t a = c->r[d->b], b = c->r[d->e];
	if (d->op) { int32_t x = (int32_t) a, y = (int32_t) b; c->r[d->a] = y == 0 ? 0 : (x == (int32_t) 0x80000000 && y == -1) ? (uint32_t) x : (uint32_t) (x / y); }
	else c->r[d->a] = b ? a / b : 0;
	c->cycles += 6;
}
static void h_shreg (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t co, r = shift_c (c->r[d->b], d->op, c->r[d->e] & 0xff, c->c, &co);
	c->r[d->a] = r;
	if (d->f1) { nz (c, r); c->c = co; }
}
static void h_ext32 (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t m = ror32 (c->r[d->e], d->imm), x;
	switch (d->op) {
	case 0: x = (uint32_t) (int16_t) m; break;
	case 1: x = m & 0xffff; break;
	case 4: x = (uint32_t) (int8_t) m; break;
	default: x = m & 0xff; break;
	}
	if (d->f1) x += c->r[d->b];
	c->r[d->a] = x;
}
static void h_clz (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip; uint32_t m = c->r[d->e]; unsigned k = 0; while (k < 32 && !((m >> (31 - k)) & 1)) k++; c->r[d->a] = k;
}
static void h_ldrd (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	uint32_t base = c->r[d->b], off = base + d->imm, a = (d->f3 & 1) ? off : base;
	if (d->f2) { uint32_t v0 = RD (a, 4), v1 = RD (a + 4, 4); c->r[d->a] = v0; c->r[d->e] = v1; }
	else { WR (a, c->r[d->a], 4); WR (a + 4, c->r[d->e], 4); }
	if (d->f3 & 2) c->r[d->b] = off;
	c->cycles += 2;
}
static void h_ldmdb (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) ip;
	unsigned rn = d->a; uint32_t list = d->imm, base = c->r[rn], p = base - 4 * d->e;
	if (d->f1) {
		for (int i = 0; i < 15; i++) if ((list >> i) & 1) { c->r[i] = RD (p, 4); p += 4; }
		if (d->f2 && !((list >> rn) & 1)) c->r[rn] = base - 4 * d->e;
	} else {
		for (int i = 0; i < 15; i++) if ((list >> i) & 1) { WR (p, c->r[i], 4); p += 4; }
		if (d->f2) c->r[rn] = base - 4 * d->e;
	}
	c->cycles += d->e;
}
static void h_pop32 (cm33_t *c, const di_t *d, uint32_t ip)          /* LDMIA sp!, {..., pc} */
{
	uint32_t list = d->imm, a = c->r[13], pcv = 0;
	for (int i = 0; i < 16; i++) if ((list >> i) & 1) { uint32_t v = RD (a, 4); a += 4; if (i == 15) pcv = v; else c->r[i] = v; }
	c->r[13] = a;
	if (list & 0x8000) bx_write_pc (c, pcv, ip);
	c->cycles += d->e;
}

static unsigned popcount16 (uint32_t x) { unsigned n = 0; for (int i = 0; i < 16; i++) n += (x >> i) & 1; return n; }

/* decode the instruction at ip into d (GENERIC when it has no handler here) */
static void decode (cm33_t *c, uint32_t ip, di_t *d)
{
	uint32_t hw1 = fetch16 (c, ip);
	memset (d, 0, sizeof *d);
	d->fn = GENERIC;
	if ((hw1 >> 11) < 0x1d) {
		d->len = 2;
		switch (hw1 >> 10) {
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: {
			int t; unsigned amt; dec_imm_shift ((int) ((hw1 >> 11) & 3), (hw1 >> 6) & 31, &t, &amt);
			d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->op = (uint8_t) t; d->imm = amt; d->fn = h_shift_imm; return;
		}
		case 0x06: case 0x07:
			d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->e = (hw1 >> 6) & 7; d->imm = (hw1 >> 6) & 7;
			d->op = (uint8_t) (((hw1 >> 9) & 1) | ((hw1 & 0x400) ? 2 : 0)); d->fn = h_addsub3; return;
		case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
			d->a = (hw1 >> 8) & 7; d->imm = hw1 & 0xff; d->op = (hw1 >> 11) & 3; d->fn = h_imm8; return;
		case 0x10: d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->op = (hw1 >> 6) & 15; d->fn = h_dp16; return;
		case 0x11: {
			unsigned op = (hw1 >> 8) & 3, rd = ((hw1 >> 4) & 8) | (hw1 & 7), rm = (hw1 >> 3) & 15;
			if (op == 3) { if (rm == 15) return; d->a = rm; d->op = (hw1 & 0x80) != 0; d->fn = h_bx; return; }
			if (rd == 15 || rm == 15) return;
			d->a = rd; d->b = rm;
			d->fn = op == 0 ? h_hi_add : op == 1 ? h_hi_cmp : h_hi_mov;
			if (op == 1 && (rd == 13 || rm == 13)) { /* fine */ }
			return;
		}
		case 0x12: case 0x13: d->a = (hw1 >> 8) & 7; d->imm = ((ip + 4) & ~3u) + (hw1 & 0xff) * 4; d->e = 4; d->fn = h_ld_lit; return;
		case 0x14: case 0x15: case 0x16: case 0x17:
			d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->e = (hw1 >> 6) & 7; d->op = (hw1 >> 9) & 7; d->fn = h_ldst_reg; return;
		case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
			int byte = (hw1 >> 12) & 1, load = (hw1 >> 11) & 1; unsigned imm5 = (hw1 >> 6) & 31;
			d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->imm = byte ? imm5 : imm5 * 4; d->e = byte ? 1 : 4;
			d->fn = load ? h_ld_imm : h_st_imm; return;
		}
		case 0x20: case 0x21: case 0x22: case 0x23:
			d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->imm = ((hw1 >> 6) & 31) * 2; d->e = 2;
			d->fn = (hw1 & 0x800) ? h_ld_imm : h_st_imm; return;
		case 0x24: case 0x25: case 0x26: case 0x27:
			d->a = (hw1 >> 8) & 7; d->b = 13; d->imm = (hw1 & 0xff) * 4; d->e = 4;
			d->fn = (hw1 & 0x800) ? h_ld_imm : h_st_imm; return;
		case 0x28: case 0x29: d->a = (hw1 >> 8) & 7; d->imm = ((ip + 4) & ~3u) + (hw1 & 0xff) * 4; d->fn = h_movi; return;
		case 0x2a: case 0x2b: d->a = (hw1 >> 8) & 7; d->imm = (hw1 & 0xff) * 4; d->fn = h_addspi; return;
		case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
			unsigned op = (hw1 >> 5) & 0x7f;
			if ((op & 0x7c) == 0x00) { d->imm = (hw1 & 0x7f) * 4; d->fn = h_spadj; return; }
			if ((op & 0x7c) == 0x04) { d->imm = (uint32_t) -(int32_t) ((hw1 & 0x7f) * 4); d->fn = h_spadj; return; }
			if ((op & 0x28) == 0x08) {
				uint32_t off = (((hw1 >> 9) & 1) << 6) | (((hw1 >> 3) & 31) << 1);
				d->a = hw1 & 7; d->op = (hw1 >> 11) & 1; d->imm = ip + 4 + off; d->fn = h_cbz; return;
			}
			if ((op & 0x78) == 0x10) { d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->op = (hw1 >> 6) & 3; d->fn = h_ext16; return; }
			if ((op & 0x70) == 0x20) { d->imm = (hw1 & 0xff) | ((hw1 & 0x100) ? 0x4000 : 0); d->e = (uint8_t) popcount16 (d->imm); d->fn = h_push; return; }
			if ((op & 0x78) == 0x50 && ((hw1 >> 6) & 3) != 2) {
				unsigned k = (hw1 >> 6) & 3; d->a = hw1 & 7; d->b = (hw1 >> 3) & 7; d->op = k == 3 ? 2 : (uint8_t) k; d->fn = h_rev; return;
			}
			if ((op & 0x70) == 0x60) { d->imm = (hw1 & 0xff) | ((hw1 & 0x100) ? 0x8000 : 0); d->fn = h_pop; return; }
			if ((op & 0x78) == 0x78) {
				if (hw1 & 15) { d->imm = hw1 & 0xff; d->fn = h_it; return; }
				if ((hw1 & 0xff) == 0) { d->fn = h_nop; return; }
			}
			return;                                              /* CPS, BKPT, hints: the full decoder */
		}
		case 0x30: case 0x31: case 0x32: case 0x33:
			d->a = (hw1 >> 8) & 7; d->imm = hw1 & 0xff; d->f1 = (hw1 >> 11) & 1; d->f2 = 1; d->e = (uint8_t) popcount16 (d->imm);
			d->fn = h_ldmia; return;
		case 0x34: case 0x35: case 0x36: case 0x37: {
			unsigned cond = (hw1 >> 8) & 15;
			if (cond >= 14) return;
			d->op = (uint8_t) cond; d->imm = ip + 4 + (uint32_t) ((int32_t) (int8_t) (hw1 & 0xff) * 2); d->fn = h_bcond; return;
		}
		case 0x38: case 0x39: d->imm = ip + 4 + (uint32_t) (((int32_t) (hw1 << 21)) >> 20); d->fn = h_b; return;
		}
		return;
	}
	/* 32-bit */
	uint32_t hw2 = fetch16 (c, ip + 2);
	d->len = 4;
	unsigned op1 = (hw1 >> 11) & 3, op2 = (hw1 >> 4) & 0x7f;
	if (op1 == 1) {
		if ((op2 & 0x64) == 0x00) {                                /* LDM / STM */
			unsigned op = (hw1 >> 7) & 3, L = (hw1 >> 4) & 1, W = (hw1 >> 5) & 1, rn = hw1 & 15;
			uint32_t list = hw2;
			if (rn == 15 || (!L && (list & 0x8000))) return;
			if (L && (list & 0x8000)) {
				if (op == 1 && rn == 13 && W) { d->imm = list; d->e = (uint8_t) popcount16 (list); d->fn = h_pop32; }
				return;
			}
			if (op == 1) { d->a = rn; d->imm = list; d->f1 = L; d->f2 = W; d->e = (uint8_t) popcount16 (list); d->fn = h_ldmia; return; }
			if (op == 2) {
				if (!L && W && rn != 13) { d->op = 1; d->f1 = (uint8_t) rn; d->imm = list; d->e = (uint8_t) popcount16 (list); d->fn = h_push; return; }
				if (!L && W && rn == 13) { d->imm = list; d->e = (uint8_t) popcount16 (list); d->fn = h_push; return; }
				d->a = rn; d->imm = list; d->f1 = L; d->f2 = W; d->e = (uint8_t) popcount16 (list); d->fn = h_ldmdb; return;
			}
			return;
		}
		if ((op2 & 0x64) == 0x04) {                                /* LDRD / STRD (immediate, not literal) */
			unsigned o1 = (hw1 >> 7) & 3, o2 = (hw1 >> 4) & 3, rn = hw1 & 15;
			if (((o1 & 2) || (o2 & 2)) && rn != 15) {
				unsigned P = (hw1 >> 8) & 1, U = (hw1 >> 7) & 1, W = (hw1 >> 5) & 1, L = (hw1 >> 4) & 1;
				uint32_t imm = (hw2 & 0xff) * 4;
				d->a = hw2 >> 12; d->e = (hw2 >> 8) & 15; d->b = (uint8_t) rn; d->imm = U ? imm : (uint32_t) -(int32_t) imm;
				d->f2 = (uint8_t) L; d->f3 = (uint8_t) (P | (W << 1));
				if (d->a == 15 || d->e == 15) { d->fn = GENERIC; return; }
				d->fn = h_ldrd;
			}
			return;
		}
		if ((op2 & 0x60) == 0x20) {                                /* data processing, shifted register */
			unsigned op = (hw1 >> 5) & 15, S = (hw1 >> 4) & 1, rn = hw1 & 15, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
			unsigned imm5 = ((hw2 >> 10) & 0x1c) | ((hw2 >> 6) & 3);
			int t; unsigned amt; dec_imm_shift ((hw2 >> 4) & 3, imm5, &t, &amt);
			if (rm == 15 || op == 6) return;
			if (op == 2 && rn == 15) op = 16;                      /* MOV */
			else if (op == 3 && rn == 15) op = 17;                 /* MVN */
			else if (rn == 15) return;
			int test = 0;
			if (rd == 15) { if (S && (op == 0 || op == 4 || op == 8 || op == 13)) test = 1; else return; }
			if (op != 0 && op != 1 && op != 2 && op != 3 && op != 4 && op != 8 && op != 10 && op != 11 && op != 13 && op != 14 && op != 16 && op != 17) return;
			d->op = (uint8_t) op; d->a = (uint8_t) rd; d->b = (uint8_t) (rn & 15); d->e = (uint8_t) rm; d->f1 = (uint8_t) S; d->f2 = (uint8_t) t; d->f3 = (uint8_t) test; d->imm = amt;
			d->fn = h_dpreg; return;
		}
		return;                                                    /* coprocessor / FP: the full decoder */
	}
	if (op1 == 2) {
		if (!(hw2 & 0x8000)) {
			if (!(op2 & 0x20)) {                                   /* data processing, modified immediate */
				unsigned op = (hw1 >> 5) & 15, S = (hw1 >> 4) & 1, rn = hw1 & 15, rd = (hw2 >> 8) & 15;
				uint32_t imm12 = ((hw1 & 0x400) << 1) | ((hw2 >> 4) & 0x700) | (hw2 & 0xff), co;
				uint32_t v = expand_imm_c (imm12, 2, &co);           /* (co 2: unchanged) */
				if (op == 2 && rn == 15) op = 16;
				else if (op == 3 && rn == 15) op = 17;
				else if (rn == 15) return;
				int test = 0;
				if (rd == 15) { if (S && (op == 0 || op == 4 || op == 8 || op == 13)) test = 1; else return; }
				if (op != 0 && op != 1 && op != 2 && op != 3 && op != 4 && op != 8 && op != 10 && op != 11 && op != 13 && op != 14 && op != 16 && op != 17) return;
				d->op = (uint8_t) op; d->a = (uint8_t) rd; d->b = (uint8_t) (rn & 15); d->imm = v; d->f1 = (uint8_t) S; d->f3 = (uint8_t) test;
				/* the carry out: logical ops take it from the expansion (when rotated), the arithmetic ones compute */
				d->f2 = (uint8_t) (co == 2 ? 0 : co ? 2 : 1);
				d->fn = h_dpimm; return;
			}
			{                                                      /* plain binary immediate */
				unsigned op = (hw1 >> 4) & 31, rn = hw1 & 15, rd = (hw2 >> 8) & 15;
				uint32_t imm12 = ((hw1 & 0x400) << 1) | ((hw2 >> 4) & 0x700) | (hw2 & 0xff);
				unsigned lsb = ((hw2 >> 10) & 0x1c) | ((hw2 >> 6) & 3), wm1 = hw2 & 31;
				if (rd == 15) return;
				switch (op) {
				case 0: if (rn == 15) { d->a = (uint8_t) rd; d->imm = ((ip + 4) & ~3u) + imm12; d->fn = h_movi; } else { d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->imm = imm12; d->fn = h_addw; } return;
				case 10: if (rn == 15) { d->a = (uint8_t) rd; d->imm = ((ip + 4) & ~3u) - imm12; d->fn = h_movi; } else { d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->imm = (uint32_t) -(int32_t) imm12; d->fn = h_addw; } return;
				case 4: d->a = (uint8_t) rd; d->imm = ((hw1 & 15) << 12) | imm12; d->fn = h_movi; return;
				case 12: d->a = (uint8_t) rd; d->imm = ((hw1 & 15) << 12) | imm12; d->fn = h_movt; return;
				case 20: case 28: if (rn == 15) return; d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->imm = lsb; d->e = (uint8_t) (wm1 + 1); d->op = op == 20; d->fn = h_ubfx; return;
				case 22: {
					if (wm1 < lsb) return;
					uint32_t mask = (wm1 - lsb + 1 >= 32 ? 0xffffffffu : ((1u << (wm1 - lsb + 1)) - 1)) << lsb;
					d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->imm = mask; d->e = (uint8_t) lsb; d->op = rn == 15; d->fn = h_bfi; return;
				}
				}
				return;
			}
		}
		{                                                          /* branches */
			unsigned o1 = (hw2 >> 12) & 7, op = (hw1 >> 4) & 0x7f;
			if ((o1 & 5) == 0) {
				if ((op & 0x38) != 0x38) {
					uint32_t S = (hw1 >> 10) & 1, J1 = (hw2 >> 13) & 1, J2 = (hw2 >> 11) & 1;
					int32_t off = (int32_t) ((S << 20) | (J2 << 19) | (J1 << 18) | ((hw1 & 0x3f) << 12) | ((hw2 & 0x7ff) << 1));
					off = (off << 11) >> 11;
					d->op = (hw1 >> 6) & 15; d->imm = ip + 4 + (uint32_t) off; d->fn = h_bcond;
				}
				return;
			}
			uint32_t S = (hw1 >> 10) & 1, J1 = (hw2 >> 13) & 1, J2 = (hw2 >> 11) & 1;
			uint32_t I1 = !(J1 ^ S), I2 = !(J2 ^ S);
			int32_t off = (int32_t) ((S << 24) | (I1 << 23) | (I2 << 22) | ((hw1 & 0x3ff) << 12) | ((hw2 & 0x7ff) << 1));
			off = (off << 7) >> 7;
			d->imm = ip + 4 + (uint32_t) off;
			if (o1 & 4) { if (o1 & 1) d->fn = h_bl; return; }
			d->fn = h_b; return;
		}
	}
	/* op1 == 3 */
	if ((op2 & 0x71) == 0x00 || (op2 & 0x67) == 0x01 || (op2 & 0x67) == 0x03 || (op2 & 0x67) == 0x05) {   /* load / store single */
		int load = (op2 & 1) != 0;
		unsigned size = (hw1 >> 5) & 3, sgn = (hw1 >> 8) & 1, rn = hw1 & 15, rt = hw2 >> 12;
		int n = size == 0 ? 1 : size == 1 ? 2 : 4;
		if (!load) sgn = 0;
		if (load && rt == 15) return;                              /* (PLD / PLI, loads to PC: the full decoder) */
		if (size == 3) return;
		d->a = (uint8_t) rt; d->b = (uint8_t) rn; d->e = (uint8_t) n; d->f1 = (uint8_t) sgn; d->f2 = (uint8_t) load;
		if (rn == 15) {
			if (!load) return;
			uint32_t imm = hw2 & 0xfff, base = (ip + 4) & ~3u;
			d->imm = (hw1 & 0x80) ? base + imm : base - imm; d->fn = h_ld_lit; return;
		}
		if (hw1 & 0x80) { d->imm = hw2 & 0xfff; d->fn = load ? h_ld_imm : h_st_imm; return; }
		if ((hw2 & 0x800) == 0) {
			if (hw2 & 0x7c0) return;
			d->f3 = hw2 & 15; d->imm = (hw2 >> 4) & 3;
			if (d->f3 == 15 || d->f3 == 13) return;
			d->fn = h_ldst_rs; return;
		}
		{
			unsigned P = (hw2 >> 10) & 1, U = (hw2 >> 9) & 1, W = (hw2 >> 8) & 1, imm = hw2 & 0xff;
			if ((hw2 & 0xf00) == 0xe00) return;                    /* (unprivileged forms) */
			d->imm = U ? imm : (uint32_t) -(int32_t) imm;
			d->f3 = (uint8_t) (P | ((W || !P) << 1));
			d->fn = h_ldst_idx; return;
		}
	}
	if ((op2 & 0x70) == 0x20) {                                    /* data processing (register) */
		unsigned o1 = (hw1 >> 4) & 15, o2 = (hw2 >> 4) & 15, rn = hw1 & 15, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
		if (rd == 15 || rm == 15) return;
		if ((o1 & 8) == 0 && o2 == 0) { if (rn == 15) return; d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->e = (uint8_t) rm; d->op = (uint8_t) ((o1 >> 1) & 3); d->f1 = o1 & 1; d->fn = h_shreg; return; }
		if ((o1 & 8) == 0 && (o2 & 8)) {
			unsigned k = o1 & 7;
			if (k != 0 && k != 1 && k != 4 && k != 5) return;
			d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->e = (uint8_t) rm; d->op = (uint8_t) k; d->imm = ((hw2 >> 4) & 3) * 8; d->f1 = rn != 15;
			d->fn = h_ext32; return;
		}
		if (o1 == 0xb && o2 == 8) { d->a = (uint8_t) rd; d->e = (uint8_t) rm; d->fn = h_clz; return; }
		return;
	}
	if ((op2 & 0x78) == 0x30) {                                    /* MUL / MLA / MLS */
		unsigned o1 = (hw1 >> 4) & 7, o2 = (hw2 >> 4) & 3, rn = hw1 & 15, ra = hw2 >> 12, rd = (hw2 >> 8) & 15, rm = hw2 & 15;
		if (o1 != 0 || o2 > 1 || rd == 15 || rn == 15 || rm == 15) return;
		d->a = (uint8_t) rd; d->b = (uint8_t) rn; d->e = (uint8_t) rm; d->f1 = (uint8_t) ra;
		d->op = (uint8_t) (o2 == 1 ? 2 : ra == 15 ? 0 : 1);
		d->fn = h_mul32; return;
	}
	if ((op2 & 0x78) == 0x38) {                                    /* long multiply, divide */
		unsigned o1 = (hw1 >> 4) & 7, o2 = (hw2 >> 4) & 15, rn = hw1 & 15, rlo = hw2 >> 12, rhi = (hw2 >> 8) & 15, rm = hw2 & 15;
		if (rn == 15 || rm == 15 || rhi == 15) return;
		if ((o1 == 1 || o1 == 3) && o2 == 15) { d->a = (uint8_t) rhi; d->b = (uint8_t) rn; d->e = (uint8_t) rm; d->op = o1 == 1; d->fn = h_div; return; }
		if (o2 != 0 || rlo == 15) return;
		d->a = (uint8_t) rlo; d->f1 = (uint8_t) rhi; d->b = (uint8_t) rn; d->e = (uint8_t) rm;
		switch (o1) { case 0: d->op = 0; break; case 2: d->op = 1; break; case 4: d->op = 2; break; case 6: d->op = 3; break; default: return; }
		d->fn = h_lmul; return;
	}
}

static di_t *dcache_for (cm33_t *c)
{
	cm33_sys_t *s = c->sys;
	void **slot = (c->core & 1) ? &s->dcache1 : &s->dcache;          /* (one per core: they may run on two host threads) */
	if (!s->code_pages) s->code_pages = (uint8_t *) calloc ((s->ram_size >> 12) + 2, 1);
	if (!*slot) {
		di_t *dc = (di_t *) calloc ((s->ram_size >> 1) + 2, sizeof (di_t));
		if (dc) for (uint32_t k = 0; k < (s->ram_size >> 1) + 2; k++) dc[k].fn = h_decode;
		*slot = dc;
	}
	if (!*slot || !s->code_pages) return NULL;
	return (di_t *) *slot;
}

/* the full decoder, out of line: the run loop stays small (its hot path keeps its registers) */
static NOINLINE void step_out (cm33_t *c) { step (c); }

/* an entry not decoded yet: decode it (its code page noted, for writes there), then run it */
/* ----- specialised handlers: the commonest ones once per opcode / size, without the switch inside (that jump
 * is an unpredictable one when one handler serves many instructions) */
#define DPI_LOGIC(name, EXPR) static void name (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; \
	uint32_t a = c->r[d->b], b = d->imm, r = (EXPR); (void) a; \
	if (!d->f3) c->r[d->a] = r; if (d->f1) { nz (c, r); if (d->f2) c->c = (uint32_t) (d->f2 - 1); } }
#define DPI_ARITH(name, X, Y, CIN) static void name (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; \
	uint32_t a = c->r[d->b], b = d->imm, co, ov, r = add_c ((X), (Y), (CIN), &co, &ov); \
	if (!d->f3) c->r[d->a] = r; if (d->f1) { nz (c, r); c->c = co; c->v = ov; } }
DPI_LOGIC (h_dpi_and, a & b) DPI_LOGIC (h_dpi_bic, a & ~b) DPI_LOGIC (h_dpi_orr, a | b) DPI_LOGIC (h_dpi_orn, a | ~b)
DPI_LOGIC (h_dpi_eor, a ^ b) DPI_LOGIC (h_dpi_mov, b) DPI_LOGIC (h_dpi_mvn, ~b)
DPI_ARITH (h_dpi_add, a, b, 0) DPI_ARITH (h_dpi_adc, a, b, c->c) DPI_ARITH (h_dpi_sbc, a, ~b, c->c)
DPI_ARITH (h_dpi_sub, a, ~b, 1) DPI_ARITH (h_dpi_rsb, b, ~a, 1)
static void h_dpi_addn (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = c->r[d->b] + d->imm; }   /* (no flags) */
static void h_dpi_subn (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = c->r[d->b] - d->imm; }
static void h_dpi_cmp (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; uint32_t co, ov, r = add_c (c->r[d->b], ~d->imm, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov; }

#define DPR_LOGIC(name, EXPR) static void name (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; \
	uint32_t sc, b = shift_c (c->r[d->e], d->f2, d->imm, c->c, &sc), a = c->r[d->b], r = (EXPR); (void) a; \
	if (!d->f3) c->r[d->a] = r; if (d->f1) { nz (c, r); c->c = sc; } }
#define DPR_ARITH(name, X, Y, CIN) static void name (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; \
	uint32_t sc, b = shift_c (c->r[d->e], d->f2, d->imm, c->c, &sc), a = c->r[d->b], co, ov, r = add_c ((X), (Y), (CIN), &co, &ov); \
	if (!d->f3) c->r[d->a] = r; if (d->f1) { nz (c, r); c->c = co; c->v = ov; } }
DPR_LOGIC (h_dpr_and, a & b) DPR_LOGIC (h_dpr_bic, a & ~b) DPR_LOGIC (h_dpr_orr, a | b) DPR_LOGIC (h_dpr_orn, a | ~b)
DPR_LOGIC (h_dpr_eor, a ^ b) DPR_LOGIC (h_dpr_mov, b) DPR_LOGIC (h_dpr_mvn, ~b)
DPR_ARITH (h_dpr_add, a, b, 0) DPR_ARITH (h_dpr_adc, a, b, c->c) DPR_ARITH (h_dpr_sbc, a, ~b, c->c)
DPR_ARITH (h_dpr_sub, a, ~b, 1) DPR_ARITH (h_dpr_rsb, b, ~a, 1)

/* 16-bit MOVS / CMP / ADDS / SUBS #imm8 */
static void h_movs8 (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = d->imm; if (!c->it) nz (c, d->imm); }
static void h_cmp8 (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; uint32_t co, ov, r = add_c (c->r[d->a], ~d->imm, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov; }
static void h_adds8 (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; uint32_t co, ov, r = add_c (c->r[d->a], d->imm, 0, &co, &ov); c->r[d->a] = r; if (!c->it) { nz (c, r); c->c = co; c->v = ov; } }
static void h_subs8 (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; uint32_t co, ov, r = add_c (c->r[d->a], ~d->imm, 1, &co, &ov); c->r[d->a] = r; if (!c->it) { nz (c, r); c->c = co; c->v = ov; } }

/* 16-bit data processing, register */
#define DP16(name, BODY) static void name (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; \
	unsigned rdn = d->a; uint32_t a = c->r[rdn], b = c->r[d->b], co = c->c, ov = c->v, r; (void) a; (void) co; (void) ov; BODY }
#define DP16_SET if (!c->it) { nz (c, r); c->c = co; c->v = ov; }
DP16 (h_dp16_and, r = a & b; c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_eor, r = a ^ b; c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_lsl, r = shift_c (a, 0, b & 0xff, c->c, &co); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_lsr, r = shift_c (a, 1, b & 0xff, c->c, &co); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_asr, r = shift_c (a, 2, b & 0xff, c->c, &co); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_adc, r = add_c (a, b, c->c, &co, &ov); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_sbc, r = add_c (a, ~b, c->c, &co, &ov); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_ror, r = shift_c (a, 3, b & 0xff, c->c, &co); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_tst, r = a & b; nz (c, r); c->c = co;)
DP16 (h_dp16_rsb, r = add_c (0, ~b, 1, &co, &ov); c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_cmp, r = add_c (a, ~b, 1, &co, &ov); nz (c, r); c->c = co; c->v = ov;)
DP16 (h_dp16_cmn, r = add_c (a, b, 0, &co, &ov); nz (c, r); c->c = co; c->v = ov;)
DP16 (h_dp16_orr, r = a | b; c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_mul, r = a * b; c->r[rdn] = r; if (!c->it) nz (c, r);)
DP16 (h_dp16_bic, r = a & ~b; c->r[rdn] = r; DP16_SET)
DP16 (h_dp16_mvn, r = ~b; c->r[rdn] = r; DP16_SET)

/* loads and stores with an immediate offset, by size */
static void h_ldw_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = rd_mem (c, c->r[d->b] + d->imm, 4); c->cycles++; }
static void h_ldb_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = rd_mem (c, c->r[d->b] + d->imm, 1); c->cycles++; }
static void h_ldh_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; c->r[d->a] = rd_mem (c, c->r[d->b] + d->imm, 2); c->cycles++; }
static void h_stw_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; WR (c->r[d->b] + d->imm, c->r[d->a], 4); c->cycles++; }
static void h_stb_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; WR (c->r[d->b] + d->imm, c->r[d->a], 1); c->cycles++; }
static void h_sth_imm (cm33_t *c, const di_t *d, uint32_t ip) { (void) ip; WR (c->r[d->b] + d->imm, c->r[d->a], 2); c->cycles++; }

/* conditional branches, one per condition */
#define BCC(name, COND) static void name (cm33_t *c, const di_t *d, uint32_t ip) { \
	if (COND) { c->r[15] = d->imm; c->cycles++; if (d->imm == ip) c->sleeping = 1, c->attn = 1; } }
BCC (h_beq, c->z) BCC (h_bne, !c->z) BCC (h_bcs, c->c) BCC (h_bcc, !c->c) BCC (h_bmi, c->n) BCC (h_bpl, !c->n)
BCC (h_bvs, c->v) BCC (h_bvc, !c->v) BCC (h_bhi, c->c && !c->z) BCC (h_bls, !(c->c && !c->z))
BCC (h_bge, c->n == c->v) BCC (h_blt, c->n != c->v) BCC (h_bgt, c->n == c->v && !c->z) BCC (h_ble, !(c->n == c->v && !c->z))

/* the FP coprocessor's instructions (cp10 / cp11): straight to its code, not through the full decoder */
static void h_vfp (cm33_t *c, const di_t *d, uint32_t ip)
{
	if (!copro (c, d->imm >> 16, d->imm & 0xffff, ip)) { fault (c, "undefined (coprocessor)", ip, d->imm); return; }
	c->cycles++;
}

/* a decoded entry to its specialised handler, where there is one */
static void specialise (di_t *d)
{
	static const hfn bcc[14] = { h_beq, h_bne, h_bcs, h_bcc, h_bmi, h_bpl, h_bvs, h_bvc, h_bhi, h_bls, h_bge, h_blt, h_bgt, h_ble };
	static const hfn dpi[18] = { h_dpi_and, h_dpi_bic, h_dpi_orr, h_dpi_orn, h_dpi_eor, 0, 0, 0, h_dpi_add, 0, h_dpi_adc, h_dpi_sbc, 0, h_dpi_sub, h_dpi_rsb, 0, h_dpi_mov, h_dpi_mvn };
	static const hfn dpr[18] = { h_dpr_and, h_dpr_bic, h_dpr_orr, h_dpr_orn, h_dpr_eor, 0, 0, 0, h_dpr_add, 0, h_dpr_adc, h_dpr_sbc, 0, h_dpr_sub, h_dpr_rsb, 0, h_dpr_mov, h_dpr_mvn };
	static const hfn dp16[16] = { h_dp16_and, h_dp16_eor, h_dp16_lsl, h_dp16_lsr, h_dp16_asr, h_dp16_adc, h_dp16_sbc, h_dp16_ror,
	                              h_dp16_tst, h_dp16_rsb, h_dp16_cmp, h_dp16_cmn, h_dp16_orr, h_dp16_mul, h_dp16_bic, h_dp16_mvn };
	static const hfn imm8[4] = { h_movs8, h_cmp8, h_adds8, h_subs8 };
	hfn f = d->fn;
	if (f == h_dpimm && d->op < 18 && dpi[d->op]) {
		if (d->op == 8 && !d->f1 && !d->f3) d->fn = h_dpi_addn;
		else if (d->op == 13 && !d->f1 && !d->f3) d->fn = h_dpi_subn;
		else if (d->op == 13 && d->f1 && d->f3) d->fn = h_dpi_cmp;
		else d->fn = dpi[d->op];
	}
	else if (f == h_dpreg && d->op < 18 && dpr[d->op]) d->fn = dpr[d->op];
	else if (f == h_dp16) d->fn = dp16[d->op & 15];
	else if (f == h_imm8) d->fn = imm8[d->op & 3];
	else if (f == h_ld_imm && !d->f1) d->fn = d->e == 4 ? h_ldw_imm : d->e == 2 ? h_ldh_imm : h_ldb_imm;
	else if (f == h_st_imm) d->fn = d->e == 4 ? h_stw_imm : d->e == 2 ? h_sth_imm : h_stb_imm;
	else if (f == h_bcond && d->op < 14) d->fn = bcc[d->op];
}

static void decode_at (cm33_t *c, uint32_t ip, di_t *d)
{
	cm33_sys_t *s = c->sys;
	uint32_t o = ip - s->ram_base;
	decode (c, ip, d);
	if (d->fn == GENERIC && cm33_specialise) {                         /* (an FP instruction: its own handler) */
		uint32_t hw1 = fetch16 (c, ip), hw2 = fetch16 (c, ip + 2);
		if ((hw1 >> 11) >= 0x1d && (hw1 & 0xec00) == 0xec00 && ((hw2 >> 8) & 14) == 10) { d->fn = h_vfp; d->imm = (hw1 << 16) | hw2; d->len = 4; }
	}
	if (cm33_specialise) specialise (d);
	s->code_pages[o >> 12] = 1; if (o + 4 > ((o >> 12) + 1) << 12) s->code_pages[(o >> 12) + 1] = 1;
}
static void h_decode (cm33_t *c, const di_t *d, uint32_t ip)
{
	c->r[15] = ip; c->cycles--;                                         /* (undo what the loop did before the call) */
	decode_at (c, ip, (di_t *) d);
	if (d->fn == GENERIC) { step_out (c); c->attn = 1; return; }
	c->cycles++; c->r[15] = ip + d->len; d->fn (c, d, ip);
}
/* an instruction without a handler of its own: the full decoder (which advances IT and watches loops itself) */
static void h_generic (cm33_t *c, const di_t *d, uint32_t ip)
{
	(void) d;
	c->r[15] = ip; c->cycles--;
	if (cm33_gen_log) { cm33_gen_count++; cm33_gen_hist[(ip >> 1) & 4095] = ip; cm33_gen_op[c->core & 1][FETCH (ip) >> 4]++; }
	step_out (c); c->attn = 1;
}

/* one instruction the slow way: outside RAM, or inside an IT block */
static NOINLINE void exec_slow (cm33_t *c, di_t *dc)
{
	uint32_t ip = c->r[15];
	cm33_sys_t *s = c->sys;
	uint32_t o = ip - s->ram_base;
	if (!dc || o + 4 > s->ram_size) { step_out (c); c->attn = 1; return; }
	di_t *d = &dc[o >> 1];
	if (d->fn == h_decode) decode_at (c, ip, d);
	if (d->fn == GENERIC) { step_out (c); c->attn = 1; return; }
	c->last_pc = ip;
	int in_it = c->it != 0;
	if (in_it && !cond_pass (c, (c->it >> 4) & 15)) { c->r[15] = ip + d->len; c->cycles++; }
	else { c->cycles++; c->r[15] = ip + d->len; d->fn (c, d, ip); }
	post (c, ip, in_it);
}

/* ---------------------------------------------------------------- the run loop */
static void systick (cm33_t *c, uint64_t dt)
{
	if (!(c->syst_csr & 1) || !dt) return;
	/* (CLKSOURCE 1: the core clock; 0: the 1 MHz reference - the system gives the ratio as 150) */
	uint64_t ticks = (c->syst_csr & 4) ? dt : 0;
	if (!(c->syst_csr & 4)) { c->syst_t += dt; ticks = c->syst_t / 150; c->syst_t %= 150; }
	while (ticks) {
		if (c->syst_cvr == 0) { c->syst_cvr = c->syst_rvr; ticks--; continue; }
		if (ticks < c->syst_cvr) { c->syst_cvr -= (uint32_t) ticks; break; }
		ticks -= c->syst_cvr; c->syst_cvr = 0;
		c->syst_csr |= 0x10000u;
		if (c->syst_csr & 2) c->icsr_pend |= 1u << 26, c->attn = 1;
		c->syst_cvr = c->syst_rvr;
		if (!c->syst_rvr) break;
	}
}

/* a counting spin: how many rounds its counters may move on without reaching a value another register
 * holds (or 0) - that may be where the loop ends */
static uint64_t spin_cap_rounds (cm33_t *c)
{
	uint64_t k = 1000000;
	for (int i = 0; i < 13; i++) {
		int32_t d = (int32_t) c->spin_delta[i];
		if (!d) continue;
		uint32_t ad = d < 0 ? (uint32_t) -d : (uint32_t) d;
		for (int j = -1 - c->spin_nb; j < 13; j++) {
			if (j == i || (j >= 0 && c->spin_delta[j])) continue;
			if (j < -1 && c->spin_breg[-2 - j] != i) continue;
			uint32_t v = j < -1 ? c->spin_bval[-2 - j] : j < 0 ? 0 : c->r[j];
			uint32_t dist = d > 0 ? v - c->r[i] : c->r[i] - v;      /* (ahead, in the counting direction) */
			uint64_t steps = dist / ad;
			if (steps == 0) return 0;
			if (steps - 1 < k) k = steps - 1;
		}
	}
	return k;
}
/* it slept for spin_skipped cycles: the rounds it would have run move its counters on */
void cm33_spin_wake (cm33_t *c)
{
	uint64_t k = c->spin_iter ? c->spin_skipped / c->spin_iter : 0, cap = spin_cap_rounds (c);
	if (k > cap) k = cap;
	if (k) for (int i = 0; i < 13; i++) if (c->spin_delta[i]) c->r[i] += (uint32_t) (k * c->spin_delta[i]);
	for (int i = 0; i < 13; i++) c->spin_prev[i] = c->r[i];
	c->spin_skipped = 0; c->spin_last = 0; c->spin_stable = 0; c->spin_run = 0;
}

/* the run loop's rarer work, when attn is up: a polling loop seen (the slice cut short), a fault, an interrupt,
 * a sleep. 0: run on, 1: the slice ends, 2: look again */
static NOINLINE int run_attn (cm33_t *c, uint64_t until)
{
	c->attn = 0;
	if (c->poll_skip) {
		c->poll_skip = 0;
		if (!c->wait_on && c->spin_slow && c->spin_stable && c->spin_iter && c->prev_loop_loads == 1) {
			/* a steady counting spin: asleep on the value it reads, at most until its counters could matter */
			uint64_t cap = spin_cap_rounds (c);
			if (cap > 1) { c->wait_on = 2; c->wait_addr = c->ld_addr; c->wait_val = c->ld_val; c->wait_n = c->ld_n;
			               c->spin_cap = (cap - 1) * c->spin_iter; c->spin_skipped = until > c->cycles ? until - c->cycles : 0;   if (cm33_spin_log) { static uint32_t seen[256]; static int ns; int k; for (k = 0; k < ns; k++) if (seen[k] == c->r[15]) break;
			    if (k == ns && ns < 256) { seen[ns++] = c->r[15]; fprintf (stderr, "skip-ahead core %d pc %08X-%08X iter %llu cap %llu ld %08X=%08X d:", c->core, c->spin_lo, c->spin_hi, (unsigned long long) c->spin_iter, (unsigned long long) cap, c->wait_addr, c->wait_val);
			      for (int i = 0; i < 13; i++) if (c->spin_delta[i]) fprintf (stderr, " r%d%+d(%08X)", i, (int) c->spin_delta[i], c->r[i]); fputc (10, stderr); } } }
		}
		if (c->wait_on || c->halted) { if (c->cycles < until) c->cycles = until; c->attn = 1; return 1; }
	}
	if (c->halted) { c->cycles = until; c->attn = 1; return 1; }
	if (c->icsr_pend | (c->nvic_pend[0] & c->nvic_en[0]) | (c->nvic_pend[1] & c->nvic_en[1])) {
		if (c->it) c->attn = 1;                                         /* (not inside an IT block: look again next time) */
		else {
			int e = exc_pending (c);
			if (e) { uint32_t ret = c->r[15]; exc_entry (c, e, ret); return 2; }
		}
	}
	if (c->sleeping) {
		/* WFE also wakes (spuriously, as the architecture allows - software checks again) while a timer alarm's
		 * interrupt is pending but disabled: VecOS (UVMC2) turns every interrupt but USB's off on core 0, and
		 * tinyusb's enumeration then sleeps there (pico-sdk sleep_until: WFE until its alarm's callback SEVs) -
		 * the sleep polls the clock instead and ends on time */
		if (c->sleeping == 1 ? any_wake (c) : (c->event || any_wake (c) || (c->nvic_pend[0] & ~c->nvic_en[0] & 0xffu))) {
			static int wl = -1; static unsigned nw; if (wl < 0) wl = getenv ("VT_WAKELOG") != NULL;
			if (wl && nw++ < 200) fprintf (stderr, "wake: core %d pc %08X sleep %d event %d irq %d t %.4f%c", c->core, c->r[15], c->sleeping, c->event, any_wake (c), (double) c->cycles / 150e6, 10);
			if (c->sleeping == 2) c->event = 0; c->sleeping = 0;
		}
		else { c->cycles = until; c->attn = 1; return 1; }
	}
	return 0;
}

uint64_t cm33_run (cm33_t *c, uint64_t until)
{
	uint64_t start = c->cycles;
	di_t *dc = dcache_for (c);
	unsigned long long nsteps = 0;
	uint32_t base = c->sys->ram_base, size = c->sys->ram_size;
	int slow = !dc;
	/* (attn: something happened the loop must look at - a peripheral read or write, the SCS, an exception, a
	 * sleep, a fault, a polling loop, an instruction of the full decoder; and at the start) */
	c->attn = 1;
	for (;;) {
		if (c->attn) { int r = run_attn (c, until); if (r == 1) break; if (r == 2) continue; }
		if (c->cycles >= until) break;
		/* the fast path: an instruction in RAM, not in an IT block - one call through its decoded entry */
		do {
			uint32_t ip = c->r[15], o = ip - base;
			if (o + 4 > size || c->it || slow) exec_slow (c, dc);
			else {
				const di_t *d = &dc[o >> 1];
				hfn fn = d->fn;
				c->last_pc = ip;
				c->cycles++; c->r[15] = ip + d->len; fn (c, d, ip);
				if (c->r[15] < ip && ip - c->r[15] <= 64 && cm33_spin_mode && d->fn != GENERIC) loop_round (c, ip);
			}
			nsteps++;
		} while (!c->attn && c->cycles < until);
	}
	/* (SysTick: the slice's cycles at once - its interrupt is taken from the next slice on) */
	if (c->syst_csr & 1) systick (c, c->cycles - start);
	cm33_steps[(c->core & 1) * 8] += nsteps;
	return c->cycles - start;
}
