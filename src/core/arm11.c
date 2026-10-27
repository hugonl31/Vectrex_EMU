/* arm11.c - an ARM1176JZF-S interpreter (ARMv6: the Raspberry Pi Zero of the PiTrex). See arm11.h.
 *
 * The ARM instruction set of ARMv6 (with the v6K hints and exclusives), the modes with their banked registers,
 * exceptions, CP15 (control, the cycle counter; the MMU's and the caches' operations are accepted - programs
 * map memory flat), VFPv2 (single and double precision, scalar). From the ARM Architecture Reference Manual
 * (ARMv6, DDI 0100I) and the ARM1176JZF-S TRM. Checked against unicorn (ARM1176) instruction by instruction.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "arm11.h"

#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_UND 0x1b
#define MODE_SYS 0x1f

static int bank_of (uint32_t mode)
{
	switch (mode) {
	case MODE_FIQ: return 1; case MODE_IRQ: return 2; case MODE_SVC: return 3;
	case MODE_ABT: return 4; case MODE_UND: return 5; default: return 0;
	}
}
static void set_mode (arm11_t *c, uint32_t m)
{
	if (m == c->mode) return;
	int ob = bank_of (c->mode), nb = bank_of (m);
	if (ob != nb) {
		c->bank_r13[ob] = c->r[13]; c->bank_r14[ob] = c->r[14];
		if (ob == 1) { for (int i = 0; i < 5; i++) { c->fiq_r8_12[i] = c->r[8 + i]; c->r[8 + i] = c->usr_r8_12[i]; } }
		if (nb == 1) { for (int i = 0; i < 5; i++) { c->usr_r8_12[i] = c->r[8 + i]; c->r[8 + i] = c->fiq_r8_12[i]; } }
		c->r[13] = c->bank_r13[nb]; c->r[14] = c->bank_r14[nb];
	}
	c->mode = m;
}
uint32_t arm11_cpsr (const arm11_t *c)
{
	return (c->n << 31) | (c->z << 30) | (c->c << 29) | (c->v << 28) | (c->q << 27) | (c->ge << 16) | (c->ebit << 9)
	     | (c->abit << 8) | (c->ibit << 7) | (c->fbit << 6) | (c->tbit << 5) | c->mode;
}
/* write CPSR fields: mask bit 0 control (mode, I, F, T only from exception returns), 3 flags, 1 E (via its byte) */
static void set_cpsr (arm11_t *c, uint32_t v, unsigned fields, int all)
{
	if (fields & 8) { c->n = v >> 31; c->z = (v >> 30) & 1; c->c = (v >> 29) & 1; c->v = (v >> 28) & 1; c->q = (v >> 27) & 1; }
	if (fields & 4) c->ge = (v >> 16) & 15;
	if (fields & 2) { c->ebit = (v >> 9) & 1; if (c->mode != MODE_USR) c->abit = (v >> 8) & 1; }
	if ((fields & 1) && c->mode != MODE_USR) {
		c->ibit = (v >> 7) & 1; c->fbit = (v >> 6) & 1;
		if (all) c->tbit = (v >> 5) & 1;
		set_mode (c, v & 0x1f);
	}
}
static uint32_t *spsr_p (arm11_t *c) { int b = bank_of (c->mode); return b ? &c->bank_spsr[b] : NULL; }

/* ---------------------------------------------------------------- memory */
static int tr_on, tr_bad; static uint32_t tr_addr, tr_val;   /* (arm11_trial: a round on a copy - addr reads val, nothing else outside RAM) */
static uint32_t mrd (arm11_t *c, uint32_t a, int n)
{
	uint32_t v;
	if (a < c->sys->ram_size && a + n <= c->sys->ram_size) {
		const uint8_t *p = c->sys->ram + a;
		if (n == 4) memcpy (&v, p, 4); else if (n == 2) { uint16_t h; memcpy (&h, p, 2); v = h; } else v = *p;
	} else if (tr_on) { if (a == tr_addr && n == 4) v = tr_val; else { tr_bad = 1; v = 0; } c->mmio_round = 1; }
	else { v = c->sys->rd (c, a, n); c->mmio_round = 1; }
	c->loop_loads++; c->ld_addr = a; c->ld_val = v; c->ld_hash = (c->ld_hash ^ a) * 0x9E3779B1u + v;
	return v;
}
static void mwr (arm11_t *c, uint32_t a, uint32_t v, int n)
{
	if (tr_on) { tr_bad = 1; return; }
	c->loop_stores++;
	if (c->excl && ((c->excl_addr ^ a) & ~7u) == 0) c->excl = 0;
	if (a < c->sys->ram_size && a + n <= c->sys->ram_size) {
		uint8_t *p = c->sys->ram + a;
		if (n == 4) memcpy (p, &v, 4); else if (n == 2) { uint16_t h = (uint16_t) v; memcpy (p, &h, 2); } else *p = (uint8_t) v;
		return;
	}
	c->sys->wr (c, a, v, n);
}
#define RD(a, n) mrd (c, (a), (n))
#define WR(a, v, n) mwr (c, (a), (v), (n))

static void fault (arm11_t *c, const char *why, uint32_t pc, uint32_t ins)
{
	if (!c->halted) { snprintf (c->fault, sizeof c->fault, "%s at %08X (%08X)", why, pc, ins); c->halted = 1; }
}

/* ---------------------------------------------------------------- exceptions */
static void exception (arm11_t *c, uint32_t vec, uint32_t newmode, uint32_t lr)
{
	uint32_t old = arm11_cpsr (c);
	set_mode (c, newmode);
	c->bank_spsr[bank_of (newmode)] = old;
	c->r[14] = lr;
	c->ibit = 1;
	if (newmode == MODE_FIQ || vec == 0) c->fbit = 1;
	if (vec == 0x0c || vec == 0x10 || vec == 0x18 || vec == 0x1c) c->abit = 1;
	c->tbit = 0;
	c->ebit = (c->sctlr >> 25) & 1;
	uint32_t base = (c->sctlr & (1u << 13)) ? 0xffff0000u : c->vbar;
	c->r[15] = base + vec;
	c->sleeping = 0; c->wait_on = 0;
	c->cycles += 8;
}
void arm11_reset (arm11_t *c, uint32_t pc)
{
	arm11_sys_t *s = c->sys; void *u = c->user;
	memset (c, 0, sizeof *c);
	c->sys = s; c->user = u;
	c->mode = MODE_SVC; c->ibit = 1; c->fbit = 1; c->abit = 1;
	c->sctlr = 0x00050078u;
	c->r[15] = pc;
}

/* ---------------------------------------------------------------- helpers */
static inline uint32_t ror32 (uint32_t x, unsigned n) { n &= 31; return n ? (x >> n) | (x << (32 - n)) : x; }
static inline int cond_ok (arm11_t *c, unsigned cond)
{
	switch (cond) {
	case 0: return c->z; case 1: return !c->z; case 2: return c->c; case 3: return !c->c;
	case 4: return c->n; case 5: return !c->n; case 6: return c->v; case 7: return !c->v;
	case 8: return c->c && !c->z; case 9: return !c->c || c->z;
	case 10: return c->n == c->v; case 11: return c->n != c->v;
	case 12: return !c->z && c->n == c->v; case 13: return c->z || c->n != c->v;
	default: return 1;
	}
}
static inline uint32_t add_c (uint32_t a, uint32_t b, uint32_t cin, uint32_t *co, uint32_t *ov)
{
	uint64_t r = (uint64_t) a + b + cin;
	uint32_t r32 = (uint32_t) r;
	*co = (uint32_t) (r >> 32);
	*ov = ((~(a ^ b) & (a ^ r32)) >> 31) & 1;
	return r32;
}
/* a shift by an immediate (type 0 LSL 1 LSR 2 ASR 3 ROR/RRX), with the carry out */
static inline uint32_t shift_imm (uint32_t v, unsigned type, unsigned imm, uint32_t cin, uint32_t *co)
{
	switch (type) {
	case 0: if (!imm) { *co = cin; return v; } *co = (v >> (32 - imm)) & 1; return v << imm;
	case 1: if (!imm) { *co = v >> 31; return 0; } *co = (v >> (imm - 1)) & 1; return v >> imm;
	case 2: if (!imm) { *co = v >> 31; return (uint32_t) ((int32_t) v >> 31); } *co = (v >> (imm - 1)) & 1; return (uint32_t) ((int32_t) v >> imm);
	default:
		if (!imm) { *co = v & 1; return (cin << 31) | (v >> 1); }       /* RRX */
		*co = (v >> (imm - 1)) & 1; return ror32 (v, imm);
	}
}
/* a shift by a register's bottom byte */
static inline uint32_t shift_reg (uint32_t v, unsigned type, unsigned amt, uint32_t cin, uint32_t *co)
{
	amt &= 0xff;
	if (!amt) { *co = cin; return v; }
	switch (type) {
	case 0: if (amt < 32) { *co = (v >> (32 - amt)) & 1; return v << amt; } *co = amt == 32 ? v & 1 : 0; return 0;
	case 1: if (amt < 32) { *co = (v >> (amt - 1)) & 1; return v >> amt; } *co = amt == 32 ? v >> 31 : 0; return 0;
	case 2: if (amt < 32) { *co = (v >> (amt - 1)) & 1; return (uint32_t) ((int32_t) v >> amt); } *co = v >> 31; return (uint32_t) ((int32_t) v >> 31);
	default: { unsigned r = amt & 31; if (!r) { *co = v >> 31; return v; } *co = (v >> (r - 1)) & 1; return ror32 (v, r); }
	}
}
static inline int32_t sat_s (int64_t v, unsigned bits, int *sat)
{
	int64_t hi = ((int64_t) 1 << (bits - 1)) - 1, lo = -((int64_t) 1 << (bits - 1));
	if (v > hi) { *sat = 1; return (int32_t) hi; } if (v < lo) { *sat = 1; return (int32_t) lo; } return (int32_t) v;
}
static inline uint32_t sat_u (int64_t v, unsigned bits, int *sat)
{
	int64_t hi = ((int64_t) 1 << bits) - 1;
	if (v > hi) { *sat = 1; return (uint32_t) hi; } if (v < 0) { *sat = 1; return 0; } return (uint32_t) v;
}
/* a branch by a value: bit 0 picks Thumb (not supported here: it stops) */
static void bx_to (arm11_t *c, uint32_t v, uint32_t ip)
{
	if (v & 1) { fault (c, "Thumb state (not supported)", ip, v); return; }
	c->r[15] = v & ~3u;
}

/* ---------------------------------------------------------------- VFP */
static inline float fs (uint32_t b) { float f; memcpy (&f, &b, 4); return f; }
static inline uint32_t fb (float f) { uint32_t b; memcpy (&b, &f, 4); return b; }
static inline double dget (arm11_t *c, unsigned d) { uint64_t b = (uint64_t) c->s[2 * d] | ((uint64_t) c->s[2 * d + 1] << 32); double x; memcpy (&x, &b, 8); return x; }
static inline void dset (arm11_t *c, unsigned d, double x) { uint64_t b; memcpy (&b, &x, 8); c->s[2 * d] = (uint32_t) b; c->s[2 * d + 1] = (uint32_t) (b >> 32); }
static void fp_cmp (arm11_t *c, double a, double b)
{
	uint32_t f;
	if (isnan (a) || isnan (b)) f = 0x3; else if (a == b) f = 0x6; else if (a < b) f = 0x8; else f = 0x2;
	c->fpscr = (c->fpscr & 0x0fffffffu) | (f << 28);
}
static double fp_round (double x, int mode)          /* 0 nearest even, 1 +inf, 2 -inf, 3 zero */
{
	switch (mode) { case 0: return nearbyint (x); case 1: return ceil (x); case 2: return floor (x); default: return trunc (x); }
}
static uint32_t fp_to_int (double x, int mode, int sgn)
{
	if (isnan (x)) return 0;
	double r = fp_round (x, mode);
	if (sgn) { if (r >= 2147483647.0) return 0x7fffffffu; if (r <= -2147483648.0) return 0x80000000u; return (uint32_t) (int32_t) r; }
	if (r >= 4294967295.0) return 0xffffffffu; if (r <= 0) return 0; return (uint32_t) r;
}
/* CDP p10/p11: data processing */
static int vfp_dp (arm11_t *c, uint32_t ins)
{
	int dbl = (ins >> 8) & 1;
	unsigned D = (ins >> 22) & 1, N = (ins >> 7) & 1, M = (ins >> 5) & 1, Vd = (ins >> 12) & 15, Vn = (ins >> 16) & 15, Vm = ins & 15;
	unsigned op = ((ins >> 20) & 3) | (((ins >> 23) & 1) << 2), s = (ins >> 6) & 1;
	unsigned d = dbl ? Vd : (Vd << 1) | D, n = dbl ? Vn : (Vn << 1) | N, m = dbl ? Vm : (Vm << 1) | M;
	double a, b, dd, r;
	if (dbl) { a = dget (c, n); b = dget (c, m); dd = dget (c, d); } else { a = fs (c->s[n]); b = fs (c->s[m]); dd = fs (c->s[d]); }
#define PUT(x) do { if (dbl) dset (c, d, (x)); else c->s[d] = fb ((float) (x)); } while (0)
	/* (single precision arithmetic in float, so its rounding is single's) */
#define AR(expr_d, expr_f) do { if (dbl) dset (c, d, (expr_d)); else { float fa = (float) a, fbv = (float) b, fd = (float) dd; (void) fa; (void) fbv; (void) fd; c->s[d] = fb (expr_f); } } while (0)
	switch (op) {
	case 0: AR (s ? dd - a * b : dd + a * b, s ? fd - fa * fbv : fd + fa * fbv); return 1;            /* VMLA / VMLS */
	case 1: AR (s ? -dd - a * b : -dd + a * b, s ? -fd - fa * fbv : -fd + fa * fbv); return 1;        /* VNMLS / VNMLA */
	case 2: AR (s ? -(a * b) : a * b, s ? -(fa * fbv) : fa * fbv); return 1;                          /* VMUL / VNMUL */
	case 3: AR (s ? a - b : a + b, s ? fa - fbv : fa + fbv); return 1;                                /* VADD / VSUB */
	case 4: if (s) return 0; AR (a / b, fa / fbv); return 1;                                         /* VDIV */
	case 7: break;
	default: return 0;
	}
	if (!s) return 0;                                                       /* (extension: bit 6 set) */
	switch (Vn) {
	case 0: if (dbl) dset (c, d, N ? fabs (b) : b); else c->s[d] = N ? c->s[m] & 0x7fffffffu : c->s[m]; return 1;   /* VMOV / VABS */
	case 1:
		if (!N) { if (dbl) { uint64_t x; double t = b; memcpy (&x, &t, 8); x ^= 1ull << 63; memcpy (&t, &x, 8); dset (c, d, t); } else c->s[d] = c->s[m] ^ 0x80000000u; }
		else { if (dbl) dset (c, d, sqrt (b)); else c->s[d] = fb (sqrtf ((float) b)); }
		return 1;                                                          /* VNEG / VSQRT */
	case 4: fp_cmp (c, dd, b); return 1;                                   /* VCMP(E) */
	case 5: fp_cmp (c, dd, 0.0); return 1;                                  /* VCMP(E) with zero */
	case 7:                                                                 /* VCVT double <-> single */
		if (!N) return 0;
		if (dbl) { unsigned sd = (Vd << 1) | D; c->s[sd] = fb ((float) dget (c, Vm)); }
		else { unsigned dd2 = Vd; dset (c, dd2, (double) fs (c->s[(Vm << 1) | M])); }
		return 1;
	case 8: {                                                               /* VCVT int -> float (the source a single register) */
		uint32_t iv = c->s[(Vm << 1) | M];
		double x = N ? (double) (int32_t) iv : (double) iv;
		PUT (x); return 1;
	}
	case 12: case 13: {                                                     /* VCVT float -> int (the result a single register) */
		int mode = N ? 3 : (int) ((c->fpscr >> 22) & 3);
		c->s[(Vd << 1) | D] = fp_to_int (b, mode, Vn & 1);
		return 1;
	}
	default: return 0;
	}
#undef PUT
#undef AR
}

/* ---------------------------------------------------------------- coprocessors */
static int cp15 (arm11_t *c, int load, unsigned opc1, unsigned crn, unsigned crm, unsigned opc2, uint32_t *rt)
{
	if (load) {
		uint32_t v = 0;
		if (crn == 0 && opc1 == 0 && crm == 0) {
			switch (opc2) { case 0: v = 0x410FB767u; break; case 1: v = 0x1D152152u; break; case 2: v = 0; break; case 3: v = 0x00000800u; break; default: v = 0; }
		} else if (crn == 1 && crm == 0) { v = opc2 == 0 ? c->sctlr : opc2 == 1 ? c->actlr : c->cpacr; }
		else if (crn == 2) v = opc2 == 0 ? c->ttbr0 : opc2 == 1 ? c->ttbr1 : c->ttbcr;
		else if (crn == 3) v = c->dacr;
		else if (crn == 13) v = opc2 == 1 ? c->contextid : opc2 >= 2 ? c->tpidr[opc2 - 2] : 0;
		else if (crn == 15 && crm == 12) {                                   /* the performance monitor */
			if (opc2 == 0) v = c->pmnc;
			else if (opc2 == 1) { v = (uint32_t) (c->cycles - c->ccnt_base); c->ctr_read = 1; }
			else if (opc2 == 2 || opc2 == 3) { v = (uint32_t) (c->cycles - c->pmn_base[opc2 - 2]); c->ctr_read = 1; }   /* (PMN0/1: counting cycles) */
			else v = 0;
		} else if (crn == 12 && crm == 0) v = c->vbar;
		*rt = v; return 1;
	}
	uint32_t v = *rt;
	if (crn == 1 && crm == 0) { if (opc2 == 0) c->sctlr = v; else if (opc2 == 1) c->actlr = v; else c->cpacr = v; return 1; }
	if (crn == 2) { if (opc2 == 0) c->ttbr0 = v; else if (opc2 == 1) c->ttbr1 = v; else c->ttbcr = v; return 1; }
	if (crn == 3) { c->dacr = v; return 1; }
	if (crn == 7) { if (crm == 0 && opc2 == 4) { c->sleeping = 1; } return 1; }     /* (cache / barrier ops; c7,c0,4 WFI) */
	if (crn == 8 || crn == 9 || crn == 10 || crn == 5 || crn == 6) return 1;      /* (TLB, lockdown, faults) */
	if (crn == 13) { if (opc2 == 1) c->contextid = v; else if (opc2 >= 2) c->tpidr[opc2 - 2] = v; return 1; }
	if (crn == 12 && crm == 0) { c->vbar = v; return 1; }
	if (crn == 15 && crm == 12) {
		if (opc2 == 0) { c->pmnc = v & ~6u; if (v & 4) c->ccnt_base = c->cycles; if (v & 2) c->pmn_base[0] = c->pmn_base[1] = c->cycles; }
		else if (opc2 == 1) c->ccnt_base = c->cycles - v;
		else if (opc2 == 2 || opc2 == 3) c->pmn_base[opc2 - 2] = c->cycles - v;
		return 1;
	}
	return 1;
}

/* the coprocessor instructions: 0 undefined */
static int copro (arm11_t *c, uint32_t ins, uint32_t ip)
{
	unsigned cp = (ins >> 8) & 15;
	if ((ins & 0x0f000010u) == 0x0e000010u) {                                       /* MCR / MRC */
		unsigned L = (ins >> 20) & 1, rt = (ins >> 12) & 15, opc1 = (ins >> 21) & 7, crn = (ins >> 16) & 15, crm = ins & 15, opc2 = (ins >> 5) & 7;
		if (cp == 15) {
			uint32_t v = rt == 15 ? ip + 8 : c->r[rt];
			if (!cp15 (c, L, opc1, crn, crm, opc2, &v)) return 0;
			if (L) { if (rt == 15) { c->n = v >> 31; c->z = (v >> 30) & 1; c->c = (v >> 29) & 1; c->v = (v >> 28) & 1; } else c->r[rt] = v; }
			return 1;
		}
		if (cp == 10 || cp == 11) {
			if (opc1 == 7) {                                                            /* VMRS / VMSR */
				if (L) {
					uint32_t v = crn == 1 ? c->fpscr : crn == 8 ? c->fpexc : crn == 0 ? 0x410120B4u : 0;
					if (rt == 15) { c->n = c->fpscr >> 31; c->z = (c->fpscr >> 30) & 1; c->c = (c->fpscr >> 29) & 1; c->v = (c->fpscr >> 28) & 1; }
					else c->r[rt] = v;
				} else { if (crn == 1) c->fpscr = c->r[rt]; else if (crn == 8) c->fpexc = c->r[rt]; }
				return 1;
			}
			if (cp == 10 && (opc1 & 6) == 0) {                                          /* VMOV single <-> core */
				unsigned sn = (crn << 1) | ((ins >> 7) & 1);
				if (L) c->r[rt] = c->s[sn]; else c->s[sn] = c->r[rt];
				return 1;
			}
			if (cp == 11 && (opc1 & 6) == 0) {                                          /* VMOV half of a double <-> core */
				unsigned h = (ins >> 21) & 1;
				if (L) c->r[rt] = c->s[2 * crn + h]; else c->s[2 * crn + h] = c->r[rt];
				return 1;
			}
		}
		return 0;
	}
	if ((ins & 0x0f000010u) == 0x0e000000u) {                                       /* CDP */
		if (cp == 10 || cp == 11) return vfp_dp (c, ins);
		return 0;
	}
	if ((ins & 0x0fe00000u) == 0x0c400000u) {                                       /* MCRR / MRRC */
		if (cp == 10 || cp == 11) {
			unsigned L = (ins >> 20) & 1, rt = (ins >> 12) & 15, rt2 = (ins >> 16) & 15, m = ins & 15, M = (ins >> 5) & 1;
			unsigned s0 = cp == 11 ? 2 * m : (m << 1) | M;
			if (L) { c->r[rt] = c->s[s0]; c->r[rt2] = c->s[(s0 + 1) & 31]; }
			else { c->s[s0] = c->r[rt]; c->s[(s0 + 1) & 31] = c->r[rt2]; }
			return 1;
		}
		if (cp == 15) return 1;
		return 0;
	}
	if ((ins & 0x0e000000u) == 0x0c000000u && (cp == 10 || cp == 11)) {              /* VLDR / VSTR / VLDM / VSTM */
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, D = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		unsigned rn = (ins >> 16) & 15, Vd = (ins >> 12) & 15, imm8 = ins & 0xff;
		unsigned s0 = cp == 11 ? 2 * Vd : (Vd << 1) | D;
		uint32_t base = rn == 15 ? ((ip + 8) & ~3u) : c->r[rn];
		if (P && !W) {                                                              /* VLDR / VSTR */
			uint32_t a = U ? base + imm8 * 4 : base - imm8 * 4;
			int words = cp == 11 ? 2 : 1;
			for (int i = 0; i < words; i++) { if (L) c->s[(s0 + i) & 31] = RD (a + 4 * i, 4); else WR (a + 4 * i, c->s[(s0 + i) & 31], 4); }
			return 1;
		}
		unsigned words = imm8;
		if (cp == 11 && (imm8 & 1)) words = imm8 - 1;                             /* (FLDMX / FSTMX: one word more) */
		uint32_t a = U ? base : base - imm8 * 4;
		for (unsigned i = 0; i < words; i++) { if (L) c->s[(s0 + i) & 31] = RD (a + 4 * i, 4); else WR (a + 4 * i, c->s[(s0 + i) & 31], 4); }
		if (W) c->r[rn] = U ? base + imm8 * 4 : base - imm8 * 4;
		return 1;
	}
	return 0;
}

/* ---------------------------------------------------------------- the ARMv6 media instructions */
static int media (arm11_t *c, uint32_t ins, uint32_t ip)
{
	(void) ip;
	unsigned op1 = (ins >> 20) & 31, op2 = (ins >> 5) & 7;
	unsigned rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
	uint32_t a = c->r[rn], b = c->r[rm];
	if ((op1 & 0x18) == 0) {                                                    /* parallel add / subtract */
		unsigned pre = op1 & 7;
		int sgn = pre <= 3;
		unsigned kind = pre & 3;                                                /* 1 plain (GE), 2 saturating, 3 halving */
		if (kind == 0) return 0;
		uint32_t r = 0, ge = 0;
		if (op2 <= 3) {                                                         /* 16-bit lanes */
			int32_t x[2], y[2], z[2];
			for (int i = 0; i < 2; i++) {
				x[i] = sgn ? (int16_t) (a >> (16 * i)) : (uint16_t) (a >> (16 * i));
				y[i] = sgn ? (int16_t) (b >> (16 * i)) : (uint16_t) (b >> (16 * i));
			}
			switch (op2) {
			case 0: z[0] = x[0] + y[0]; z[1] = x[1] + y[1]; break;             /* ADD16 */
			case 1: z[0] = x[0] - y[1]; z[1] = x[1] + y[0]; break;             /* ASX */
			case 2: z[0] = x[0] + y[1]; z[1] = x[1] - y[0]; break;             /* SAX */
			default: z[0] = x[0] - y[0]; z[1] = x[1] - y[1]; break;            /* SUB16 */
			}
			for (int i = 0; i < 2; i++) {
				int32_t v = z[i]; uint32_t lane;
				int add = op2 == 0 || (op2 == 1 && i == 1) || (op2 == 2 && i == 0);
				if (kind == 2) { int s2 = 0; lane = sgn ? (uint32_t) sat_s (v, 16, &s2) : sat_u (v, 16, &s2); }
				else if (kind == 3) lane = (uint32_t) (v >> 1);
				else { lane = (uint32_t) v; if (sgn ? v >= 0 : (add ? v >= 0x10000 : v >= 0)) ge |= 3u << (2 * i); }
				r |= (lane & 0xffff) << (16 * i);
			}
		} else if (op2 == 4 || op2 == 7) {                                      /* 8-bit lanes: ADD8 / SUB8 */
			for (int i = 0; i < 4; i++) {
				int32_t x = sgn ? (int8_t) (a >> (8 * i)) : (uint8_t) (a >> (8 * i)), y = sgn ? (int8_t) (b >> (8 * i)) : (uint8_t) (b >> (8 * i));
				int32_t v = op2 == 4 ? x + y : x - y; uint32_t lane;
				if (kind == 2) { int s2 = 0; lane = sgn ? (uint32_t) sat_s (v, 8, &s2) : sat_u (v, 8, &s2); }
				else if (kind == 3) lane = (uint32_t) (v >> 1);
				else { lane = (uint32_t) v; if (sgn ? v >= 0 : (op2 == 4 ? v >= 0x100 : v >= 0)) ge |= 1u << i; }
				r |= (lane & 0xff) << (8 * i);
			}
		} else return 0;
		c->r[rd] = r;
		if (kind == 1) c->ge = ge;
		return 1;
	}
	if (op1 == 8 && (op2 & 1) == 0) {                                          /* PKHBT / PKHTB */
		unsigned imm = (ins >> 7) & 31, tb = (ins >> 6) & 1;
		if (!tb) c->r[rd] = (a & 0xffff) | ((b << imm) & 0xffff0000u);
		else { uint32_t sh = imm ? (uint32_t) ((int32_t) b >> imm) : (uint32_t) ((int32_t) b >> 31); c->r[rd] = (a & 0xffff0000u) | (sh & 0xffff); }
		return 1;
	}
	if (op1 == 8 && op2 == 5) {                                                /* SEL */
		uint32_t r = 0;
		for (int i = 0; i < 4; i++) r |= (((c->ge >> i) & 1) ? a : b) & (0xffu << (8 * i));
		c->r[rd] = r; return 1;
	}
	if ((op1 & 0x1a) == 0x0a && (op2 & 1) == 0) {                              /* SSAT / USAT */
		unsigned sat = (ins >> 16) & 31, imm = (ins >> 7) & 31, sh = (ins >> 6) & 1;
		int32_t v = sh ? (int32_t) b >> (imm ? imm : 31) : (int32_t) (b << imm);
		int s2 = 0;
		if (op1 & 4) c->r[rd] = sat_u (v, sat, &s2); else c->r[rd] = (uint32_t) sat_s (v, sat + 1, &s2);
		if (s2) c->q = 1;
		return 1;
	}
	if ((op1 == 0x0a || op1 == 0x0e) && op2 == 1) {                           /* SSAT16 / USAT16 */
		unsigned sat = (ins >> 16) & 15; int s2 = 0; uint32_t r = 0;
		for (int i = 0; i < 2; i++) {
			int32_t v = (int16_t) (b >> (16 * i));
			uint32_t lane = op1 == 0x0e ? sat_u (v, sat, &s2) : (uint32_t) sat_s (v, sat + 1, &s2);
			r |= (lane & 0xffff) << (16 * i);
		}
		c->r[rd] = r; if (s2) c->q = 1; return 1;
	}
	if (op1 == 0x0b && op2 == 1) { c->r[rd] = (b >> 24) | ((b >> 8) & 0xff00) | ((b << 8) & 0xff0000) | (b << 24); return 1; }   /* REV */
	if (op1 == 0x0b && op2 == 5) { c->r[rd] = ((b >> 8) & 0x00ff00ffu) | ((b << 8) & 0xff00ff00u); return 1; }                  /* REV16 */
	if (op1 == 0x0f && op2 == 5) { c->r[rd] = (uint32_t) (int32_t) (int16_t) (((b >> 8) & 0xff) | ((b & 0xff) << 8)); return 1; } /* REVSH */
	if (op2 == 3 && (op1 & 0x18) == 0x08) {                                    /* the extends (with an add) */
		unsigned rot = ((ins >> 10) & 3) * 8;
		uint32_t x = ror32 (b, rot), r;
		switch (op1 & 7) {
		case 0: r = ((uint32_t) (int32_t) (int8_t) x & 0xffff) | (((uint32_t) (int32_t) (int8_t) (x >> 16)) << 16);
		        if (rn != 15) r = ((a + r) & 0xffff) | (((a >> 16) + (r >> 16)) << 16); break;              /* SXTB16 / SXTAB16 */
		case 2: r = (uint32_t) (int32_t) (int8_t) x; if (rn != 15) r += a; break;                         /* SXTB / SXTAB */
		case 3: r = (uint32_t) (int32_t) (int16_t) x; if (rn != 15) r += a; break;                        /* SXTH / SXTAH */
		case 4: r = x & 0x00ff00ffu; if (rn != 15) r = ((a + r) & 0xffff) | (((a >> 16) + (r >> 16)) << 16); break;   /* UXTB16 / UXTAB16 */
		case 6: r = x & 0xff; if (rn != 15) r += a; break;                                                /* UXTB / UXTAB */
		case 7: r = x & 0xffff; if (rn != 15) r += a; break;                                              /* UXTH / UXTAH */
		default: return 0;
		}
		c->r[rd] = r; return 1;
	}
	if (op1 == 0x10 || op1 == 0x14) {                                           /* SMLAD / SMUAD / SMLSD / SMUSD / SMLALD / SMLSLD */
		/* (here rd is bits 19:16 and ra 15:12) */
		unsigned r_d = rn, ra = rd, swap = (ins >> 5) & 1, sub = (ins >> 6) & 1;
		uint32_t x = c->r[rs]; if (swap) x = ror32 (x, 16);               /* (Rm: bits 11:8 - the one swapped) */
		uint32_t y = c->r[rm];
		int64_t p1 = (int64_t) (int16_t) y * (int16_t) x, p2 = (int64_t) (int16_t) (y >> 16) * (int16_t) (x >> 16);
		int64_t sum = sub ? p1 - p2 : p1 + p2;
		if (op1 == 0x10) {
			if (ra != 15) sum += (int32_t) c->r[ra];
			if (sum != (int32_t) sum) c->q = 1;
			c->r[r_d] = (uint32_t) sum;
		} else {
			int64_t acc = (int64_t) (((uint64_t) c->r[r_d] << 32) | c->r[ra]);
			acc += sum;
			c->r[ra] = (uint32_t) acc; c->r[r_d] = (uint32_t) ((uint64_t) acc >> 32);
		}
		return 1;
	}
	if (op1 == 0x15) {                                                          /* SMMLA / SMMUL / SMMLS */
		unsigned r_d = rn, ra = rd, R = (ins >> 5) & 1, sub = (op2 & 6) == 6;
		int64_t p = (int64_t) (int32_t) c->r[rm] * (int32_t) c->r[rs];
		int64_t acc = ra != 15 ? (int64_t) ((uint64_t) c->r[ra] << 32) : 0;
		int64_t r = sub ? acc - p : acc + p;
		if (R) r += 0x80000000ll;
		c->r[r_d] = (uint32_t) ((uint64_t) r >> 32);
		return 1;
	}
	if (op1 == 0x18 && op2 == 0) {                                              /* USAD8 / USADA8 */
		unsigned r_d = rn, ra = rd; uint32_t x = c->r[rm], y = c->r[rs], s2 = 0;
		for (int i = 0; i < 4; i++) { int d2 = (int) ((x >> (8 * i)) & 0xff) - (int) ((y >> (8 * i)) & 0xff); s2 += (uint32_t) (d2 < 0 ? -d2 : d2); }
		c->r[r_d] = ra != 15 ? s2 + c->r[ra] : s2;
		return 1;
	}
	return 0;
}

/* ---------------------------------------------------------------- one instruction */
#define REG(n) ((n) == 15 ? ip + 8 : c->r[n])
static void step (arm11_t *c)
{
	uint32_t ip = c->r[15];
	if (ip & 3) { fault (c, "unaligned PC", ip, 0); return; }
	uint32_t ins;
	if (ip + 4 <= c->sys->ram_size) memcpy (&ins, c->sys->ram + ip, 4);
	else { fault (c, "running outside RAM", ip, 0); return; }
	c->last_pc = ip;
	c->r[15] = ip + 4;
	c->cycles++;
	unsigned cond = ins >> 28;
	if (cond == 15) {                                                           /* the unconditional space */
		if ((ins & 0x0e000000u) == 0x0a000000u) {                               /* BLX (immediate): to Thumb */
			fault (c, "BLX to Thumb (not supported)", ip, ins); return;
		}
		if ((ins & 0x0ff10020u) == 0x01000000u) {                               /* CPS */
			unsigned imod = (ins >> 18) & 3, M = (ins >> 17) & 1;
			if (c->mode != MODE_USR) {
				if (imod & 2) {
					uint32_t v = imod == 2 ? 0 : 1;
					if (ins & 0x100) c->abit = v; if (ins & 0x80) c->ibit = v; if (ins & 0x40) c->fbit = v;
				}
				if (M) set_mode (c, ins & 0x1f);
			}
			return;
		}
		if ((ins & 0x0ffffdffu) == 0x01010000u) { c->ebit = (ins >> 9) & 1; return; }     /* SETEND */
		if ((ins & 0x0d70f000u) == 0x0550f000u) return;                                  /* PLD */
		if (ins == 0xf57ff01fu) { c->excl = 0; return; }                                 /* CLREX */
		if ((ins & 0x0ffffff0u) == 0x057ff040u || (ins & 0x0ffffff0u) == 0x057ff050u || (ins & 0x0ffffff0u) == 0x057ff060u) return;   /* DSB / DMB / ISB */
		if ((ins & 0x0e5fffe0u) == 0x084d0500u) {                                        /* SRS */
			unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, W = (ins >> 21) & 1, m = ins & 0x1f;
			int b = bank_of (m); uint32_t sp = (m == c->mode) ? c->r[13] : c->bank_r13[b];
			uint32_t a = U ? sp + (P ? 4 : 0) : sp - (P ? 8 : 4);
			uint32_t *sps = spsr_p (c);
			WR (a, c->r[14], 4); WR (a + 4, sps ? *sps : arm11_cpsr (c), 4);
			if (W) { uint32_t nsp = U ? sp + 8 : sp - 8; if (m == c->mode) c->r[13] = nsp; else c->bank_r13[b] = nsp; }
			return;
		}
		if ((ins & 0x0e50ffffu) == 0x08100a00u) {                                        /* RFE */
			unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, W = (ins >> 21) & 1, rn = (ins >> 16) & 15;
			uint32_t base = c->r[rn], a = U ? base + (P ? 4 : 0) : base - (P ? 8 : 4);
			uint32_t pc = RD (a, 4), ps = RD (a + 4, 4);
			if (W) c->r[rn] = U ? base + 8 : base - 8;
			set_cpsr (c, ps, 15, 1); c->r[15] = pc & ~3u;
			return;
		}
		fault (c, "undefined (unconditional)", ip, ins); return;
	}
	if (!cond_ok (c, cond)) return;

	unsigned op = (ins >> 25) & 7;
	unsigned rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
	switch (op) {
	case 0: case 1: {
		if (op == 0 && (ins & 0x90) == 0x90) {
			/* multiplies, swap, exclusives, the extra loads and stores */
			unsigned sh = (ins >> 5) & 3;
			if (sh == 0) {
				if ((ins & 0x0f000000u) == 0) {                                     /* multiplies */
					unsigned mop = (ins >> 21) & 7, S = (ins >> 20) & 1;
					uint32_t a = c->r[rm], b = c->r[rs];
					switch (mop) {
					case 0: case 1: {                                                /* MUL / MLA */
						uint32_t r = a * b + (mop ? c->r[rd] : 0);
						c->r[rn] = r; if (S) { c->n = r >> 31; c->z = r == 0; }
						return;
					}
					case 2: {                                                        /* UMAAL */
						uint64_t r = (uint64_t) a * b + c->r[rd] + c->r[rn];
						c->r[rd] = (uint32_t) r; c->r[rn] = (uint32_t) (r >> 32); return;
					}
					case 3: { uint32_t r = c->r[rd] - a * b; c->r[rn] = r; return; }     /* MLS (v6T2: here for safety) */
					default: {                                                       /* UMULL / UMLAL / SMULL / SMLAL */
						uint64_t r;
						if (mop & 2) r = (uint64_t) ((int64_t) (int32_t) a * (int32_t) b); else r = (uint64_t) a * b;
						if (mop & 1) r += ((uint64_t) c->r[rn] << 32) | c->r[rd];
						c->r[rd] = (uint32_t) r; c->r[rn] = (uint32_t) (r >> 32);
						if (S) { c->n = (uint32_t) (r >> 63); c->z = r == 0; }
						return;
					}
					}
				}
				if ((ins & 0x0fb00ff0u) == 0x01000090u) {                           /* SWP / SWPB */
					int n = (ins & 0x400000) ? 1 : 4;
					uint32_t a = c->r[rn], v = RD (a, n);
					if (n == 4 && (a & 3)) v = ror32 (v, (a & 3) * 8);
					WR (a, c->r[rm], n); c->r[rd] = v; return;
				}
				if ((ins & 0x0f800ff0u) == 0x01800f90u || (ins & 0x0f900fffu) == 0x01900f9fu) {   /* LDREX* / STREX* */
					unsigned eop = (ins >> 21) & 3, L = (ins >> 20) & 1;
					uint32_t a = c->r[rn];
					int n = eop == 0 ? 4 : eop == 1 ? 8 : eop == 2 ? 1 : 2;
					if (L) {
						if (n == 8) { c->r[rd] = RD (a, 4); c->r[rd + 1] = RD (a + 4, 4); }
						else c->r[rd] = RD (a, n);
						c->excl = 1; c->excl_addr = a;
					} else {
						if (c->excl && c->excl_addr == a) {
							c->excl = 0;
							if (n == 8) { WR (a, c->r[rm], 4); WR (a + 4, c->r[rm + 1], 4); } else WR (a, c->r[rm], n);
							c->r[rd] = 0;
						} else c->r[rd] = 1;
						c->excl = 0;
					}
					return;
				}
				fault (c, "undefined (multiply space)", ip, ins); return;
			}
			/* the extra loads and stores */
			unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, I = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
			uint32_t off = I ? (((ins >> 4) & 0xf0) | (ins & 15)) : c->r[rm];
			uint32_t base = REG (rn), addr = P ? (U ? base + off : base - off) : base;
			uint32_t wb = U ? base + off : base - off;
			if (L) {
				uint32_t v;
				if (sh == 1) v = RD (addr, 2);
				else if (sh == 2) v = (uint32_t) (int32_t) (int8_t) RD (addr, 1);
				else v = (uint32_t) (int32_t) (int16_t) RD (addr, 2);
				if (!P || W) c->r[rn] = wb;
				if (rd == 15) bx_to (c, v, ip); else c->r[rd] = v;
			} else {
				if (sh == 1) { WR (addr, REG (rd), 2); if (!P || W) c->r[rn] = wb; }
				else if (sh == 2) {                                                  /* LDRD */
					uint32_t v0 = RD (addr, 4), v1 = RD (addr + 4, 4);
					if (!P || W) c->r[rn] = wb;
					c->r[rd] = v0; c->r[(rd + 1) & 15] = v1;
				} else {                                                             /* STRD */
					WR (addr, c->r[rd], 4); WR (addr + 4, c->r[(rd + 1) & 15], 4);
					if (!P || W) c->r[rn] = wb;
				}
			}
			return;
		}
		unsigned opc = (ins >> 21) & 15, S = (ins >> 20) & 1;
		if ((opc & 0xc) == 0x8 && !S) {
			/* the miscellaneous instructions (and MSR immediate, the hints) */
			if (op == 1) {
				if (opc == 9 && rn == 0 && rd == 15 && ((ins >> 8) & 15) == 0) {          /* hints */
					switch (ins & 0xff) { case 2: case 3: c->sleeping = 1; break; default: break; }   /* WFE / WFI */
					return;
				}
				if (opc == 9 || opc == 11) {                                            /* MSR immediate */
					uint32_t v = ror32 (ins & 0xff, ((ins >> 8) & 15) * 2);
					unsigned f = (ins >> 16) & 15;
					if (opc == 11) { uint32_t *sp = spsr_p (c); if (sp) { uint32_t m = 0; if (f & 1) m |= 0xff; if (f & 2) m |= 0xff00; if (f & 4) m |= 0xff0000; if (f & 8) m |= 0xff000000u; *sp = (*sp & ~m) | (v & m); } }
					else set_cpsr (c, v, f, 0);
					return;
				}
				fault (c, "undefined (misc imm)", ip, ins); return;
			}
			unsigned mop = (ins >> 4) & 15;
			if (mop == 0) {
				if ((opc & 1) == 0) {                                                   /* MRS */
					if (opc == 10) { uint32_t *sp = spsr_p (c); c->r[rd] = sp ? *sp : 0; } else c->r[rd] = arm11_cpsr (c);
					return;
				}
				uint32_t v = c->r[rm]; unsigned f = (ins >> 16) & 15;                    /* MSR register */
				if (opc == 11) { uint32_t *sp = spsr_p (c); if (sp) { uint32_t m = 0; if (f & 1) m |= 0xff; if (f & 2) m |= 0xff00; if (f & 4) m |= 0xff0000; if (f & 8) m |= 0xff000000u; *sp = (*sp & ~m) | (v & m); } }
				else set_cpsr (c, v, f, 0);
				return;
			}
			if (opc == 9 && (mop == 1 || mop == 2)) { bx_to (c, c->r[rm], ip); return; }            /* BX / BXJ */
			if (opc == 9 && mop == 3) { uint32_t t = c->r[rm]; c->r[14] = ip + 4; bx_to (c, t, ip); return; }   /* BLX register */
			if (opc == 11 && mop == 1) { uint32_t v = c->r[rm]; unsigned n = 0; if (!v) n = 32; else while (!(v & 0x80000000u)) { v <<= 1; n++; } c->r[rd] = n; return; }   /* CLZ */
			if (mop == 5) {                                                             /* QADD / QSUB / QDADD / QDSUB */
				int s2 = 0; int64_t a = (int32_t) c->r[rm], b = (int32_t) c->r[rn];
				if (opc & 2) { b = sat_s (2 * b, 32, &s2); }
				int64_t r = (opc & 1) ? a - b : a + b;
				c->r[rd] = (uint32_t) sat_s (r, 32, &s2); if (s2) c->q = 1;
				return;
			}
			if (opc == 9 && mop == 7) { fault (c, "BKPT", ip, ins); return; }
			if ((mop & 9) == 8) {                                                        /* the halfword multiplies */
				unsigned x = (ins >> 5) & 1, y = (ins >> 6) & 1;
				int32_t a = (int32_t) c->r[rm], b = (int32_t) c->r[rs];
				int32_t ah = x ? a >> 16 : (int16_t) a, bh = y ? b >> 16 : (int16_t) b;
				switch (opc) {
				case 8: { int64_t r = (int64_t) ah * bh + (int32_t) c->r[rd]; if (r != (int32_t) r) c->q = 1; c->r[rn] = (uint32_t) r; return; }   /* SMLAxy */
				case 9: {
					int64_t p = ((int64_t) a * bh) >> 16;
					if (!x) { int64_t r = p + (int32_t) c->r[rd]; if (r != (int32_t) r) c->q = 1; c->r[rn] = (uint32_t) r; }   /* SMLAWy */
					else c->r[rn] = (uint32_t) p;                                                                       /* SMULWy */
					return;
				}
				case 10: { int64_t acc = (int64_t) (((uint64_t) c->r[rn] << 32) | c->r[rd]); acc += (int64_t) ah * bh; c->r[rd] = (uint32_t) acc; c->r[rn] = (uint32_t) ((uint64_t) acc >> 32); return; }   /* SMLALxy */
				default: c->r[rn] = (uint32_t) (ah * bh); return;                      /* SMULxy */
				}
			}
			fault (c, "undefined (misc)", ip, ins); return;
		}
		/* data processing */
		uint32_t b, sc;
		if (op == 1) { unsigned rot = ((ins >> 8) & 15) * 2; b = ror32 (ins & 0xff, rot); sc = rot ? b >> 31 : c->c; }
		else if (ins & 0x10) b = shift_reg (rn == 15 || rm == 15 ? (rm == 15 ? ip + 12 : c->r[rm]) : c->r[rm], (ins >> 5) & 3, c->r[rs], c->c, &sc);
		else b = shift_imm (REG (rm), (ins >> 5) & 3, (ins >> 7) & 31, c->c, &sc);
		uint32_t a = (op == 0 && (ins & 0x10) && rn == 15) ? ip + 12 : REG (rn), r, co = sc, ov = c->v;
		int wr = 1;
		switch (opc) {
		case 0: r = a & b; break;
		case 1: r = a ^ b; break;
		case 2: r = add_c (a, ~b, 1, &co, &ov); break;
		case 3: r = add_c (b, ~a, 1, &co, &ov); break;
		case 4: r = add_c (a, b, 0, &co, &ov); break;
		case 5: r = add_c (a, b, c->c, &co, &ov); break;
		case 6: r = add_c (a, ~b, c->c, &co, &ov); break;
		case 7: r = add_c (b, ~a, c->c, &co, &ov); break;
		case 8: r = a & b; wr = 0; break;
		case 9: r = a ^ b; wr = 0; break;
		case 10: r = add_c (a, ~b, 1, &co, &ov); wr = 0; break;
		case 11: r = add_c (a, b, 0, &co, &ov); wr = 0; break;
		case 12: r = a | b; break;
		case 13: r = b; break;
		case 14: r = a & ~b; break;
		default: r = ~b; break;
		}
		if (wr && rd == 15) {
			if (S) { uint32_t *sp = spsr_p (c); if (sp) set_cpsr (c, *sp, 15, 1); }
			c->r[15] = r & (c->tbit ? ~1u : ~3u);
			if (c->tbit) fault (c, "Thumb state (not supported)", ip, ins);
			c->cycles += 2;
			return;
		}
		if (wr) c->r[rd] = r;
		if (S) { c->n = r >> 31; c->z = r == 0; c->c = co; c->v = ov; }
		return;
	}
	case 2: case 3: {
		if (op == 3 && (ins & 0x10)) {                                              /* media */
			if ((ins & 0x0ff000f0u) == 0x07f000f0u) { fault (c, "undefined (UDF)", ip, ins); return; }
			if (!media (c, ins, ip)) fault (c, "undefined (media)", ip, ins);
			return;
		}
		/* LDR / STR / LDRB / STRB */
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, B = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		uint32_t off, sc;
		if (op == 2) off = ins & 0xfff; else off = shift_imm (c->r[rm], (ins >> 5) & 3, (ins >> 7) & 31, c->c, &sc);
		uint32_t base = REG (rn), wb = U ? base + off : base - off, addr = P ? wb : base;
		if (L) {
			uint32_t v = RD (addr, B ? 1 : 4);
			if (!P || W) c->r[rn] = wb;
			if (rd == 15) { bx_to (c, v, ip); c->cycles += 2; } else c->r[rd] = v;
		} else {
			WR (addr, rd == 15 ? ip + 8 : c->r[rd], B ? 1 : 4);
			if (!P || W) c->r[rn] = wb;
		}
		c->cycles++;
		return;
	}
	case 4: {                                                                       /* LDM / STM */
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, S = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		uint32_t list = ins & 0xffff; unsigned cnt = 0;
		for (int i = 0; i < 16; i++) cnt += (list >> i) & 1;
		uint32_t base = c->r[rn];
		uint32_t a = U ? base + (P ? 4 : 0) : base - 4 * cnt + (P ? 0 : 4);
		uint32_t nb = U ? base + 4 * cnt : base - 4 * cnt;
		int user = S && !(L && (list & 0x8000));                                   /* (the user bank) */
		uint32_t omode = c->mode;
		if (user && c->mode != MODE_USR && c->mode != MODE_SYS) set_mode (c, MODE_SYS);
		if (L) {
			if (W && !((list >> rn) & 1)) c->r[rn] = nb;
			for (int i = 0; i < 15; i++) if ((list >> i) & 1) { c->r[i] = RD (a, 4); a += 4; }
			if (W && ((list >> rn) & 1)) { /* (the loaded value wins) */ }
			if (user) set_mode (c, omode);
			if (list & 0x8000) {
				uint32_t v = RD (a, 4);
				if (S) { uint32_t *sp = spsr_p (c); if (sp) set_cpsr (c, *sp, 15, 1); c->r[15] = v & ~3u; if (c->tbit) fault (c, "Thumb state (not supported)", ip, ins); }
				else bx_to (c, v, ip);
				c->cycles += 2;
			}
		} else {
			for (int i = 0; i < 16; i++) if ((list >> i) & 1) { WR (a, i == 15 ? ip + 8 : c->r[i], 4); a += 4; }
			if (user) set_mode (c, omode);
			if (W) c->r[rn] = nb;
		}
		c->cycles += cnt;
		return;
	}
	case 5: {                                                                       /* B / BL */
		int32_t off = (int32_t) (ins << 8) >> 6;
		if (ins & 0x01000000u) c->r[14] = ip + 4;
		c->r[15] = ip + 8 + (uint32_t) off;
		if (c->r[15] == ip) c->sleeping = 1;                                       /* (b . : stuck - asleep until an interrupt) */
		c->cycles += 2;
		return;
	}
	case 6: case 7:
		if (op == 7 && (ins & 0x01000000u)) { exception (c, 0x08, MODE_SVC, ip + 4); return; }   /* SWI */
		if (!copro (c, ins, ip)) {
			if (((ins >> 8) & 14) == 10 || ((ins >> 8) & 15) == 15) fault (c, "undefined (coprocessor)", ip, ins);
			else exception (c, 0x04, MODE_UND, ip + 4);
		}
		return;
	}
}

/* ---------------------------------------------------------------- the decoded-instruction cache
 * The commonest instructions, decoded once: data processing with an immediate or an immediate-shifted
 * register, loads / stores with an immediate or a shifted-register offset, B / BL. A direct-mapped table by
 * address; each entry keeps the instruction word it was made from and is checked against memory at every
 * use, so code loaded or changed later (the loader copies programs in) is never run stale. The rest: step. */
typedef struct ad ad_t;
typedef void (*afn) (arm11_t *c, const ad_t *e, uint32_t ip);
struct ad { uint32_t pc, ins; afn fn; uint32_t imm; uint8_t cond, opc, s, rd, rn, rm, type, amt, flags; };
#define AD_BITS 16
static void a_dp (arm11_t *c, const ad_t *e, uint32_t ip)
{
	(void) ip;
	uint32_t b, sc;
	if (e->flags & 1) { b = e->imm; sc = (e->flags & 2) ? b >> 31 : c->c; }           /* (immediate; rotated: its carry) */
	else b = shift_imm (c->r[e->rm], e->type, e->amt, c->c, &sc);
	uint32_t a = c->r[e->rn], r, co = sc, ov = c->v;
	switch (e->opc) {
	case 0: r = a & b; break;
	case 1: r = a ^ b; break;
	case 2: r = add_c (a, ~b, 1, &co, &ov); break;
	case 3: r = add_c (b, ~a, 1, &co, &ov); break;
	case 4: r = add_c (a, b, 0, &co, &ov); break;
	case 5: r = add_c (a, b, c->c, &co, &ov); break;
	case 6: r = add_c (a, ~b, c->c, &co, &ov); break;
	case 7: r = add_c (b, ~a, c->c, &co, &ov); break;
	case 8: r = a & b; break;
	case 9: r = a ^ b; break;
	case 10: r = add_c (a, ~b, 1, &co, &ov); break;
	case 11: r = add_c (a, b, 0, &co, &ov); break;
	case 12: r = a | b; break;
	case 13: r = b; break;
	case 14: r = a & ~b; break;
	default: r = ~b; break;
	}
	if ((e->opc & 0xc) != 0x8) c->r[e->rd] = r;
	if (e->s) { c->n = r >> 31; c->z = r == 0; c->c = co; c->v = ov; }
}
static void a_ls (arm11_t *c, const ad_t *e, uint32_t ip)
{
	(void) ip;
	uint32_t off, sc;
	if (e->flags & 1) off = e->imm; else off = shift_imm (c->r[e->rm], e->type, e->amt, c->c, &sc);
	unsigned P = e->flags & 4, U = e->flags & 8, B = e->flags & 16, W = e->flags & 32, L = e->flags & 64;
	uint32_t base = c->r[e->rn], wb = U ? base + off : base - off, addr = P ? wb : base;
	if (L) { uint32_t v = RD (addr, B ? 1 : 4); if (!P || W) c->r[e->rn] = wb; c->r[e->rd] = v; }
	else { WR (addr, c->r[e->rd], B ? 1 : 4); if (!P || W) c->r[e->rn] = wb; }
	c->cycles++;
}
static void a_b (arm11_t *c, const ad_t *e, uint32_t ip)
{
	if (e->flags & 1) c->r[14] = ip + 4;
	c->r[15] = e->imm; c->cycles += 2;
}
static void ad_make (ad_t *e, uint32_t ip, uint32_t ins)
{
	e->pc = ip; e->ins = ins; e->fn = NULL; e->cond = (uint8_t) (ins >> 28);
	if (e->cond == 15) return;
	unsigned op = (ins >> 25) & 7, rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rm = ins & 15;
	e->rn = (uint8_t) rn; e->rd = (uint8_t) rd; e->rm = (uint8_t) rm;
	if (op == 0 || op == 1) {                                                   /* data processing */
		unsigned opc = (ins >> 21) & 15, S = (ins >> 20) & 1;
		if ((opc & 0xc) == 0x8 && !S) return;                                  /* (the misc instructions) */
		if (op == 0 && (ins & 0x10)) return;                                   /* (register-shifted register, multiplies, extra loads) */
		if (rd == 15 || rn == 15 || (op == 0 && rm == 15)) return;
		e->opc = (uint8_t) opc; e->s = (uint8_t) S;
		if (op == 1) { unsigned rot = ((ins >> 8) & 15) * 2; e->imm = ror32 (ins & 0xff, rot); e->flags = (uint8_t) (1 | (rot ? 2 : 0)); }
		else { e->flags = 0; e->type = (uint8_t) ((ins >> 5) & 3); e->amt = (uint8_t) ((ins >> 7) & 31); }
		e->fn = a_dp; return;
	}
	if (op == 2 || (op == 3 && !(ins & 0x10))) {                               /* LDR / STR (B) */
		if (rd == 15 || rn == 15 || (op == 3 && rm == 15)) return;
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, B = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		if (!P && W) return;                                                   /* (LDRT / STRT: the full decoder) */
		if ((!P || W) && rn == rd) return;
		e->flags = (uint8_t) ((op == 2 ? 1 : 0) | (P << 2) | (U << 3) | (B << 4) | (W << 5) | (L << 6));
		if (op == 2) e->imm = ins & 0xfff; else { e->type = (uint8_t) ((ins >> 5) & 3); e->amt = (uint8_t) ((ins >> 7) & 31); }
		e->fn = a_ls; return;
	}
	if (op == 5) {                                                              /* B / BL */
		int32_t off = (int32_t) (ins << 8) >> 6;
		e->imm = ip + 8 + (uint32_t) off;
		if (e->imm == ip) return;                                              /* (b . : the full decoder, it sleeps) */
		e->flags = (uint8_t) ((ins >> 24) & 1);
		e->fn = a_b; return;
	}
}

/* a short loop went round: a pure spin on one value waits until it changes (the system wakes it); a loop
 * that reads a cycle counter (and no peripheral) waits for a time: the rest of the slice at once. 1: stop */
static int loop_round (arm11_t *c, uint64_t until)
{
	if (c->ctr_read && !c->mmio_round && c->r[15] == c->loop_target) {
		if (++c->ctr_iters >= 2) { c->ctr_iters = 0; c->ctr_read = 0; if (until > c->cycles) c->slept[0] += until - c->cycles; c->cycles = until; c->loop_target = c->r[15]; c->loop_loads = c->loop_stores = 0; return 1; }
	} else c->ctr_iters = 0;
	int mmio = c->mmio_round;                         /* (polling a peripheral: a wait after two equal rounds, not four) */
	c->ctr_read = 0; c->mmio_round = 0;
	if (c->r[15] == c->loop_target && !c->loop_stores && c->loop_loads == 1 && c->ld_addr == c->prev_ld_addr
	    && c->ld_val == c->prev_ld_val && !memcmp (c->loop_regs, c->r, sizeof c->loop_regs)) {
		if (++c->loop_iters >= (mmio ? 1u : 4u)) { c->wait_on = 1; c->wait_id++; c->wait_addr = c->ld_addr; c->wait_val = c->ld_val; c->wait_n = 4; c->loop_iters = 0; if (until > c->cycles) c->slept[1] += until - c->cycles; c->cycles = until; }
	} else c->loop_iters = 0;
	memcpy (c->loop_regs, c->r, sizeof c->loop_regs);
	c->loop_target = c->r[15]; c->prev_ld_addr = c->ld_addr; c->prev_ld_val = c->ld_val;
	c->loop_loads = c->loop_stores = 0;
	return c->wait_on != 0;
}

/* the waiting loop (c at its head, as loop_round left it) run on a copy, with addr reading v: 1 it leaves the loop, 0 it
 * comes round twice, -1 unknown (it stored, read another peripheral, ran a coprocessor instruction, faulted, took too
 * long). The copy changes nothing: stores and other reads outside RAM stop it. "Leaves" is anything outside the 72
 * bytes from the head - a call counts too, which only means waking sooner than needed */
int arm11_trial (const arm11_t *c0, uint32_t addr, uint32_t v)
{
	if (c0->tbit || (c0->r[15] & 3)) return -1;
	arm11_t t = *c0; uint32_t head = c0->r[15]; int rounds = 0, res = -1;
	tr_on = 1; tr_bad = 0; tr_addr = addr; tr_val = v;
	for (int k = 0; k < 512; k++) {
		uint32_t ip = t.r[15];
		if (ip < head || ip >= head + 72 || (ip & 3) || t.tbit) { res = 1; break; }
		uint32_t ins; if (ip + 4 > t.sys->ram_size) break;
		memcpy (&ins, t.sys->ram + ip, 4);
		if (((ins >> 25) & 7) >= 6) break;                   /* (coprocessor, SWI: not here) */
		step (&t);
		if (tr_bad || t.halted) break;
		if (t.r[15] == head && ++rounds == 2) { res = 0; break; }
	}
	tr_on = 0;
	return res;
}

#include "arm11_jit.c"

/* ---------------------------------------------------------------- the run loop */
uint32_t *arm11_prof;                                                            /* (PROFILE: a histogram of PCs / 4, every 64th step) */
uint64_t arm11_run (arm11_t *c, uint64_t until)
{
	uint64_t start = c->cycles;
	if (!c->dcache) c->dcache = calloc ((size_t) 1 << AD_BITS, sizeof (ad_t));
	ad_t *dc = (ad_t *) c->dcache;
	const uint8_t *ram = c->sys->ram; uint32_t ram_size = c->sys->ram_size;
	while (c->cycles < until) {
		if (c->halted || c->sleeping || ((c->irq_line && !c->ibit) || (c->fiq_line && !c->fbit)) || !dc) goto slow;
#ifdef ARM11_JIT
		if (jit_try (c, until)) continue;
#endif
		{
			/* the fast path: ARM state, an instruction in RAM, decoded already */
			uint32_t ip = c->r[15];
			if ((ip & 3) || ip + 4 > ram_size || c->tbit) goto slow;
			uint32_t ins; memcpy (&ins, ram + ip, 4);
			ad_t *e = &dc[(ip >> 2) & ((1u << AD_BITS) - 1)];
			if (e->pc != ip || e->ins != ins) ad_make (e, ip, ins);
			if (!e->fn) goto slow;
			c->last_pc = ip; c->r[15] = ip + 4; c->cycles++; c->steps++;
			if (arm11_prof && !(c->steps & 63)) arm11_prof[(ip >> 2) & 0xfffff]++;
			if (e->cond == 14 || cond_ok (c, e->cond)) e->fn (c, e, ip);
			if (c->r[15] < ip && ip - c->r[15] <= 64 && loop_round (c, until)) break;
			continue;
		}
	slow:
		if (c->halted) { if (until > c->cycles) c->slept[2] += until - c->cycles; c->cycles = until; break; }
		if (c->fiq_line && !c->fbit) { exception (c, 0x1c, MODE_FIQ, c->r[15] + 4); continue; }
		if (c->irq_line && !c->ibit) { exception (c, 0x18, MODE_IRQ, c->r[15] + 4); continue; }
		if (c->sleeping) {
			if (c->irq_line || c->fiq_line) c->sleeping = 0;
			else { if (until > c->cycles) c->slept[2] += until - c->cycles; c->cycles = until; break; }
		}
		uint32_t ip = c->r[15];
		step (c); c->steps++;
		if (arm11_prof && !(c->steps & 63)) arm11_prof[(ip >> 2) & 0xfffff]++;
		if (c->r[15] < ip && ip - c->r[15] <= 64 && loop_round (c, until)) break;
	}
	return c->cycles - start;
}
