/* arm11_jit.c - a JIT for arm11.c: the ARM code of a PiTrex program translated, a block at a time, into x86-64
 * machine code (included by arm11.c; x86-64 hosts only - elsewhere the interpreter runs everything).
 *
 * A block is a run of ARM instructions from one address: up to the first instruction that changes the PC (or
 * one the JIT leaves to the interpreter), never across a 4 KB page. Its code keeps the ARM state where the
 * interpreter keeps it - the registers, flags and counters in arm11_t (rbx points at it) - so the two can take
 * turns at any block boundary: what the JIT does not translate (the system instructions, VFP, the media
 * instructions, ...) the interpreter runs, one instruction, and the JIT carries on after it.
 *
 * It counts cycles exactly as the interpreter does, and takes its decisions at the same moments: an interrupt
 * raised by a peripheral access is taken before the next instruction, a short backward branch runs the same
 * spin detection. With VT_JIT=exact a block only runs when it fits in what is left of the slice (the rest is
 * interpreted), so a run gives exactly the interpreter's result - the check that the JIT is right. Without it
 * a block may run past the slice's end by a few cycles (the next slice is that much shorter: pitrex_cart.c).
 *
 * Writes to a page that holds translated code drop that page's blocks (the loader copies programs in).
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#if (defined(_M_X64) || defined(__x86_64__)) && !defined(ARM11_NO_JIT)
#define ARM11_JIT 1
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

typedef struct { uint32_t pc, cost; uint8_t *code; } jent_t;     /* (cost: the most cycles the block can take) */
#define JT_BITS 16
#define JT_MASK ((1u << JT_BITS) - 1)
#define JCODE_SIZE (48u << 20)
#define JMAX 64                                                    /* (instructions in a block, at most) */
static jent_t *jt;                                                 /* the blocks, by address */
static uint8_t *jcode, *jcode_end, *jfree;                         /* the code buffer */
static uint8_t *jpage; static uint32_t jpage_n;                    /* per 4 KB page of RAM: translated code in it */
static int jit_state;                                              /* 0 not set up, 1 on, -1 off */
static int jit_exact;
static uint8_t *j_enter, *j_dispatch, *j_exit, *j_stubs_end;
static unsigned long jit_n_blocks, jit_n_flush, jit_n_inval, jit_n_interp;
unsigned long long jit_c_run, jit_c_slow, jit_c_loop, jit_c_interp, jit_c_enter;
unsigned long jit_interp_hist[4096]; uint32_t jit_interp_ins[4096];
typedef void (*jenter_fn) (arm11_t *c, uint8_t *code);

/* ---------------------------------------------------------------- an x86-64 emitter (just what the templates use) */
static uint8_t *E;
static void b1 (int x) { *E++ = (uint8_t) x; }
static void d4 (uint32_t x) { memcpy (E, &x, 4); E += 4; }
static void q8 (uint64_t x) { memcpy (E, &x, 8); E += 8; }
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
#ifdef _WIN32
enum { A0 = RCX, A1 = RDX, A2 = R8, A3 = R9 };
#else
enum { A0 = RDI, A1 = RSI, A2 = RDX, A3 = RCX };
#endif
#define OF(f) ((int32_t) offsetof (arm11_t, f))
#define OR(n) ((int32_t) (offsetof (arm11_t, r) + 4 * (n)))
static void rex (int w, int r, int b) { int v = 0x40 | (w << 3) | ((r >> 3) << 2) | (b >> 3); if (v != 0x40) b1 (v); }
static void modm (int r, int32_t off) { b1 (0x80 | ((r & 7) << 3) | 3); d4 ((uint32_t) off); }      /* [rbx + off] */
static void ld (int r, int32_t off) { rex (0, r, 0); b1 (0x8B); modm (r, off); }                    /* mov r32, [rbx+off] */
static void st (int32_t off, int r) { rex (0, r, 0); b1 (0x89); modm (r, off); }                    /* mov [rbx+off], r32 */
static void sti (int32_t off, uint32_t v) { b1 (0xC7); modm (0, off); d4 (v); }                     /* mov dword [rbx+off], imm */
static void movi (int r, uint32_t v) { if (!v) { rex (0, r, r); b1 (0x31); b1 (0xC0 | ((r & 7) << 3) | (r & 7)); return; } rex (0, 0, r); b1 (0xB8 + (r & 7)); d4 (v); }
static void rr (int op, int dst, int src) { rex (0, src, dst); b1 (op); b1 (0xC0 | ((src & 7) << 3) | (dst & 7)); }   /* op r/m32(dst), r32(src) */
static void rm_ (int op, int r, int32_t off) { rex (0, r, 0); b1 (op); modm (r, off); }             /* op r32, [rbx+off] (03 add, 0B or, 23 and, 33 xor, 3B cmp, 2B sub, 13 adc, 1B sbb) */
static void ri (int ext, int dst, uint32_t imm)                                                        /* add0 or1 adc2 sbb3 and4 sub5 xor6 cmp7 */
{
	rex (0, 0, dst);
	if ((int32_t) imm >= -128 && (int32_t) imm <= 127) { b1 (0x83); b1 (0xC0 | (ext << 3) | (dst & 7)); b1 (imm & 0xff); }
	else { b1 (0x81); b1 (0xC0 | (ext << 3) | (dst & 7)); d4 (imm); }
}
static void shi (int ext, int r, int n) { rex (0, 0, r); b1 (0xC1); b1 (0xC0 | (ext << 3) | (r & 7)); b1 (n); }   /* rol0 ror1 rcl2 rcr3 shl4 shr5 sar7 */
static void shcl (int ext, int r) { rex (0, 0, r); b1 (0xD3); b1 (0xC0 | (ext << 3) | (r & 7)); }
static void un (int ext, int r) { rex (0, 0, r); b1 (0xF7); b1 (0xC0 | (ext << 3) | (r & 7)); }     /* not2 neg3 mul4 imul5 */
static void setm (int cc, int32_t off) { b1 (0x0F); b1 (0x90 + cc); modm (0, off); }                 /* setcc byte [rbx+off] */
static void cmpbm1 (int32_t off) { b1 (0x80); modm (7, off); b1 (1); }                             /* cmp byte [rbx+off], 1: CF = !flag */
static void btm (int32_t off) { cmpbm1 (off); b1 (0xF5); }                                           /* CF = the flag (cmp, cmc) */
static void ldb (int r, int32_t off) { rex (0, r, 0); b1 (0x0F); b1 (0xB6); modm (r, off); }      /* movzx r32, byte [rbx+off] */
static void rmb (int op, int r, int32_t off) { rex (0, r, 0); b1 (op); modm (r, off); }             /* op r8, byte [rbx+off] (22 and, 32 xor, 0A or, 3A cmp) */
static void btr_ (int r, int bit) { rex (0, 0, r); b1 (0x0F); b1 (0xBA); b1 (0xC0 | (4 << 3) | (r & 7)); b1 (bit); }
static void cmpmi (int32_t off, int v) { b1 (0x83); modm (7, off); b1 (v); }                       /* cmp dword [rbx+off], imm8 */
static void cmpbm0 (int32_t off) { b1 (0x80); modm (7, off); b1 (0); }                              /* cmp byte [rbx+off], 0 */
static void cmpf (int32_t off) { cmpbm0 (off); }
static void mov64 (int dst, int src) { rex (1, src, dst); b1 (0x89); b1 (0xC0 | ((src & 7) << 3) | (dst & 7)); }
static void mov32 (int dst, int src) { rr (0x89, dst, src); }
static void addm64 (int32_t off, int32_t v) { if (!v) return; b1 (0x48); b1 (0x81); modm (0, off); d4 ((uint32_t) v); }
static void incm (int32_t off) { b1 (0xFF); modm (0, off); }
static uint8_t *jcc (int cc) { b1 (0x0F); b1 (0x80 + cc); d4 (0); return E; }
static uint8_t *jmp (void) { b1 (0xE9); d4 (0); return E; }
static void here (uint8_t *end) { int32_t rel = (int32_t) (E - end); memcpy (end - 4, &rel, 4); }
static void jmp_to (uint8_t *t) { b1 (0xE9); int32_t rel = (int32_t) (t - (E + 4)); d4 ((uint32_t) rel); }
static void call (void *fn) { b1 (0x48); b1 (0xB8); q8 ((uint64_t) (uintptr_t) fn); b1 (0xFF); b1 (0xD0); }
enum { CC_O, CC_NO, CC_C, CC_NC, CC_Z, CC_NZ, CC_BE, CC_A, CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };
/* RAM at r12 + rcx */
static void ram_ld (int n, int sx) { b1 (0x41); if (n == 4) b1 (0x8B); else { b1 (0x0F); b1 (n == 1 ? (sx ? 0xBE : 0xB6) : (sx ? 0xBF : 0xB7)); } b1 (0x04); b1 (0x0C); }   /* eax = [r12+rcx] */
static void ram_st (int n) { if (n == 2) b1 (0x66); b1 (0x41); b1 (n == 1 ? 0x88 : 0x89); b1 (0x04); b1 (0x0C); }   /* [r12+rcx] = al / ax / eax */

/* ---------------------------------------------------------------- the helpers the code calls */
static uint32_t jh_rd (arm11_t *c, uint32_t a, uint32_t n) { jit_c_slow++; return mrd (c, a, (int) n); }
static uint32_t jh_rds (arm11_t *c, uint32_t a, uint32_t n)       /* (signed byte / halfword) */
{ uint32_t v = mrd (c, a, (int) n); return n == 1 ? (uint32_t) (int32_t) (int8_t) v : (uint32_t) (int32_t) (int16_t) v; }
static void jit_flush (void);
static void jh_wr (arm11_t *c, uint32_t a, uint32_t v, uint32_t n)
{
	jit_c_slow++; mwr (c, a, v, (int) n);
	if (a < jpage_n * 4096u) {
		uint32_t p0 = a >> 12, p1 = (a + n - 1) >> 12;
		if (jpage[p0] || (p1 < jpage_n && jpage[p1])) { jit_flush (); jit_n_inval++; c->jit_until = 0; }   /* (stop at the next block) */
	}
}
static uint32_t jh_loop (arm11_t *c) { jit_c_loop++; return (uint32_t) loop_round (c, c->jit_until); }
static void jh_bx (arm11_t *c, uint32_t v, uint32_t ip) { bx_to (c, v, ip); }
static uint32_t jh_mrc (arm11_t *c, uint32_t ins) { unsigned rt = (ins >> 12) & 15; uint32_t v = 0; cp15 (c, 1, (ins >> 21) & 7, (ins >> 16) & 15, ins & 15, (ins >> 5) & 7, &v); (void) rt; return v; }
static void jh_mcr (arm11_t *c, uint32_t ins, uint32_t v) { cp15 (c, 0, (ins >> 21) & 7, (ins >> 16) & 15, ins & 15, (ins >> 5) & 7, &v); }

/* call fn (c, ecx, eax[, edx]) - the arguments as the code holds them */
static void call3 (void *fn, int nargs)
{
#ifdef _WIN32
	if (nargs >= 4) mov32 (A3, RDX);                                /* r9 = edx, then rdx = ecx, r8 = eax */
	if (nargs >= 2) mov32 (A1, RCX);
	if (nargs >= 3) mov32 (A2, RAX);
#else
	if (nargs >= 2) mov32 (A1, RCX);                                /* rsi = ecx, then rcx = edx, rdx = eax */
	if (nargs >= 4) mov32 (A3, RDX);
	if (nargs >= 3) mov32 (A2, RAX);
#endif
	mov64 (A0, RBX);
	call (fn);
}

/* ---------------------------------------------------------------- setting up */
static int jit_setup (arm11_t *c)
{
	const char *z = getenv ("VT_JIT");
	if (z && (z[0] == '0' || !strcmp (z, "off"))) { jit_state = -1; return 0; }
	jit_exact = z && !strcmp (z, "exact");
#ifdef _WIN32
	jcode = (uint8_t *) VirtualAlloc (NULL, JCODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
	jcode = (uint8_t *) mmap (NULL, JCODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (jcode == (uint8_t *) MAP_FAILED) jcode = NULL;
#endif
	jt = (jent_t *) calloc ((size_t) 1 << JT_BITS, sizeof *jt);
	jpage_n = c->sys->ram_size >> 12; jpage = (uint8_t *) calloc (jpage_n ? jpage_n : 1, 1);
	if (!jcode || !jt || !jpage) { fprintf (stderr, "PITREX: no JIT (no executable memory)\n"); jit_state = -1; return 0; }
	jcode_end = jcode + JCODE_SIZE;
	E = jcode;
	/* enter (c, code): save the registers the code uses, rbx = c, r12 = RAM, r14 = the page map, r15 = the blocks */
	j_enter = E;
	b1 (0x53); b1 (0x55); b1 (0x41); b1 (0x54); b1 (0x41); b1 (0x55); b1 (0x41); b1 (0x56); b1 (0x41); b1 (0x57);   /* push rbx rbp r12 r13 r14 r15 */
#ifdef _WIN32
	b1 (0x56); b1 (0x57);                                                                                          /* push rsi rdi */
#endif
	b1 (0x48); b1 (0x83); b1 (0xEC); b1 (40);                                                                       /* sub rsp, 40 */
	mov64 (RBX, A0);
	b1 (0x49); b1 (0xBC); q8 ((uint64_t) (uintptr_t) c->sys->ram);                                                  /* mov r12, ram */
	b1 (0x49); b1 (0xBE); q8 ((uint64_t) (uintptr_t) jpage);                                                        /* mov r14, jpage */
	b1 (0x49); b1 (0xBF); q8 ((uint64_t) (uintptr_t) jt);                                                           /* mov r15, jt */
	rex (1, 0, A1); b1 (0xFF); b1 (0xE0 | (A1 & 7));                                                                /* jmp A1 */
	/* exit */
	j_exit = E;
	b1 (0x48); b1 (0x83); b1 (0xC4); b1 (40);                                                                       /* add rsp, 40 */
#ifdef _WIN32
	b1 (0x5F); b1 (0x5E);
#endif
	b1 (0x41); b1 (0x5F); b1 (0x41); b1 (0x5E); b1 (0x41); b1 (0x5D); b1 (0x41); b1 (0x5C); b1 (0x5D); b1 (0x5B);   /* pop r15 r14 r13 r12 rbp rbx */
	b1 (0xC3);
	/* dispatch: r[15] set - the next block, if there is time left and no interrupt waits */
	j_dispatch = E;
	{
		uint8_t *x1, *x2, *x3, *x4, *x5, *ok;
		cmpmi (OF (halted), 0); x5 = jcc (CC_NZ);                               /* (stopped: back) */
		cmpmi (OF (irq_line), 0); ok = jcc (CC_Z);                              /* (an interrupt that is not masked: back) */
		cmpmi (OF (ibit), 0); x1 = jcc (CC_Z);
		here (ok);
		ld (RAX, OR (15)); mov32 (RCX, RAX); shi (5, RCX, 2); ri (4, RCX, JT_MASK); shi (4, RCX, 4);   /* rcx = index * 16 */
		b1 (0x41); b1 (0x3B); b1 (0x04); b1 (0x0F);                              /* cmp eax, [r15+rcx] */
		x2 = jcc (CC_NZ);
		b1 (0x49); b1 (0x8B); b1 (0x54); b1 (0x0F); b1 (8);                      /* mov rdx, [r15+rcx+8] */
		b1 (0x48); b1 (0x85); b1 (0xD2); x3 = jcc (CC_Z);                         /* test rdx, rdx */
		b1 (0x48); b1 (0x8B); modm (RAX, OF (cycles));                            /* mov rax, [rbx+cycles] */
		if (jit_exact) { b1 (0x41); b1 (0x8B); b1 (0x4C); b1 (0x0F); b1 (4); /* mov ecx, [r15+rcx+4] */ b1 (0x48); b1 (0x01); b1 (0xC8); /* add rax, rcx */
		                 b1 (0x48); b1 (0x3B); modm (RAX, OF (jit_until)); x4 = jcc (CC_A); }   /* cmp rax, until; ja */
		else { b1 (0x48); b1 (0x3B); modm (RAX, OF (jit_until)); x4 = jcc (CC_NC); }            /* cmp rax, until; jae */
		b1 (0xFF); b1 (0xE2);                                                    /* jmp rdx */
		here (x1); here (x2); here (x3); here (x4); here (x5);
		jmp_to (j_exit);
	}
	jfree = j_stubs_end = E;
	jit_state = 1;
	return 1;
}
/* the links: jumps to a known address, patched straight to that block's code (until then: the dispatcher) */
typedef struct { uint8_t *jmp_end, *cost; uint32_t target; } jlink_t;
#define JLINKS 32768
static jlink_t jlinks[JLINKS]; static int jlinks_n;
static void jlink_patch (jlink_t *l, jent_t *t)
{
	int32_t rel = (int32_t) (t->code - l->jmp_end); memcpy (l->jmp_end - 4, &rel, 4);
	if (l->cost) memcpy (l->cost, &t->cost, 4);
}
static void jit_flush (void)
{
	if (jit_state != 1) return;
	memset (jt, 0, sizeof *jt << JT_BITS); memset (jpage, 0, jpage_n);
	jfree = E = j_stubs_end;                                           /* (the enter / exit / dispatch code stays) */
	jlinks_n = 0;
	jit_n_flush++;
}


/* ---------------------------------------------------------------- translating */
/* what the JIT does with an instruction: 0 leave it to the interpreter; else its kind */
enum { K_NONE, K_DP, K_MUL, K_MULL, K_LS, K_LSX, K_LDM, K_B, K_BX, K_CPNOP, K_MRC, K_CLZ, K_EXT };
static int j_kind (uint32_t ins, uint32_t ip, int *ends, unsigned *extra)
{
	unsigned cond = ins >> 28, op = (ins >> 25) & 7, rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
	*ends = 0; *extra = 0;
	if (cond == 15) return K_NONE;
	switch (op) {
	case 0: case 1:
		if (op == 0 && (ins & 0x90) == 0x90) {
			unsigned sh = (ins >> 5) & 3;
			if (sh == 0) {
				if ((ins & 0x0f000000u) == 0) {
					unsigned mop = (ins >> 21) & 7;
					if (rm == 15 || rs == 15 || rn == 15 || rd == 15) return K_NONE;
					if (mop <= 1) return K_MUL;
					if (mop >= 4) return rd == rn ? K_NONE : K_MULL;
				}
				return K_NONE;
			}
			{ unsigned P = (ins >> 24) & 1, I = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
			  if (!I && rm == 15) return K_NONE;
			  if (rd == 15 || (!P && W)) return K_NONE;
			  if ((!P || W) && (rn == rd || rn == 15)) return K_NONE;
			  if (!L && sh == 1) return K_LSX;                               /* STRH */
			  if (L) return K_LSX;                                           /* LDRH / LDRSB / LDRSH */
			  if (rd >= 14 || (rd & 1)) return K_NONE;                        /* LDRD / STRD */
			  if ((!P || W) && rn == rd + 1) return K_NONE;
			  return K_LSX; }
		}
		{
			unsigned opc = (ins >> 21) & 15, S = (ins >> 20) & 1;
			if ((opc & 0xc) == 0x8 && !S) {
				if (op == 0 && opc == 9 && ((ins >> 4) & 15) == 1 && rm != 15) { *ends = 1; return K_BX; }   /* BX */
				if (op == 0 && opc == 9 && ((ins >> 4) & 15) == 3 && rm != 15) { *ends = 1; return K_BX; }   /* BLX register */
				if (op == 0 && opc == 11 && ((ins >> 4) & 15) == 1 && rm != 15 && rd != 15) return K_CLZ;
				return K_NONE;
			}
			if (op == 0 && (ins & 0x10)) {                                       /* register-shifted register */
				if (rn == 15 || rm == 15 || rs == 15 || rd == 15) return K_NONE;
				int logical = !(opc >= 2 && opc <= 7) && opc != 10 && opc != 11;
				if (S && logical) return K_NONE;                                   /* (its carry: the interpreter) */
				return K_DP;
			}
			if (rd == 15 && (opc & 0xc) != 0x8) { if (S) return K_NONE; *ends = 1; *extra = 2; }
			return K_DP;
		}
	case 2: case 3:
		if (op == 3 && (ins & 0x10)) {
			unsigned op1 = (ins >> 20) & 31, op2 = (ins >> 5) & 7;
			if (op2 == 3 && (op1 & 0x18) == 0x08 && rn == 15 && rd != 15 && rm != 15 && ((op1 & 7) == 2 || (op1 & 7) == 3 || (op1 & 7) == 6 || (op1 & 7) == 7)) return K_EXT;   /* SXTB SXTH UXTB UXTH */
			return K_NONE;
		}
		{ unsigned P = (ins >> 24) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		  if (!P && W) return K_NONE;
		  if (op == 3 && rm == 15) return K_NONE;
		  if ((!P || W) && (rn == rd || rn == 15)) return K_NONE;
		  if (rd == 15 && !L) { *extra = 1; return K_LS; }
		  if (rd == 15) { *ends = 1; *extra = 1 + 2; return K_LS; }
		  *extra = 1; return K_LS; }
	case 4: {
		unsigned S = (ins >> 22) & 1, L = (ins >> 20) & 1, W = (ins >> 21) & 1; uint32_t list = ins & 0xffff;
		if (S || rn == 15 || !list) return K_NONE;
		if (W && ((list >> rn) & 1) && !L) {                                      /* (STM writing back its base, in the list: the interpreter) */
			return K_NONE;
		}
		unsigned cnt = 0; for (int i = 0; i < 16; i++) cnt += (list >> i) & 1;
		*extra = cnt;
		if (L && (list & 0x8000)) { *ends = 1; *extra += 2; }
		return K_LDM;
	}
	case 5: {
		int32_t off = (int32_t) (ins << 8) >> 6;
		if (ip + 8 + (uint32_t) off == ip) return K_NONE;                        /* (b . : the interpreter sleeps) */
		*ends = 1; *extra = 2; return K_B;
	}
	case 7:
		if ((ins & 0x0f000010u) == 0x0e000010u && ((ins >> 8) & 15) == 15) {     /* MRC / MCR p15 */
			unsigned L = (ins >> 20) & 1, crn = (ins >> 16) & 15, crm = ins & 15, opc2 = (ins >> 5) & 7, rt = (ins >> 12) & 15;
			if (rt == 15) return K_NONE;
			if (!L && crn == 7 && !(crm == 0 && opc2 == 4)) return K_CPNOP;      /* (cache and barrier operations) */
			if (L) return K_MRC;
			return K_NONE;
		}
		return K_NONE;
	default: return K_NONE;
	}
}

typedef struct { uint32_t ins, ip; int kind, ends; unsigned extra, after; int folded; } jins_t;
static const uint8_t *j_ram;
static uint8_t *j_exits[JMAX * 4]; static uint32_t j_exit_pc[JMAX * 4]; static unsigned j_exit_back[JMAX * 4]; static int j_nexits;
static uint32_t j_ram_size;

/* code for "the condition fails: skip" - returns the jump to patch, or NULL (always) */
static uint8_t *j_cond_skip (unsigned cond)
{
	switch (cond) {
	case 0: cmpf (OF (z)); return jcc (CC_Z);
	case 1: cmpf (OF (z)); return jcc (CC_NZ);
	case 2: cmpf (OF (c)); return jcc (CC_Z);
	case 3: cmpf (OF (c)); return jcc (CC_NZ);
	case 4: cmpf (OF (n)); return jcc (CC_Z);
	case 5: cmpf (OF (n)); return jcc (CC_NZ);
	case 6: cmpf (OF (v)); return jcc (CC_Z);
	case 7: cmpf (OF (v)); return jcc (CC_NZ);
	case 8: ldb (RAX, OF (z)); ri (6, RAX, 1); rmb (0x22, RAX, OF (c)); return jcc (CC_Z);           /* HI: c && !z */
	case 9: ldb (RAX, OF (z)); ri (6, RAX, 1); rmb (0x22, RAX, OF (c)); return jcc (CC_NZ);          /* LS */
	case 10: ldb (RAX, OF (n)); rmb (0x3A, RAX, OF (v)); return jcc (CC_NZ);                         /* GE */
	case 11: ldb (RAX, OF (n)); rmb (0x3A, RAX, OF (v)); return jcc (CC_Z);                          /* LT */
	case 12: ldb (RAX, OF (n)); rmb (0x32, RAX, OF (v)); rmb (0x0A, RAX, OF (z)); return jcc (CC_NZ); /* GT: !z && n == v */
	case 13: ldb (RAX, OF (n)); rmb (0x32, RAX, OF (v)); rmb (0x0A, RAX, OF (z)); return jcc (CC_Z);  /* LE */
	default: return NULL;
	}
}
/* go on at a known address: the cycle adjustment, the time check, then a jump - patched to the block's code */
static void j_link (uint32_t target, int32_t adj)
{
	sti (OR (15), target);
	addm64 (OF (cycles), adj);
	b1 (0x48); b1 (0x8B); modm (RAX, OF (cycles));                       /* mov rax, [cycles] */
	uint8_t *cost = NULL;
	if (jit_exact) { b1 (0x48); b1 (0x05); cost = E; d4 (0); }            /* add rax, cost (patched with the block's) */
	b1 (0x48); b1 (0x3B); modm (RAX, OF (jit_until));                    /* cmp rax, [until] */
	b1 (0x0F); b1 (0x80 + (jit_exact ? CC_A : CC_NC)); { int32_t rel = (int32_t) (j_exit - (E + 4)); d4 ((uint32_t) rel); }
	jmp_to (j_dispatch);
	jent_t *t = &jt[(target >> 2) & JT_MASK];
	jlink_t l = { E, cost, target };
	if (t->pc == target && t->code) jlink_patch (&l, t);
	else if (jlinks_n < JLINKS) jlinks[jlinks_n++] = l;

}
/* leave the block: r15 = pc (already stored when pc == ~0), give back the cycles of the instructions not run */
static void j_leave (uint32_t pc, unsigned back, int via_dispatch)
{
	if (pc != 0xffffffffu) sti (OR (15), pc);
	addm64 (OF (cycles), -(int32_t) back);
	jmp_to (via_dispatch ? j_dispatch : j_exit);
}
/* after a peripheral access: an interrupt raised and not masked - leave before the next instruction */
typedef struct { uint8_t *j, *ret; uint32_t next; unsigned back; } icold_t;
static icold_t icolds[JMAX * 2]; static int nicold;
static void j_irq_check (uint32_t next, unsigned back)
{
	cmpmi (OF (irq_line), 0);
	icold_t *k = &icolds[nicold++]; k->j = jcc (CC_NZ); k->ret = E; k->next = next; k->back = back;
}
static void j_icolds (void)
{
	for (int i = 0; i < nicold; i++) {
		icold_t *k = &icolds[i];
		here (k->j);
		cmpmi (OF (ibit), 0); uint8_t *b = jcc (CC_NZ);
		j_leave (k->next, k->back, 0);
		here (b); jmp_to (k->ret);
	}
	nicold = 0;
}
/* the slow paths of the loads and stores (a peripheral, a page with code in it, an exclusive monitor set): out of
 * the way, after the block's code - the fast path falls straight through */
typedef struct { uint8_t *j[3]; int nj; uint8_t *ret; int store, n, sx; unsigned delta; } cold_t;
static cold_t colds[JMAX * 24]; static int ncold;
/* eax = [ecx], n bytes (sx: sign-extend); delta: our cycle count minus the interpreter's at the access */
static void j_load (int n, int sx, unsigned delta)
{
	cold_t *k = &colds[ncold++];
	ri (7, RCX, j_ram_size - n); k->j[0] = jcc (CC_A); k->nj = 1;
	ram_ld (n, sx);
	incm (OF (loop_loads)); st (OF (ld_addr), RCX); st (OF (ld_val), RAX);
	k->ret = E; k->store = 0; k->n = n; k->sx = sx; k->delta = delta;
}
/* [ecx] = eax, n bytes */
static void j_store (int n, unsigned delta)
{
	cold_t *k = &colds[ncold++];
	ri (7, RCX, j_ram_size - n); k->j[0] = jcc (CC_A);
	mov32 (RDX, RCX); shi (5, RDX, 12); b1 (0x41); b1 (0x80); b1 (0x3C); b1 (0x16); b1 (0);   /* cmp byte [r14+rdx], 0 */
	k->j[1] = jcc (CC_NZ);
	cmpmi (OF (excl), 0); k->j[2] = jcc (CC_NZ); k->nj = 3;
	ram_st (n);
	incm (OF (loop_stores));
	k->ret = E; k->store = 1; k->n = n; k->sx = 0; k->delta = delta;
}
static void j_colds (void)
{
	for (int i = 0; i < ncold; i++) {
		cold_t *k = &colds[i];
		for (int j = 0; j < k->nj; j++) here (k->j[j]);
		addm64 (OF (cycles), -(int32_t) k->delta);
		if (k->store) { movi (RDX, (uint32_t) k->n); call3 ((void *) jh_wr, 4); }
		else { movi (RAX, (uint32_t) k->n); call3 (k->sx ? (void *) jh_rds : (void *) jh_rd, 3); }
		addm64 (OF (cycles), (int32_t) k->delta);
		jmp_to (k->ret);
	}
	ncold = 0;
}
/* the operand of a data-processing instruction into ecx; sets c (the shifter's carry) when setc */
static void j_op2 (uint32_t ins, uint32_t ip, int setc)
{
	unsigned op = (ins >> 25) & 7, rm = ins & 15;
	if (op == 1) {
		unsigned rot = ((ins >> 8) & 15) * 2; uint32_t imm = ror32 (ins & 0xff, rot);
		movi (RCX, imm);
		if (setc && rot) sti (OF (c), imm >> 31);
		return;
	}
	if (ins & 0x10) {                                                            /* by a register's bottom byte (no carry out here) */
		unsigned type = (ins >> 5) & 3, rs = (ins >> 8) & 15;
		ld (RDX, OR (rm)); ld (RCX, OR (rs)); ri (4, RCX, 0xff);
		if (type == 3) { ri (4, RCX, 31); shcl (1, RDX); }
		else {
			ri (7, RCX, 32); uint8_t *lt = jcc (CC_C);
			if (type == 2) movi (RCX, 31); else movi (RDX, 0);
			here (lt);
			if (type != 2) { uint8_t *sk = NULL; (void) sk; }
			shcl (type == 0 ? 4 : type == 1 ? 5 : 7, RDX);
		}
		mov32 (RCX, RDX);
		return;
	}
	unsigned type = (ins >> 5) & 3, amt = (ins >> 7) & 31;
	if (rm == 15) movi (RCX, ip + 8); else ld (RCX, OR (rm));
	switch (type) {
	case 0: if (!amt) return; if (setc) { btr_ (RCX, 32 - amt); setm (CC_C, OF (c)); } shi (4, RCX, amt); return;
	case 1: if (!amt) { if (setc) { btr_ (RCX, 31); setm (CC_C, OF (c)); } movi (RCX, 0); return; }
	        if (setc) { btr_ (RCX, amt - 1); setm (CC_C, OF (c)); } shi (5, RCX, amt); return;
	case 2: if (!amt) amt = 32;
	        if (setc) { btr_ (RCX, amt - 1); setm (CC_C, OF (c)); } shi (7, RCX, amt == 32 ? 31 : amt); return;
	default:
		if (!amt) { btm (OF (c)); shi (3, RCX, 1); if (setc) setm (CC_C, OF (c)); return; }     /* RRX */
		if (setc) { btr_ (RCX, amt - 1); setm (CC_C, OF (c)); } shi (1, RCX, amt); return;
	}
}
static void j_nz (void) { rr (0x85, RAX, RAX); setm (CC_S, OF (n)); setm (CC_Z, OF (z)); }
/* a branch to a value in eax (BX, LDR pc, LDM pc, MOV pc): through the interpreter's bx_to for bit 0 */
static void j_branch_reg (uint32_t ip, unsigned back, int bx)
{
	if (bx) {
		btr_ (RAX, 0); uint8_t *thumb = jcc (CC_C);
		ri (4, RAX, ~3u); st (OR (15), RAX);
		uint8_t *ok = jmp ();
		here (thumb);
		mov32 (RCX, RAX); movi (RAX, ip); call3 ((void *) jh_bx, 3);
		here (ok);
	} else { ri (4, RAX, ~3u); st (OR (15), RAX); }
	/* a short backward jump: the spin check */
	ld (RAX, OR (15)); ri (7, RAX, ip); uint8_t *fw = jcc (CC_NC);
	movi (RCX, ip); rr (0x29, RCX, RAX); ri (7, RCX, 64); uint8_t *far_ = jcc (CC_A);
	addm64 (OF (cycles), -(int32_t) back);
	mov64 (A0, RBX); call ((void *) jh_loop);
	addm64 (OF (cycles), (int32_t) back);
	rr (0x85, RAX, RAX); uint8_t *go = jcc (CC_Z);
	addm64 (OF (cycles), -(int32_t) back); jmp_to (j_exit);
	here (go); here (fw); here (far_);
	addm64 (OF (cycles), -(int32_t) back);
	jmp_to (j_dispatch);
}

/* one instruction's code. Returns 0, or 1 when it ends the block (its code leaves) */
static void j_emit (jins_t *I, unsigned uncond_back)
{
	uint32_t ins = I->ins, ip = I->ip;
	unsigned cond = ins >> 28, rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rs = (ins >> 8) & 15, rm = ins & 15;
	unsigned after = I->after;                         /* (the static cycles of the instructions after this one) */
	unsigned ue = cond == 14 ? I->extra : 0;           /* (its own extra cycles, counted already at the block's start) */
	unsigned dl = ue + after;                          /* (ours minus the interpreter's, at its memory access) */
	uint8_t *skip = j_cond_skip (cond);
	if (cond != 14 && I->extra && I->kind != K_B && I->kind != K_LDM && I->kind != K_LS) addm64 (OF (cycles), (int32_t) I->extra);
	switch (I->kind) {
	case K_DP: {
		unsigned op = (ins >> 25) & 7, opc = (ins >> 21) & 15, S = (ins >> 20) & 1;
		int logical = !(opc >= 2 && opc <= 7) && opc != 10 && opc != 11;
		if (op == 1 && rd != 15 && opc != 3 && opc != 7) {                     /* (an immediate: straight into the instruction) */
			unsigned rot = ((ins >> 8) & 15) * 2; uint32_t imm = ror32 (ins & 0xff, rot);
			if (S && logical && rot) sti (OF (c), imm >> 31);
			if (opc == 13 || opc == 15) {                                        /* MOV / MVN */
				uint32_t v = opc == 13 ? imm : ~imm;
				sti (OR (rd), v);
				if (S) { sti (OF (n), v >> 31); sti (OF (z), v == 0); }
				break;
			}
			if (rn == 15) movi (RAX, ip + 8); else ld (RAX, OR (rn));
			switch (opc) {
			case 0: case 8: ri (4, RAX, imm); break;
			case 1: case 9: ri (6, RAX, imm); break;
			case 2: case 10: ri (5, RAX, imm); break;
			case 4: case 11: ri (0, RAX, imm); break;
			case 5: btm (OF (c)); ri (2, RAX, imm); break;
			case 6: cmpbm1 (OF (c)); ri (3, RAX, imm); break;
			case 12: ri (1, RAX, imm); break;
			default: ri (4, RAX, ~imm); break;                                  /* BIC */
			}
			if (S) {
				if (!logical) { int sub = opc == 2 || opc == 6 || opc == 10; setm (sub ? CC_NC : CC_C, OF (c)); setm (CC_O, OF (v)); setm (CC_S, OF (n)); setm (CC_Z, OF (z)); }
				else { setm (CC_S, OF (n)); setm (CC_Z, OF (z)); }              /* (and / or / xor set SF and ZF) */
			}
			if (!(opc >= 8 && opc <= 11)) st (OR (rd), RAX);
			break;
		}
		j_op2 (ins, ip, S && logical);
		if (opc != 13 && opc != 15) { if (rn == 15) movi (RAX, ip + 8); else ld (RAX, OR (rn)); }
		(void) op;
		int wr = !(opc >= 8 && opc <= 11), arith = !logical;
		switch (opc) {
		case 0: case 8: rr (0x21, RAX, RCX); break;
		case 1: case 9: rr (0x31, RAX, RCX); break;
		case 2: case 10: rr (0x29, RAX, RCX); break;
		case 3: rr (0x29, RCX, RAX); mov32 (RAX, RCX); break;
		case 4: case 11: rr (0x01, RAX, RCX); break;
		case 5: btm (OF (c)); rr (0x11, RAX, RCX); break;
		case 6: cmpbm1 (OF (c)); rr (0x19, RAX, RCX); break;
		case 7: cmpbm1 (OF (c)); rr (0x19, RCX, RAX); mov32 (RAX, RCX); break;
		case 12: rr (0x09, RAX, RCX); break;
		case 13: mov32 (RAX, RCX); break;
		case 14: un (2, RCX); rr (0x21, RAX, RCX); break;
		default: mov32 (RAX, RCX); un (2, RAX); break;
		}
		if (S) {
			if (arith) {
				int sub = opc == 2 || opc == 3 || opc == 6 || opc == 7 || opc == 10;
				setm (sub ? CC_NC : CC_C, OF (c)); setm (CC_O, OF (v)); setm (CC_S, OF (n)); setm (CC_Z, OF (z));
			} else j_nz ();
		}
		if (wr) {
			if (rd == 15) { j_branch_reg (ip, after, 0); break; }
			st (OR (rd), RAX);
		}
		break;
	}
	case K_MUL: {
		unsigned mop = (ins >> 21) & 7, S = (ins >> 20) & 1;
		ld (RAX, OR (rm)); b1 (0x0F); b1 (0xAF); modm (RAX, OR (rs));   /* imul eax, [rbx+rs] */
		if (mop) rm_ (0x03, RAX, OR (rd));
		st (OR (rn), RAX);
		if (S) j_nz ();
		break;
	}
	case K_MULL: {
		unsigned mop = (ins >> 21) & 7, S = (ins >> 20) & 1;
		ld (RAX, OR (rm)); ld (RCX, OR (rs));
		un ((mop & 2) ? 5 : 4, RCX);                          /* edx:eax = eax * ecx */
		if (mop & 1) { rm_ (0x03, RAX, OR (rd)); rm_ (0x13, RDX, OR (rn)); }
		st (OR (rd), RAX); st (OR (rn), RDX);
		if (S) { mov32 (RCX, RAX); rr (0x09, RCX, RDX); setm (CC_Z, OF (z)); rr (0x85, RDX, RDX); setm (CC_S, OF (n)); }
		break;
	}
	case K_CLZ: {
		/* lzcnt is not on every x86-64: bsr, and 32 for zero */
		ld (RCX, OR (rm)); movi (RAX, 32); rr (0x85, RCX, RCX); uint8_t *zr = jcc (CC_Z);
		rex (0, RAX, RCX); b1 (0x0F); b1 (0xBD); b1 (0xC0 | (RAX << 3) | (RCX & 7));   /* bsr eax, ecx */
		ri (6, RAX, 31);
		here (zr);
		st (OR (rd), RAX);
		break;
	}
	case K_EXT: {
		unsigned op1 = (ins >> 20) & 7, rot = ((ins >> 10) & 3) * 8;
		ld (RCX, OR (rm)); if (rot) shi (1, RCX, rot);
		b1 (0x0F); b1 (op1 == 2 ? 0xBE : op1 == 3 ? 0xBF : op1 == 6 ? 0xB6 : 0xB7); b1 (0xC1);   /* movsx/movzx eax, cl / cx */
		st (OR (rd), RAX);
		break;
	}
	case K_LS: {
		unsigned op = (ins >> 25) & 7, P = (ins >> 24) & 1, U = (ins >> 23) & 1, B = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		unsigned dla = (cond == 14 ? I->extra : 0) + after;      /* (at the access the interpreter has not added its extra yet) */
		if (op == 2 && rn == 15 && P && !W && L && rd != 15) {                  /* a literal (ldr rd, [pc, #x]): its value now */
			uint32_t a = U ? ip + 8 + (ins & 0xfff) : ip + 8 - (ins & 0xfff);
			if ((a >> 12) == (ip >> 12) && a + 4 <= j_ram_size) {                /* (in this page: a write to it drops the block) */
				uint32_t v; if (B) v = j_ram[a]; else memcpy (&v, j_ram + a, 4);
				sti (OR (rd), v); incm (OF (loop_loads)); sti (OF (ld_addr), a); sti (OF (ld_val), v);
				I->folded = 1;
				if (cond != 14) addm64 (OF (cycles), 1);
				break;
			}
		}
		if (op == 2 && P && !W) {                                              /* (the common case: [rn, #x]) */
			uint32_t off = ins & 0xfff;
			if (rn == 15) movi (RCX, U ? ip + 8 + off : ip + 8 - off);
			else { ld (RCX, OR (rn)); if (off) ri (U ? 0 : 5, RCX, off); }
			if (L) {
				j_load (B ? 1 : 4, 0, dla);
				if (rd == 15) { if (cond != 14) addm64 (OF (cycles), 3); j_branch_reg (ip, after, 1); break; }
				st (OR (rd), RAX);
			} else {
				if (rd == 15) movi (RAX, ip + 8); else ld (RAX, OR (rd));
				j_store (B ? 1 : 4, dla);
			}
			if (cond != 14) addm64 (OF (cycles), 1);
			break;
		}
		/* the offset into edx, the base into eax; address ecx */
		if (op == 2) movi (RDX, ins & 0xfff);
		else {
			unsigned type = (ins >> 5) & 3, amt = (ins >> 7) & 31;
			ld (RDX, OR (rm));
			switch (type) {
			case 0: if (amt) shi (4, RDX, amt); break;
			case 1: if (amt) shi (5, RDX, amt); else movi (RDX, 0); break;
			case 2: shi (7, RDX, amt ? amt : 31); break;
			default: if (amt) shi (1, RDX, amt); else { btm (OF (c)); shi (3, RDX, 1); } break;
			}
		}
		if (rn == 15) movi (RAX, ip + 8); else ld (RAX, OR (rn));
		/* wb = U ? base + off : base - off */
		mov32 (RCX, RAX); rr (U ? 0x01 : 0x29, RCX, RDX);         /* ecx = wb */
		if (!P) { mov32 (RDX, RCX); mov32 (RCX, RAX); }            /* (post-indexed: the access at the base, edx = wb) */
		else mov32 (RDX, RCX);
		/* edx = wb; ecx = address. Keep wb in rbp over the call */
		mov32 (RBP, RDX);
		if (L) {
			j_load (B ? 1 : 4, 0, dla);
			if (!P || W) st (OR (rn), RBP);
			if (rd == 15) { if (cond != 14) addm64 (OF (cycles), 3); j_branch_reg (ip, after, 1); break; }
			st (OR (rd), RAX);
		} else {
			if (rd == 15) movi (RAX, ip + 8); else ld (RAX, OR (rd));
			j_store (B ? 1 : 4, dla);
			if (!P || W) st (OR (rn), RBP);
		}
		if (cond != 14) addm64 (OF (cycles), 1);
		break;
	}
	case K_LSX: {
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, I_ = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1, sh = (ins >> 5) & 3;
		if (I_ && P && !W && sh != 2 && !(sh == 3 && !L)) {                  /* (the common case: [rn, #x]) */
			uint32_t off = ((ins >> 4) & 0xf0) | (ins & 15);
			if (rn == 15) movi (RCX, U ? ip + 8 + off : ip + 8 - off);
			else { ld (RCX, OR (rn)); if (off) ri (U ? 0 : 5, RCX, off); }
			if (L) { j_load (sh == 1 ? 2 : 2, sh != 1, dl); st (OR (rd), RAX); }
			else { ld (RAX, OR (rd)); j_store (2, dl); }
			break;
		}
		if (I_) movi (RDX, ((ins >> 4) & 0xf0) | (ins & 15)); else ld (RDX, OR (rm));
		if (rn == 15) movi (RAX, ip + 8); else ld (RAX, OR (rn));
		mov32 (RCX, RAX); rr (U ? 0x01 : 0x29, RCX, RDX);
		if (!P) { mov32 (RDX, RCX); mov32 (RCX, RAX); } else mov32 (RDX, RCX);
		mov32 (RBP, RDX);
		if (L) {
			j_load (sh == 1 ? 2 : sh == 2 ? 1 : 2, sh != 1, dl);
			if (!P || W) st (OR (rn), RBP);
			st (OR (rd), RAX);
		} else if (sh == 1) {
			ld (RAX, OR (rd)); j_store (2, dl);
			if (!P || W) st (OR (rn), RBP);
		} else if (sh == 2) {                                       /* LDRD: the address kept in r13d over the first load */
			b1 (0x41); b1 (0x89); b1 (0xCD);                         /* mov r13d, ecx */
			j_load (4, 0, dl); st (OR (rd), RAX);
			b1 (0x44); b1 (0x89); b1 (0xE9); ri (0, RCX, 4);          /* mov ecx, r13d; add ecx, 4 */
			j_load (4, 0, dl); st (OR (rd + 1), RAX);
			if (!P || W) st (OR (rn), RBP);
		} else {                                                    /* STRD */
			b1 (0x41); b1 (0x89); b1 (0xCD);
			ld (RAX, OR (rd)); j_store (4, dl);
			b1 (0x44); b1 (0x89); b1 (0xE9); ri (0, RCX, 4);
			ld (RAX, OR (rd + 1)); j_store (4, dl);
			if (!P || W) st (OR (rn), RBP);
		}
		break;
	}
	case K_LDM: {
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		uint32_t list = ins & 0xffff; unsigned cnt = 0;
		for (int i = 0; i < 16; i++) cnt += (list >> i) & 1;
		unsigned dla = (cond == 14 ? I->extra : 0) + after;
		ld (RAX, OR (rn));
		/* start address in ebp, the written-back base in r13d */
		if (U) { mov32 (RBP, RAX); if (P) ri (0, RBP, 4); }
		else { mov32 (RBP, RAX); ri (5, RBP, 4 * cnt); if (!P) ri (0, RBP, 4); }
		b1 (0x41); b1 (0x89); b1 (0xC5);                            /* mov r13d, eax */
		ri (U ? 0 : 5, R13, 4 * cnt);
		if (L) {
			if (W && !((list >> rn) & 1)) st (OR (rn), R13);
			/* fast: the whole range in RAM */
			mov32 (RCX, RBP); ri (7, RCX, j_ram_size - 4 * cnt); uint8_t *slow = jcc (CC_A);
			{ int k = 0;
			  for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				b1 (0x41); b1 (0x8B); b1 (0x84); b1 (0x0C); d4 (4u * k);                /* mov eax, [r12+rcx+4k] */
				if (i != 15) st (OR (i), RAX);
				k++;
			  }
			  b1 (0x81); modm (0, OF (loop_loads)); d4 (cnt);                         /* add dword [loop_loads], cnt */
			  ri (0, RCX, 4 * (cnt - 1)); st (OF (ld_addr), RCX); st (OF (ld_val), RAX); }
			uint8_t *done = jmp ();
			here (slow);
			for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				mov32 (RCX, RBP); j_load (4, 0, dla);
				ri (0, RBP, 4);
				if (i != 15) st (OR (i), RAX);
			}
			here (done);
			if (list & 0x8000) {                                                    /* (eax: the new PC) */
				if (cond != 14) addm64 (OF (cycles), (int32_t) I->extra);
				j_branch_reg (ip, after, 1);
				break;
			}
		} else {
			/* fast: the whole range in RAM, no code in its pages, no exclusive monitor */
			mov32 (RCX, RBP); ri (7, RCX, j_ram_size - 4 * cnt); uint8_t *s1 = jcc (CC_A);
			mov32 (RDX, RCX); shi (5, RDX, 12); b1 (0x41); b1 (0x80); b1 (0x3C); b1 (0x16); b1 (0); uint8_t *s2 = jcc (CC_NZ);
			b1 (0x8D); b1 (0x91); d4 (4 * cnt - 1); shi (5, RDX, 12);                    /* lea edx, [rcx + 4cnt-1]; shr edx, 12 */
			b1 (0x41); b1 (0x80); b1 (0x3C); b1 (0x16); b1 (0); uint8_t *s3 = jcc (CC_NZ);
			cmpmi (OF (excl), 0); uint8_t *s4 = jcc (CC_NZ);
			{ int k = 0;
			  for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				if (i == 15) movi (RAX, ip + 8); else ld (RAX, OR (i));
				b1 (0x41); b1 (0x89); b1 (0x84); b1 (0x0C); d4 (4u * k);                /* mov [r12+rcx+4k], eax */
				k++;
			  }
			  b1 (0x81); modm (0, OF (loop_stores)); d4 (cnt); }
			uint8_t *done = jmp ();
			here (s1); here (s2); here (s3); here (s4);
			for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				mov32 (RCX, RBP);
				if (i == 15) movi (RAX, ip + 8); else ld (RAX, OR (i));
				j_store (4, dla);
				ri (0, RBP, 4);
			}
			here (done);
			if (W) st (OR (rn), R13);
		}
		if (cond != 14) addm64 (OF (cycles), (int32_t) I->extra);
		break;
	}
	case K_B: {
		int32_t off = (int32_t) (ins << 8) >> 6; uint32_t t = ip + 8 + (uint32_t) off;
		if (ins & 0x01000000u) sti (OR (14), ip + 4);
		int32_t adj = (cond != 14 ? 2 : 0) - (int32_t) after;
		if (t < ip && ip - t <= 64) {
			sti (OR (15), t); addm64 (OF (cycles), adj); adj = 0;
			mov64 (A0, RBX); call ((void *) jh_loop);
			rr (0x85, RAX, RAX); uint8_t *go = jcc (CC_Z);
			jmp_to (j_exit);
			here (go);
		}
		j_link (t, adj);
		break;
	}
	case K_BX: {
		ld (RAX, OR (rm));
		if (((ins >> 4) & 15) == 3) sti (OR (14), ip + 4);
		j_branch_reg (ip, after, 1);
		break;
	}
	case K_CPNOP: break;
	case K_MRC: {
		unsigned rt = (ins >> 12) & 15;
		addm64 (OF (cycles), -(int32_t) dl);
		movi (RCX, ins); call3 ((void *) jh_mrc, 2);
		addm64 (OF (cycles), (int32_t) dl);
		st (OR (rt), RAX);
		break;
	}
	}
	if (skip) here (skip);
	(void) uncond_back;
}

static int jit_try (arm11_t *c, uint64_t until);
/* translate the block at pc: its entry (code NULL when the first instruction is the interpreter's) */
static jent_t *jit_translate (arm11_t *c, uint32_t pc)
{
	jent_t *e = &jt[(pc >> 2) & JT_MASK];
	jins_t L[JMAX]; int n = 0;
	uint32_t pg = pc >> 12;
	const uint8_t *ram = c->sys->ram;
	j_ram_size = c->sys->ram_size; j_ram = c->sys->ram;
	for (uint32_t ip = pc; n < JMAX && (ip >> 12) == pg && ip + 4 <= j_ram_size; ip += 4) {
		uint32_t ins; memcpy (&ins, ram + ip, 4);
		int ends; unsigned extra;
		int k = j_kind (ins, ip, &ends, &extra);
		if (!k) break;
		L[n].ins = ins; L[n].ip = ip; L[n].kind = k; L[n].ends = ends; L[n].extra = extra; L[n].folded = 0; n++;
		if (ends && !(k == K_B && (ins >> 28) != 14)) break;
	}
	e->pc = pc; e->code = NULL; e->cost = 1;
	jpage[pg] = 1;
	if (!n) { jit_n_interp++; return e; }
	if (jfree + 64 * 1024 > jcode_end) { jit_flush (); e = &jt[(pc >> 2) & JT_MASK]; e->pc = pc; e->cost = 1; jpage[pg] = 1; }
	/* the static cycles: 1 each, and the extras of the unconditional ones */
	unsigned tot = 0, maxc = 0;
	for (int i = 0; i < n; i++) { unsigned s = 1 + ((L[i].ins >> 28) == 14 ? L[i].extra : 0); tot += s; maxc += 1 + L[i].extra; }
	unsigned acc = 0;
	for (int i = n - 1; i >= 0; i--) { L[i].after = acc; acc += 1 + ((L[i].ins >> 28) == 14 ? L[i].extra : 0); }
	E = jfree;
	uint8_t *code = E;
	addm64 (OF (cycles), (int32_t) tot);
#ifdef JIT_COUNT
	b1 (0x48); b1 (0xB8); q8 ((uint64_t) (uintptr_t) &jit_c_run); b1 (0x48); b1 (0xFF); b1 (0x00);   /* (inc qword [jit_c_run]) */
#endif
	ncold = 0; nicold = 0;
	for (int i = 0; i < n; i++) {
		int mem = L[i].kind == K_LS || L[i].kind == K_LSX || L[i].kind == K_LDM || L[i].kind == K_MRC;
		j_emit (&L[i], 0);
		if (L[i].ends && ((L[i].ins >> 28) == 14 || i == n - 1)) break;
		if (L[i].kind == K_B) continue;
		/* (a peripheral access may have raised an interrupt: taken before the next instruction) */
		if (mem && !L[i].folded) j_irq_check (L[i].ip + 4, L[i].after);
	}
	/* fell through the end (or a conditional last instruction not taken) */
	{ jins_t *l = &L[n - 1]; j_link (l->ip + 4, 0); }
	j_colds (); j_icolds ();
	jfree = E;
	e->code = code; e->cost = maxc;
	jit_n_blocks++;
	for (int i = 0; i < jlinks_n; i++) if (jlinks[i].target == pc) { jlink_patch (&jlinks[i], e); jlinks[i--] = jlinks[--jlinks_n]; }
	return e;
}
/* run JIT code from the PC, if there is a block there (and, exact, it fits in the slice): 1 if it ran */
static int jit_try (arm11_t *c, uint64_t until)
{
	if (!jit_state) jit_setup (c);
	if (jit_state != 1) return 0;
	uint32_t pc = c->r[15];
	if ((pc & 3) || pc + 4 > c->sys->ram_size || c->tbit) return 0;
	jent_t *e = &jt[(pc >> 2) & JT_MASK];
	if (e->pc != pc || !e->cost) e = jit_translate (c, pc);
	if (!e->code) { jit_c_interp++; { uint32_t ins; memcpy (&ins, c->sys->ram + pc, 4); unsigned h = ((ins >> 20) & 0xff0) | ((ins >> 4) & 15); jit_interp_hist[h]++; jit_interp_ins[h] = ins; } return 0; }
	jit_c_enter++;
	if (jit_exact && c->cycles + e->cost > until) return 0;
	c->jit_until = until;
	((jenter_fn) (void *) j_enter) (c, e->code);
	return 1;
}
void arm11_invalidate (arm11_t *c) { (void) c; jit_flush (); }
void arm11_jit_map (const char *fn)          /* (for profiling: each block's code address and ARM address) */
{
	FILE *f = fopen (fn, "wb"); if (!f || jit_state != 1) { if (f) fclose (f); return; }
	for (uint32_t i = 0; i <= JT_MASK; i++) if (jt[i].code) { uint64_t a = (uint64_t) (uintptr_t) jt[i].code; fwrite (&a, 8, 1, f); fwrite (&jt[i].pc, 4, 1, f); }
	fclose (f);
	char fn2[1024]; snprintf (fn2, sizeof fn2, "%s.code", fn);
	f = fopen (fn2, "wb"); if (!f) return;
	{ uint64_t a = (uint64_t) (uintptr_t) jcode; fwrite (&a, 8, 1, f); fwrite (jcode, 1, (size_t) (jfree - jcode), f); }
	fclose (f);
}
void arm11_jit_stats (void)
{
	if (jit_state == 1) fprintf (stderr, "jit: blocks %lu flushes %lu page drops %lu interpreter entries %lu code %lu KB\n",
	                            jit_n_blocks, jit_n_flush, jit_n_inval, jit_n_interp, (unsigned long) ((jfree - jcode) >> 10));
	if (jit_state == 1) {
		fprintf (stderr, "jit: run %llu enter %llu slow %llu loop %llu interp %llu\n", jit_c_run, jit_c_enter, jit_c_slow, jit_c_loop, jit_c_interp);
		for (int k = 0; k < 12; k++) { unsigned long best = 0; int bi = -1; for (int i = 0; i < 4096; i++) if (jit_interp_hist[i] > best) { best = jit_interp_hist[i]; bi = i; }
			if (bi < 0) break; fprintf (stderr, "jit:   interp %08X x %lu\n", jit_interp_ins[bi], best); jit_interp_hist[bi] = 0; }
		jit_c_run = jit_c_enter = jit_c_slow = jit_c_loop = jit_c_interp = 0;
	}
}
#else
void arm11_invalidate (arm11_t *c) { (void) c; }
void arm11_jit_stats (void) {}
void arm11_jit_map (const char *fn) { (void) fn; }
#endif
