/* host.c - the few things about the host machine the emulator asks for: which processors to run its busy
 * threads on.
 *
 * Two busy threads (the UVMC2's core 0 beside the rest, rp2350.c) run far slower when they share one physical
 * core (the two hardware threads of an SMT core compete for its units): measured on a Ryzen, 0.6x against 1.1x
 * of real time. So the pair is pinned to two different physical cores - the fastest kind on hybrid CPUs - and
 * not the first one (where the system's interrupts go).
 *
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <string.h>
#include "host.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

/* the first hardware thread of each physical core, the fastest cores first (none for one core or an error) */
static int cores (uint64_t *first, int max)
{
	DWORD len = 0;
	GetLogicalProcessorInformationEx (RelationProcessorCore, NULL, &len);
	if (!len) return 0;
	char *buf = (char *) malloc (len);
	if (!buf) return 0;
	int n = 0; BYTE eff[64];
	if (GetLogicalProcessorInformationEx (RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) buf, &len)) {
		for (DWORD off = 0; off < len && n < max && n < 64; ) {
			PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX p = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) (buf + off);
			if (p->Relationship == RelationProcessorCore && p->Processor.GroupCount >= 1 && p->Processor.GroupMask[0].Group == 0) {
				KAFFINITY m = p->Processor.GroupMask[0].Mask;
				if (m) { first[n] = (uint64_t) (m & (~m + 1)); eff[n] = p->Processor.EfficiencyClass; n++; }
			}
			off += p->Size;
		}
	}
	free (buf);
	/* the fastest kind first (EfficiencyClass: higher is faster), keeping the order otherwise */
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && eff[j] > eff[j - 1]; j--) {
			uint64_t t = first[j]; first[j] = first[j - 1]; first[j - 1] = t;
			BYTE e = eff[j]; eff[j] = eff[j - 1]; eff[j - 1] = e;
		}
	return n;
}
int host_pin (int which)
{
	static uint64_t first[64]; static int n = -1;
	if (n < 0) n = cores (first, 64);
	if (n < 3) return 0;                       /* (two cores or fewer: the system knows best) */
	/* the fastest cores but the very first: which 0 and 1 on two different ones */
	int k = 1 + (which & 1);
	if (k >= n) return 0;
	return SetThreadAffinityMask (GetCurrentThread (), (DWORD_PTR) first[k]) != 0;
}

#elif defined(__linux__)
#include <sched.h>
#include <stdio.h>
#include <pthread.h>

/* the first hardware thread of each physical core (from sysfs: its lowest sibling) */
static int cores (int *first, int max)
{
	int n = 0;
	for (int cpu = 0; cpu < 1024 && n < max; cpu++) {
		char path[128]; snprintf (path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
		FILE *f = fopen (path, "r");
		if (!f) { if (cpu > 0) break; else return 0; }
		int lo = -1; if (fscanf (f, "%d", &lo) != 1) lo = -1;
		fclose (f);
		if (lo == cpu) first[n++] = cpu;
	}
	return n;
}
int host_pin (int which)
{
	static int first[256]; static int n = -1;
	if (n < 0) n = cores (first, 256);
	if (n < 3) return 0;
	int k = 1 + (which & 1);
	cpu_set_t s; CPU_ZERO (&s); CPU_SET (first[k], &s);
	return pthread_setaffinity_np (pthread_self (), sizeof s, &s) == 0;
}

#else
int host_pin (int which) { (void) which; return 0; }    /* (macOS: no SMT on Apple chips; the system places threads) */
#endif
