/* state_core.c - saving and restoring the running 6809 game whole, for run-ahead (live.c; Visual C++ builds only).
 *
 * The emulator's own files are compiled with all their data in two sections of their own (state_vtest.c, state_vecx.c,
 * state_e6809.c, state_e8910.c: .vxd initialised, .vxb zeroed). The linker sorts ".vxd$a" before every ".vxd$m" and
 * ".vxd$z" after them (the same for ".vxb"), so the marks here bound all of that data. A save copies those ranges less
 * the big buffers that are not the game's state (vxs_exclude); a load copies them back. The .vxb marks are
 * uninitialised statics, as the emulator's zeroed data is: an initialised one would land in a section of another kind
 * that the linker keeps apart. (The same scheme as the EmuVR core's save states, 2026-11-04.)
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#pragma data_seg(".vxd$a")
static char vxd_a = 1;
#pragma data_seg(".vxd$z")
static char vxd_z = 1;
#pragma data_seg()
#pragma bss_seg(".vxb$a")
static char vxb_a;
#pragma bss_seg(".vxb$z")
static char vxb_z;
#pragma bss_seg()
#define VXD_A ((unsigned char *) &vxd_a)
#define VXD_Z ((unsigned char *) &vxd_z + 1)
#define VXB_A ((unsigned char *) &vxb_a)
#define VXB_Z ((unsigned char *) &vxb_z + 1)
#elif defined(__clang__)
/* clang (the libretro core: Android, Linux): the sections vxd / vxb - ELF names without a dot, so the linker bounds each
 * with __start_ / __stop_ (Mach-O would need section$start$ names: not built yet) */
extern unsigned char __start_vxd[] __attribute__((weak, visibility ("hidden"))), __stop_vxd[] __attribute__((weak, visibility ("hidden")));
extern unsigned char __start_vxb[] __attribute__((weak, visibility ("hidden"))), __stop_vxb[] __attribute__((weak, visibility ("hidden")));
#define VXD_A __start_vxd
#define VXD_Z __stop_vxd
#define VXB_A __start_vxb
#define VXB_Z __stop_vxb
#else
#error "the save-state sections need Visual C++ or clang"
#endif

typedef struct { unsigned char *p; size_t n; } vxs_rng;
#define VXS_MAX 48
static vxs_rng ex[VXS_MAX], cp[VXS_MAX]; static int nex, ncp; static size_t raw;

void vxs_exclude (const void *p, size_t n)
{
	if (nex < VXS_MAX) { ex[nex].p = (unsigned char *) p; ex[nex].n = n; nex++; }
}
static int rng_cmp (const void *a, const void *b) { const vxs_rng *x = a, *y = b; return x->p < y->p ? -1 : x->p > y->p; }
static void cut (unsigned char *a, unsigned char *z)       /* (the range less the left-out buffers in it) */
{
	for (int i = 0; i < nex && a < z; i++) {
		unsigned char *e0 = ex[i].p, *e1 = e0 + ex[i].n;
		if (e1 <= a || e0 >= z) continue;
		if (e0 > a && ncp < VXS_MAX) { cp[ncp].p = a; cp[ncp].n = (size_t) (e0 - a); ncp++; }
		if (e1 > a) a = e1;
	}
	if (a < z && ncp < VXS_MAX) { cp[ncp].p = a; cp[ncp].n = (size_t) (z - a); ncp++; }
}

/* after the vxs_exclude calls: the bytes a save takes */
size_t vxs_init (void)
{
	extern void vxs_excl_vecx (void (*add) (const void *, size_t));
	vxs_excl_vecx (vxs_exclude);
	qsort (ex, nex, sizeof (vxs_rng), rng_cmp);
	ncp = 0;
	if (VXD_A && VXD_Z > VXD_A) cut (VXD_A, VXD_Z);
	if (VXB_A && VXB_Z > VXB_A) cut (VXB_A, VXB_Z);
	raw = 0; for (int i = 0; i < ncp; i++) raw += cp[i].n;
	return raw;
}
/* the copies: every range is copied whole by memcpy, so the bookkeeping here (in no section) is never overwritten */
void vxs_save (unsigned char *o) { for (int i = 0; i < ncp; i++) { memcpy (o, cp[i].p, cp[i].n); o += cp[i].n; } }
void vxs_load (const unsigned char *s) { for (int i = 0; i < ncp; i++) { memcpy (cp[i].p, s, cp[i].n); s += cp[i].n; } }
