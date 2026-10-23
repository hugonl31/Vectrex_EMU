/* cm33.h - an ARM Cortex-M33 (ARMv8-M Mainline: Thumb-2, DSP, single precision FPU) interpreter, for the
 * RP2350 in add-on cartridges (rp2350.c). One cm33_t per core; the memory and the coprocessors are the
 * system's (callbacks), the exceptions (SVC, SysTick, interrupts) are taken here. */
#ifndef CM33_H
#define CM33_H
#include <stdint.h>

typedef struct cm33 cm33_t;

typedef struct cm33_sys {
	/* memory: size 1, 2 or 4; addresses are not aligned for the sizes that allow it */
	uint32_t (*rd) (cm33_t *c, uint32_t addr, int size);
	void (*wr) (cm33_t *c, uint32_t addr, uint32_t v, int size);
	/* coprocessor transfers (MCR / MRC / MCRR / MRRC / CDP): 0 = done, -1 = undefined */
	int (*cp) (cm33_t *c, int kind, int coproc, int opc1, int crn, int crm, int opc2, uint32_t *rt, uint32_t *rt2);
	/* a write to the system control space (SCS, 0xE000E000-0xE000EFFF) the core does not keep itself */
	void (*event) (cm33_t *c, int what);       /* CM33_EV_SEV, CM33_EV_RESET */
	/* the fast path: memory that is plain RAM (reads and writes straight to a host buffer) */
	uint8_t *ram; uint32_t ram_base, ram_size;
	uint8_t *xram; uint32_t xram_size;     /* (a second one: 0x1x000000 with bit 24 set, as the RP2350 maps its PSRAM) */
	void *dcache, *dcache1; uint8_t *code_pages;   /* (the decoded-instruction caches, core 0 and core 1: cm33.c) */
} cm33_sys_t;

enum { CM33_CP_MCR, CM33_CP_MRC, CM33_CP_MCRR, CM33_CP_MRRC, CM33_CP_CDP };
enum { CM33_EV_SEV = 1, CM33_EV_RESET = 2 };

#define CM33_NIRQ 64

struct cm33 {
	uint32_t r[16];
	uint32_t n, z, c, v, q, ge;            /* APSR flags (0 / 1; ge 4 bits) */
	uint32_t it;                            /* ITSTATE (8 bits) */
	uint32_t s[32];                         /* the FPU registers (bit patterns) */
	uint32_t fpscr;
	uint32_t msp, psp, msplim, psplim;      /* the inactive stack pointer lives here, the active one in r[13] */
	uint32_t control, primask, faultmask, basepri;
	uint32_t ipsr;                          /* the exception being handled (0 = thread mode) */
	uint64_t cycles;
	int core;                               /* 0 / 1 */
	int halted;                             /* stopped on a fault or BKPT (for the debugger / the log) */
	int sleeping;                           /* WFI / WFE: until an interrupt / an event */
	int event;                              /* the event register (SEV) */
	int bkpt_stop;                          /* BKPT stops the core (else a debug monitor would) */
	char fault[160];                        /* why it stopped */
	/* the exclusive monitor */
	int excl; uint32_t excl_addr, excl_val;  /* (excl_val: what LDREX read - STREX stores only if memory still holds it) */
	/* the system control block */
	uint32_t vtor, scr, ccr, shpr[3], cpacr, fpccr, fpcar, aircr, icsr_pend;   /* pend: bit 28 PendSV, 26 SysTick */
	uint32_t syst_csr, syst_rvr, syst_cvr; uint64_t syst_t;
	uint32_t nvic_en[2], nvic_act[2]; uint8_t nvic_pri[CM33_NIRQ];
	volatile uint32_t nvic_pend[2];          /* (volatile: another host thread may raise them - cm33_irq) */
	uint32_t active_exc[16];               /* the system exceptions active (nested) */
	uint64_t poll_skip;                     /* (rp2350.c: a polling loop seen - end the slice) */
	volatile int attn;                      /* (something changed that the run loop must look at: interrupts, sleep, faults) */
	/* spin detection: a short backward loop that only reads the same unchanged value (no stores) */
	uint32_t loop_target, loop_iters, loop_loads, loop_stores, ld_addr, ld_val, prev_ld_addr, prev_ld_val; int ld_n;
	uint32_t ld_hash, prev_ld_hash, prev_loop_loads;            /* (every load of a loop round: all the same each round?) */
#define CM33_WL 8
	int ldl_on, ldl_cnt; uint32_t ldl_a[CM33_WL], ldl_v[CM33_WL]; uint8_t ldl_n[CM33_WL];   /* (a round's loads, one by one) */
	int wl_cnt; uint32_t wl_a[CM33_WL], wl_v[CM33_WL]; uint8_t wl_n[CM33_WL];             /* (wait_on 3: asleep on these) */
	int wait_on; uint32_t wait_addr, wait_val; int wait_n;   /* suspended until this value changes */
	uint32_t loop_regs[13]; int spin_slow; uint32_t spin_lo, spin_hi;   /* (a loop with a counter: one round per slice) */
	uint32_t spin_prev[13], spin_delta[13]; uint64_t spin_last, spin_iter; int spin_stable, spin_run;
	int spin_nb; uint8_t spin_breg[4]; uint32_t spin_bval[4];      /* (the loop's compares with constants: where it may end) */   /* (its counters, extrapolated) */
	uint64_t spin_cap, spin_skipped;        /* (wait_on 2: a counting spin asleep - at most spin_cap cycles) */
	cm33_sys_t *sys;
	void *user;
	uint32_t last_pc;
	char pad_end[64];                       /* (two cores side by side: their busy fields on different cache lines) */
};

void cm33_reset (cm33_t *c, uint32_t vtor);          /* SP and PC from the vector table at vtor */
/* run until `cycles` is reached (or the core sleeps / halts / the slice is cut); returns cycles run */
uint64_t cm33_run (cm33_t *c, uint64_t until);
void cm33_irq (cm33_t *c, int n, int level);          /* an interrupt line (0 .. CM33_NIRQ-1) pending / not */
uint32_t cm33_scs_read (cm33_t *c, uint32_t addr);
void cm33_scs_write (cm33_t *c, uint32_t addr, uint32_t v);
void cm33_excl_clear_addr (cm33_t *c, uint32_t addr);  /* another core wrote here */
int cm33_irq_pending (cm33_t *c);                      /* an interrupt waits that may run now */
void cm33_spin_wake (cm33_t *c);                       /* a counting spin wakes: its counters move on */
void cm33_code_written (cm33_sys_t *s, uint32_t addr, uint32_t n);   /* RAM changed by someone else (DMA, a loader) */
#endif
