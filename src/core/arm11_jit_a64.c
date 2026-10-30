/* arm11_jit_a64.c - the ARM64 code of the JIT (arm11_jit.c includes it on ARM64 hosts: Apple silicon Macs, ARM64 Linux,
 * Windows on ARM, Android). The same blocks, cycle counting, spin checks, interrupt points and links as the x86-64
 * code in arm11_jit.c, so VT_JIT=exact gives the interpreter's result here too.
 *
 * The ARM state stays in arm11_t (x19 points at it). x20 = RAM, x21 = the page map, x22 = the blocks, x26 = the RAM
 * size; x23 / x24 hold values over a call (a written-back base, an LDM's address). w0 is the value / result, w1 the
 * address or the second operand, w2 an offset, w9-w16 scratch. Helpers are called with x0 = the CPU, x1.. as C wants.
 * ARM64's NZCV flags work as ARM's (the carry of a subtraction is "no borrow"): ADDS / SUBS / ADCS / SBCS give the
 * ARM flags as they are. Apple silicon maps the code MAP_JIT and switches it writable while it is written.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

/* ---------------------------------------------------------------- an ARM64 emitter (just what the code uses) */
static uint8_t *E;
static void w (uint32_t x) { memcpy (E, &x, 4); E += 4; }
#define OF(f) ((uint32_t) offsetof (arm11_t, f))
#define OR(n) ((uint32_t) (offsetof (arm11_t, r) + 4 * (n)))
enum { ZR = 31 };
enum { EQ, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE };
static void ldw (int rt, uint32_t off) { w (0xB9400000u | ((off >> 2) << 10) | (19u << 5) | (uint32_t) rt); }    /* ldr wt, [x19, #off] */
static void stw (int rt, uint32_t off) { w (0xB9000000u | ((off >> 2) << 10) | (19u << 5) | (uint32_t) rt); }
static void ldx (int rt, uint32_t off) { w (0xF9400000u | ((off >> 3) << 10) | (19u << 5) | (uint32_t) rt); }
static void stx (int rt, uint32_t off) { w (0xF9000000u | ((off >> 3) << 10) | (19u << 5) | (uint32_t) rt); }
static void movw (int rd, uint32_t v)                                                  /* mov wd, #v */
{
	if (!(v & 0xffff) && v) { w (0x52A00000u | ((v >> 16) << 5) | (uint32_t) rd); return; }
	w (0x52800000u | ((v & 0xffff) << 5) | (uint32_t) rd);
	if (v >> 16) w (0x72A00000u | ((v >> 16) << 5) | (uint32_t) rd);
}
static void movx (int rd, uint64_t v)                                                  /* mov xd, #v */
{
	w (0xD2800000u | ((uint32_t) (v & 0xffff) << 5) | (uint32_t) rd);
	for (int h = 1; h < 4; h++) { uint32_t p = (uint32_t) (v >> (16 * h)) & 0xffff; if (p) w (0xF2800000u | ((uint32_t) h << 21) | (p << 5) | (uint32_t) rd); }
}
static void stiw (uint32_t off, uint32_t v) { if (!v) stw (ZR, off); else { movw (9, v); stw (9, off); } }   /* (w9) */
#define ADD_W 0x0B000000u
#define ADDS_W 0x2B000000u
#define SUB_W 0x4B000000u
#define SUBS_W 0x6B000000u
#define AND_W 0x0A000000u
#define BIC_W 0x0A200000u
#define ORR_W 0x2A000000u
#define ORN_W 0x2A200000u
#define EOR_W 0x4A000000u
static void rrr (uint32_t base, int rd, int rn, int rm, int sh, int amt)              /* op wd, wn, wm {, shift #amt} */
{ w (base | ((uint32_t) sh << 22) | ((uint32_t) rm << 16) | ((uint32_t) amt << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void mov_r (int rd, int rm) { if (rd != rm) rrr (ORR_W, rd, ZR, rm, 0, 0); }
static void addi (int rd, int rn, uint32_t imm) { if (imm < 4096) w (0x11000000u | (imm << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); else { movw (16, imm); rrr (ADD_W, rd, rn, 16, 0, 0); } }
static void subi (int rd, int rn, uint32_t imm) { if (imm < 4096) w (0x51000000u | (imm << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); else { movw (16, imm); rrr (SUB_W, rd, rn, 16, 0, 0); } }
static void cmpi (int rn, uint32_t imm) { if (imm < 4096) w (0x7100001Fu | (imm << 10) | ((uint32_t) rn << 5)); else { movw (16, imm); rrr (SUBS_W, ZR, rn, 16, 0, 0); } }
static void cmpr (int rn, int rm) { rrr (SUBS_W, ZR, rn, rm, 0, 0); }
static void adc_ (int s, int rd, int rn, int rm) { w ((s ? 0x3A000000u : 0x1A000000u) | ((uint32_t) rm << 16) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void sbc_ (int s, int rd, int rn, int rm) { w ((s ? 0x7A000000u : 0x5A000000u) | ((uint32_t) rm << 16) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void shv (uint32_t op, int rd, int rn, int rm) { w (op | ((uint32_t) rm << 16) | ((uint32_t) rn << 5) | (uint32_t) rd); }   /* lslv 2000 lsrv 2400 asrv 2800 rorv 2C00 */
#define LSLV 0x1AC02000u
#define LSRV 0x1AC02400u
#define ASRV 0x1AC02800u
#define RORV 0x1AC02C00u
static void lsli (int rd, int rn, int n) { if (!n) { mov_r (rd, rn); return; } w (0x53000000u | ((uint32_t) ((32 - n) & 31) << 16) | ((uint32_t) (31 - n) << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void lsri (int rd, int rn, int n) { w (0x53000000u | ((uint32_t) n << 16) | (31u << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void asri (int rd, int rn, int n) { w (0x13000000u | ((uint32_t) n << 16) | (31u << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void rori (int rd, int rn, int n) { w (0x13800000u | ((uint32_t) rn << 16) | ((uint32_t) n << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void ubfx (int rd, int rn, int lsb, int width) { w (0x53000000u | ((uint32_t) lsb << 16) | ((uint32_t) (lsb + width - 1) << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void cset (int rd, int cond) { w (0x1A9F07E0u | ((uint32_t) (cond ^ 1) << 12) | (uint32_t) rd); }
static void csel (int rd, int rn, int rm, int cond) { w (0x1A800000u | ((uint32_t) rm << 16) | ((uint32_t) cond << 12) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void madd (int rd, int rn, int rm, int ra) { w (0x1B000000u | ((uint32_t) rm << 16) | ((uint32_t) ra << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }
static void mov_x0_c (void) { w (0xAA1303E0u); }                                      /* mov x0, x19 */
static void cmpx (int rn, int rm) { w (0xEB00001Fu | ((uint32_t) rm << 16) | ((uint32_t) rn << 5)); }
static void addxi (int rd, int rn, uint32_t imm) { w (0x91000000u | (imm << 10) | ((uint32_t) rn << 5) | (uint32_t) rd); }   /* (imm < 4096) */
static void add_x_uxtw (int rd, int rn, int rm) { w (0x8B204000u | ((uint32_t) rm << 16) | ((uint32_t) rn << 5) | (uint32_t) rd); }   /* add xd, xn, wm, uxtw */
static void addm64 (uint32_t off, int32_t v)                                           /* (x9, x10) */
{
	if (!v) return;
	ldx (9, off);
	uint32_t a = (uint32_t) (v < 0 ? -v : v);
	if (a < 4096) w ((v < 0 ? 0xD1000000u : 0x91000000u) | (a << 10) | (9u << 5) | 9u);
	else { movw (10, a); w ((v < 0 ? 0xCB000000u : 0x8B000000u) | (10u << 16) | (9u << 5) | 9u); }
	stx (9, off);
}
static void incm (uint32_t off, uint32_t k) { ldw (9, off); addi (9, 9, k); stw (9, off); }   /* (w9) */
/* branches: emitted with a zero offset, the site returned; here (site) points it at the current position */
static uint8_t *bc (int cond) { uint8_t *p = E; w (0x54000000u | (uint32_t) cond); return p; }
static uint8_t *cbz_ (int rt, int nz, int x) { uint8_t *p = E; w ((x ? 0xB4000000u : 0x34000000u) | (nz ? 0x01000000u : 0) | (uint32_t) rt); return p; }
static uint8_t *tbnz0 (int rt) { uint8_t *p = E; w (0x37000000u | (uint32_t) rt); return p; }   /* tbnz wt, #0 */
static uint8_t *bb (void) { uint8_t *p = E; w (0x14000000u); return p; }
static void patch (uint8_t *site, uint8_t *t)
{
	uint32_t ins; memcpy (&ins, site, 4);
	int32_t off = (int32_t) (t - site) / 4;
	if ((ins & 0x7C000000u) == 0x14000000u) ins = (ins & 0xFC000000u) | ((uint32_t) off & 0x3FFFFFFu);            /* B / BL */
	else if ((ins & 0x7E000000u) == 0x36000000u) ins = (ins & 0xFFF8001Fu) | (((uint32_t) off & 0x3FFFu) << 5);   /* TBZ / TBNZ */
	else ins = (ins & 0xFF00001Fu) | (((uint32_t) off & 0x7FFFFu) << 5);                                          /* B.cond / CBZ / CBNZ */
	memcpy (site, &ins, 4);
}
static void here (uint8_t *site) { if (site) patch (site, E); }
static void b_to (uint8_t *t) { patch (bb (), t); }
static void call (void *fn) { movx (16, (uint64_t) (uintptr_t) fn); w (0xD63F0000u | (16u << 5)); }   /* blr x16 */
/* RAM at x20 + w1 */
static void ram_ld (int n, int sx) { w ((n == 4 ? 0xB8604800u : n == 2 ? (sx ? 0x78E04800u : 0x78604800u) : (sx ? 0x38E04800u : 0x38604800u)) | (1u << 16) | (20u << 5)); }   /* w0 = [x20, w1, uxtw] */
static void ram_st (int n) { w ((n == 4 ? 0xB8204800u : n == 2 ? 0x78204800u : 0x38204800u) | (1u << 16) | (20u << 5)); }   /* [x20, w1, uxtw] = w0 */

/* the code memory: writable while it is written (Apple silicon), and the instruction cache told */
static void j_wr_on (void)
{
#if defined(__APPLE__)
	if (__builtin_available (macOS 11.0, *)) pthread_jit_write_protect_np (0);
#endif
}
static void j_wr_off (void)
{
#if defined(__APPLE__)
	if (__builtin_available (macOS 11.0, *)) pthread_jit_write_protect_np (1);
#endif
}
static void j_sync (uint8_t *a, uint8_t *b)
{
	if (b <= a) return;
#if defined(__APPLE__)
	sys_icache_invalidate (a, (size_t) (b - a));
#elif defined(_WIN32)
	FlushInstructionCache (GetCurrentProcess (), a, (SIZE_T) (b - a));
#else
	__builtin___clear_cache ((char *) a, (char *) b);
#endif
}

/* ---------------------------------------------------------------- setting up */
static uint32_t j_ram_size;
static int jit_setup (arm11_t *c)
{
	const char *z = getenv ("VT_JIT");
	if (z && (z[0] == '0' || !strcmp (z, "off"))) { jit_state = -1; return 0; }
	jit_exact = z && !strcmp (z, "exact");
	if (jit_exact) { const char *q = getenv ("VT_JITXMAX"); jit_maxn = q ? atoi (q) : JIT_XMAX; if (jit_maxn < 1 || jit_maxn > JMAX) jit_maxn = JMAX;
	                 q = getenv ("VT_JITXSHORT"); if (q) jit_xshort = atoi (q); if (jit_xshort < 0 || jit_xshort > JMAX) jit_xshort = 0; }
#ifdef _WIN32
	jcode = (uint8_t *) VirtualAlloc (NULL, JCODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
#if defined(__APPLE__) && defined(MAP_JIT)
	jcode = (uint8_t *) mmap (NULL, JCODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
#else
	jcode = (uint8_t *) mmap (NULL, JCODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
	if (jcode == (uint8_t *) MAP_FAILED) jcode = NULL;
#endif
	jt = (jent_t *) calloc ((size_t) 1 << JT_BITS, sizeof *jt);
	jpage_n = c->sys->ram_size >> 12; jpage = (uint8_t *) calloc (jpage_n ? jpage_n : 1, 1);
	if (!jcode || !jt || !jpage) { fprintf (stderr, "PITREX: no JIT (no executable memory)\n"); jit_state = -1; return 0; }
	jcode_end = jcode + JCODE_SIZE;
	fprintf (stderr, "PITREX: JIT on (ARM64)\n");
	j_wr_on ();
	E = jcode;
	/* enter (c, code): save x19-x30, x19 = c, x20 = RAM, x21 = the page map, x22 = the blocks, x26 = RAM's size */
	j_enter = E;
	w (0xA9800000u | (0x74u << 15) | (30u << 10) | (31u << 5) | 29u);                  /* stp x29, x30, [sp, #-96]! */
	w (0xA9000000u | (2u << 15) | (20u << 10) | (31u << 5) | 19u);                     /* stp x19, x20, [sp, #16] */
	w (0xA9000000u | (4u << 15) | (22u << 10) | (31u << 5) | 21u);                     /* stp x21, x22, [sp, #32] */
	w (0xA9000000u | (6u << 15) | (24u << 10) | (31u << 5) | 23u);                     /* stp x23, x24, [sp, #48] */
	w (0xA9000000u | (8u << 15) | (26u << 10) | (31u << 5) | 25u);                     /* stp x25, x26, [sp, #64] */
	w (0xA9000000u | (10u << 15) | (28u << 10) | (31u << 5) | 27u);                    /* stp x27, x28, [sp, #80] */
	w (0x910003FDu);                                                                  /* mov x29, sp */
	w (0xAA0003F3u);                                                                  /* mov x19, x0 */
	movx (20, (uint64_t) (uintptr_t) c->sys->ram);
	movx (21, (uint64_t) (uintptr_t) jpage);
	movx (22, (uint64_t) (uintptr_t) jt);
	movx (26, c->sys->ram_size);
	w (0xD61F0000u | (1u << 5));                                                      /* br x1 */
	/* exit */
	j_exit = E;
	w (0xA9400000u | (10u << 15) | (28u << 10) | (31u << 5) | 27u);                    /* ldp x27, x28, [sp, #80] */
	w (0xA9400000u | (8u << 15) | (26u << 10) | (31u << 5) | 25u);
	w (0xA9400000u | (6u << 15) | (24u << 10) | (31u << 5) | 23u);
	w (0xA9400000u | (4u << 15) | (22u << 10) | (31u << 5) | 21u);
	w (0xA9400000u | (2u << 15) | (20u << 10) | (31u << 5) | 19u);
	w (0xA8C00000u | (12u << 15) | (30u << 10) | (31u << 5) | 29u);                    /* ldp x29, x30, [sp], #96 */
	w (0xD65F03C0u);                                                                  /* ret */
	/* dispatch: r[15] set - the next block, if there is time left and no interrupt waits */
	j_dispatch = E;
	{
		uint8_t *x[5], *ok;
		ldw (9, OF (halted)); x[0] = cbz_ (9, 1, 0);
		ldw (9, OF (irq_line)); ok = cbz_ (9, 0, 0);
		ldw (9, OF (ibit)); x[1] = cbz_ (9, 0, 0);
		here (ok);
		ldw (9, OR (15)); ubfx (10, 9, 2, JT_BITS);
		w (0x8B000000u | (10u << 16) | (4u << 10) | (22u << 5) | 10u);                  /* add x10, x22, x10, lsl #4 */
		w (0xB9400000u | (10u << 5) | 11u);                                           /* ldr w11, [x10] */
		cmpr (11, 9); x[2] = bc (NE);
		w (0xF9400000u | (1u << 10) | (10u << 5) | 12u);                              /* ldr x12, [x10, #8] */
		x[3] = cbz_ (12, 0, 1);
		ldx (13, OF (cycles));
		if (jit_exact) { w (0xB9400000u | (1u << 10) | (10u << 5) | 14u); w (0x8B000000u | (14u << 16) | (13u << 5) | 13u); }   /* ldr w14, [x10, #4]; add x13, x13, x14 */
		ldx (15, OF (jit_until)); cmpx (13, 15); x[4] = bc (jit_exact ? HI : HS);
		w (0xD61F0000u | (12u << 5));                                                 /* br x12 */
		for (int i = 0; i < 5; i++) patch (x[i], j_exit);
	}
	jfree = j_stubs_end = E;
	j_sync (jcode, E);
	j_wr_off ();
	jit_state = 1;
	return 1;
}

/* the links: jumps to a known address, patched straight to that block's code (until then: the dispatcher) */
typedef struct { uint8_t *jmp, *cost; uint32_t target; } jlink_t;
#define JLINKS 32768
static jlink_t jlinks[JLINKS]; static int jlinks_n;
static void jlink_patch (jlink_t *l, jent_t *t)
{
	patch (l->jmp, t->code); j_sync (l->jmp, l->jmp + 4);
	if (l->cost) { uint32_t ins; memcpy (&ins, l->cost, 4); ins = (ins & ~(0xFFFu << 10)) | ((t->cost & 0xFFFu) << 10); memcpy (l->cost, &ins, 4); j_sync (l->cost, l->cost + 4); }
}
static void jit_flush (void)
{
	if (jit_state != 1) return;
	memset (jt, 0, sizeof *jt << JT_BITS); memset (jpage, 0, jpage_n);
	if (jt2) memset (jt2, 0, sizeof *jt2 << JT_BITS);
	jfree = E = j_stubs_end;                                           /* (the enter / exit / dispatch code stays) */
	jlinks_n = 0;
	jit_n_flush++;
}

typedef struct { uint32_t ins, ip; int kind, ends; unsigned extra, after; int folded; } jins_t;
static const uint8_t *j_ram;

/* code for "the condition fails: skip" - returns the jump to patch, or NULL (always) */
static uint8_t *j_cond_skip (unsigned cond)
{
	switch (cond) {
	case 0: ldw (9, OF (z)); return cbz_ (9, 0, 0);
	case 1: ldw (9, OF (z)); return cbz_ (9, 1, 0);
	case 2: ldw (9, OF (c)); return cbz_ (9, 0, 0);
	case 3: ldw (9, OF (c)); return cbz_ (9, 1, 0);
	case 4: ldw (9, OF (n)); return cbz_ (9, 0, 0);
	case 5: ldw (9, OF (n)); return cbz_ (9, 1, 0);
	case 6: ldw (9, OF (v)); return cbz_ (9, 0, 0);
	case 7: ldw (9, OF (v)); return cbz_ (9, 1, 0);
	case 8: ldw (9, OF (c)); ldw (10, OF (z)); rrr (BIC_W, 9, 9, 10, 0, 0); return cbz_ (9, 0, 0);    /* HI: c && !z */
	case 9: ldw (9, OF (c)); ldw (10, OF (z)); rrr (BIC_W, 9, 9, 10, 0, 0); return cbz_ (9, 1, 0);    /* LS */
	case 10: ldw (9, OF (n)); ldw (10, OF (v)); cmpr (9, 10); return bc (NE);                       /* GE */
	case 11: ldw (9, OF (n)); ldw (10, OF (v)); cmpr (9, 10); return bc (EQ);                       /* LT */
	case 12: ldw (9, OF (n)); ldw (10, OF (v)); rrr (EOR_W, 9, 9, 10, 0, 0); ldw (10, OF (z)); rrr (ORR_W, 9, 9, 10, 0, 0); return cbz_ (9, 1, 0);   /* GT */
	case 13: ldw (9, OF (n)); ldw (10, OF (v)); rrr (EOR_W, 9, 9, 10, 0, 0); ldw (10, OF (z)); rrr (ORR_W, 9, 9, 10, 0, 0); return cbz_ (9, 0, 0);   /* LE */
	default: return NULL;
	}
}
/* go on at a known address: the cycle adjustment, the time check, then a jump - patched to the block's code */
static void j_link (uint32_t target, int32_t adj)
{
	stiw (OR (15), target);
	addm64 (OF (cycles), adj);
	ldx (9, OF (cycles));
	uint8_t *cost = NULL;
	if (jit_exact) { cost = E; addxi (9, 9, 0); }                                     /* add x9, x9, #cost (patched) */
	ldx (10, OF (jit_until)); cmpx (9, 10);
	uint8_t *go = bc (jit_exact ? LS : LO);
	b_to (j_exit);
	here (go);
	uint8_t *j = bb (); patch (j, j_dispatch);
	jent_t *t = &jt[(target >> 2) & JT_MASK];
	jlink_t l = { j, cost, target };
	if (t->pc == target && t->code) jlink_patch (&l, t);
	else if (jlinks_n < JLINKS) jlinks[jlinks_n++] = l;
}
/* leave the block: r15 = pc, give back the cycles of the instructions not run */
static void j_leave (uint32_t pc, unsigned back, int via_dispatch)
{
	stiw (OR (15), pc);
	addm64 (OF (cycles), -(int32_t) back);
	b_to (via_dispatch ? j_dispatch : j_exit);
}
/* after a peripheral access: an interrupt raised and not masked - leave before the next instruction */
typedef struct { uint8_t *j, *ret; uint32_t next; unsigned back; } icold_t;
static icold_t icolds[JMAX * 2]; static int nicold;
static void j_irq_check (uint32_t next, unsigned back)
{
	ldw (9, OF (irq_line));
	icold_t *k = &icolds[nicold++]; k->j = cbz_ (9, 1, 0); k->ret = E; k->next = next; k->back = back;
}
static void j_icolds (void)
{
	for (int i = 0; i < nicold; i++) {
		icold_t *k = &icolds[i];
		here (k->j);
		ldw (9, OF (ibit)); patch (cbz_ (9, 1, 0), k->ret);
		j_leave (k->next, k->back, 0);
	}
	nicold = 0;
}
/* the slow paths of the loads and stores, out of the way after the block's code */
typedef struct { uint8_t *j[3]; int nj; uint8_t *ret; int store, n, sx; unsigned delta; } cold_t;
static cold_t colds[JMAX * 24]; static int ncold;
/* w0 = [w1], n bytes (sx: sign-extend); delta: our cycle count minus the interpreter's at the access */
static void j_load (int n, int sx, unsigned delta)
{
	cold_t *k = &colds[ncold++];
	addxi (9, 1, (uint32_t) n); cmpx (9, 26); k->j[0] = bc (HI); k->nj = 1;
	ram_ld (n, sx);
	incm (OF (loop_loads), 1); stw (1, OF (ld_addr)); stw (0, OF (ld_val));
	k->ret = E; k->store = 0; k->n = n; k->sx = sx; k->delta = delta;
}
/* [w1] = w0, n bytes */
static void j_store (int n, unsigned delta)
{
	cold_t *k = &colds[ncold++];
	addxi (9, 1, (uint32_t) n); cmpx (9, 26); k->j[0] = bc (HI);
	lsri (9, 1, 12); w (0x38604800u | (9u << 16) | (21u << 5) | 10u); k->j[1] = cbz_ (10, 1, 0);   /* ldrb w10, [x21, w9, uxtw] */
	ldw (10, OF (excl)); k->j[2] = cbz_ (10, 1, 0); k->nj = 3;
	ram_st (n);
	incm (OF (loop_stores), 1);
	k->ret = E; k->store = 1; k->n = n; k->sx = 0; k->delta = delta;
}
static void j_colds (void)
{
	for (int i = 0; i < ncold; i++) {
		cold_t *k = &colds[i];
		for (int j = 0; j < k->nj; j++) here (k->j[j]);
		addm64 (OF (cycles), -(int32_t) k->delta);
		if (k->store) { mov_r (2, 0); movw (3, (uint32_t) k->n); mov_x0_c (); call ((void *) jh_wr); }
		else { movw (2, (uint32_t) k->n); mov_x0_c (); call (k->sx ? (void *) jh_rds : (void *) jh_rd); }
		addm64 (OF (cycles), (int32_t) k->delta);
		b_to (k->ret);
	}
	ncold = 0;
}
/* the operand of a data-processing instruction into w1; sets c (the shifter's carry) when setc */
static void j_op2 (uint32_t ins, uint32_t ip, int setc)
{
	unsigned op = (ins >> 25) & 7, rm = ins & 15;
	if (op == 1) {
		unsigned rot = ((ins >> 8) & 15) * 2; uint32_t imm = ror32 (ins & 0xff, rot);
		if (setc && rot) stiw (OF (c), imm >> 31);
		movw (1, imm);
		return;
	}
	if (ins & 0x10) {                                                            /* by a register's bottom byte (no carry out here) */
		unsigned type = (ins >> 5) & 3, rs = (ins >> 8) & 15;
		ldw (2, OR (rm)); ldw (9, OR (rs)); w (0x12001C00u | (9u << 5) | 9u);        /* and w9, w9, #0xff */
		if (type == 3) shv (RORV, 2, 2, 9);
		else if (type == 2) { cmpi (9, 32); movw (10, 31); csel (9, 10, 9, HS); shv (ASRV, 2, 2, 9); }
		else { shv (type == 0 ? LSLV : LSRV, 2, 2, 9); cmpi (9, 32); csel (2, ZR, 2, HS); }
		mov_r (1, 2);
		return;
	}
	unsigned type = (ins >> 5) & 3, amt = (ins >> 7) & 31;
	if (rm == 15) movw (1, ip + 8); else ldw (1, OR (rm));
	switch (type) {
	case 0: if (!amt) return; if (setc) { ubfx (9, 1, 32 - (int) amt, 1); stw (9, OF (c)); } lsli (1, 1, (int) amt); return;
	case 1: if (!amt) { if (setc) { ubfx (9, 1, 31, 1); stw (9, OF (c)); } movw (1, 0); return; }
	        if (setc) { ubfx (9, 1, (int) amt - 1, 1); stw (9, OF (c)); } lsri (1, 1, (int) amt); return;
	case 2: if (!amt) amt = 32;
	        if (setc) { ubfx (9, 1, (int) amt - 1, 1); stw (9, OF (c)); } asri (1, 1, amt == 32 ? 31 : (int) amt); return;
	default:
		if (!amt) {                                                              /* RRX */
			ldw (10, OF (c)); if (setc) ubfx (9, 1, 0, 1);
			lsri (1, 1, 1); rrr (ORR_W, 1, 1, 10, 0, 31);
			if (setc) stw (9, OF (c));
			return;
		}
		if (setc) { ubfx (9, 1, (int) amt - 1, 1); stw (9, OF (c)); } rori (1, 1, (int) amt); return;
	}
}
static void j_nz (int r) { lsri (10, r, 31); stw (10, OF (n)); cmpi (r, 0); cset (10, EQ); stw (10, OF (z)); }
static void j_nzcv (void)                                                         /* the host's flags are the ARM's */
{
	w (0xD53B4200u | 9u);                                                        /* mrs x9, nzcv */
	ubfx (10, 9, 31, 1); stw (10, OF (n)); ubfx (10, 9, 30, 1); stw (10, OF (z));
	ubfx (10, 9, 29, 1); stw (10, OF (c)); ubfx (10, 9, 28, 1); stw (10, OF (v));
}
/* a branch to a value in w0 (BX, LDR pc, LDM pc, MOV pc): through the interpreter's bx_to for bit 0 */
static void j_branch_reg (uint32_t ip, unsigned back, int bx)
{
	if (bx) {
		uint8_t *thumb = tbnz0 (0);
		lsri (9, 0, 2); lsli (9, 9, 2); stw (9, OR (15));
		uint8_t *ok = bb ();
		here (thumb);
		mov_r (1, 0); movw (2, ip); mov_x0_c (); call ((void *) jh_bx);
		here (ok);
	} else { lsri (9, 0, 2); lsli (9, 9, 2); stw (9, OR (15)); }
	/* a short backward jump: the spin check */
	ldw (9, OR (15)); movw (10, ip); cmpr (9, 10); uint8_t *fw = bc (HS);
	rrr (SUB_W, 11, 10, 9, 0, 0); cmpi (11, 64); uint8_t *far_ = bc (HI);
	addm64 (OF (cycles), -(int32_t) back);
	mov_x0_c (); call ((void *) jh_loop);
	addm64 (OF (cycles), (int32_t) back);
	uint8_t *go = cbz_ (0, 0, 0);
	addm64 (OF (cycles), -(int32_t) back); b_to (j_exit);
	here (go); here (fw); here (far_);
	addm64 (OF (cycles), -(int32_t) back);
	b_to (j_dispatch);
}

/* one instruction's code */
static void j_emit (jins_t *I)
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
		if (op == 1 && rd != 15 && (opc == 13 || opc == 15)) {                    /* MOV / MVN with an immediate */
			unsigned rot = ((ins >> 8) & 15) * 2; uint32_t imm = ror32 (ins & 0xff, rot), v = opc == 13 ? imm : ~imm;
			if (S && rot) stiw (OF (c), imm >> 31);
			stiw (OR (rd), v);
			if (S) { stiw (OF (n), v >> 31); stiw (OF (z), v == 0); }
			break;
		}
		j_op2 (ins, ip, S && logical);
		if (opc != 13 && opc != 15) { if (rn == 15) movw (0, ip + 8); else ldw (0, OR (rn)); }
		if (opc >= 5 && opc <= 7) { ldw (9, OF (c)); cmpi (9, 1); }               /* (the carry in: host C = c) */
		int wr = !(opc >= 8 && opc <= 11);
		switch (opc) {
		case 0: case 8: rrr (AND_W, 0, 0, 1, 0, 0); break;
		case 1: case 9: rrr (EOR_W, 0, 0, 1, 0, 0); break;
		case 2: case 10: rrr (S ? SUBS_W : SUB_W, 0, 0, 1, 0, 0); break;
		case 3: rrr (S ? SUBS_W : SUB_W, 0, 1, 0, 0, 0); break;
		case 4: case 11: rrr (S ? ADDS_W : ADD_W, 0, 0, 1, 0, 0); break;
		case 5: adc_ (S, 0, 0, 1); break;
		case 6: sbc_ (S, 0, 0, 1); break;
		case 7: sbc_ (S, 0, 1, 0); break;
		case 12: rrr (ORR_W, 0, 0, 1, 0, 0); break;
		case 13: mov_r (0, 1); break;
		case 14: rrr (BIC_W, 0, 0, 1, 0, 0); break;
		default: rrr (ORN_W, 0, ZR, 1, 0, 0); break;
		}
		if (S) { if (!logical) j_nzcv (); else j_nz (0); }
		if (wr) {
			if (rd == 15) { j_branch_reg (ip, after, 0); break; }
			stw (0, OR (rd));
		}
		break;
	}
	case K_MUL: {
		unsigned mop = (ins >> 21) & 7, S = (ins >> 20) & 1;
		ldw (0, OR (rm)); ldw (9, OR (rs));
		if (mop) { ldw (10, OR (rd)); madd (0, 0, 9, 10); } else madd (0, 0, 9, ZR);
		stw (0, OR (rn));
		if (S) j_nz (0);
		break;
	}
	case K_MULL: {
		unsigned mop = (ins >> 21) & 7, S = (ins >> 20) & 1;
		ldw (0, OR (rm)); ldw (9, OR (rs));
		int acc = ZR;
		if (mop & 1) { ldw (10, OR (rd)); ldw (11, OR (rn)); w (0xAA000000u | (11u << 16) | (32u << 10) | (10u << 5) | 10u); acc = 10; }   /* orr x10, x10, x11, lsl #32 */
		w (((mop & 2) ? 0x9B200000u : 0x9BA00000u) | (9u << 16) | ((uint32_t) acc << 10) | (0u << 5) | 0u);   /* smaddl / umaddl x0, w0, w9, acc */
		stw (0, OR (rd));
		w (0xD360FC00u | (0u << 5) | 9u); stw (9, OR (rn));                            /* lsr x9, x0, #32 */
		if (S) { w (0xD37FFC00u | (0u << 5) | 10u); stw (10, OF (n)); w (0xF100001Fu); cset (10, EQ); stw (10, OF (z)); }   /* lsr x10, x0, #63; cmp x0, #0 */
		break;
	}
	case K_CLZ: ldw (9, OR (rm)); w (0x5AC01000u | (9u << 5) | 0u); stw (0, OR (rd)); break;
	case K_EXT: {
		unsigned op1 = (ins >> 20) & 7, rot = ((ins >> 10) & 3) * 8;
		ldw (1, OR (rm)); if (rot) rori (1, 1, (int) rot);
		w ((op1 == 2 ? 0x13001C00u : op1 == 3 ? 0x13003C00u : op1 == 6 ? 0x53001C00u : 0x53003C00u) | (1u << 5) | 0u);   /* sxtb / sxth / uxtb / uxth w0, w1 */
		stw (0, OR (rd));
		break;
	}
	case K_LS: {
		unsigned op = (ins >> 25) & 7, P = (ins >> 24) & 1, U = (ins >> 23) & 1, B = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		unsigned dla = (cond == 14 ? I->extra : 0) + after;      /* (at the access the interpreter has not added its extra yet) */
		if (op == 2 && rn == 15 && P && !W && L && rd != 15) {                  /* a literal (ldr rd, [pc, #x]): its value now */
			uint32_t a = U ? ip + 8 + (ins & 0xfff) : ip + 8 - (ins & 0xfff);
			if ((a >> 12) == (ip >> 12) && a + 4 <= j_ram_size) {                /* (in this page: a write to it drops the block) */
				uint32_t v; if (B) v = j_ram[a]; else memcpy (&v, j_ram + a, 4);
				stiw (OR (rd), v); incm (OF (loop_loads), 1); stiw (OF (ld_addr), a); stiw (OF (ld_val), v);
				I->folded = 1;
				if (cond != 14) addm64 (OF (cycles), 1);
				break;
			}
		}
		if (op == 2 && P && !W) {                                              /* (the common case: [rn, #x]) */
			uint32_t off = ins & 0xfff;
			if (rn == 15) movw (1, U ? ip + 8 + off : ip + 8 - off);
			else { ldw (1, OR (rn)); if (off) { if (U) addi (1, 1, off); else subi (1, 1, off); } }
			if (L) {
				j_load (B ? 1 : 4, 0, dla);
				if (rd == 15) { if (cond != 14) addm64 (OF (cycles), 3); j_branch_reg (ip, after, 1); break; }
				stw (0, OR (rd));
			} else {
				if (rd == 15) movw (0, ip + 8); else ldw (0, OR (rd));
				j_store (B ? 1 : 4, dla);
			}
			if (cond != 14) addm64 (OF (cycles), 1);
			break;
		}
		/* the offset into w2, the base into w0; the address w1, the written-back base w23 */
		if (op == 2) movw (2, ins & 0xfff);
		else {
			unsigned type = (ins >> 5) & 3, amt = (ins >> 7) & 31;
			ldw (2, OR (rm));
			switch (type) {
			case 0: if (amt) lsli (2, 2, (int) amt); break;
			case 1: if (amt) lsri (2, 2, (int) amt); else movw (2, 0); break;
			case 2: asri (2, 2, amt ? (int) amt : 31); break;
			default: if (amt) rori (2, 2, (int) amt); else { ldw (10, OF (c)); lsri (2, 2, 1); rrr (ORR_W, 2, 2, 10, 0, 31); } break;
			}
		}
		if (rn == 15) movw (0, ip + 8); else ldw (0, OR (rn));
		rrr (U ? ADD_W : SUB_W, 1, 0, 2, 0, 0);                    /* w1 = wb */
		mov_r (23, 1);
		if (!P) mov_r (1, 0);                                      /* (post-indexed: the access at the base) */
		if (L) {
			j_load (B ? 1 : 4, 0, dla);
			if (!P || W) stw (23, OR (rn));
			if (rd == 15) { if (cond != 14) addm64 (OF (cycles), 3); j_branch_reg (ip, after, 1); break; }
			stw (0, OR (rd));
		} else {
			if (rd == 15) movw (0, ip + 8); else ldw (0, OR (rd));
			j_store (B ? 1 : 4, dla);
			if (!P || W) stw (23, OR (rn));
		}
		if (cond != 14) addm64 (OF (cycles), 1);
		break;
	}
	case K_LSX: {
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, I_ = (ins >> 22) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1, sh = (ins >> 5) & 3;
		if (I_ && P && !W && sh != 2 && !(sh == 3 && !L)) {                  /* (the common case: [rn, #x]) */
			uint32_t off = ((ins >> 4) & 0xf0) | (ins & 15);
			if (rn == 15) movw (1, U ? ip + 8 + off : ip + 8 - off);
			else { ldw (1, OR (rn)); if (off) { if (U) addi (1, 1, off); else subi (1, 1, off); } }
			if (L) { j_load (2, sh != 1, dl); stw (0, OR (rd)); }
			else { ldw (0, OR (rd)); j_store (2, dl); }
			break;
		}
		if (I_) movw (2, ((ins >> 4) & 0xf0) | (ins & 15)); else ldw (2, OR (rm));
		if (rn == 15) movw (0, ip + 8); else ldw (0, OR (rn));
		rrr (U ? ADD_W : SUB_W, 1, 0, 2, 0, 0);
		mov_r (23, 1);
		if (!P) mov_r (1, 0);
		if (L) {
			j_load (sh == 1 ? 2 : sh == 2 ? 1 : 2, sh != 1, dl);
			if (!P || W) stw (23, OR (rn));
			stw (0, OR (rd));
		} else if (sh == 1) {
			ldw (0, OR (rd)); j_store (2, dl);
			if (!P || W) stw (23, OR (rn));
		} else if (sh == 2) {                                       /* LDRD: the address kept in w24 over the first load */
			mov_r (24, 1);
			j_load (4, 0, dl); stw (0, OR (rd));
			addi (1, 24, 4);
			j_load (4, 0, dl); stw (0, OR (rd + 1));
			if (!P || W) stw (23, OR (rn));
		} else {                                                    /* STRD */
			mov_r (24, 1);
			ldw (0, OR (rd)); j_store (4, dl);
			addi (1, 24, 4);
			ldw (0, OR (rd + 1)); j_store (4, dl);
			if (!P || W) stw (23, OR (rn));
		}
		break;
	}
	case K_LDM: {
		unsigned P = (ins >> 24) & 1, U = (ins >> 23) & 1, W = (ins >> 21) & 1, L = (ins >> 20) & 1;
		uint32_t list = ins & 0xffff; unsigned cnt = 0;
		for (int i = 0; i < 16; i++) cnt += (list >> i) & 1;
		unsigned dla = (cond == 14 ? I->extra : 0) + after;
		ldw (0, OR (rn));
		/* the start address in w23, the written-back base in w24 */
		if (U) { if (P) addi (23, 0, 4); else mov_r (23, 0); }
		else { subi (23, 0, 4 * cnt); if (!P) addi (23, 23, 4); }
		if (U) addi (24, 0, 4 * cnt); else subi (24, 0, 4 * cnt);
		if (L) {
			if (W && !((list >> rn) & 1)) stw (24, OR (rn));
			/* fast: the whole range in RAM */
			addxi (9, 23, 4 * cnt); cmpx (9, 26); uint8_t *slow = bc (HI);
			add_x_uxtw (10, 20, 23);
			{ int k = 0;
			  for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				w (0xB9400000u | ((uint32_t) k << 10) | (10u << 5) | 0u);              /* ldr w0, [x10, #4k] */
				if (i != 15) stw (0, OR (i));
				k++;
			  }
			  incm (OF (loop_loads), cnt);
			  addi (9, 23, 4 * (cnt - 1)); stw (9, OF (ld_addr)); stw (0, OF (ld_val)); }
			uint8_t *done = bb ();
			here (slow);
			for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				mov_r (1, 23); j_load (4, 0, dla);
				addi (23, 23, 4);
				if (i != 15) stw (0, OR (i));
			}
			here (done);
			if (list & 0x8000) {                                                    /* (w0: the new PC) */
				if (cond != 14) addm64 (OF (cycles), (int32_t) I->extra);
				j_branch_reg (ip, after, 1);
				break;
			}
		} else {
			/* fast: the whole range in RAM, no code in its pages, no exclusive monitor */
			addxi (9, 23, 4 * cnt); cmpx (9, 26); uint8_t *s1 = bc (HI);
			lsri (9, 23, 12); w (0x38604800u | (9u << 16) | (21u << 5) | 10u); uint8_t *s2 = cbz_ (10, 1, 0);
			addi (9, 23, 4 * cnt - 1); lsri (9, 9, 12); w (0x38604800u | (9u << 16) | (21u << 5) | 10u); uint8_t *s3 = cbz_ (10, 1, 0);
			ldw (10, OF (excl)); uint8_t *s4 = cbz_ (10, 1, 0);
			add_x_uxtw (10, 20, 23);
			{ int k = 0;
			  for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				if (i == 15) movw (0, ip + 8); else ldw (0, OR (i));
				w (0xB9000000u | ((uint32_t) k << 10) | (10u << 5) | 0u);              /* str w0, [x10, #4k] */
				k++;
			  }
			  incm (OF (loop_stores), cnt); }
			uint8_t *done = bb ();
			here (s1); here (s2); here (s3); here (s4);
			for (int i = 0; i < 16; i++) if ((list >> i) & 1) {
				mov_r (1, 23);
				if (i == 15) movw (0, ip + 8); else ldw (0, OR (i));
				j_store (4, dla);
				addi (23, 23, 4);
			}
			here (done);
			if (W) stw (24, OR (rn));
		}
		if (cond != 14) addm64 (OF (cycles), (int32_t) I->extra);
		break;
	}
	case K_B: {
		int32_t off = (int32_t) (ins << 8) >> 6; uint32_t t = ip + 8 + (uint32_t) off;
		if (ins & 0x01000000u) stiw (OR (14), ip + 4);
		int32_t adj = (cond != 14 ? 2 : 0) - (int32_t) after;
		if (t < ip && ip - t <= 64) {
			stiw (OR (15), t); addm64 (OF (cycles), adj); adj = 0;
			mov_x0_c (); call ((void *) jh_loop);
			uint8_t *go = cbz_ (0, 0, 0);
			b_to (j_exit);
			here (go);
		}
		j_link (t, adj);
		break;
	}
	case K_BX: {
		ldw (0, OR (rm));
		if (((ins >> 4) & 15) == 3) stiw (OR (14), ip + 4);
		j_branch_reg (ip, after, 1);
		break;
	}
	case K_CPNOP: break;
	case K_COP:                                        /* (the interpreter's copro, called in place: jh_cop - VFP, the counters) */
		addm64 (OF (cycles), -(int32_t) dl);
		movw (1, ins); movw (2, ip); mov_x0_c (); call ((void *) jh_cop);
		addm64 (OF (cycles), (int32_t) dl);
		break;
	case K_MRC: {
		unsigned rt = (ins >> 12) & 15;
		addm64 (OF (cycles), -(int32_t) dl);
		movw (1, ins); mov_x0_c (); call ((void *) jh_mrc);
		addm64 (OF (cycles), (int32_t) dl);
		stw (0, OR (rt));
		break;
	}
	}
	if (skip) here (skip);
	(void) rs;
}

/* translate the block at pc: its entry (code NULL when the first instruction is the interpreter's) */
static jent_t *jit_translate (arm11_t *c, uint32_t pc)
{
	jent_t *e = JT_SLOT (pc);
	jins_t L[JMAX]; int n = 0; int maxn = jt_short ? jit_xshort : jit_maxn;
	uint32_t pg = pc >> 12;
	const uint8_t *ram = c->sys->ram;
	j_ram_size = c->sys->ram_size; j_ram = c->sys->ram;
	for (uint32_t ip = pc; n < maxn && (ip >> 12) == pg && ip + 4 <= j_ram_size; ip += 4) {
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
	if (jfree + 64 * 1024 > jcode_end) { jit_flush (); e = JT_SLOT (pc); e->pc = pc; e->cost = 1; jpage[pg] = 1; }
	/* the static cycles: 1 each, and the extras of the unconditional ones */
	unsigned tot = 0, maxc = 0;
	for (int i = 0; i < n; i++) { unsigned s = 1 + ((L[i].ins >> 28) == 14 ? L[i].extra : 0); tot += s; maxc += 1 + L[i].extra; }
	unsigned acc = 0;
	for (int i = n - 1; i >= 0; i--) { L[i].after = acc; acc += 1 + ((L[i].ins >> 28) == 14 ? L[i].extra : 0); }
	j_wr_on ();
	E = jfree;
	uint8_t *code = E;
	addm64 (OF (cycles), (int32_t) tot);
	ncold = 0; nicold = 0;
	for (int i = 0; i < n; i++) {
		int mem = L[i].kind == K_LS || L[i].kind == K_LSX || L[i].kind == K_LDM || L[i].kind == K_MRC ||
		          (L[i].kind == K_COP && ((L[i].ins >> 25) & 7) == 6);   /* (VLDR / VSTR: a peripheral too) */
		j_emit (&L[i]);
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
	if (!jt_short) for (int i = 0; i < jlinks_n; i++) if (jlinks[i].target == pc) { jlink_patch (&jlinks[i], e); jlinks[i--] = jlinks[--jlinks_n]; }
	j_sync (code, E);
	j_wr_off ();
	return e;
}
