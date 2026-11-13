/* veclink.c - the VecLink cable (Alex Herbert's, V2: controller port 2 to port 2, pins 1, 2, 3 and 8 straight through)
 * between two Vectrex_EMU cores, each its own process - two consoles, two windows (included by vecx.c).
 *
 * The wires: port 2's buttons 1-3 = the AY's port A bits 4-6. A console drives them when port A is an output (register
 * 7 bit 6): a 0 pulls the wire low, a 1 leaves it to the pull-ups. So each wire is low when either console drives a 0
 * (or a button on it is pressed): wired-AND, and a console reads the wire itself, also while its port is an output
 * (Alex's V2 routines keep port A an output on both sides for the whole game and still read the other's bits).
 *
 * Exact to the cycle: both cores keep their own clock (CPU cycles since the link opened). A core publishes how far it
 * has run, and every change of what it drives with the cycle it happened. When a core reads port A it first waits until
 * the other has run at least that far, then reads the other's value at that very cycle - so whatever the two host
 * threads do, both consoles see the wires exactly as two real consoles running side by side would. The two clocks start
 * together: opening the link waits (up to 15 s) for the other side.
 *
 * The cable is a small file both cores map (VT_VECLINK=<file>,<A|B>, written by the frontend). A side that closes or
 * stops (no progress for half a second) leaves its wires high, as an unplugged cable.
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <stdint.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#endif

#define VL_MAGIC 0x564C4E4Bu                /* "VLNK" */
#define VL_RING 65536                       /* changes kept per side (a byte at 9600 baud is ~12 changes) */
typedef struct {
	volatile uint64_t t;                    /* how far this side has run (cycles since the link opened) */
	volatile uint32_t head;                 /* changes written */
	volatile uint32_t alive;                /* 1 while the core runs; 0 when it ended */
	volatile uint64_t beat;                 /* (bumped while it runs: a side that stops counting is gone) */
	uint64_t pad[5];
	struct { uint64_t t; uint32_t v; uint32_t pad; } ring[VL_RING];   /* (t, the 3 wires it drives: bits 0-2 = buttons 1-3) */
} vl_side_t;
typedef struct { volatile uint32_t magic; volatile uint32_t pad; uint64_t pad2[7]; vl_side_t s[2]; } vl_shm_t;

int vt_veclink;                             /* the cable is in */
static vl_shm_t *vl;                        /* (outside the save-state sections: a pointer, set once) */
static int vl_me;                           /* 0 = A, 1 = B */
static uint64_t vl_t0;                      /* alg_cycles when the link opened (B: less its head start) */
static unsigned vl_drive = 7;               /* what this side drives now (1 = released) */
static uint64_t vl_pub;
static FILE *vl_dbg; static long vl_dbg_n;   /* (VT_VLDEBUG=file: the first events of this side) */                     /* (the last time published) */
extern unsigned long long alg_cycles;

static void vl_yield (void)
{
#ifdef _WIN32
	SwitchToThread ();
#else
	sched_yield ();
#endif
}
static double vl_now (void)
{
#ifdef _WIN32
	static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency (&f);
	QueryPerformanceCounter (&c); return (double) c.QuadPart / (double) f.QuadPart;
#else
	struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

/* the link from VT_VECLINK: map the file, mark this side alive, wait for the other, start both clocks */
static void vl_open (void)
{
	const char *z = getenv ("VT_VECLINK");
	if (!z || !*z) return;
	char path[1024]; const char *comma = strrchr (z, ',');
	if (!comma || (size_t) (comma - z) >= sizeof path) return;
	memcpy (path, z, (size_t) (comma - z)); path[comma - z] = 0;
	vl_me = (comma[1] == 'B' || comma[1] == 'b') ? 1 : 0;
	size_t n = sizeof (vl_shm_t);
#ifdef _WIN32
	HANDLE f = CreateFileA (path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) { fprintf (stderr, "VECLINK: cannot open %s\n", path); return; }
	HANDLE m = CreateFileMappingA (f, NULL, PAGE_READWRITE, 0, (DWORD) n, NULL);
	if (!m) { fprintf (stderr, "VECLINK: cannot map %s\n", path); return; }
	vl = (vl_shm_t *) MapViewOfFile (m, FILE_MAP_ALL_ACCESS, 0, 0, n);
#else
	int fd = open (path, O_RDWR | O_CREAT, 0644);
	if (fd < 0 || ftruncate (fd, (off_t) n) != 0) { fprintf (stderr, "VECLINK: cannot open %s\n", path); return; }
	void *p = mmap (NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	vl = p == MAP_FAILED ? NULL : (vl_shm_t *) p;
#endif
	if (!vl) { fprintf (stderr, "VECLINK: cannot map %s\n", path); return; }
	vl_side_t *s = &vl->s[vl_me], *o = &vl->s[vl_me ^ 1];
	s->alive = 0; s->head = 0; s->t = 0;
	s->ring[0].t = 0; s->ring[0].v = 7; s->head = 1;
	vl->magic = VL_MAGIC;
	s->alive = 1;
	double t0 = vl_now ();
	while (!o->alive && vl_now () - t0 < 15.0) { s->beat++;
#ifdef _WIN32
		Sleep (2);
#else
		usleep (2000);
#endif
	}
	vl_t0 = alg_cycles; vl_drive = 7; vl_pub = 0;
	if (o->alive && o->t > 100000)          /* (the other console was already running - this one changed its game or
		                                       was switched off and on: it joins at the other's time, as a console
		                                       plugged into a running one) */
		vl_t0 = alg_cycles - o->t;
	else if (vl_me) {                            /* (two real consoles never start in the same cycle: B is switched on a few
		                                       thousand cycles before A - from the power-on seed - or the two run the
		                                       same code in step and both claim the link at once) */
		const char *z2 = getenv ("VT_POWERSEED"); unsigned long sd = z2 ? strtoul (z2, 0, 10) : 12345;
		vl_t0 -= 1000 + (sd * 2654435761u >> 7) % 20000;
	}
	vt_veclink = 1;
	{ const char *d = getenv ("VT_VLDEBUG"); if (d) { char fn[1100]; snprintf (fn, sizeof fn, "%s.%c.txt", d, vl_me ? 'B' : 'A'); vl_dbg = fopen (fn, "w"); } }
	fprintf (stderr, "VECLINK: side %c, %s\n", vl_me ? 'B' : 'A', o->alive ? "the other console is connected" : "waiting for the other console");
}

static void vl_close (void)
{
	if (vl) { vl->s[vl_me].alive = 0; vl->s[vl_me].t = ~0ull >> 1; }
}

static inline uint64_t vl_time (void) { return alg_cycles - vl_t0; }

/* how far this console has run, published (often enough that the other never waits long) */
static inline void vl_tick (void)
{
	if (!vt_veclink) return;
	uint64_t t = vl_time ();
	if (t - vl_pub >= 16) { vl_pub = t; vl->s[vl_me].t = t ? t - 1 : 0; vl->s[vl_me].beat++; }   /* (t - 1: a change can
	                                           still come in this very cycle - what is published is final) */
}

/* what this console drives on the wires changed (port A written, or its direction): with the cycle it happened */
static void vl_set (unsigned drive)
{
	drive &= 7;
	if (!vt_veclink || drive == vl_drive) return;
	vl_side_t *s = &vl->s[vl_me];
	uint64_t t = vl_time ();
	uint32_t h = s->head;
	s->ring[h % VL_RING].t = t; s->ring[h % VL_RING].v = drive;
#if defined(_MSC_VER)
	MemoryBarrier ();
#else
	__sync_synchronize ();
#endif
	s->head = h + 1;
	vl_drive = drive;
	if (vl_dbg && vl_dbg_n++ < 20000) fprintf (vl_dbg, "%llu set %u\n", (unsigned long long) t, drive);
}

/* the wires as this console reads them now: 3 bits, 0 = low. Waits for the other console to reach this cycle. */
static unsigned vl_wires (void)
{
	if (!vt_veclink) return 7;
	vl_side_t *s = &vl->s[vl_me], *o = &vl->s[vl_me ^ 1];
	uint64_t t = vl_time ();
	s->t = t; vl_pub = t;                   /* (the whole cycle: a read is this console's only access in it - and two
	                                           consoles reading in the same cycle must not wait for each other) */
	if (o->alive) {
		uint64_t beat = o->beat; double since = 0; unsigned spins = 0;
		while (o->alive && o->t < t) {
			if (++spins > 64) vl_yield ();
			if ((spins & 1023) == 0) {        /* (the other stopped: paused, closed - its wires go high) */
				if (o->beat != beat) { beat = o->beat; since = 0; }
				else if (!since) since = vl_now ();
				else if (vl_now () - since > 0.5) break;
			}
		}
	}
	unsigned v = 7;
#if defined(_MSC_VER)
	MemoryBarrier ();
#else
	__sync_synchronize ();
#endif
	if (o->alive && o->t >= t) {
		uint32_t h = o->head;
#if defined(_MSC_VER)
		MemoryBarrier ();
#else
		__sync_synchronize ();
#endif
		for (uint32_t k = h; k > 0 && h - k < VL_RING; k--) {     /* (the newest change at or before t) */
			if (o->ring[(k - 1) % VL_RING].t <= t) { v = o->ring[(k - 1) % VL_RING].v; break; }
		}
	}
	if (vl_dbg && vl_dbg_n++ < 20000) fprintf (vl_dbg, "%llu read other=%u mine=%u other_t=%llu alive=%u\n", (unsigned long long) t, v, vl_drive, (unsigned long long) o->t, o->alive);
	return v & vl_drive;
}

/* (for live.c) */
void vecx_veclink_open (void) { vl_open (); }
void vecx_veclink_close (void) { vl_close (); }
