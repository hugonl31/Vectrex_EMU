/* arm11.h - an ARM1176JZF-S (ARMv6, the Raspberry Pi Zero's core) interpreter: the ARM instruction set, the
 * processor modes and their banked registers, CP15 (as much as bare-metal programs touch), VFPv2 (single and
 * double precision) and the cycle counter. No MMU translation (programs map flat, as the PiTrex's do) and no
 * Thumb yet. See arm11.c.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#ifndef ARM11_H
#define ARM11_H
#include <stdint.h>

typedef struct arm11 arm11_t;

typedef struct arm11_sys {
	/* memory beyond RAM (the peripherals): size 1, 2 or 4 */
	uint32_t (*rd) (arm11_t *c, uint32_t addr, int size);
	void (*wr) (arm11_t *c, uint32_t addr, uint32_t v, int size);
	/* plain RAM from address 0 */
	uint8_t *ram; uint32_t ram_size;
} arm11_sys_t;

struct arm11 {
	uint32_t r[16];
	uint32_t n, z, c, v, q, ge;             /* the flags, apart */
	uint32_t mode, ibit, fbit, tbit, ebit, abit;   /* CPSR: mode bits, I, F, T, E, A */
	/* banked registers: index 0 usr/sys, 1 fiq, 2 irq, 3 svc, 4 abt, 5 und */
	uint32_t bank_r13[6], bank_r14[6], bank_spsr[6];
	uint32_t fiq_r8_12[5], usr_r8_12[5];
	/* VFP */
	uint32_t s[32]; uint32_t fpscr, fpexc;
	/* CP15 */
	uint32_t sctlr, actlr, cpacr, ttbr0, ttbr1, ttbcr, dacr, contextid, tpidr[3], vbar, pmnc;
	uint64_t ccnt_base, pmn_base[2];        /* (the cycle counter: cycles - ccnt_base; the event counters count cycles too) */
	int ctr_read, ctr_iters, mmio_round;    /* (a loop round read a counter / a peripheral) */
	/* the exclusive monitor */
	int excl; uint32_t excl_addr;
	uint64_t cycles, steps;                 /* (steps: instructions run - the rest of the cycles slept through) */
	int irq_line, fiq_line;                 /* (the interrupt inputs, set by the system) */
	int halted, sleeping;
	char fault[160];
	void *dcache;                           /* (the decoded-instruction cache: arm11.c) */
	arm11_sys_t *sys;
	void *user;
	uint32_t last_pc;
	/* spin detection, as cm33.c does it: a short loop that reads the same values, or counts towards a value */
	uint32_t loop_target, loop_iters, loop_loads, loop_stores, ld_addr, ld_val, prev_ld_addr, prev_ld_val, ld_hash, prev_ld_hash;
	uint32_t loop_regs[13];
	int wait_on; uint32_t wait_addr, wait_val; int wait_n;
	int poll_skip;
	uint64_t jit_until;
	uint64_t slept[3];                      /* (cycles slept through: 0 a timing loop on a counter, 1 a poll, 2 WFI / halted) */                     /* (the JIT: the run's end, for its dispatcher - arm11_jit.c) */
};

void arm11_reset (arm11_t *c, uint32_t pc);              /* SVC mode, interrupts off, ARM state, at pc */
uint64_t arm11_run (arm11_t *c, uint64_t until);         /* run until cycles reaches until (or it halts / sleeps) */
uint32_t arm11_cpsr (const arm11_t *c);
void arm11_invalidate (arm11_t *c);                      /* RAM changed behind its back: drop the translated code */
void arm11_jit_stats (void);
#endif
