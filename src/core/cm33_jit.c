/* cm33_jit.c - a block JIT for cm33.c (included by it, just before the run loop): the Thumb code of a UVMC2 game
 * (the RP2350's two Cortex-M33 cores) run as native x86-64 or ARM64 code, a block at a time.
 *
 * Stage 1 (2026-10-27): call threading. The interpreter already decodes every instruction once into an entry with
 * a handler of its own (the decoded-instruction cache, h_*); what it spends its time on is the loop around them -
 * an indirect call per instruction that the host's branch predictor cannot follow (thousands of handlers in turn),
 * and the checks before and after. A block here is the run of decoded instructions from one address, compiled into
 * one function that does for each of them exactly what the loop does - r15 = the next address, last_pc, one
 * cycle, the handler (a direct call: always the same target from that place, so predicted) - and leaves on
 * exactly the loop's conditions: the PC went elsewhere (a branch, an exception), attn was raised (a peripheral,
 * a sleep, an interrupt to look at), the slice is used up. After the block the loop runs the spin detection for
 * the last instruction, as it does after every instruction (only a backward branch can trigger it, and that ends
 * a block). So the JIT gives the interpreter's result to the bit - check: VT_M33JIT=0 against the default.
 *
 * Blocks end before an instruction without a handler of its own (the full decoder runs it), after an IT (the
 * next instructions are conditional: the loop's slow path), after an unconditional branch, at 64 instructions.
 * Each core has its blocks, its code buffer and its page map (the cores may run on two host threads). A write
 * into a page with blocks drops them (cm33_code_written: the core's flush request, and attn so a block running
 * now stops after the store). VT_M33JIT=0 switches it off.
 *
 * Stage 2 (x86-64, 2026-10-27): the common instructions compiled inline - moves, adds, compares, logic, immediate
 * shifts, extends, loads / stores / PUSH / POP with a fast path for the RAM (the spin detector's bookkeeping kept as
 * mrd / mwr keep it), the branches. The cycle count lives in r14 and goes to memory at the exits and before a
 * call; r15 and last_pc are written only there too (nothing reads them in between). An instruction without an
 * inline form, or one whose fast path does not apply (a peripheral, ...), calls its handler with the state the
 * loop would give it. VT_M33JIT=calls: stage 1 only (the reference). Next: the same on ARM64; loops inside a block.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#if (defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)) && !defined(CM33_NO_JIT)
#define CM33_JIT 1
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
#define MJ_A64 1
#endif

#define MJ_CODE (64u << 20)                     /* (code buffer per core) */
#define MJ_MAX 64                               /* (instructions in a block, at most) */
#define MJ_NONE ((uint8_t *) 1)                 /* (no block can start here: the loop runs it) */
#define MJ_DONE 0xFFFFFFFFu                     /* (a block run's result: run_attn ended the slice - the loop just stops) */
typedef uint32_t (*mj_fn) (cm33_t *c, uint64_t until);   /* returns the address of the last instruction it ran */
typedef struct {
	uint8_t **blk;                              /* per halfword of RAM: the block starting there */
	uint8_t *pages;                             /* per 4 KB page of RAM: blocks with code from it */
	uint8_t *incode;                            /* per halfword of RAM: compiled into some block (a write there drops blocks) */
	uint8_t *code, *code_end, *free;
	uint8_t *stubs_end, *enter, *dispatch;      /* (x86-64: the entry and the dispatcher, at the buffer's start) */
	volatile int flush_req;
	uint32_t npages;
	unsigned long n_blocks, n_flush, n_inval;
	uint32_t nhalf;
} mj_core_t;
static mj_core_t mj[2];
/* the two cores may run on two host threads (rp2350.c VT_THREADS): compiling uses shared scratch (the emitter, the exit
 * lists) and edits the block tables, as invalidation does - one at a time (both are rare next to running code) */
static volatile long mj_lk;
static void mj_lock (void)
{
#if defined(_MSC_VER)
	while (_InterlockedExchange (&mj_lk, 1)) while (mj_lk) _mm_pause ();
#else
	while (__atomic_exchange_n (&mj_lk, 1, __ATOMIC_ACQUIRE)) while (__atomic_load_n (&mj_lk, __ATOMIC_RELAXED)) ;
#endif
}
static void mj_unlock (void)
{
#if defined(_MSC_VER)
	_InterlockedExchange (&mj_lk, 0);
#else
	__atomic_store_n (&mj_lk, 0, __ATOMIC_RELEASE);
#endif
}
static int mj_state;                            /* 0 not set up, 1 on, -1 off */
static cm33_t *mj_cpu[2];
static int mj_mode;
static int mj_stats;
static unsigned mj_noinl;                        /* (VT_M33NOINL=hex: inline groups off - to find a wrong one: 1 branches, 2 ALU, 4 single loads / stores, 8 PUSH, 10 POP, 20 POP pc, 40 LDM / STM, 80 [rn, rm, lsl], 100 LDRD / STRD, 200 shifted-register ops, 400 PSRAM) */
static void mj_stubs (mj_core_t *J, cm33_t *c);
static mj_core_t *mj_cur;
static uint8_t *mj_entry[MJ_MAX];               /* (x86-64: each instruction's code - a slice that stopped there goes on there) */
/* (2026-10-30) IT blocks in the blocks: the instructions an IT makes conditional are compiled as calls of h_itw, which
 * does for them what the loop's slow path (exec_slow) does - the condition, the handler, the IT state on - so a block
 * goes on through them instead of ending at the IT (VecDoom: 130 M of these in 40 s of play went one by one through
 * exec_slow). VT_M33IT=0: as before */
static uint8_t mj_itf[MJ_MAX];                  /* (the block's instruction i is inside an IT block) */
static hfn mj_hfn;                              /* (x_handler / a_handler: call this one, not the instruction's own) */
static int mj_it_on = 1;
static void h_itw (cm33_t *c, const di_t *d, uint32_t ip)
{
	if (d->fn == h_decode || d->fn == GENERIC) {                     /* (its entry was dropped: the loop's own slow path) */
		c->r[15] = ip; c->cycles--; exec_slow (c, dcache_for (c)); return;
	}
	int in_it = c->it != 0;
	if (!(in_it && !cond_pass (c, (c->it >> 4) & 15))) d->fn (c, d, ip);
	if (in_it) {
		if ((c->it & 7) == 0) c->it = 0;
		else c->it = (c->it & 0xe0) | ((c->it << 1) & 0x1f);
	}
}
static uint8_t *mj_len;                         /* (VT_M33STATS: each block's instructions, by its start - the statistics) */
unsigned long long mj_st_calls, mj_st_ins, mj_st_inl, mj_st_hand, mj_st_it, mj_st_out, mj_st_none;
static void mj_stats_print (void)
{
	fprintf (stderr, "M33 JIT: %llu block runs, %.2f instructions a block (static), %llu inline / %llu handler instructions compiled\n",
	         mj_st_calls, mj_st_calls ? (double) mj_st_ins / mj_st_calls : 0.0, mj_st_inl, mj_st_hand);
	fprintf (stderr, "M33 JIT: interpreted between blocks: %llu in IT blocks, %llu outside RAM, %llu where no block can start (no handler)\n", mj_st_it, mj_st_out, mj_st_none);
	for (int k = 0; k < 2; k++) fprintf (stderr, "M33 JIT core %d: %lu blocks compiled, %lu flushes, %lu invalidations, %lu KB of code now\n", k, mj[k].n_blocks, mj[k].n_flush, mj[k].n_inval, mj[k].code ? (unsigned long) ((mj[k].free - mj[k].code) >> 10) : 0ul);
}

static void mj_flush (mj_core_t *J, cm33_sys_t *s)
{
	memset (J->blk, 0, sizeof (uint8_t *) * ((s->ram_size >> 1) + 2));
	memset (J->pages, 0, J->npages);
	memset (J->incode, 0, (s->ram_size >> 1) + 2);
	J->free = J->stubs_end; J->flush_req = 0; J->n_flush++;
	if (mj_stats > 0 && (J->n_flush < 10 || !(J->n_flush % 1000))) fprintf (stderr, "M33 JIT flush %lu (core %d, %lu blocks so far)\n", J->n_flush, (int) (J - mj), J->n_blocks);
}

/* called from cm33_code_written: RAM at offset o, n bytes, was written. Only a write over compiled instructions counts
 * (a game's variables often share a page with its code: dropping every block then cost 1.4 million recompiles in the
 * UVMC2 Demo's first 20 s); then the blocks that can contain those bytes - starting at most 256 bytes before them, a
 * block's longest - lose their entries (the code stays in the buffer until it is full), and attn makes a block running
 * now stop after the store */
static void mj_written (uint32_t o, uint32_t n)
{
	for (int k = 0; k < 2; k++) {
		mj_core_t *J = &mj[k];
		if (!J->incode) continue;
		uint32_t h0 = (o >= 2 ? o - 2 : 0) >> 1, h1 = (o + n - 1) >> 1, hmax = J->nhalf;
		int hit = 0;
		for (uint32_t h = h0; h <= h1 && h < hmax; h++) if (J->incode[h]) { hit = 1; break; }
		if (!hit) continue;
		uint32_t lo = h0 >= 128 ? h0 - 128 : 0, hi = h1 + 128 < hmax ? h1 + 128 : hmax - 1;
		mj_lock ();
		for (uint32_t h = lo; h <= hi; h++) J->blk[h] = NULL;
		mj_unlock ();
		J->n_inval++;
		if (mj_cpu[k]) mj_cpu[k]->attn = 1;
	}
}

static int mj_setup_core (cm33_t *c)
{
	if (mj_state == 0) {
		const char *z = getenv ("VT_M33JIT");
		mj_state = (z && (z[0] == '0' || !strcmp (z, "off"))) ? -1 : 1;
		mj_mode = z && !strcmp (z, "calls");
		{ const char *q = getenv ("VT_M33NOINL"); mj_noinl = q ? (unsigned) strtoul (q, NULL, 16) : 0; }
		{ const char *q = getenv ("VT_M33IT"); if (q) mj_it_on = atoi (q); }
		mj_stats = getenv ("VT_M33STATS") != NULL; if (mj_stats) atexit (mj_stats_print);                   /* (VT_M33JIT=calls: every instruction through its handler - stage 1, the reference) */
		if (mj_state == 1) fprintf (stderr, "UVMC2: JIT on (%s)\n",
#ifdef MJ_A64
			"ARM64"
#else
			"x86-64"
#endif
			);
	}
	if (mj_state != 1) return 0;
	mj_core_t *J = &mj[c->core & 1];
	if (J->code) { mj_cpu[c->core & 1] = c; return 1; }
	cm33_sys_t *s = c->sys;
#ifdef _WIN32
	J->code = (uint8_t *) VirtualAlloc (NULL, MJ_CODE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
#if defined(__APPLE__) && defined(MAP_JIT)
	J->code = (uint8_t *) mmap (NULL, MJ_CODE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
#else
	J->code = (uint8_t *) mmap (NULL, MJ_CODE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
	if (J->code == (uint8_t *) MAP_FAILED) J->code = NULL;
#endif
	J->npages = (s->ram_size >> 12) + 2;
	J->blk = (uint8_t **) calloc ((s->ram_size >> 1) + 2, sizeof (uint8_t *));
	J->pages = (uint8_t *) calloc (J->npages, 1);
	J->nhalf = (s->ram_size >> 1) + 2; J->incode = (uint8_t *) calloc (J->nhalf, 1);
	if (!J->code || !J->blk || !J->pages || !J->incode) { fprintf (stderr, "UVMC2: no JIT (no executable memory)\n"); mj_state = -1; return 0; }
	J->code_end = J->code + MJ_CODE; J->free = J->stubs_end = J->code;
	mj_cpu[c->core & 1] = c;
	mj_stubs (J, c);
	return 1;
}

static uint8_t *E;                              /* (the emitter's position) */
static void mj_sync (uint8_t *a, uint8_t *b)
{
#if defined(__APPLE__)
	sys_icache_invalidate (a, (size_t) (b - a));
#elif defined(_WIN32)
	FlushInstructionCache (GetCurrentProcess (), a, (SIZE_T) (b - a));
#elif defined(MJ_A64)
	__builtin___clear_cache ((char *) a, (char *) b);
#else
	(void) a; (void) b;
#endif
}
static void mj_write_on (int on)
{
#if defined(__APPLE__) && defined(MAP_JIT) && defined(MJ_A64)
	if (__builtin_available (macOS 11.0, *)) pthread_jit_write_protect_np (on ? 0 : 1);
#else
	(void) on;
#endif
}

#define MO_R15 ((uint32_t) (offsetof (cm33_t, r) + 4 * 15))
#define MO_LAST ((uint32_t) offsetof (cm33_t, last_pc))
#define MO_CYC ((uint32_t) offsetof (cm33_t, cycles))
#define MO_ATTN ((uint32_t) offsetof (cm33_t, attn))

#ifndef MJ_A64
/* ---------------------------------------------------------------- x86-64. rbx = the core, r12 = until, r14 = the
 * cycle count (in memory only at the exits and around calls), r15 = the RAM (host address). The Thumb registers and
 * flags stay in cm33_t. Scratch: eax, ecx, edx, r8d, r9d. r15 and last_pc in memory are written only where C code
 * can see them (the exits, before a call): in between nothing reads them. */
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { C_O = 0, C_NO, C_B, C_AE, C_E, C_NE, C_BE, C_A, C_S, C_NS, C_P, C_NP, C_L, C_GE, C_LE, C_G };
static void b1 (int x) { *E++ = (uint8_t) x; }
static void d4 (uint32_t x) { memcpy (E, &x, 4); E += 4; }
static void q8 (uint64_t x) { memcpy (E, &x, 8); E += 8; }
#define MO_R(n) ((uint32_t) (offsetof (cm33_t, r) + 4 * (n)))
#define MO_F(f) ((uint32_t) offsetof (cm33_t, f))
static void rex (int w64, int r, int x, int b) { int v = 0x40 | (w64 << 3) | ((r >> 3) << 2) | ((x >> 3) << 1) | (b >> 3); if (v != 0x40) b1 (v); }
static void m_rbx (int reg, uint32_t off) { b1 (0x80 | ((reg & 7) << 3) | 3); d4 (off); }              /* [rbx + disp32] */
static void x_ld (int r, uint32_t off) { rex (0, r, 0, 0); b1 (0x8B); m_rbx (r, off); }                 /* mov r32, [rbx+off] */
static void x_st (uint32_t off, int r) { rex (0, r, 0, 0); b1 (0x89); m_rbx (r, off); }                 /* mov [rbx+off], r32 */
static void x_sti (uint32_t off, uint32_t v) { b1 (0xC7); m_rbx (0, off); d4 (v); }                     /* mov dword [rbx+off], imm */
static void x_ri (int ext, int r, uint32_t v) { rex (0, 0, 0, r); b1 (0x81); b1 (0xC0 | (ext << 3) | (r & 7)); d4 (v); }   /* add0 or1 adc2 sbb3 and4 sub5 xor6 cmp7 r32, imm32 */
static void x_rr (int op, int dst, int src) { rex (0, src, 0, dst); b1 (op); b1 (0xC0 | ((src & 7) << 3) | (dst & 7)); }  /* 01 add 09 or 21 and 29 sub 31 xor 39 cmp 85 test 89 mov */
static void x_rm (int op, int r, uint32_t off) { rex (0, r, 0, 0); b1 (op); m_rbx (r, off); }           /* 03 add 0B or 23 and 2B sub 33 xor 3B cmp, r32 [rbx+off] */
static void x_mi8 (int ext, uint32_t off, int v) { b1 (0x83); m_rbx (ext, off); b1 (v); }               /* op dword [rbx+off], imm8 */
static void x_setm (int cc, uint32_t off) { b1 (0x0F); b1 (0x90 + cc); m_rbx (0, off); }                /* setcc byte [rbx+off] (the flags are 0 / 1: the low byte is all of it) */
static void x_movi (int r, uint32_t v) { if (!v) { x_rr (0x31, r, r); return; } rex (0, 0, 0, r); b1 (0xB8 + (r & 7)); d4 (v); }
static void x_shi (int ext, int r, int n) { rex (0, 0, 0, r); b1 (0xC1); b1 (0xC0 | (ext << 3) | (r & 7)); b1 (n); }   /* shl4 shr5 sar7 */
static void x_un (int ext, int r) { rex (0, 0, 0, r); b1 (0xF7); b1 (0xC0 | (ext << 3) | (r & 7)); }      /* not2 neg3 */
static void x_ext (int op2, int dst, int src) { rex (0, dst, 0, src); b1 (0x0F); b1 (op2); b1 (0xC0 | ((dst & 7) << 3) | (src & 7)); }   /* B6 movzx8 B7 movzx16 BE movsx8 BF movsx16 */
static void x_imul (int dst, int src) { rex (0, dst, 0, src); b1 (0x0F); b1 (0xAF); b1 (0xC0 | ((dst & 7) << 3) | (src & 7)); }
static void x_jcc_to (int cc, uint8_t *t) { b1 (0x0F); b1 (0x80 + cc); d4 ((uint32_t) (int32_t) (t - (E + 4))); }
static uint8_t *x_jcc (int cc) { b1 (0x0F); b1 (0x80 + cc); d4 (0); return E; }
static uint8_t *x_jmp (void) { b1 (0xE9); d4 (0); return E; }
static void x_jmp_to (uint8_t *t) { b1 (0xE9); d4 ((uint32_t) (int32_t) (t - (E + 4))); }
static void patch_to (uint8_t *site, uint8_t *t) { int32_t rel = (int32_t) (t - site); memcpy (site - 4, &rel, 4); }
static void x_cyc_add (int k) { b1 (0x49); b1 (0x83); b1 (0xC6); b1 (k); }                              /* add r14, k */
static void x_cyc_st (void) { b1 (0x4C); b1 (0x89); m_rbx (R14, MO_CYC); }                              /* mov [rbx+cycles], r14 */
static void x_cyc_ld (void) { b1 (0x4C); b1 (0x8B); m_rbx (R14, MO_CYC); }
/* the RAM at [r15 + rdx + disp8]: loads into eax, stores from eax */
static void x_ram_ld (int n, int sx, int disp)
{
	b1 (0x41);
	if (n == 4) b1 (0x8B); else { b1 (0x0F); b1 (n == 1 ? (sx ? 0xBE : 0xB6) : (sx ? 0xBF : 0xB7)); }
	if (disp) { b1 (0x44); b1 (0x17); b1 (disp); } else { b1 (0x04); b1 (0x17); }
}
static void x_ram_st (int n, int disp)
{
	if (n == 2) b1 (0x66);
	b1 (0x41); b1 (n == 1 ? 0x88 : 0x89);
	if (disp) { b1 (0x44); b1 (0x17); b1 (disp); } else { b1 (0x04); b1 (0x17); }
}
static uint32_t mj_xram_size;
static void sl_add (uint8_t *site, uint32_t ip, uint32_t next, const di_t *d);
/* the PSRAM at [r13 + rdx] (r13 as a base needs a displacement: disp8 0) */
static void x_xram_ld (int n)
{
	b1 (0x41);
	if (n == 4) b1 (0x8B); else { b1 (0x0F); b1 (n == 1 ? 0xB6 : 0xB7); }
	b1 (0x44); b1 (0x15); b1 (0);
}
static void x_xram_st (int n) { if (n == 2) b1 (0x66); b1 (0x41); b1 (n == 1 ? 0x88 : 0x89); b1 (0x44); b1 (0x15); b1 (0); }
/* ecx = the address: in the PSRAM window (0x1x000000 with bit 24 set, as XRAM tests it)? edx = the offset; else to the
 * slow path */
static void x_xram_check (uint32_t ip, uint32_t next, const di_t *d, int n)
{
	x_rr (0x89, RDX, RCX); x_shi (5, RDX, 24); x_ri (4, RDX, 0xF1); x_ri (7, RDX, 0x11); sl_add (x_jcc (C_NE), ip, next, d);
	x_rr (0x89, RDX, RCX); x_ri (4, RDX, 0xffffff); x_ri (7, RDX, mj_xram_size - (uint32_t) n); sl_add (x_jcc (C_A), ip, next, d);
}
static void x_call (void *fn) { b1 (0x48); b1 (0xB8); q8 ((uint64_t) (uintptr_t) fn); b1 (0xFF); b1 (0xD0); }

/* the exits, emitted after the block's code: set - the PC, last_pc and the cycles from the registers; noset - a call
 * left them in memory already. Both return the instruction's address (the loop's spin detection) */
typedef struct { uint8_t *site; uint32_t ip, pc; int set; } mj_exit_t;
typedef struct { uint8_t *site, *join; uint32_t ip, next; const di_t *d; } mj_slow_t;
static mj_exit_t mj_ex[MJ_MAX * 24]; static int mj_nex;
static mj_slow_t mj_sl[MJ_MAX * 6]; static int mj_nsl;
static uint32_t mj_ram_base, mj_ram_size, mj_xram_size; static uint8_t *mj_code_pages;   /* (mode 1: calls only - stage 1, the reference) */
static void ex_add (uint8_t *site, uint32_t ip, uint32_t pc, int set) { mj_exit_t *x = &mj_ex[mj_nex++]; x->site = site; x->ip = ip; x->pc = pc; x->set = set; }
static void sl_add (uint8_t *site, uint32_t ip, uint32_t next, const di_t *d) { mj_slow_t *x = &mj_sl[mj_nsl++]; x->site = site; x->ip = ip; x->next = next; x->d = d; x->join = NULL; }

static void mj_prologue (void)
{
	b1 (0x53); b1 (0x41); b1 (0x54); b1 (0x41); b1 (0x55); b1 (0x41); b1 (0x56); b1 (0x41); b1 (0x57);   /* push rbx r12 r13 r14 r15 */
	b1 (0x48); b1 (0x83); b1 (0xEC); b1 (0x20);                             /* sub rsp, 32 (aligned; Windows' shadow space) */
#ifdef _WIN32
	b1 (0x48); b1 (0x89); b1 (0xCB); b1 (0x49); b1 (0x89); b1 (0xD4);       /* mov rbx, rcx; mov r12, rdx */
#else
	b1 (0x48); b1 (0x89); b1 (0xFB); b1 (0x49); b1 (0x89); b1 (0xF4);       /* mov rbx, rdi; mov r12, rsi */
#endif
	x_cyc_ld ();
}
static uint8_t *mj_epi;
static void mj_epilogue (void)
{
	mj_epi = E;
	b1 (0x48); b1 (0x83); b1 (0xC4); b1 (0x20);                             /* add rsp, 32 */
	b1 (0x41); b1 (0x5F); b1 (0x41); b1 (0x5E); b1 (0x41); b1 (0x5D); b1 (0x41); b1 (0x5C); b1 (0x5B); b1 (0xC3);
}
/* a call of the instruction's handler, as the interpreter's loop makes it */
static void x_handler (uint32_t ip, uint32_t next, const di_t *d)
{
	x_sti (MO_R15, next); x_sti (MO_LAST, ip);
	b1 (0x49); b1 (0x8D); b1 (0x46); b1 (0x01); b1 (0x48); b1 (0x89); m_rbx (RAX, MO_CYC);   /* lea rax, [r14+1]; mov [rbx+cycles], rax */
#ifdef _WIN32
	b1 (0x48); b1 (0x89); b1 (0xD9); b1 (0x48); b1 (0xBA); q8 ((uint64_t) (uintptr_t) d); b1 (0x41); b1 (0xB8); d4 (ip);
#else
	b1 (0x48); b1 (0x89); b1 (0xDF); b1 (0x48); b1 (0xBE); q8 ((uint64_t) (uintptr_t) d); b1 (0xBA); d4 (ip);
#endif
	x_call ((void *) (mj_hfn ? mj_hfn : d->fn));
	x_cyc_ld ();
	b1 (0x81); m_rbx (7, MO_R15); d4 (next); ex_add (x_jcc (C_NE), ip, 0, 0);   /* (the PC went elsewhere) */
	x_mi8 (7, MO_ATTN, 0); ex_add (x_jcc (C_NE), ip, 0, 0);                    /* (attn) */
}
/* the flags of an x86 add / sub just done (sub: ARM's C is "no borrow") */
static void x_flags_add (void) { x_setm (C_S, MO_F (n)); x_setm (C_E, MO_F (z)); x_setm (C_B, MO_F (c)); x_setm (C_O, MO_F (v)); }
static void x_flags_sub (void) { x_setm (C_S, MO_F (n)); x_setm (C_E, MO_F (z)); x_setm (C_AE, MO_F (c)); x_setm (C_O, MO_F (v)); }
static void x_flags_nz (int r) { x_rr (0x85, r, r); x_setm (C_S, MO_F (n)); x_setm (C_E, MO_F (z)); }
/* the condition codes: eax = 1 when cond passes */
static void x_cond (int cond)
{
	switch (cond >> 1) {
	case 0: x_ld (RAX, MO_F (z)); break;
	case 1: x_ld (RAX, MO_F (c)); break;
	case 2: x_ld (RAX, MO_F (n)); break;
	case 3: x_ld (RAX, MO_F (v)); break;
	case 4: x_ld (RAX, MO_F (z)); x_ri (6, RAX, 1); x_rm (0x23, RAX, MO_F (c)); break;   /* c && !z */
	case 5: x_ld (RAX, MO_F (n)); x_rm (0x33, RAX, MO_F (v)); x_ri (6, RAX, 1); break;   /* n == v */
	default: x_ld (RAX, MO_F (n)); x_rm (0x33, RAX, MO_F (v)); x_rm (0x0B, RAX, MO_F (z)); x_ri (6, RAX, 1); break;   /* n == v && !z */
	}
	if (cond & 1) x_ri (6, RAX, 1);
}
static int bcc_cond (hfn f, const di_t *d)
{
	static const hfn bcc[14] = { h_beq, h_bne, h_bcs, h_bcc, h_bmi, h_bpl, h_bvs, h_bvc, h_bhi, h_bls, h_bge, h_blt, h_bgt, h_ble };
	for (int k = 0; k < 14; k++) if (f == bcc[k]) return k;
	if (f == h_bcond && d->op < 14) return d->op;
	return -1;
}
/* a load from RAM inline: ecx = the address. Out of RAM, or the spin detector listing a round's loads: the handler
 * (slow). Then the bookkeeping mrd keeps, and eax = the value (sign-extended for the register when sx) */
static void x_load (uint32_t ip, uint32_t next, const di_t *d, int n, int sx)
{
	x_mi8 (7, MO_F (ldl_on), 0); sl_add (x_jcc (C_NE), ip, next, d);
	x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - (uint32_t) n);
	if (mj_xram_size >= 4) {                                                      /* (not RAM: the PSRAM, then the handler) */
		uint8_t *px = x_jcc (C_A);
		x_ram_ld (n, 0, 0); uint8_t *done = x_jmp ();
		patch_to (px, E); x_xram_check (ip, next, d, n); x_xram_ld (n);
		patch_to (done, E);
	} else { sl_add (x_jcc (C_A), ip, next, d); x_ram_ld (n, 0, 0); }
	b1 (0xFF); m_rbx (0, MO_F (loop_loads));                                       /* inc dword [rbx+loop_loads] */
	x_st (MO_F (ld_addr), RCX); x_st (MO_F (ld_val), RAX); x_sti (MO_F (ld_n), (uint32_t) n);
	x_ld (R8, MO_F (ld_hash)); x_rr (0x31, R8, RCX);                              /* ld_hash = (ld_hash ^ a) * K + v */
	b1 (0x45); b1 (0x69); b1 (0xC0); d4 (0x9E3779B1u); x_rr (0x01, R8, RAX); x_st (MO_F (ld_hash), R8);
	if (sx) x_ext (n == 1 ? 0xBE : 0xBF, RAX, RAX);
}
/* a store to RAM inline: ecx = the address, eax = the value. Out of RAM, an exclusive monitor anywhere, or code in
 * the page (decoded entries to drop): the handler */
static void x_store (uint32_t ip, uint32_t next, const di_t *d, int n)
{
	b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &excl_any); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0);   /* mov r8, &excl_any; cmp dword [r8], 0 */
	sl_add (x_jcc (C_NE), ip, next, d);
	x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - (uint32_t) n);
	if (mj_xram_size >= 4) {                                                      /* (not RAM: the PSRAM - no code there - then the handler) */
		uint8_t *pr = x_jcc (C_BE);
		x_xram_check (ip, next, d, n); x_xram_st (n); uint8_t *done = x_jmp ();
		patch_to (pr, E);
		x_ri (7, RDX, 2); sl_add (x_jcc (C_B), ip, next, d);
		b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) mj_code_pages);
		x_rr (0x89, R9, RDX); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		x_rr (0x89, R9, RDX); x_ri (5, R9, 2); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		x_ram_st (n, 0);
		patch_to (done, E);
		b1 (0xFF); m_rbx (0, MO_F (loop_stores));
		return;
	}
	sl_add (x_jcc (C_A), ip, next, d);
	x_ri (7, RDX, 2); sl_add (x_jcc (C_B), ip, next, d);                         /* (the first two bytes: the slow way) */
	b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) mj_code_pages);           /* mov r8, code_pages */
	x_rr (0x89, R9, RDX); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0);   /* cmp byte [r8+r9], 0 */
	sl_add (x_jcc (C_NE), ip, next, d);
	x_rr (0x89, R9, RDX); x_ri (5, R9, 2); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0);
	sl_add (x_jcc (C_NE), ip, next, d);
	x_ram_st (n, 0);
	b1 (0xFF); m_rbx (0, MO_F (loop_stores));
}
/* one instruction inline; 0 when it has no inline form (then its handler is called). *cost: its cycles, 1 + the
 * handler's extra ones; *branch: 1 when it ends in its own exits (a branch) */
static int x_inline (uint32_t ip, uint32_t next, const di_t *d, int *cost, int *branch)
{
	hfn f = d->fn; int a = d->a, b = d->b, e = d->e; uint32_t imm = d->imm;
	*cost = 1; *branch = 0;
	if (mj_mode == 1) return 0;
	int cond = bcc_cond (f, d);
	if ((cond >= 0 || f == h_b || f == h_bl || f == h_cbz) && !(mj_noinl & 1)) {
		if (imm == ip) return 0;                                                 /* (a branch to itself: a sleep - the handler) */
		uint8_t *skip = NULL;
		if (cond >= 0) { x_cond (cond); x_rr (0x85, RAX, RAX); skip = x_jcc (C_E); }
		else if (f == h_cbz) { x_mi8 (7, MO_R (a), 0); skip = x_jcc (d->op ? C_E : C_NE); }   /* (taken when (r != 0) == op) */
		if (f == h_bl) x_sti (MO_R (14), (ip + 4) | 1);
		x_cyc_add (2); x_sti (MO_R15, imm); x_sti (MO_LAST, ip);                    /* (taken: one cycle more) */
		ex_add (x_jmp (), ip, 0, 0);
		if (skip) patch_to (skip, E);
		*branch = skip ? 1 : 2;                                                  /* (1: falls through when not taken) */
		return 1;
	}
	/* (the PC as an operand or the destination: the handler, which sees r15 as the loop sets it) */
	if ((mj_noinl & 2) && f != h_ldw_imm && f != h_ldh_imm && f != h_ldb_imm && f != h_stw_imm && f != h_sth_imm && f != h_stb_imm && f != h_ld_imm && f != h_st_imm && f != h_ldst_idx && f != h_ldst_reg && f != h_ld_lit && f != h_push && f != h_pop && f != h_pop32 && f != h_ldmia && f != h_ldst_rs && f != h_ldrd && f != h_dpr_add && f != h_dpr_sub && f != h_dpr_rsb && f != h_dpr_and && f != h_dpr_orr && f != h_dpr_eor && f != h_dpr_bic && f != h_dpr_mov && f != h_dpr_mvn) return 0;
	if (f == h_movi) { if (a == 15) return 0; x_sti (MO_R (a), imm); return 1; }
	if (f == h_movs8) { x_sti (MO_R (a), imm); x_sti (MO_F (n), imm >> 31); x_sti (MO_F (z), imm == 0); return 1; }
	if (f == h_cmp8 || f == h_adds8 || f == h_subs8) {
		x_ld (RAX, MO_R (a)); x_ri (f == h_adds8 ? 0 : f == h_subs8 ? 5 : 7, RAX, imm);
		if (f == h_adds8) x_flags_add (); else x_flags_sub ();
		if (f != h_cmp8) x_st (MO_R (a), RAX);
		return 1;
	}
	if (f == h_addsub3) {
		if (a == 15 || b == 15 || (!(d->op & 2) && e == 15)) return 0;
		x_ld (RAX, MO_R (b));
		if (d->op & 2) x_ri ((d->op & 1) ? 5 : 0, RAX, imm); else x_rm ((d->op & 1) ? 0x2B : 0x03, RAX, MO_R (e));
		if (d->op & 1) x_flags_sub (); else x_flags_add ();
		x_st (MO_R (a), RAX); return 1;
	}
	if (f == h_hi_mov || f == h_hi_add) {
		if (a == 15 || b == 15) return 0;
		x_ld (RAX, MO_R (b)); if (f == h_hi_add) x_rm (0x03, RAX, MO_R (a)); x_st (MO_R (a), RAX); return 1;
	}
	if (f == h_hi_cmp) { if (a == 15 || b == 15) return 0; x_ld (RAX, MO_R (a)); x_rm (0x3B, RAX, MO_R (b)); x_flags_sub (); return 1; }
	if (f == h_addspi) { if (a == 15) return 0; x_ld (RAX, MO_R (13)); x_ri (0, RAX, imm); x_st (MO_R (a), RAX); return 1; }
	if (f == h_spadj) { x_ld (RAX, MO_R (13)); x_ri (0, RAX, imm); x_st (MO_R (13), RAX); return 1; }
	if (f == h_dpi_addn || f == h_dpi_subn || f == h_addw) {
		if (a == 15 || b == 15) return 0;
		x_ld (RAX, MO_R (b)); x_ri (f == h_dpi_subn ? 5 : 0, RAX, imm); x_st (MO_R (a), RAX); return 1;
	}
	if (f == h_dpi_cmp) { if (b == 15) return 0; x_ld (RAX, MO_R (b)); x_ri (7, RAX, imm); x_flags_sub (); return 1; }
	if (f == h_movt) { if (a == 15) return 0; x_ld (RAX, MO_R (a)); x_ri (4, RAX, 0xffff); x_ri (1, RAX, imm << 16); x_st (MO_R (a), RAX); return 1; }
	if (f == h_dpi_and || f == h_dpi_bic || f == h_dpi_orr || f == h_dpi_orn || f == h_dpi_eor || f == h_dpi_mov || f == h_dpi_mvn) {
		if ((!d->f3 && a == 15) || b == 15) return 0;
		uint32_t v = imm;
		if (f == h_dpi_mov) x_movi (RAX, v);
		else if (f == h_dpi_mvn) x_movi (RAX, ~v);
		else {
			x_ld (RAX, MO_R (b));
			if (f == h_dpi_and) x_ri (4, RAX, v); else if (f == h_dpi_bic) x_ri (4, RAX, ~v);
			else if (f == h_dpi_orr) x_ri (1, RAX, v); else if (f == h_dpi_orn) x_ri (1, RAX, ~v); else x_ri (6, RAX, v);
		}
		if (!d->f3) x_st (MO_R (a), RAX);
		if (d->f1) { x_flags_nz (RAX); if (d->f2) x_sti (MO_F (c), (uint32_t) (d->f2 - 1)); }
		return 1;
	}
	if (f == h_dpi_add || f == h_dpi_sub || f == h_dpi_rsb) {
		if ((!d->f3 && a == 15) || b == 15) return 0;
		if (f == h_dpi_rsb) { x_movi (RAX, imm); x_rm (0x2B, RAX, MO_R (b)); }
		else { x_ld (RAX, MO_R (b)); x_ri (f == h_dpi_add ? 0 : 5, RAX, imm); }
		if (d->f1) { if (f == h_dpi_add) x_flags_add (); else x_flags_sub (); }
		if (!d->f3) x_st (MO_R (a), RAX);
		return 1;
	}
	if (f == h_dp16_and || f == h_dp16_eor || f == h_dp16_orr || f == h_dp16_bic || f == h_dp16_mvn || f == h_dp16_tst || f == h_dp16_mul) {
		if (a == 15 || b == 15) return 0;
		if (f == h_dp16_mvn) { x_ld (RAX, MO_R (b)); x_un (2, RAX); }
		else if (f == h_dp16_bic) { x_ld (RAX, MO_R (b)); x_un (2, RAX); x_rm (0x23, RAX, MO_R (a)); }
		else if (f == h_dp16_mul) { x_ld (RAX, MO_R (a)); x_ld (RCX, MO_R (b)); x_imul (RAX, RCX); }
		else { x_ld (RAX, MO_R (a)); x_rm (f == h_dp16_and || f == h_dp16_tst ? 0x23 : f == h_dp16_eor ? 0x33 : 0x0B, RAX, MO_R (b)); }
		if (f != h_dp16_tst) x_st (MO_R (a), RAX);
		x_flags_nz (RAX); return 1;
	}
	if (f == h_dp16_cmp || f == h_dp16_cmn || f == h_dp16_rsb) {
		if (a == 15 || b == 15) return 0;
		if (f == h_dp16_rsb) { x_rr (0x31, RAX, RAX); x_rm (0x2B, RAX, MO_R (b)); x_flags_sub (); x_st (MO_R (a), RAX); }
		else { x_ld (RAX, MO_R (a)); x_rm (f == h_dp16_cmp ? 0x3B : 0x03, RAX, MO_R (b)); if (f == h_dp16_cmp) x_flags_sub (); else x_flags_add (); }
		return 1;
	}
	if (f == h_shift_imm) {
		if (a == 15 || b == 15 || d->op > 2 || imm >= 32) return 0;
		x_ld (RAX, MO_R (b));
		if (imm) { x_shi (d->op == 0 ? 4 : d->op == 1 ? 5 : 7, RAX, (int) imm); x_setm (C_B, MO_F (c)); }   /* (C: the last bit out) */
		x_st (MO_R (a), RAX); x_flags_nz (RAX); return 1;
	}
	if (f == h_ext16) {
		if (a == 15 || b == 15) return 0;
		x_ld (RCX, MO_R (b)); x_ext (d->op == 0 ? 0xBF : d->op == 1 ? 0xBE : d->op == 2 ? 0xB7 : 0xB6, RAX, RCX); x_st (MO_R (a), RAX); return 1;
	}
	if (f == h_nop) return 1;
	if (f == h_it && mj_it_on) { x_sti (MO_F (it), imm); return 1; }        /* (2026-10-30: IT, as h_it - its instructions follow in the block, h_itw) */
	/* loads and stores */
	int n = 0, ld = 0, sx = 0, wb = 0, base = -1; uint32_t off = 0;
	if (f == h_ldw_imm || f == h_ldh_imm || f == h_ldb_imm) { n = f == h_ldw_imm ? 4 : f == h_ldh_imm ? 2 : 1; ld = 1; base = b; off = imm; }
	else if (f == h_stw_imm || f == h_sth_imm || f == h_stb_imm) { n = f == h_stw_imm ? 4 : f == h_sth_imm ? 2 : 1; base = b; off = imm; }
	else if (f == h_ld_imm) { n = e; ld = 1; sx = d->f1; base = b; off = imm; }
	else if (f == h_st_imm) { n = e; base = b; off = imm; }
	else if (f == h_ldst_idx) { n = e; ld = d->f2; sx = d->f1; base = b; off = imm; wb = d->f3; }
	if (n && (n == 1 || n == 2 || n == 4) && !(mj_noinl & 4)) {
		if (a == 15 || base == 15) return 0;
		x_ld (RCX, MO_R (base));
		if (f == h_ldst_idx) {
			if (d->f3 & 1) x_ri (0, RCX, off);                                    /* (pre-index: the address with the offset) */
		} else if (off) x_ri (0, RCX, off);
		if (ld) { x_load (ip, next, d, n, sx); if (wb & 2) { x_ld (R9, MO_R (base)); x_ri (0, R9, off); x_st (MO_R (base), R9); } x_st (MO_R (a), RAX); }
		else { x_ld (RAX, MO_R (a)); x_store (ip, next, d, n); if (wb & 2) { x_ld (R9, MO_R (base)); x_ri (0, R9, off); x_st (MO_R (base), R9); } }
		*cost = 2; return 1;
	}
	if (f == h_ldst_reg && !(mj_noinl & 4)) {
		if (a == 15 || b == 15 || e == 15) return 0;
		static const int sz[8] = { 4, 2, 1, 1, 4, 2, 1, 2 }, sxs[8] = { 0, 0, 0, 1, 0, 0, 0, 1 };
		int op = d->op & 7; n = sz[op];
		x_ld (RCX, MO_R (b)); x_rm (0x03, RCX, MO_R (e));
		if (op >= 3) { x_load (ip, next, d, n, sxs[op]); x_st (MO_R (a), RAX); }
		else { x_ld (RAX, MO_R (a)); x_store (ip, next, d, n); }
		*cost = 2; return 1;
	}
	if (f == h_ld_lit && !(mj_noinl & 4)) {
		if (a == 15 || (d->e != 1 && d->e != 2 && d->e != 4)) return 0;
		x_movi (RCX, imm); x_load (ip, next, d, d->e, d->f1); x_st (MO_R (a), RAX); *cost = 2; return 1;
	}
	if (f == h_push && !d->op && !(imm & 0x8000) && e && !(mj_noinl & 8)) {
		/* the whole range in RAM, no monitor, no code in its pages: the stores inline, else the handler */
		x_ld (RCX, MO_R (13)); x_ri (5, RCX, 4u * (uint32_t) e);
		x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 4u * (uint32_t) e); sl_add (x_jcc (C_A), ip, next, d);
		x_ri (7, RDX, 2); sl_add (x_jcc (C_B), ip, next, d);
		b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &excl_any); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) mj_code_pages);
		x_rr (0x89, R9, RDX); x_ri (5, R9, 2); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		x_rr (0x89, R9, RDX); x_ri (0, R9, 4u * (uint32_t) e - 4); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		x_st (MO_R (13), RCX);
		int k = 0;
		for (int i = 0; i < 15; i++) if ((imm >> i) & 1) { x_ld (RAX, MO_R (i)); x_ram_st (4, 4 * k); k++; }
		x_mi8 (0, MO_F (loop_stores), e);
		*cost = 1 + e; return 1;
	}
	if (f == h_push && d->op && !(imm & 0x8000) && e && d->f1 != 15 && !(mj_noinl & 8)) {
		/* STMDB rn!, {list}: the stores (the old values), then rn = the address */
		int rn = d->f1;
		x_ld (RCX, MO_R (rn)); x_ri (5, RCX, 4u * (uint32_t) e);
		x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 4u * (uint32_t) e); sl_add (x_jcc (C_A), ip, next, d);
		x_ri (7, RDX, 2); sl_add (x_jcc (C_B), ip, next, d);
		b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &excl_any); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) mj_code_pages);
		x_rr (0x89, R9, RDX); x_ri (5, R9, 2); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		x_rr (0x89, R9, RDX); x_ri (0, R9, 4u * (uint32_t) e - 4); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
		int k = 0;
		for (int i = 0; i < 15; i++) if ((imm >> i) & 1) { x_ld (RAX, MO_R (i)); x_ram_st (4, 4 * k); k++; }
		x_st (MO_R (rn), RCX);
		x_mi8 (0, MO_F (loop_stores), e);
		*cost = 1 + e; return 1;
	}
	if (((f == h_pop && (imm & 0x8000)) || (f == h_pop32 && (imm & 0x8000))) && !(mj_noinl & 0x20)) {
		/* POP {..., pc} (and LDMIA sp!, {..., pc}): the return address first - a plain Thumb address, else the handler
		 * (an exception return, ...) - then the loads in order, sp, the PC; an exit */
		int cnt = 0; for (int i = 0; i < 15; i++) cnt += (imm >> i) & 1;
		if (imm & (1u << 13)) return 0;
		x_mi8 (7, MO_F (ldl_on), 0); sl_add (x_jcc (C_NE), ip, next, d);
		x_ld (RCX, MO_R (13));
		x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 4u * (uint32_t) (cnt + 1)); sl_add (x_jcc (C_A), ip, next, d);
		x_ram_ld (4, 0, 4 * cnt); x_rr (0x89, R9, RAX);                          /* (r9 = the return address) */
		x_ri (4, R9, 1); x_ri (7, R9, 1); sl_add (x_jcc (C_NE), ip, next, d);
		x_rr (0x89, R9, RAX); x_shi (5, R9, 24); x_ri (7, R9, 0xFE); sl_add (x_jcc (C_AE), ip, next, d);
		x_ld (R8, MO_F (ld_hash));
		for (int i = 0, k = 0; i < 16; i++) if ((imm >> i) & 1) {
			x_ram_ld (4, 0, 4 * k);
			if (i < 15) x_st (MO_R (i), RAX); else { x_rr (0x89, R10, RAX); x_ri (4, R10, ~1u); }
			x_rr (0x89, R9, RCX); if (k) x_ri (0, R9, 4u * (uint32_t) k);
			x_rr (0x31, R8, R9); b1 (0x45); b1 (0x69); b1 (0xC0); d4 (0x9E3779B1u); x_rr (0x01, R8, RAX);
			k++;
		}
		x_st (MO_F (ld_hash), R8); x_st (MO_F (ld_addr), R9); x_st (MO_F (ld_val), RAX); x_sti (MO_F (ld_n), 4);
		x_mi8 (0, MO_F (loop_loads), cnt + 1);
		x_ri (0, RCX, 4u * (uint32_t) (cnt + 1)); x_st (MO_R (13), RCX);
		x_st (MO_R15, R10); x_sti (MO_LAST, ip);
		x_cyc_add (f == h_pop ? 1 + cnt + 2 : 1 + e);
		ex_add (x_jmp (), ip, 0, 0);
		*branch = 2; return 1;
	}
	if (f == h_ldmia && a != 15 && imm && !(imm & 0x8000) && !(mj_noinl & 0x40)) {
		/* LDMIA / STMIA rn{!}: loads (writeback unless rn is loaded) or stores, ascending */
		int cnt = 0; for (int i = 0; i < 15; i++) cnt += (imm >> i) & 1;
		int rn = a;
		if (d->f1) { x_mi8 (7, MO_F (ldl_on), 0); sl_add (x_jcc (C_NE), ip, next, d); }
		x_ld (RCX, MO_R (rn));
		x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 4u * (uint32_t) cnt); sl_add (x_jcc (C_A), ip, next, d);
		if (d->f1) {
			x_ld (R8, MO_F (ld_hash));
			for (int i = 0, k = 0; i < 15; i++) if ((imm >> i) & 1) {
				x_ram_ld (4, 0, 4 * k); x_st (MO_R (i), RAX);
				x_rr (0x89, R9, RCX); if (k) x_ri (0, R9, 4u * (uint32_t) k);
				x_rr (0x31, R8, R9); b1 (0x45); b1 (0x69); b1 (0xC0); d4 (0x9E3779B1u); x_rr (0x01, R8, RAX);
				k++;
			}
			x_st (MO_F (ld_hash), R8); x_st (MO_F (ld_addr), R9); x_st (MO_F (ld_val), RAX); x_sti (MO_F (ld_n), 4);
			x_mi8 (0, MO_F (loop_loads), cnt);
			if (d->f2 && !((imm >> rn) & 1)) { x_ri (0, RCX, 4u * (uint32_t) cnt); x_st (MO_R (rn), RCX); }
		} else {
			x_ri (7, RDX, 2); sl_add (x_jcc (C_B), ip, next, d);
			b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &excl_any); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
			b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) mj_code_pages);
			x_rr (0x89, R9, RDX); x_ri (5, R9, 2); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
			x_rr (0x89, R9, RDX); x_ri (0, R9, 4u * (uint32_t) cnt - 4); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
			for (int i = 0, k = 0; i < 15; i++) if ((imm >> i) & 1) { x_ld (RAX, MO_R (i)); x_ram_st (4, 4 * k); k++; }
			x_mi8 (0, MO_F (loop_stores), cnt);
			if (d->f2) { x_ri (0, RCX, 4u * (uint32_t) cnt); x_st (MO_R (rn), RCX); }
		}
		*cost = 1 + e; return 1;
	}
	if (f == h_ldst_rs && (e == 1 || e == 2 || e == 4) && imm < 4 && !(mj_noinl & 0x80)) {
		if (a == 15 || b == 15 || d->f3 == 15) return 0;
		x_ld (RCX, MO_R (d->f3)); if (imm) x_shi (4, RCX, (int) imm); x_rm (0x03, RCX, MO_R (b));
		if (d->f2) { x_load (ip, next, d, e, d->f1); x_st (MO_R (a), RAX); }
		else { x_ld (RAX, MO_R (a)); x_store (ip, next, d, e); }
		*cost = 2; return 1;
	}
	if (f == h_ldrd && !(mj_noinl & 0x100)) {
		/* LDRD / STRD: two words, as two loads (each with its bookkeeping) or two stores; writeback after */
		if (a == 15 || b == 15 || e == 15 || (d->f2 && a == e)) return 0;
		x_ld (RCX, MO_R (b)); if (d->f3 & 1) x_ri (0, RCX, imm);
		if (d->f2) {
			/* (both loads' ranges first: a slow second load after a fast first would load it twice) */
			x_mi8 (7, MO_F (ldl_on), 0); sl_add (x_jcc (C_NE), ip, next, d);
			x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 8); sl_add (x_jcc (C_A), ip, next, d);
			x_load (ip, next, d, 4, 0); x_rr (0x89, R10, RAX);
			x_ri (0, RCX, 4); x_load (ip, next, d, 4, 0);
			x_st (MO_R (a), R10); x_st (MO_R (e), RAX);
		} else {
			/* (both stores checked before either) */
			x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 8); sl_add (x_jcc (C_A), ip, next, d);
			x_ri (7, RDX, 2); sl_add (x_jcc (C_B), ip, next, d);
			b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &excl_any); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
			b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) mj_code_pages);
			x_rr (0x89, R9, RDX); x_ri (5, R9, 2); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
			x_rr (0x89, R9, RDX); x_ri (0, R9, 4); x_shi (5, R9, 12); b1 (0x43); b1 (0x80); b1 (0x3C); b1 (0x08); b1 (0); sl_add (x_jcc (C_NE), ip, next, d);
			x_ld (RAX, MO_R (a)); x_ram_st (4, 0); x_ld (RAX, MO_R (e)); x_ram_st (4, 4);
			x_mi8 (0, MO_F (loop_stores), 2);
		}
		if (d->f3 & 2) { x_ld (R9, MO_R (b)); x_ri (0, R9, imm); x_st (MO_R (b), R9); }
		*cost = 3; return 1;
	}
	if ((f == h_dpr_add || f == h_dpr_sub || f == h_dpr_rsb || f == h_dpr_and || f == h_dpr_orr || f == h_dpr_eor || f == h_dpr_bic || f == h_dpr_mov || f == h_dpr_mvn)
	    && !d->f1 && !d->f3 && d->f2 <= 2 && imm < 32 && !(mj_noinl & 0x200)) {
		/* data processing with a shifted register, no flags: rd = rn op (rm shift #k) */
		if (a == 15 || b == 15 || e == 15) return 0;
		x_ld (RCX, MO_R (e)); if (imm) x_shi (d->f2 == 0 ? 4 : d->f2 == 1 ? 5 : 7, RCX, (int) imm);
		if (f == h_dpr_mov) x_rr (0x89, RAX, RCX);
		else if (f == h_dpr_mvn) { x_rr (0x89, RAX, RCX); x_un (2, RAX); }
		else if (f == h_dpr_rsb) { x_rr (0x89, RAX, RCX); x_rm (0x2B, RAX, MO_R (b)); }
		else {
			x_ld (RAX, MO_R (b));
			if (f == h_dpr_bic) { x_un (2, RCX); x_rr (0x21, RAX, RCX); }
			else x_rr (f == h_dpr_add ? 0x01 : f == h_dpr_sub ? 0x29 : f == h_dpr_and ? 0x21 : f == h_dpr_orr ? 0x09 : 0x31, RAX, RCX);
		}
		x_st (MO_R (a), RAX); return 1;
	}
	if ((f == h_ubfx || f == h_bfi || f == h_mul32) && !(mj_noinl & 0x2000)) {
		/* (2026-10-30) bit fields and 32-bit multiplies, as their handlers: UBFX / SBFX, BFI / BFC, MUL / MLA / MLS */
		if (f == h_ubfx) {
			if (a == 15 || b == 15 || imm >= 32 || e < 1 || e > 32 || imm + e > 32) return 0;
			x_ld (RAX, MO_R (b));
			if (e < 32) {
				if (d->op) { if (32 - imm - e) x_shi (4, RAX, (int) (32 - imm - e)); x_shi (7, RAX, (int) (32 - e)); }
				else { if (imm) x_shi (5, RAX, (int) imm); x_ri (4, RAX, (1u << e) - 1); }
			}
			x_st (MO_R (a), RAX); return 1;
		}
		if (f == h_bfi) {
			if (a == 15 || (!d->op && b == 15) || e > 31) return 0;
			if (d->op) x_movi (RAX, 0); else { x_ld (RAX, MO_R (b)); if (e) x_shi (4, RAX, e); x_ri (4, RAX, imm); }
			x_ld (RCX, MO_R (a)); x_ri (4, RCX, ~imm); x_rr (0x09, RAX, RCX); x_st (MO_R (a), RAX); return 1;
		}
		if (a == 15 || b == 15 || e == 15 || d->op > 2 || (d->op && d->f1 == 15)) return 0;
		x_ld (RAX, MO_R (b)); x_ld (RCX, MO_R (e)); x_imul (RAX, RCX);
		if (d->op == 1) x_rm (0x03, RAX, MO_R (d->f1));
		else if (d->op == 2) { x_ld (RCX, MO_R (d->f1)); x_rr (0x29, RCX, RAX); x_rr (0x89, RAX, RCX); }
		x_st (MO_R (a), RAX); return 1;
	}
	if (f == h_pop && !(imm & 0x8000) && imm && !(mj_noinl & 0x10)) {
		int cnt = 0; for (int i = 0; i < 15; i++) cnt += (imm >> i) & 1;
		if (imm & (1u << 13)) return 0;
		x_mi8 (7, MO_F (ldl_on), 0); sl_add (x_jcc (C_NE), ip, next, d);
		x_ld (RCX, MO_R (13));
		x_rr (0x89, RDX, RCX); x_ri (5, RDX, mj_ram_base); x_ri (7, RDX, mj_ram_size - 4u * (uint32_t) cnt); sl_add (x_jcc (C_A), ip, next, d);
		x_ld (R8, MO_F (ld_hash));
		int k = 0;
		for (int i = 0; i < 15; i++) if ((imm >> i) & 1) {
			x_ram_ld (4, 0, 4 * k); x_st (MO_R (i), RAX);
			x_rr (0x89, R9, RCX); if (k) x_ri (0, R9, 4u * (uint32_t) k);       /* (the hash chain: each load's address and value) */
			x_rr (0x31, R8, R9); b1 (0x45); b1 (0x69); b1 (0xC0); d4 (0x9E3779B1u); x_rr (0x01, R8, RAX);
			k++;
		}
		x_st (MO_F (ld_hash), R8); x_st (MO_F (ld_addr), R9); x_st (MO_F (ld_val), RAX); x_sti (MO_F (ld_n), 4);
		x_mi8 (0, MO_F (loop_loads), cnt);
		x_ri (0, RCX, 4u * (uint32_t) cnt); x_st (MO_R (13), RCX);
		*cost = 1 + cnt; return 1;
	}
	return 0;
}

/* the entry - enter (c, until, code): the registers saved and set, on into the block - and the dispatcher, where every
 * block ends: eax = the last instruction's address, the PC in memory. It does what the loop does between two
 * instructions - a short backward branch (the spin detection), attn, the slice's end, an IT block, the PC outside
 * RAM, a flush asked for, no block there yet: back to the loop (the cycles to memory); else straight into the next
 * block */
typedef uint32_t (*mj_enter_fn) (cm33_t *c, uint64_t until, uint8_t *code);
static void mj_stubs (mj_core_t *J, cm33_t *c)
{
	cm33_sys_t *s = c->sys;
	mj_write_on (1);
	E = J->code;
	J->enter = E;
	mj_prologue ();
	b1 (0x49); b1 (0xBF); q8 ((uint64_t) (uintptr_t) s->ram);                 /* mov r15, RAM */
	b1 (0x49); b1 (0xBD); q8 ((uint64_t) (uintptr_t) s->xram);                /* mov r13, PSRAM */
#ifdef _WIN32
	b1 (0x41); b1 (0xFF); b1 (0xE0);                                         /* jmp r8 */
#else
	b1 (0xFF); b1 (0xE2);                                                    /* jmp rdx */
#endif
	J->dispatch = E;
	uint8_t *back[8]; int nb = 0;
	x_ld (RCX, MO_R15);
	x_rr (0x39, RCX, RAX); uint8_t *fwd1 = x_jcc (C_AE);                       /* cmp ecx, eax: not backward */
	x_rr (0x89, RDX, RAX); x_rr (0x29, RDX, RCX); x_ri (7, RDX, 64); uint8_t *fwd2 = x_jcc (C_A);
	b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &cm33_spin_mode); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0);
	uint8_t *fwd3 = x_jcc (C_E);
	if (mj_noinl & 0x1000) back[nb++] = x_jmp ();                            /* (VT_M33NOINL 1000: back to the loop for it) */
	/* a short backward branch: the loop's spin detection, loop_round (c, ip) - here, without leaving the code */
	x_cyc_st ();
#ifdef _WIN32
	b1 (0x48); b1 (0x89); b1 (0xD9); b1 (0x89); b1 (0xC2);                   /* mov rcx, rbx; mov edx, eax */
#else
	b1 (0x48); b1 (0x89); b1 (0xDF); b1 (0x89); b1 (0xC6);                   /* mov rdi, rbx; mov esi, eax */
#endif
	x_call ((void *) loop_round);
	x_cyc_ld ();
	x_rr (0x31, RAX, RAX);                                                   /* (eax = 0: done - the loop does not run it again) */
	x_ld (RCX, MO_R15);
	patch_to (fwd1, E); patch_to (fwd2, E); patch_to (fwd3, E);
	/* attn: run_attn (c, until) here, as the loop would - 1: the slice is over (back, MJ_DONE), 2: an exception was
	 * entered (look again), 0: on */
	uint8_t *top = E;
	x_mi8 (7, MO_ATTN, 0); uint8_t *no_attn = x_jcc (C_E);
	x_cyc_st ();
#ifdef _WIN32
	b1 (0x48); b1 (0x89); b1 (0xD9); b1 (0x4C); b1 (0x89); b1 (0xE2);       /* mov rcx, rbx; mov rdx, r12 */
#else
	b1 (0x48); b1 (0x89); b1 (0xDF); b1 (0x4C); b1 (0x89); b1 (0xE6);       /* mov rdi, rbx; mov rsi, r12 */
#endif
	x_call ((void *) run_attn);
	x_cyc_ld ();
	x_ri (7, RAX, 1); uint8_t *not1 = x_jcc (C_NE);
	b1 (0xB8); d4 (MJ_DONE); back[nb++] = x_jmp ();                         /* (1: mov eax, MJ_DONE - back) */
	patch_to (not1, E);
	x_ld (RCX, MO_R15); x_ri (7, RAX, 2); b1 (0xB8); d4 (0); x_jcc_to (C_E, top);   /* (2: again; eax = 0 either way - mov, not xor: the flags stay) */
	patch_to (no_attn, E);
	b1 (0x4D); b1 (0x39); b1 (0xE6); back[nb++] = x_jcc (C_AE);               /* cmp r14, r12 */
	x_mi8 (7, MO_F (it), 0); back[nb++] = x_jcc (C_NE);
	b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &J->flush_req); b1 (0x41); b1 (0x83); b1 (0x38); b1 (0); back[nb++] = x_jcc (C_NE);
	x_rr (0x89, RDX, RCX); x_ri (5, RDX, s->ram_base); x_ri (7, RDX, s->ram_size - 4); back[nb++] = x_jcc (C_A);
	x_shi (5, RDX, 1);
	b1 (0x49); b1 (0xB8); q8 ((uint64_t) (uintptr_t) J->blk);                 /* mov r8, blocks */
	b1 (0x49); b1 (0x8B); b1 (0x14); b1 (0xD0);                               /* mov rdx, [r8 + rdx*8] */
	b1 (0x48); b1 (0x83); b1 (0xFA); b1 (0x01); back[nb++] = x_jcc (C_BE);     /* (none yet, or none can start there) */
	b1 (0xFF); b1 (0xE2);                                                    /* jmp rdx */
	for (int k = 0; k < nb; k++) patch_to (back[k], E);
	x_cyc_st ();
	mj_epilogue ();
	J->free = J->stubs_end = E;
	mj_write_on (0);
	mj_sync (J->code, E);
}

/* the block's code */
static uint8_t *mj_compile (cm33_t *c, const di_t **ins, const uint32_t *ips, int n)
{
	cm33_sys_t *s = c->sys;
	mj_ram_base = s->ram_base; mj_ram_size = s->ram_size; mj_code_pages = s->code_pages; mj_xram_size = (s->xram && !(mj_noinl & 0x400)) ? s->xram_size : 0;
	uint8_t *start = E;
	mj_nex = mj_nsl = 0;
	for (int i = 0; i < n; i++) {
		const di_t *d = ins[i]; uint32_t ip = ips[i], next = ip + d->len;
		int cost, br, sl0 = mj_nsl;
		mj_entry[i] = E;
		int inl = mj_itf[i] ? 0 : x_inline (ip, next, d, &cost, &br);
		if (inl) mj_st_inl++; else mj_st_hand++;
		if (inl) {
			if (br == 2) {                                                       /* (an unconditional branch: its exit is all - */
				if (mj_nsl > sl0) {                                              /*  a slow path, the handler, leaves too) */
					for (int k = sl0; k < mj_nsl; k++) mj_sl[k].join = E;
					ex_add (x_jmp (), ip, 0, 0);
				}
				continue;
			}
			x_cyc_add (cost);
			for (int k = sl0; k < mj_nsl; k++) mj_sl[k].join = E;                /* (the handler, when a fast path does not apply: back here) */
			if (i == n - 1) { x_sti (MO_R15, next); x_sti (MO_LAST, ip); ex_add (x_jmp (), ip, 0, 0); }
			else { b1 (0x4D); b1 (0x39); b1 (0xE6); ex_add (x_jcc (C_AE), ip, next, 1); }   /* cmp r14, r12; jae: the slice is used up */
		} else {
			mj_hfn = mj_itf[i] ? h_itw : NULL;
			x_handler (ip, next, d);
			mj_hfn = NULL;
			if (i == n - 1) ex_add (x_jmp (), ip, 0, 0);
			else { b1 (0x4D); b1 (0x39); b1 (0xE6); ex_add (x_jcc (C_AE), ip, 0, 0); }
		}
	}
	/* the slow paths: the handler for an instruction whose fast path did not apply, then back after it */
	for (int k = 0; k < mj_nsl; k++) {
		mj_slow_t *x = &mj_sl[k];
		patch_to (x->site, E);
		x_handler (x->ip, x->next, x->d);
		x_jmp_to (x->join);
	}
	/* the exits: the PC and last_pc in memory, eax = the instruction's address - on to the dispatcher (the next block,
	 * or back to the loop) */
	for (int k = 0; k < mj_nex; k++) {
		mj_exit_t *x = &mj_ex[k];
		patch_to (x->site, E);
		if (x->set) { x_sti (MO_R15, x->pc); x_sti (MO_LAST, x->ip); }
		b1 (0xB8); d4 (x->ip);                                                   /* mov eax, ip */
		x_jmp_to (mj_cur->dispatch);
	}
	(void) s;
	return start;
}
#else
/* ---------------------------------------------------------------- ARM64 (stage 2, as the x86-64 code above). x19 = the
 * core, x20 = until, x21 = the cycle count, x22 = the RAM (host address), x23 = the PSRAM. Scratch: w9 (the value),
 * w10 (the address), w11 (an offset), w12-w17. AArch64's flags follow the same carry convention as Thumb's (C = no
 * borrow after a subtraction): ADDS / SUBS give N, Z, C, V as they are */
static void w (uint32_t x) { memcpy (E, &x, 4); E += 4; }
enum { A_EQ, A_NE, A_HS, A_LO, A_MI, A_PL, A_VS, A_VC, A_HI, A_LS, A_GE, A_LT, A_GT, A_LE };
#define MO_R(n) ((uint32_t) (offsetof (cm33_t, r) + 4 * (n)))
#define MO_F(f) ((uint32_t) offsetof (cm33_t, f))
static void a_movw (int rd, uint32_t v)
{
	if (!(v & 0xffff) && v) { w (0x52A00000u | ((v >> 16) << 5) | (uint32_t) rd); return; }
	w (0x52800000u | ((v & 0xffff) << 5) | (uint32_t) rd);
	if (v >> 16) w (0x72A00000u | ((v >> 16) << 5) | (uint32_t) rd);
}
static void a_movx (int rd, uint64_t v)
{
	w (0xD2800000u | ((uint32_t) (v & 0xffff) << 5) | (uint32_t) rd);
	for (int h = 1; h < 4; h++) { uint32_t p = (uint32_t) (v >> (16 * h)) & 0xffff; if (p) w (0xF2800000u | ((uint32_t) h << 21) | (p << 5) | (uint32_t) rd); }
}
static void a_ldw (int rt, uint32_t off) { w (0xB9400000u | ((off >> 2) << 10) | (19u << 5) | (uint32_t) rt); }   /* ldr wt, [x19, #off] */
static void a_stw (int rt, uint32_t off) { w (0xB9000000u | ((off >> 2) << 10) | (19u << 5) | (uint32_t) rt); }
static void a_ldx (int rt, uint32_t off) { w (0xF9400000u | ((off >> 3) << 10) | (19u << 5) | (uint32_t) rt); }
static void a_stx (int rt, uint32_t off) { w (0xF9000000u | ((off >> 3) << 10) | (19u << 5) | (uint32_t) rt); }
static void a_sti (uint32_t off, uint32_t v) { if (!v) { a_stw (31, off); return; } a_movw (15, v); a_stw (15, off); }   /* (w15) */
#define A_ADD 0x0B000000u
#define A_ADDS 0x2B000000u
#define A_SUB 0x4B000000u
#define A_SUBS 0x6B000000u
#define A_AND 0x0A000000u
#define A_ANDS 0x6A000000u
#define A_BIC 0x0A200000u
#define A_ORR 0x2A000000u
#define A_ORN 0x2A200000u
#define A_EOR 0x4A000000u
static void a_rrr (uint32_t op, int rd, int rn, int rm) { w (op | ((uint32_t) rm << 16) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_mov (int rd, int rm) { if (rd != rm) a_rrr (A_ORR, rd, 31, rm); }
static void a_ri (uint32_t op, int rd, int rn, uint32_t v) { a_movw (16, v); a_rrr (op, rd, rn, 16); }   /* op wd, wn, #v (through w16) */
static void a_cmpr (int rn, int rm) { a_rrr (A_SUBS, 31, rn, rm); }
static void a_cmpi (int rn, uint32_t v) { if (v < 4096) w (0x7100001Fu | (v << 10) | ((uint32_t) rn << 5)); else { a_movw (16, v); a_cmpr (rn, 16); } }
static void a_cset (int rd, int cond) { w (0x1A9F07E0u | ((uint32_t) (cond ^ 1) << 12) | (uint32_t) rd); }
static void a_eor1 (int rd, int rn) { w (0x52000000u | ((uint32_t) rn << 5) | (uint32_t) rd); }               /* eor wd, wn, #1 */
static void a_ubfx (int rd, int rn, int lsb, int width) { w (0x53000000u | ((uint32_t) lsb << 16) | ((uint32_t) (lsb + width - 1) << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_lsl (int rd, int rn, int k) { w (0x53000000u | ((uint32_t) ((32 - k) & 31) << 16) | ((uint32_t) (31 - k) << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_lsr (int rd, int rn, int k) { w (0x53000000u | ((uint32_t) k << 16) | (31u << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_asr (int rd, int rn, int k) { w (0x13000000u | ((uint32_t) k << 16) | (31u << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_ext (int op, int rd, int rn)                                    /* 0 sxth, 1 sxtb, 2 uxth, 3 uxtb */
{ static const uint32_t e[4] = { 0x13003C00u, 0x13001C00u, 0x53003C00u, 0x53001C00u }; w (e[op] | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_mul (int rd, int rn, int rm) { w (0x1B007C00u | ((uint32_t) rm << 16) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void a_cyc_add (int k) { w (0x91000000u | ((uint32_t) k << 10) | (21u << 5) | 21u); }                      /* add x21, x21, #k */
static void a_cyc_st (void) { a_stx (21, MO_CYC); }
static void a_cyc_ld (void) { a_ldx (21, MO_CYC); }
static void a_call (void *fn) { a_movx (16, (uint64_t) (uintptr_t) fn); w (0xD63F0200u); }                         /* blr x16 */
/* branches to patch: type 0 b.cond / cbz / cbnz (imm19), 1 b (imm26) */
static uint8_t *a_bcc (int cond) { w (0x54000000u | (uint32_t) cond); return E - 4; }
static uint8_t *a_cbz (int rt) { w (0x34000000u | (uint32_t) rt); return E - 4; }
static uint8_t *a_cbnz (int rt) { w (0x35000000u | (uint32_t) rt); return E - 4; }
static uint8_t *a_b (void) { w (0x14000000u); return E - 4; }
static void patch_to (uint8_t *site, uint8_t *t)
{
	uint32_t ins; memcpy (&ins, site, 4);
	int32_t rel = (int32_t) (t - site) >> 2;
	if ((ins & 0xFC000000u) == 0x14000000u) ins = 0x14000000u | ((uint32_t) rel & 0x3ffffff);
	else ins = (ins & ~(0x7ffffu << 5)) | (((uint32_t) rel & 0x7ffff) << 5);
	memcpy (site, &ins, 4);
}
static void a_b_to (uint8_t *t) { uint8_t *s = a_b (); patch_to (s, t); }
/* the RAM / PSRAM at x22 / x23 + w11 */
static void a_ram_ld (int base, int n) { w ((n == 4 ? 0xB8604800u : n == 2 ? 0x78604800u : 0x38604800u) | (11u << 16) | ((uint32_t) base << 5) | 9u); }
static void a_ram_st (int base, int n) { w ((n == 4 ? 0xB8204800u : n == 2 ? 0x78204800u : 0x38204800u) | (11u << 16) | ((uint32_t) base << 5) | 9u); }
static void a_addr13 (void) { w (0x8B204000u | (11u << 16) | (22u << 5) | 13u); }                                 /* add x13, x22, w11, uxtw */
static void a_ld13 (int rt, int k) { w (0xB9400000u | ((uint32_t) k << 10) | (13u << 5) | (uint32_t) rt); }       /* ldr wt, [x13, #4k] */
static void a_st13 (int rt, int k) { w (0xB9000000u | ((uint32_t) k << 10) | (13u << 5) | (uint32_t) rt); }

typedef struct { uint8_t *site; uint32_t ip, pc; int set; } mj_exit_t;
typedef struct { uint8_t *site, *join; uint32_t ip, next; const di_t *d; } mj_slow_t;
static mj_exit_t mj_ex[MJ_MAX * 24]; static int mj_nex;
static mj_slow_t mj_sl[MJ_MAX * 6]; static int mj_nsl;
static uint32_t mj_ram_base, mj_ram_size, mj_xram_size; static uint8_t *mj_code_pages;
static void ex_add (uint8_t *site, uint32_t ip, uint32_t pc, int set) { mj_exit_t *x = &mj_ex[mj_nex++]; x->site = site; x->ip = ip; x->pc = pc; x->set = set; }
static void sl_add (uint8_t *site, uint32_t ip, uint32_t next, const di_t *d) { mj_slow_t *x = &mj_sl[mj_nsl++]; x->site = site; x->ip = ip; x->next = next; x->d = d; x->join = NULL; }

static void mj_epilogue (void)
{
	w (0xA94363F7u);                        /* ldp x23, x24, [sp, #48] */
	w (0xA9425BF5u);                        /* ldp x21, x22, [sp, #32] */
	w (0xA94153F3u);                        /* ldp x19, x20, [sp, #16] */
	w (0xA8C57BFDu);                        /* ldp x29, x30, [sp], #80 */
	w (0xD65F03C0u);                        /* ret */
}
static void a_handler (uint32_t ip, uint32_t next, const di_t *d)
{
	a_sti (MO_R15, next); a_sti (MO_LAST, ip);
	w (0x91000400u | (21u << 5) | 9u); a_stx (9, MO_CYC);                   /* add x9, x21, #1; str x9, [x19, #cycles] */
	w (0xAA1303E0u);                                                        /* mov x0, x19 */
	a_movx (1, (uint64_t) (uintptr_t) d); a_movw (2, ip);
	a_call ((void *) (mj_hfn ? mj_hfn : d->fn));
	a_cyc_ld ();
	a_ldw (9, MO_R15); a_movw (10, next); a_cmpr (9, 10); ex_add (a_bcc (A_NE), ip, 0, 0);
	a_ldw (9, MO_ATTN); ex_add (a_cbnz (9), ip, 0, 0);
}
static void a_flags_nzcv (void) { a_cset (15, A_MI); a_stw (15, MO_F (n)); a_cset (15, A_EQ); a_stw (15, MO_F (z)); a_cset (15, A_HS); a_stw (15, MO_F (c)); a_cset (15, A_VS); a_stw (15, MO_F (v)); }
static void a_flags_nz (int r) { w (0x6A00001Fu | ((uint32_t) r << 16) | ((uint32_t) r << 5)); a_cset (15, A_MI); a_stw (15, MO_F (n)); a_cset (15, A_EQ); a_stw (15, MO_F (z)); }   /* tst wr, wr */
static void a_cond (int cond)                                                /* w9 = 1 when cond passes */
{
	switch (cond >> 1) {
	case 0: a_ldw (9, MO_F (z)); break;
	case 1: a_ldw (9, MO_F (c)); break;
	case 2: a_ldw (9, MO_F (n)); break;
	case 3: a_ldw (9, MO_F (v)); break;
	case 4: a_ldw (9, MO_F (z)); a_eor1 (9, 9); a_ldw (12, MO_F (c)); a_rrr (A_AND, 9, 9, 12); break;
	case 5: a_ldw (9, MO_F (n)); a_ldw (12, MO_F (v)); a_rrr (A_EOR, 9, 9, 12); a_eor1 (9, 9); break;
	default: a_ldw (9, MO_F (n)); a_ldw (12, MO_F (v)); a_rrr (A_EOR, 9, 9, 12); a_ldw (12, MO_F (z)); a_rrr (A_ORR, 9, 9, 12); a_eor1 (9, 9); break;
	}
	if (cond & 1) a_eor1 (9, 9);
}
static int bcc_cond (hfn f, const di_t *d)
{
	static const hfn bcc[14] = { h_beq, h_bne, h_bcs, h_bcc, h_bmi, h_bpl, h_bvs, h_bvc, h_bhi, h_bls, h_bge, h_blt, h_bgt, h_ble };
	for (int k = 0; k < 14; k++) if (f == bcc[k]) return k;
	if (f == h_bcond && d->op < 14) return d->op;
	return -1;
}
/* RAM range: w11 = w10 - base; past it (> size - n): the jump returned (b.hi) */
static uint8_t *a_ram_range (uint32_t n) { a_movw (16, mj_ram_base); a_rrr (A_SUB, 11, 10, 16); a_cmpi (11, mj_ram_size - n); return a_bcc (A_HI); }
static void a_xram_check (uint32_t ip, uint32_t next, const di_t *d, int n)
{
	a_lsr (11, 10, 24); a_ri (A_AND, 11, 11, 0xF1); a_cmpi (11, 0x11); sl_add (a_bcc (A_NE), ip, next, d);
	a_ri (A_AND, 11, 10, 0xffffff); a_cmpi (11, mj_xram_size - (uint32_t) n); sl_add (a_bcc (A_HI), ip, next, d);
}
static void a_hash (int addr_reg)                                            /* w12 = (w12 ^ addr) * K + w9 */
{ a_rrr (A_EOR, 12, 12, addr_reg); a_movw (16, 0x9E3779B1u); a_mul (12, 12, 16); a_rrr (A_ADD, 12, 12, 9); }
static void a_load (uint32_t ip, uint32_t next, const di_t *d, int n, int sx)
{
	a_ldw (9, MO_F (ldl_on)); sl_add (a_cbnz (9), ip, next, d);
	if (mj_xram_size >= 4) {
		uint8_t *px = a_ram_range ((uint32_t) n);
		a_ram_ld (22, n); uint8_t *done = a_b ();
		patch_to (px, E); a_xram_check (ip, next, d, n); a_ram_ld (23, n);
		patch_to (done, E);
	} else { sl_add (a_ram_range ((uint32_t) n), ip, next, d); a_ram_ld (22, n); }
	a_ldw (12, MO_F (loop_loads)); w (0x11000400u | (12u << 5) | 12u); a_stw (12, MO_F (loop_loads));   /* (add w12, w12, #1) */
	a_stw (10, MO_F (ld_addr)); a_stw (9, MO_F (ld_val)); a_sti (MO_F (ld_n), (uint32_t) n);
	a_ldw (12, MO_F (ld_hash)); a_hash (10); a_stw (12, MO_F (ld_hash));
	if (sx) a_ext (n == 1 ? 1 : 0, 9, 9);
}
/* the store checks as x_store: an exclusive monitor, the first two bytes, code in the page(s) - else the handler */
static void a_code_page_check (uint32_t ip, uint32_t next, const di_t *d, uint32_t last_off)
{
	a_movx (12, (uint64_t) (uintptr_t) mj_code_pages);
	a_ri (A_SUB, 13, 11, 2); a_lsr (13, 13, 12); w (0x38604800u | (13u << 16) | (12u << 5) | 13u); sl_add (a_cbnz (13), ip, next, d);   /* ldrb w13, [x12, w13, uxtw] */
	if (last_off) a_ri (A_ADD, 13, 11, last_off); else a_mov (13, 11);
	a_lsr (13, 13, 12); w (0x38604800u | (13u << 16) | (12u << 5) | 13u); sl_add (a_cbnz (13), ip, next, d);
}
static void a_excl_check (uint32_t ip, uint32_t next, const di_t *d)
{ a_movx (12, (uint64_t) (uintptr_t) &excl_any); w (0xB9400000u | (12u << 5) | 13u); sl_add (a_cbnz (13), ip, next, d); }
static void a_inc_stores (int k) { a_ldw (12, MO_F (loop_stores)); w (0x11000000u | ((uint32_t) k << 10) | (12u << 5) | 12u); a_stw (12, MO_F (loop_stores)); }
static void a_store (uint32_t ip, uint32_t next, const di_t *d, int n)          /* w10 = the address, w9 = the value */
{
	a_excl_check (ip, next, d);
	uint8_t *pr = a_ram_range ((uint32_t) n);
	a_cmpi (11, 2); sl_add (a_bcc (A_LO), ip, next, d);
	a_code_page_check (ip, next, d, 0);
	a_ram_st (22, n);
	if (mj_xram_size >= 4) {
		uint8_t *done = a_b ();
		patch_to (pr, E); a_xram_check (ip, next, d, n); a_ram_st (23, n);
		patch_to (done, E);
	} else sl_add (pr, ip, next, d);
	a_inc_stores (1);
}
static int a_inline (uint32_t ip, uint32_t next, const di_t *d, int *cost, int *branch)
{
	hfn f = d->fn; int a = d->a, b = d->b, e = d->e; uint32_t imm = d->imm;
	*cost = 1; *branch = 0;
	if (mj_mode == 1) return 0;
	int cond = bcc_cond (f, d);
	if ((cond >= 0 || f == h_b || f == h_bl || f == h_cbz) && !(mj_noinl & 1)) {
		if (imm == ip) return 0;
		uint8_t *skip = NULL;
		if (cond >= 0) { a_cond (cond); skip = a_cbz (9); }
		else if (f == h_cbz) { a_ldw (9, MO_R (a)); skip = d->op ? a_cbz (9) : a_cbnz (9); }   /* (taken when (r != 0) == op) */
		if (f == h_bl) a_sti (MO_R (14), (ip + 4) | 1);
		a_cyc_add (2); a_sti (MO_R15, imm); a_sti (MO_LAST, ip);
		ex_add (a_b (), ip, 0, 0);
		if (skip) patch_to (skip, E);
		*branch = skip ? 1 : 2;
		return 1;
	}
	if ((mj_noinl & 2) && f != h_ldw_imm && f != h_ldh_imm && f != h_ldb_imm && f != h_stw_imm && f != h_sth_imm && f != h_stb_imm && f != h_ld_imm && f != h_st_imm && f != h_ldst_idx && f != h_ldst_reg && f != h_ld_lit && f != h_push && f != h_pop && f != h_pop32 && f != h_ldmia && f != h_ldst_rs && f != h_ldrd) return 0;
	if (f == h_movi) { if (a == 15) return 0; a_sti (MO_R (a), imm); return 1; }
	if (f == h_movs8) { a_sti (MO_R (a), imm); a_sti (MO_F (n), imm >> 31); a_sti (MO_F (z), imm == 0); return 1; }
	if (f == h_cmp8 || f == h_adds8 || f == h_subs8) {
		a_ldw (9, MO_R (a)); a_ri (f == h_adds8 ? A_ADDS : A_SUBS, 9, 9, imm); a_flags_nzcv ();
		if (f != h_cmp8) a_stw (9, MO_R (a));
		return 1;
	}
	if (f == h_addsub3) {
		if (a == 15 || b == 15 || (!(d->op & 2) && e == 15)) return 0;
		a_ldw (9, MO_R (b));
		if (d->op & 2) a_ri ((d->op & 1) ? A_SUBS : A_ADDS, 9, 9, imm); else { a_ldw (12, MO_R (e)); a_rrr ((d->op & 1) ? A_SUBS : A_ADDS, 9, 9, 12); }
		a_flags_nzcv (); a_stw (9, MO_R (a)); return 1;
	}
	if (f == h_hi_mov || f == h_hi_add) {
		if (a == 15 || b == 15) return 0;
		a_ldw (9, MO_R (b)); if (f == h_hi_add) { a_ldw (12, MO_R (a)); a_rrr (A_ADD, 9, 9, 12); } a_stw (9, MO_R (a)); return 1;
	}
	if (f == h_hi_cmp) { if (a == 15 || b == 15) return 0; a_ldw (9, MO_R (a)); a_ldw (12, MO_R (b)); a_rrr (A_SUBS, 9, 9, 12); a_flags_nzcv (); return 1; }
	if (f == h_addspi) { if (a == 15) return 0; a_ldw (9, MO_R (13)); a_ri (A_ADD, 9, 9, imm); a_stw (9, MO_R (a)); return 1; }
	if (f == h_spadj) { a_ldw (9, MO_R (13)); a_ri (A_ADD, 9, 9, imm); a_stw (9, MO_R (13)); return 1; }
	if (f == h_dpi_addn || f == h_dpi_subn || f == h_addw) {
		if (a == 15 || b == 15) return 0;
		a_ldw (9, MO_R (b)); a_ri (f == h_dpi_subn ? A_SUB : A_ADD, 9, 9, imm); a_stw (9, MO_R (a)); return 1;
	}
	if (f == h_dpi_cmp) { if (b == 15) return 0; a_ldw (9, MO_R (b)); a_ri (A_SUBS, 9, 9, imm); a_flags_nzcv (); return 1; }
	if (f == h_movt) { if (a == 15) return 0; a_ldw (9, MO_R (a)); a_ri (A_AND, 9, 9, 0xffff); a_ri (A_ORR, 9, 9, imm << 16); a_stw (9, MO_R (a)); return 1; }
	if (f == h_dpi_and || f == h_dpi_bic || f == h_dpi_orr || f == h_dpi_orn || f == h_dpi_eor || f == h_dpi_mov || f == h_dpi_mvn) {
		if ((!d->f3 && a == 15) || b == 15) return 0;
		if (f == h_dpi_mov) a_movw (9, imm);
		else if (f == h_dpi_mvn) a_movw (9, ~imm);
		else {
			a_ldw (9, MO_R (b));
			a_ri (f == h_dpi_and ? A_AND : f == h_dpi_bic ? A_BIC : f == h_dpi_orr ? A_ORR : f == h_dpi_orn ? A_ORN : A_EOR, 9, 9, imm);
		}
		if (!d->f3) a_stw (9, MO_R (a));
		if (d->f1) { a_flags_nz (9); if (d->f2) a_sti (MO_F (c), (uint32_t) (d->f2 - 1)); }
		return 1;
	}
	if (f == h_dpi_add || f == h_dpi_sub || f == h_dpi_rsb) {
		if ((!d->f3 && a == 15) || b == 15) return 0;
		uint32_t op = d->f1 ? (f == h_dpi_add ? A_ADDS : A_SUBS) : (f == h_dpi_add ? A_ADD : A_SUB);
		if (f == h_dpi_rsb) { a_movw (9, imm); a_ldw (12, MO_R (b)); a_rrr (op, 9, 9, 12); }
		else { a_ldw (9, MO_R (b)); a_ri (op, 9, 9, imm); }
		if (d->f1) a_flags_nzcv ();
		if (!d->f3) a_stw (9, MO_R (a));
		return 1;
	}
	if (f == h_dp16_and || f == h_dp16_eor || f == h_dp16_orr || f == h_dp16_bic || f == h_dp16_mvn || f == h_dp16_tst || f == h_dp16_mul) {
		if (a == 15 || b == 15) return 0;
		a_ldw (12, MO_R (b));
		if (f == h_dp16_mvn) a_rrr (A_ORN, 9, 31, 12);
		else {
			a_ldw (9, MO_R (a));
			if (f == h_dp16_mul) a_mul (9, 9, 12);
			else a_rrr (f == h_dp16_and || f == h_dp16_tst ? A_AND : f == h_dp16_eor ? A_EOR : f == h_dp16_orr ? A_ORR : A_BIC, 9, 9, 12);
		}
		if (f != h_dp16_tst) a_stw (9, MO_R (a));
		a_flags_nz (9); return 1;
	}
	if (f == h_dp16_cmp || f == h_dp16_cmn || f == h_dp16_rsb) {
		if (a == 15 || b == 15) return 0;
		a_ldw (12, MO_R (b));
		if (f == h_dp16_rsb) { a_rrr (A_SUBS, 9, 31, 12); a_flags_nzcv (); a_stw (9, MO_R (a)); }
		else { a_ldw (9, MO_R (a)); a_rrr (f == h_dp16_cmp ? A_SUBS : A_ADDS, 9, 9, 12); a_flags_nzcv (); }
		return 1;
	}
	if (f == h_shift_imm) {
		if (a == 15 || b == 15 || d->op > 2 || imm >= 32) return 0;
		a_ldw (12, MO_R (b));
		if (imm) {
			int k = (int) imm;
			a_ubfx (15, 12, d->op == 0 ? 32 - k : k - 1, 1); a_stw (15, MO_F (c));       /* (C: the last bit out) */
			if (d->op == 0) a_lsl (9, 12, k); else if (d->op == 1) a_lsr (9, 12, k); else a_asr (9, 12, k);
		} else a_mov (9, 12);
		a_stw (9, MO_R (a)); a_flags_nz (9); return 1;
	}
	if (f == h_ext16) {
		if (a == 15 || b == 15) return 0;
		a_ldw (12, MO_R (b)); a_ext (d->op & 3, 9, 12); a_stw (9, MO_R (a)); return 1;
	}
	if (f == h_nop) return 1;
	if (f == h_it && mj_it_on) { a_sti (MO_F (it), imm); return 1; }        /* (2026-10-30: IT, as h_it - its instructions follow in the block, h_itw) */
	/* loads and stores */
	int n = 0, ld = 0, sx = 0, wb = 0, base = -1; uint32_t off = 0;
	if (f == h_ldw_imm || f == h_ldh_imm || f == h_ldb_imm) { n = f == h_ldw_imm ? 4 : f == h_ldh_imm ? 2 : 1; ld = 1; base = b; off = imm; }
	else if (f == h_stw_imm || f == h_sth_imm || f == h_stb_imm) { n = f == h_stw_imm ? 4 : f == h_sth_imm ? 2 : 1; base = b; off = imm; }
	else if (f == h_ld_imm) { n = e; ld = 1; sx = d->f1; base = b; off = imm; }
	else if (f == h_st_imm) { n = e; base = b; off = imm; }
	else if (f == h_ldst_idx) { n = e; ld = d->f2; sx = d->f1; base = b; off = imm; wb = d->f3; }
	if (n && (n == 1 || n == 2 || n == 4) && !(mj_noinl & 4)) {
		if (a == 15 || base == 15) return 0;
		a_ldw (10, MO_R (base));
		if (f == h_ldst_idx) { if (d->f3 & 1) a_ri (A_ADD, 10, 10, off); }
		else if (off) a_ri (A_ADD, 10, 10, off);
		if (ld) { a_load (ip, next, d, n, sx); if (wb & 2) { a_ldw (13, MO_R (base)); a_ri (A_ADD, 13, 13, off); a_stw (13, MO_R (base)); } a_stw (9, MO_R (a)); }
		else { a_ldw (9, MO_R (a)); a_store (ip, next, d, n); if (wb & 2) { a_ldw (13, MO_R (base)); a_ri (A_ADD, 13, 13, off); a_stw (13, MO_R (base)); } }
		*cost = 2; return 1;
	}
	if (f == h_ldst_reg && !(mj_noinl & 4)) {
		if (a == 15 || b == 15 || e == 15) return 0;
		static const int sz[8] = { 4, 2, 1, 1, 4, 2, 1, 2 }, sxs[8] = { 0, 0, 0, 1, 0, 0, 0, 1 };
		int op = d->op & 7; n = sz[op];
		a_ldw (10, MO_R (b)); a_ldw (12, MO_R (e)); a_rrr (A_ADD, 10, 10, 12);
		if (op >= 3) { a_load (ip, next, d, n, sxs[op]); a_stw (9, MO_R (a)); }
		else { a_ldw (9, MO_R (a)); a_store (ip, next, d, n); }
		*cost = 2; return 1;
	}
	if (f == h_ld_lit && !(mj_noinl & 4)) {
		if (a == 15 || (d->e != 1 && d->e != 2 && d->e != 4)) return 0;
		a_movw (10, imm); a_load (ip, next, d, d->e, d->f1); a_stw (9, MO_R (a)); *cost = 2; return 1;
	}
	if (f == h_push && !(imm & 0x8000) && e && !(mj_noinl & 8) && (!d->op || d->f1 != 15)) {
		/* PUSH (sp: written first) / STMDB rn! (rn written after the stores) */
		int rn = d->op ? d->f1 : 13;
		a_ldw (10, MO_R (rn)); a_ri (A_SUB, 10, 10, 4u * (uint32_t) e);
		sl_add (a_ram_range (4u * (uint32_t) e), ip, next, d);
		a_cmpi (11, 2); sl_add (a_bcc (A_LO), ip, next, d);
		a_excl_check (ip, next, d);
		a_code_page_check (ip, next, d, 4u * (uint32_t) e - 4);
		if (!d->op) a_stw (10, MO_R (13));
		a_addr13 ();
		for (int i = 0, k = 0; i < 15; i++) if ((imm >> i) & 1) { a_ldw (9, MO_R (i)); a_st13 (9, k); k++; }
		if (d->op) a_stw (10, MO_R (rn));
		a_inc_stores (e);
		*cost = 1 + e; return 1;
	}
	if (((f == h_pop && (imm & 0x8000)) || (f == h_pop32 && (imm & 0x8000))) && !(mj_noinl & 0x20)) {
		int cnt = 0; for (int i = 0; i < 15; i++) cnt += (imm >> i) & 1;
		if (imm & (1u << 13)) return 0;
		a_ldw (9, MO_F (ldl_on)); sl_add (a_cbnz (9), ip, next, d);
		a_ldw (10, MO_R (13));
		sl_add (a_ram_range (4u * (uint32_t) (cnt + 1)), ip, next, d);
		a_addr13 ();
		a_ld13 (14, cnt);                                                    /* (w14 = the return address: a plain Thumb one?) */
		a_ri (A_AND, 15, 14, 1); sl_add (a_cbz (15), ip, next, d);              /* (bit 0 clear: the handler) */
		a_lsr (15, 14, 24); a_cmpi (15, 0xFE); sl_add (a_bcc (A_HS), ip, next, d);
		a_ldw (12, MO_F (ld_hash));
		for (int i = 0, k = 0; i < 16; i++) if ((imm >> i) & 1) {
			a_ld13 (9, k);
			if (i < 15) a_stw (9, MO_R (i));
			a_ri (A_ADD, 17, 10, 4u * (uint32_t) k); a_hash (17);
			k++;
		}
		a_stw (12, MO_F (ld_hash)); a_stw (17, MO_F (ld_addr)); a_stw (9, MO_F (ld_val)); a_sti (MO_F (ld_n), 4);
		a_ldw (12, MO_F (loop_loads)); w (0x11000000u | ((uint32_t) (cnt + 1) << 10) | (12u << 5) | 12u); a_stw (12, MO_F (loop_loads));
		a_ri (A_ADD, 10, 10, 4u * (uint32_t) (cnt + 1)); a_stw (10, MO_R (13));
		a_ri (A_AND, 14, 14, ~1u); a_stw (14, MO_R15); a_sti (MO_LAST, ip);
		a_cyc_add (f == h_pop ? 1 + cnt + 2 : 1 + e);
		ex_add (a_b (), ip, 0, 0);
		*branch = 2; return 1;
	}
	if (f == h_pop && !(imm & 0x8000) && imm && !(mj_noinl & 0x10)) {
		int cnt = 0; for (int i = 0; i < 15; i++) cnt += (imm >> i) & 1;
		if (imm & (1u << 13)) return 0;
		a_ldw (9, MO_F (ldl_on)); sl_add (a_cbnz (9), ip, next, d);
		a_ldw (10, MO_R (13));
		sl_add (a_ram_range (4u * (uint32_t) cnt), ip, next, d);
		a_addr13 ();
		a_ldw (12, MO_F (ld_hash));
		for (int i = 0, k = 0; i < 15; i++) if ((imm >> i) & 1) {
			a_ld13 (9, k); a_stw (9, MO_R (i));
			a_ri (A_ADD, 17, 10, 4u * (uint32_t) k); a_hash (17);
			k++;
		}
		a_stw (12, MO_F (ld_hash)); a_stw (17, MO_F (ld_addr)); a_stw (9, MO_F (ld_val)); a_sti (MO_F (ld_n), 4);
		a_ldw (12, MO_F (loop_loads)); w (0x11000000u | ((uint32_t) cnt << 10) | (12u << 5) | 12u); a_stw (12, MO_F (loop_loads));
		a_ri (A_ADD, 10, 10, 4u * (uint32_t) cnt); a_stw (10, MO_R (13));
		*cost = 1 + cnt; return 1;
	}
	if (f == h_ldmia && a != 15 && imm && !(imm & 0x8000) && !(mj_noinl & 0x40)) {
		int cnt = 0; for (int i = 0; i < 15; i++) cnt += (imm >> i) & 1;
		int rn = a;
		if (d->f1) { a_ldw (9, MO_F (ldl_on)); sl_add (a_cbnz (9), ip, next, d); }
		a_ldw (10, MO_R (rn));
		sl_add (a_ram_range (4u * (uint32_t) cnt), ip, next, d);
		if (d->f1) {
			a_addr13 ();
			a_ldw (12, MO_F (ld_hash));
			for (int i = 0, k = 0; i < 15; i++) if ((imm >> i) & 1) {
				a_ld13 (9, k); a_stw (9, MO_R (i));
				a_ri (A_ADD, 17, 10, 4u * (uint32_t) k); a_hash (17);
				k++;
			}
			a_stw (12, MO_F (ld_hash)); a_stw (17, MO_F (ld_addr)); a_stw (9, MO_F (ld_val)); a_sti (MO_F (ld_n), 4);
			a_ldw (12, MO_F (loop_loads)); w (0x11000000u | ((uint32_t) cnt << 10) | (12u << 5) | 12u); a_stw (12, MO_F (loop_loads));
			if (d->f2 && !((imm >> rn) & 1)) { a_ri (A_ADD, 10, 10, 4u * (uint32_t) cnt); a_stw (10, MO_R (rn)); }
		} else {
			a_cmpi (11, 2); sl_add (a_bcc (A_LO), ip, next, d);
			a_excl_check (ip, next, d);
			a_code_page_check (ip, next, d, 4u * (uint32_t) cnt - 4);
			a_addr13 ();
			for (int i = 0, k = 0; i < 15; i++) if ((imm >> i) & 1) { a_ldw (9, MO_R (i)); a_st13 (9, k); k++; }
			a_inc_stores (cnt);
			if (d->f2) { a_ri (A_ADD, 10, 10, 4u * (uint32_t) cnt); a_stw (10, MO_R (rn)); }
		}
		*cost = 1 + e; return 1;
	}
	if (f == h_ldrd && !(mj_noinl & 0x100)) {
		/* (2026-10-30, as the x86-64 code) LDRD / STRD: two words, as two loads (each with its bookkeeping) or two
		 * stores, both checked before either - a slow second one after a fast first would do the first twice */
		if (a == 15 || b == 15 || e == 15 || (d->f2 && a == e) || ((d->f3 & 2) && (a == b || e == b))) return 0;
		a_ldw (10, MO_R (b)); if (d->f3 & 1) a_ri (A_ADD, 10, 10, imm);
		if (d->f2) {
			a_ldw (9, MO_F (ldl_on)); sl_add (a_cbnz (9), ip, next, d);
			sl_add (a_ram_range (8), ip, next, d);
			a_load (ip, next, d, 4, 0); a_mov (17, 9);
			a_ri (A_ADD, 10, 10, 4); a_load (ip, next, d, 4, 0);
			a_stw (17, MO_R (a)); a_stw (9, MO_R (e));
		} else {
			sl_add (a_ram_range (8), ip, next, d);
			a_cmpi (11, 2); sl_add (a_bcc (A_LO), ip, next, d);
			a_excl_check (ip, next, d);
			a_code_page_check (ip, next, d, 4);
			a_addr13 (); a_ldw (9, MO_R (a)); a_st13 (9, 0); a_ldw (9, MO_R (e)); a_st13 (9, 1);
			a_inc_stores (2);
		}
		if (d->f3 & 2) { a_ldw (13, MO_R (b)); a_ri (A_ADD, 13, 13, imm); a_stw (13, MO_R (b)); }
		*cost = 3; return 1;
	}
	if ((f == h_dpr_add || f == h_dpr_sub || f == h_dpr_rsb || f == h_dpr_and || f == h_dpr_orr || f == h_dpr_eor || f == h_dpr_bic || f == h_dpr_mov || f == h_dpr_mvn)
	    && !d->f1 && !d->f3 && d->f2 <= 2 && imm < 32 && !(mj_noinl & 0x200)) {
		/* (2026-10-30, as the x86-64 code) data processing with a shifted register, no flags: rd = rn op (rm shift #k) */
		if (a == 15 || b == 15 || e == 15) return 0;
		a_ldw (12, MO_R (e));
		if (imm) { if (d->f2 == 0) a_lsl (12, 12, (int) imm); else if (d->f2 == 1) a_lsr (12, 12, (int) imm); else a_asr (12, 12, (int) imm); }
		if (f == h_dpr_mov) a_mov (9, 12);
		else if (f == h_dpr_mvn) a_rrr (A_ORN, 9, 31, 12);
		else if (f == h_dpr_rsb) { a_ldw (9, MO_R (b)); a_rrr (A_SUB, 9, 12, 9); }
		else { a_ldw (9, MO_R (b)); a_rrr (f == h_dpr_add ? A_ADD : f == h_dpr_sub ? A_SUB : f == h_dpr_and ? A_AND : f == h_dpr_orr ? A_ORR : f == h_dpr_eor ? A_EOR : A_BIC, 9, 9, 12); }
		a_stw (9, MO_R (a)); return 1;
	}
	if ((f == h_ubfx || f == h_bfi || f == h_mul32) && !(mj_noinl & 0x2000)) {
		/* (2026-10-30) bit fields and 32-bit multiplies, as their handlers: UBFX / SBFX, BFI / BFC, MUL / MLA / MLS */
		if (f == h_ubfx) {
			if (a == 15 || b == 15 || imm >= 32 || e < 1 || e > 32 || imm + e > 32) return 0;
			a_ldw (12, MO_R (b));
			if (d->op) w (0x13000000u | (imm << 16) | ((imm + e - 1) << 10) | (12u << 5) | 9u);   /* sbfx w9, w12, #imm, #e */
			else a_ubfx (9, 12, (int) imm, e);
			a_stw (9, MO_R (a)); return 1;
		}
		if (f == h_bfi) {
			if (a == 15 || (!d->op && b == 15) || e > 31) return 0;
			if (d->op) a_movw (12, 0); else { a_ldw (12, MO_R (b)); if (e) a_lsl (12, 12, e); a_ri (A_AND, 12, 12, imm); }
			a_ldw (9, MO_R (a)); a_ri (A_AND, 9, 9, ~imm); a_rrr (A_ORR, 9, 9, 12); a_stw (9, MO_R (a)); return 1;
		}
		if (a == 15 || b == 15 || e == 15 || d->op > 2 || (d->op && d->f1 == 15)) return 0;
		a_ldw (12, MO_R (b)); a_ldw (13, MO_R (e));
		if (d->op) a_ldw (14, MO_R (d->f1));
		w ((d->op == 2 ? 0x1B008000u : 0x1B000000u) | (13u << 16) | ((d->op ? 14u : 31u) << 10) | (12u << 5) | 9u);   /* madd / msub w9, w12, w13, w14 (wzr) */
		a_stw (9, MO_R (a)); return 1;
	}
	if (f == h_ldst_rs && (e == 1 || e == 2 || e == 4) && imm < 4 && !(mj_noinl & 0x80)) {
		if (a == 15 || b == 15 || d->f3 == 15) return 0;
		a_ldw (10, MO_R (d->f3)); if (imm) a_lsl (10, 10, (int) imm); a_ldw (12, MO_R (b)); a_rrr (A_ADD, 10, 10, 12);
		if (d->f2) { a_load (ip, next, d, e, d->f1); a_stw (9, MO_R (a)); }
		else { a_ldw (9, MO_R (a)); a_store (ip, next, d, e); }
		*cost = 2; return 1;
	}
	return 0;
}

/* the entry (as x86-64's) and the dispatcher */
typedef uint32_t (*mj_enter_fn) (cm33_t *c, uint64_t until, uint8_t *code);
static void mj_stubs (mj_core_t *J, cm33_t *c)
{
	cm33_sys_t *s = c->sys;
	mj_write_on (1);
	E = J->code;
	J->enter = E;
	w (0xA9BB7BFDu);                        /* stp x29, x30, [sp, #-80]! */
	w (0xA90153F3u);                        /* stp x19, x20, [sp, #16] */
	w (0xA9025BF5u);                        /* stp x21, x22, [sp, #32] */
	w (0xA90363F7u);                        /* stp x23, x24, [sp, #48] */
	w (0xAA0003F3u); w (0xAA0103F4u);       /* mov x19, x0; mov x20, x1 */
	a_cyc_ld ();
	a_movx (22, (uint64_t) (uintptr_t) s->ram); a_movx (23, (uint64_t) (uintptr_t) s->xram);
	w (0xD61F0040u);                        /* br x2 */
	J->dispatch = E;
	uint8_t *back[8]; int nb = 0;
	a_ldw (10, MO_R15);
	a_cmpr (10, 0); uint8_t *fwd1 = a_bcc (A_HS);                             /* (not backward) */
	a_rrr (A_SUB, 11, 0, 10); a_cmpi (11, 64); uint8_t *fwd2 = a_bcc (A_HI);
	a_movx (12, (uint64_t) (uintptr_t) &cm33_spin_mode); w (0xB9400000u | (12u << 5) | 12u); uint8_t *fwd3 = a_cbz (12);
	if (mj_noinl & 0x1000) back[nb++] = a_b ();
	a_cyc_st ();
	a_mov (1, 0); w (0xAA1303E0u);                                          /* w1 = ip; x0 = the core */
	a_call ((void *) loop_round);
	a_cyc_ld ();
	a_movw (0, 0); a_ldw (10, MO_R15);                                       /* (w0 = 0: done - the loop does not run it again) */
	patch_to (fwd1, E); patch_to (fwd2, E); patch_to (fwd3, E);
	uint8_t *top = E;
	a_ldw (9, MO_ATTN); uint8_t *no_attn = a_cbz (9);
	a_cyc_st ();
	w (0xAA1303E0u); w (0xAA1403E1u);                                       /* mov x0, x19; mov x1, x20 */
	a_call ((void *) run_attn);
	a_cyc_ld ();
	a_cmpi (0, 1); uint8_t *not1 = a_bcc (A_NE);
	a_movw (0, MJ_DONE); back[nb++] = a_b ();
	patch_to (not1, E);
	a_ldw (10, MO_R15); a_cmpi (0, 2); a_movw (0, 0); { uint8_t *t = a_bcc (A_EQ); patch_to (t, top); }   /* (movz leaves the flags) */
	patch_to (no_attn, E);
	w (0xEB1402BFu); back[nb++] = a_bcc (A_HS);                              /* cmp x21, x20 */
	a_ldw (9, MO_F (it)); back[nb++] = a_cbnz (9);
	a_movx (12, (uint64_t) (uintptr_t) &J->flush_req); w (0xB9400000u | (12u << 5) | 9u); back[nb++] = a_cbnz (9);
	a_movw (16, s->ram_base); a_rrr (A_SUB, 11, 10, 16); a_cmpi (11, s->ram_size - 4); back[nb++] = a_bcc (A_HI);
	a_lsr (11, 11, 1);
	a_movx (12, (uint64_t) (uintptr_t) J->blk);
	w (0xF8605800u | (11u << 16) | (12u << 5) | 9u);                        /* ldr x9, [x12, w11, uxtw #3] */
	w (0xF100053Fu); back[nb++] = a_bcc (A_LS);                              /* cmp x9, #1: (none yet, or none can start there) */
	w (0xD61F0120u);                                                        /* br x9 */
	for (int k = 0; k < nb; k++) patch_to (back[k], E);
	a_cyc_st ();
	mj_epilogue ();
	J->free = J->stubs_end = E;
	mj_write_on (0);
	mj_sync (J->code, E);
}

static uint8_t *mj_compile (cm33_t *c, const di_t **ins, const uint32_t *ips, int n)
{
	cm33_sys_t *s = c->sys;
	mj_ram_base = s->ram_base; mj_ram_size = s->ram_size; mj_code_pages = s->code_pages; mj_xram_size = (s->xram && !(mj_noinl & 0x400)) ? s->xram_size : 0;
	uint8_t *start = E;
	mj_nex = mj_nsl = 0;
	for (int i = 0; i < n; i++) {
		const di_t *d = ins[i]; uint32_t ip = ips[i], next = ip + d->len;
		int cost, br, sl0 = mj_nsl;
		mj_entry[i] = E;
		int inl = mj_itf[i] ? 0 : a_inline (ip, next, d, &cost, &br);
		if (inl) mj_st_inl++; else mj_st_hand++;
		if (inl) {
			if (br == 2) {
				if (mj_nsl > sl0) { for (int k = sl0; k < mj_nsl; k++) mj_sl[k].join = E; ex_add (a_b (), ip, 0, 0); }
				continue;
			}
			a_cyc_add (cost);
			for (int k = sl0; k < mj_nsl; k++) mj_sl[k].join = E;
			if (i == n - 1) { a_sti (MO_R15, next); a_sti (MO_LAST, ip); ex_add (a_b (), ip, 0, 0); }
			else { w (0xEB1402BFu); ex_add (a_bcc (A_HS), ip, next, 1); }   /* cmp x21, x20; b.hs: the slice is used up */
		} else {
			mj_hfn = mj_itf[i] ? h_itw : NULL;
			a_handler (ip, next, d);
			mj_hfn = NULL;
			if (i == n - 1) ex_add (a_b (), ip, 0, 0);
			else { w (0xEB1402BFu); ex_add (a_bcc (A_HS), ip, 0, 0); }
		}
	}
	for (int k = 0; k < mj_nsl; k++) {
		mj_slow_t *x = &mj_sl[k];
		patch_to (x->site, E);
		a_handler (x->ip, x->next, x->d);
		a_b_to (x->join);
	}
	for (int k = 0; k < mj_nex; k++) {
		mj_exit_t *x = &mj_ex[k];
		patch_to (x->site, E);
		if (x->set) { a_sti (MO_R15, x->pc); a_sti (MO_LAST, x->ip); }
		a_movw (0, x->ip);
		a_b_to (mj_cur->dispatch);
	}
	(void) s;
	return start;
}
#endif

/* the block at ip: compiled now (or MJ_NONE: none starts here) */
static uint8_t *mj_translate_l (mj_core_t *J, cm33_t *c, di_t *dc, uint32_t ip0);
static uint8_t *mj_translate (mj_core_t *J, cm33_t *c, di_t *dc, uint32_t ip0)
{
	mj_lock ();
	uint8_t *r = mj_translate_l (J, c, dc, ip0);
	mj_unlock ();
	return r;
}
static uint8_t *mj_translate_l (mj_core_t *J, cm33_t *c, di_t *dc, uint32_t ip0)
{
	cm33_sys_t *s = c->sys;
	uint32_t base = s->ram_base, size = s->ram_size, o0 = ip0 - base;
	if (J->free + 64 * 1024 > J->code_end) mj_flush (J, s);
	/* the instructions first: decoded, up to where the block ends */
	const di_t *ins[MJ_MAX]; uint32_t ips[MJ_MAX]; int n = 0;
	uint32_t ip = ip0;
	int itn = 0;                                                    /* (instructions of an IT block still to come) */
	while (n < MJ_MAX) {
		uint32_t o = ip - base;
		if (o + 4 > size) break;
		di_t *d = &dc[o >> 1];
		if (d->fn == h_decode) decode_at (c, ip, d);
		if (d->fn == GENERIC || d->fn == h_decode) break;
		mj_itf[n] = itn > 0; if (itn > 0) itn--;
		ins[n] = d; ips[n] = ip; n++;
		J->pages[o >> 12] = 1; J->pages[(o + d->len - 1) >> 12] = 1;
		for (uint32_t h = 0; h < d->len; h += 2) J->incode[(o + h) >> 1] = 1;
		ip += d->len;
		if (d->fn == h_it && mj_it_on && !mj_itf[n - 1] && (d->imm & 15)) {   /* an IT: its 1-4 instructions follow, through h_itw */
			unsigned m = d->imm & 15; itn = (m & 1) ? 4 : (m & 2) ? 3 : (m & 4) ? 2 : 1;
			continue;
		}
		if (mj_itf[n - 1] && itn > 0) continue;                     /* (a branch inside the IT block: only as its last - the block ends there anyway) */
		if (d->fn == h_it || d->fn == h_b || d->fn == h_bl || d->fn == h_bx) break;
	}
	if (!n) { J->blk[o0 >> 1] = MJ_NONE; return MJ_NONE; }
	mj_write_on (1);
	E = J->free; mj_cur = J;
	uint8_t *start = mj_compile (c, ins, ips, n);
	J->free = E;
	mj_write_on (0);
	mj_sync (start, E);
	J->blk[o0 >> 1] = start; J->n_blocks++;
#if !defined(_WIN32)
	{ static FILE *pm; static int pm_on = -1;                       /* (VT_PERFMAP: /tmp/perf-PID.map for perf - each block named) */
	  if (pm_on < 0) { pm_on = getenv ("VT_PERFMAP") != NULL; if (pm_on) { char fn[64]; snprintf (fn, sizeof fn, "/tmp/perf-%d.map", (int) getpid ()); pm = fopen (fn, "w"); } }
	  if (pm) { fprintf (pm, "%lx %lx m33_%08x_n%d\n", (unsigned long) (uintptr_t) start, (unsigned long) (E - start), ip0, n); fflush (pm); } }
#endif
#if 1
	/* every instruction's code is an entry too (the state is all in memory but the cycles, r14): a slice that ended
	 * in the middle of this block goes on there instead of compiling a block from that address */
	if (!(mj_noinl & 0x800)) for (int i = 1; i < n; i++) { uint32_t oi = ips[i] - base; if (!J->blk[oi >> 1]) J->blk[oi >> 1] = mj_entry[i]; }
#endif
	if (mj_stats > 0) { if (!mj_len) mj_len = (uint8_t *) calloc ((s->ram_size >> 1) + 2, 1); if (mj_len) mj_len[o0 >> 1] = (uint8_t) n; }
	return start;
}
#endif
#ifndef CM33_JIT
static void mj_written (uint32_t o, uint32_t n) { (void) o; (void) n; }
#endif
