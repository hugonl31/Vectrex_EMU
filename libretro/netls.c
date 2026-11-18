/* netls.c - Vectrex_EMU's libretro core: netplay for UVMC2 and PiTrex games in lockstep, the desktop's way (netplay.py).
 * Their boards (a whole RP2350, a Raspberry Pi with 256 MB) are too big for RetroArch's netplay, which saves and loads
 * the state every frame: for these games the core takes the network over (RetroArch's netpacket interface, set in
 * retro_load_game for a .um2 / .img / .pit) and both sides run the same game from the same power-on, only each game
 * frame's controllers sent - 4 bytes. The 6809 games keep RetroArch's own netplay (rollback, save states).
 *   The host (player 1) waits; who joins (player 2) says hello: its UVMC2 mode, processor type, game and SD card files.
 * The host answers with the game's settings - the power-on seed, the UVMC2 mode both play in (the slower one's: Dev <
 * Fast < Exact, the desktop's uvm2_agree), the PiTrex JIT fast only between the same processor types, its R302 DAC
 * offset (it moves the joystick's comparator), the cartridge hardware, the intro - and the card files both have alike.
 * Both switch on at once with those, the card built from those files only, at one fixed time, nothing written back.
 *   Each game frame's input runs LS_DELAY frames after it was read; every packet repeats the last LS_KEEP inputs; a
 * frame runs only once the other side's input for it is there (the game waits - the picture stands). Every LS_CHECK
 * frames both compare a checksum of the console's RAM: out of step is said on the screen. RetroArch's Reset is sent as
 * an input (both reset in the same frame). Included by libretro_vx.c.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#ifndef RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE        /* (libretro.h of RetroArch 1.17 and later) */
#define RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE 78
#define RETRO_NETPACKET_UNRELIABLE 0
#define RETRO_NETPACKET_RELIABLE (1 << 0)
#define RETRO_NETPACKET_UNSEQUENCED (1 << 1)
#define RETRO_NETPACKET_FLUSH_HINT (1 << 2)
#define RETRO_NETPACKET_BROADCAST 0xFFFF
typedef void (RETRO_CALLCONV *retro_netpacket_send_t) (int flags, const void *buf, size_t len, uint16_t client_id);
typedef void (RETRO_CALLCONV *retro_netpacket_poll_receive_t) (void);
typedef void (RETRO_CALLCONV *retro_netpacket_start_t) (uint16_t client_id, retro_netpacket_send_t send_fn, retro_netpacket_poll_receive_t poll_receive_fn);
typedef void (RETRO_CALLCONV *retro_netpacket_receive_t) (const void *buf, size_t len, uint16_t client_id);
typedef void (RETRO_CALLCONV *retro_netpacket_stop_t) (void);
typedef void (RETRO_CALLCONV *retro_netpacket_poll_t) (void);
typedef bool (RETRO_CALLCONV *retro_netpacket_connected_t) (uint16_t client_id);
typedef void (RETRO_CALLCONV *retro_netpacket_disconnected_t) (uint16_t client_id);
struct retro_netpacket_callback {
	retro_netpacket_start_t start; retro_netpacket_receive_t receive; retro_netpacket_stop_t stop; retro_netpacket_poll_t poll;
	retro_netpacket_connected_t connected; retro_netpacket_disconnected_t disconnected; const char *protocol_version;
};
#endif

#define LS_PROTO "vectrex_emu-ls1"   /* (both sides must speak the same: raise with any change to these packets or the core's emulation) */
#define LS_DELAY 4                   /* game frames of input delay (80 ms at 50 Hz: a round trip without a stall) */
#define LS_KEEP 16                   /* inputs repeated in each packet */
#define LS_RING 512                  /* (inputs kept per player: frames) */
#define LS_CHECK 25                  /* frames between two checks of the RAM */
#define LS_WAIT_MS 6                 /* (a refresh waits this long at most for a late input, then shows the picture as it was) */

typedef struct { char rel[256]; long long size; uint32_t crc; } ls_file;
static int ls_avail;                 /* the interface is RetroArch's: this game plays online in lockstep */
static int ls_role;                  /* 0: none, 1: host, 2: joined */
static int ls_state;                 /* 0: alone, 1: setting up, 2: playing in step */
static int ls_me;                    /* this side's player: 0 (the host, player 1) or 1 */
static uint16_t ls_peer = 0xFFFF;    /* (the host: the player 2's client number) */
static retro_netpacket_send_t ls_send; static retro_netpacket_poll_receive_t ls_rx;
static unsigned char ls_in[2][LS_RING][4]; static uint32_t ls_have[2];   /* (player p's inputs: frames below ls_have[p] known) */
static uint32_t ls_f;                /* the next game frame to run */
static unsigned long long ls_cyc0; static int ls_solo;   /* (total_cycles when the online game began) */
static uint32_t ls_crc[2][16][2]; static int ls_warned;  /* (the RAM checks: [side][slot] - frame, checksum) */
/* (ls_reset_req, libretro_vx.c: RetroArch's Reset - in this side's next input) */
static unsigned char ls_pad[3] = { 128, 128, 0 };   /* (this side's controller now: retro_run reads it) */
/* the game's settings both play with (the host's): netls_env uses them while ls_cfg_on */
static int ls_cfg_on; static uint32_t ls_seed; static int ls_mode, ls_jitfast, ls_intro_skip; static double ls_dacofs; static char ls_carthw[48];
static ls_file *ls_card; static int ls_ncard;          /* (this side's card files) */
static char **ls_only; static int ls_nonly;            /* (the files both have alike) */
static unsigned char *ls_hello; static size_t ls_hello_n, ls_hello_cap;   /* (the host: the joiner's hello, in pieces) */
static int ls_peer_ok; static unsigned char *ls_hello_wait; static size_t ls_hello_wait_n;   /* (the host: a hello before RetroArch lets player 2 play - answered then) */

static void ls_log (const char *s) { fprintf (stderr, "[vectrex_emu] netplay: %s\n", s); }   /* (what the player did: the log only) */
static void ls_say (const char *s, int frames) { struct retro_message m = { s, (unsigned) frames }; env_cb (RETRO_ENVIRONMENT_SET_MESSAGE, &m); fprintf (stderr, "[vectrex_emu] netplay: %s\n", s); }
static const char *ls_arch (void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
	return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
	return "x64";
#else
	return "other";
#endif
}
static int ls_mode_now (void) { const char *um = optv ("vectrex_emu_uvm2_mode"); return !strcmp (um, "dev") ? 2 : !strcmp (um, "exact") ? 0 : 1; }   /* (0 exact, 1 fast, 2 dev) */
static const char *ls_folder (void)                   /* (the card folder of this game: <system>/vectrex_emu/uvmc2 or pitrex) */
{
	static char fo[1800]; const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
	const char *e = strrchr (cur_game, '.'); int px = e && (!vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit"));
	snprintf (fo, sizeof fo, "%s" SEP "vectrex_emu" SEP "%s", sys ? sys : ".", px ? "pitrex" : "uvmc2"); return fo;
}
static void ls_scan (const char *path, const char *rel)    /* (the card folder's files: their paths, sizes, CRCs) */
{
	sdc_ent *v; int n = sdc_list (path, &v);
	for (int i = 0; i < n; i++) { char p[2048], r[1024]; snprintf (p, sizeof p, "%s" SEP "%s", path, v[i].name); snprintf (r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", v[i].name);
		if (v[i].dir) { ls_scan (p, r); continue; }
		if (strlen (r) > 250) continue;
		uint32_t c = 0xFFFFFFFFu; FILE *f = fopen (p, "rb"); if (!f) continue;
		unsigned char *b = malloc (1 << 20); size_t k; while (b && (k = fread (b, 1, 1 << 20, f)) > 0) c = crc32_up (c, b, k); free (b); fclose (f);
		ls_card = realloc (ls_card, sizeof (ls_file) * (size_t) (ls_ncard + 1)); ls_file *x = &ls_card[ls_ncard++];
		snprintf (x->rel, sizeof x->rel, "%s", r); x->size = v[i].size; x->crc = ~c; }
	free (v);
}
static void ls_card_list (void) { free (ls_card); ls_card = NULL; ls_ncard = 0; char **so = sdc_only; sdc_only = NULL; ls_scan (ls_folder (), ""); sdc_only = so; }

/* ---- the packets: a letter, then its fields (little-endian) */
static void ls_put (int flags, const unsigned char *b, size_t n) { if (ls_send) ls_send (flags | RETRO_NETPACKET_RELIABLE, b, n, ls_role == 1 ? ls_peer : 0); }
static void put32 (unsigned char *p, uint32_t v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); }
static uint32_t get32 (const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }
static size_t ls_files_out (unsigned char *o, const ls_file *v, int n)   /* (a list of files: count, then each - path, size, CRC) */
{
	size_t k = 0; put32 (o, (uint32_t) n); k += 4;
	for (int i = 0; i < n; i++) { size_t l = strlen (v[i].rel); o[k++] = (unsigned char) l; memcpy (o + k, v[i].rel, l); k += l;
		put32 (o + k, (uint32_t) v[i].size); put32 (o + k + 4, (uint32_t) ((unsigned long long) v[i].size >> 32)); put32 (o + k + 8, v[i].crc); k += 12; }
	return k;
}
/* (big messages - a card's list - go in pieces of 4000 bytes: 'P', then the last piece 'Q' with the message's letter) */
static void ls_big (unsigned char letter, const unsigned char *b, size_t n)
{
	unsigned char pk[4100];
	while (n > 4000) { pk[0] = 'P'; memcpy (pk + 1, b, 4000); ls_put (0, pk, 4001); b += 4000; n -= 4000; }
	pk[0] = 'Q'; pk[1] = letter; memcpy (pk + 2, b, n); ls_put (RETRO_NETPACKET_FLUSH_HINT, pk, n + 2);
}

static uint32_t ls_game_crc (long long *size)          /* (the game file's CRC: both must have loaded the same) */
{
	FILE *f = fopen (cur_game, "rb"); uint32_t c = 0xFFFFFFFFu; long long n = 0; if (!f) { *size = -1; return 0; }
	unsigned char *b = malloc (1 << 20); size_t k; while (b && (k = fread (b, 1, 1 << 20, f)) > 0) { c = crc32_up (c, b, k); n += (long long) k; }
	free (b); fclose (f); *size = n; return ~c;
}
static void ls_hello_send (void)                        /* (who joins: its mode, processor, game, card) */
{
	ls_card_list ();
	size_t cap = 64 + (size_t) ls_ncard * 272; unsigned char *b = malloc (cap); size_t k = 0;
	b[k++] = (unsigned char) ls_mode_now (); b[k++] = (unsigned char) strlen (ls_arch ()); memcpy (b + k, ls_arch (), strlen (ls_arch ())); k += strlen (ls_arch ());
	long long gs; uint32_t gc = ls_game_crc (&gs); put32 (b + k, gc); put32 (b + k + 4, (uint32_t) gs); k += 8;
	k += ls_files_out (b + k, ls_card, ls_ncard);
	ls_big ('H', b, k); free (b);
	ls_log ("joined - waiting for the host's game");
}

/* ---- both: the game switched on with the agreed settings, in step from frame 0 */
static void ls_begin (void)
{
	memset (ls_in, 0, sizeof ls_in);
	for (int p = 0; p < 2; p++) for (int f = 0; f < LS_RING; f++) { ls_in[p][f][0] = ls_in[p][f][1] = 128; }
	ls_have[0] = ls_have[1] = LS_DELAY; ls_f = 0; ls_warned = 0; ls_reset_req = 0; memset (ls_crc, 0, sizeof ls_crc);
	ls_cfg_on = 1;
	static char *none_[1] = { "" }; sdc_only = ls_nonly ? ls_only : none_; sdc_nonly = ls_nonly;   /* (none alike: an empty card) */ sdc_fixed = 1735689600;   /* (the card's files: 2025-01-01 00:00 UTC, as the desktop's netplay_card_time) */
	int ok = game_switch_now (cur_game);
	sdc_only = NULL; sdc_nonly = 0; sdc_fixed = 0;
	if (!ok) { ls_state = 0; ls_cfg_on = 0; ls_say ("the game could not be started", 240); return; }
	ls_cyc0 = total_cycles;
	ls_state = 2;
	const char *e = strrchr (cur_game, '.'); const int um = e && !vx_stricmp (e, ".um2");
	char m[160]; snprintf (m, sizeof m, "playing online as player %d%s%s%s", ls_me + 1, um ? (ls_mode == 2 ? " - UVMC2 mode Dev" : ls_mode == 1 ? " - UVMC2 mode Fast" : " - UVMC2 mode Exact") : "",
	                       um || ls_jitfast ? "" : " - PiTrex exact (two processor types)", ls_nonly ? " - SD card: the files both have" : "");
	ls_log (m);
}
static int ls_have_file (const char *rel, long long size, uint32_t crc)
{
	for (int i = 0; i < ls_ncard; i++) if (!strcmp (ls_card[i].rel, rel)) return ls_card[i].size == size && ls_card[i].crc == crc;
	return 0;
}
static void ls_only_free (void) { for (int i = 0; i < ls_nonly; i++) free (ls_only[i]); free (ls_only); ls_only = NULL; ls_nonly = 0; }
static const unsigned char *ls_files_in (const unsigned char *i, const unsigned char *e, int keep_common)   /* (a list read: the files this side
                                                    has alike kept in ls_only) */
{
	if (i + 4 > e) return e; uint32_t n = get32 (i); i += 4; ls_only_free ();
	for (uint32_t k = 0; k < n && i < e; k++) { unsigned l = *i++; if (i + l + 12 > e) break; char rel[260]; memcpy (rel, i, l); rel[l] = 0; i += l;
		long long size = (long long) get32 (i) | (long long) get32 (i + 4) << 32; uint32_t crc = get32 (i + 8); i += 12;
		if (!keep_common || ls_have_file (rel, size, crc)) { ls_only = realloc (ls_only, sizeof (char *) * (size_t) (ls_nonly + 1)); ls_only[ls_nonly++] = strdup (rel); } }
	return i;
}
static void ls_on_hello (const unsigned char *b, size_t n)   /* (the host: the joiner's hello - the game's settings back, both start) */
{
	const unsigned char *e = b + n, *i = b; if (n < 2) return;
	int mode = i[0]; unsigned al = i[1]; i += 2; char arch[32] = ""; if (al < sizeof arch && i + al <= e) { memcpy (arch, i, al); arch[al] = 0; } i += al;
	if (i + 8 > e) return; uint32_t gc = get32 (i), gs = get32 (i + 4); i += 8;
	long long my_s; uint32_t my_c = ls_game_crc (&my_s);
	if (gc != my_c || (long long) gs != my_s) { unsigned char x = 'X'; ls_put (0, &x, 1); ls_say ("player 2 has another version of this game", 300); return; }
	ls_card_list (); ls_files_in (i, e, 1);              /* (the files both have alike) */
	ls_seed = (uint32_t) time (NULL) * 2654435761u ^ (uint32_t) rand (); ls_seed &= 0x7FFFFFFF;
	ls_mode = ls_mode_now (); if (mode > ls_mode) ls_mode = mode;   /* (the slower side's) */
	ls_jitfast = !strcmp (arch, ls_arch ());
	ls_intro_skip = opt_is ("vectrex_emu_skip_intro", "on");
	{ const char *z = getenv ("VT_DACOFS"); ls_dacofs = z ? atof (z) : 0; }
	{ const char *z = getenv ("VT_CARTHW"); snprintf (ls_carthw, sizeof ls_carthw, "%s", z ? z : "1"); }
	/* the game's settings: 'G', then the only list (files) */
	size_t cap = 128 + (size_t) ls_nonly * 272; unsigned char *o = malloc (cap); size_t k = 0;
	put32 (o + k, ls_seed); k += 4; o[k++] = (unsigned char) ls_mode; o[k++] = (unsigned char) ls_jitfast; o[k++] = (unsigned char) ls_intro_skip;
	memcpy (o + k, &ls_dacofs, 8); k += 8; o[k++] = (unsigned char) strlen (ls_carthw); memcpy (o + k, ls_carthw, strlen (ls_carthw)); k += strlen (ls_carthw);
	ls_file *on = calloc ((size_t) ls_nonly + 1, sizeof (ls_file));
	for (int q = 0; q < ls_nonly; q++) for (int j = 0; j < ls_ncard; j++) if (!strcmp (ls_card[j].rel, ls_only[q])) { on[q] = ls_card[j]; break; }
	k += ls_files_out (o + k, on, ls_nonly); free (on);
	ls_big ('G', o, k); free (o);
	ls_me = 0; ls_begin ();
}
static void ls_on_game (const unsigned char *b, size_t n)    /* (who joined: the host's settings - start) */
{
	const unsigned char *e = b + n, *i = b; if (n < 16) return;
	ls_seed = get32 (i); i += 4; ls_mode = i[0]; ls_jitfast = i[1]; ls_intro_skip = i[2]; i += 3; memcpy (&ls_dacofs, i, 8); i += 8;
	unsigned cl = *i++; if (cl >= sizeof ls_carthw || i + cl > e) return; memcpy (ls_carthw, i, cl); ls_carthw[cl] = 0; i += cl;
	ls_card_list (); ls_files_in (i, e, 0);
	ls_me = 1; ls_begin ();
}

/* ---- the inputs */
static void ls_send_inputs (void)
{
	unsigned char b[8 + 4 * LS_KEEP]; uint32_t hi = ls_have[ls_me], lo = hi > LS_KEEP ? hi - LS_KEEP : 0;
	b[0] = 'I'; b[1] = (unsigned char) ls_me; put32 (b + 2, lo); b[6] = (unsigned char) (hi - lo);
	for (uint32_t f = lo; f < hi; f++) memcpy (b + 7 + 4 * (f - lo), ls_in[ls_me][f % LS_RING], 4);
	ls_put (RETRO_NETPACKET_FLUSH_HINT, b, 7 + 4 * (hi - lo));
}
static void ls_on_inputs (const unsigned char *b, size_t n)
{
	if (n < 6) return; int p = b[0] & 1; if (p == ls_me) return; uint32_t lo = get32 (b + 1); unsigned c = b[5];
	if (n < 6 + 4 * (size_t) c) return;
	for (unsigned k = 0; k < c; k++) { uint32_t f = lo + k; if (f < ls_have[p]) continue; if (f != ls_have[p]) break;
		if (f >= ls_f + LS_RING - 1) break; memcpy (ls_in[p][f % LS_RING], b + 6 + 4 * k, 4); ls_have[p] = f + 1; }
}
static uint32_t ls_ram_crc (void)                     /* (as the desktop's online check, live.c: the RAM, the cycles, the cartridge's processor) */
{
	extern unsigned char *vx_ram; extern int vt_uvm2, vt_pitrex; extern uint32_t uvm2_sig (void), pitrex_sig (void);
	uint32_t s = (vx_ram ? ~crc32_up (0xFFFFFFFFu, vx_ram, 1024) : 0) ^ (uint32_t) (total_cycles - ls_cyc0) * 2654435761u;   /* (the cycles since the
	                                              online game began: the count goes on across games here) */
	if (vt_uvm2) s ^= uvm2_sig (); else if (vt_pitrex) s ^= pitrex_sig ();
	return s;
}
static void ls_check (int side, uint32_t f, uint32_t c)   /* (a RAM check, this side's or the other's: compared once both are there) */
{
	unsigned k = (f / LS_CHECK) % 16; ls_crc[side][k][0] = f; ls_crc[side][k][1] = c;
	if (ls_solo) { fprintf (stderr, "[netls] check %u %08x\n", (unsigned) f, (unsigned) c); return; }
	if (ls_warned || ls_crc[side ^ 1][k][0] != f) return;
	if (ls_crc[side ^ 1][k][1] != c) { ls_warned = 1; char m[120]; snprintf (m, sizeof m, "out of step at game frame %u - the two games differ now", (unsigned) f); ls_say (m, 600); }
	else if (getenv ("VXPOC_NETDBG")) fprintf (stderr, "[netls] frame %u: the same (%08x)\n", (unsigned) f, (unsigned) c);
}
static void ls_on_check (const unsigned char *b, size_t n) { if (n >= 8 && get32 (b)) ls_check (1, get32 (b), get32 (b + 4)); }
/* ---- RetroArch's calls */
static void RETRO_CALLCONV ls_start (uint16_t id, retro_netpacket_send_t s, retro_netpacket_poll_receive_t r)
{
	ls_send = s; ls_rx = r; ls_state = 0; ls_hello_n = 0;
	if (id == 0) { ls_role = 1; ls_peer = 0xFFFF; ls_peer_ok = 0; ls_log ("hosting - the game starts again when player 2 joins"); }
	else { ls_role = 2; ls_state = 1; ls_hello_send (); }
}
static void RETRO_CALLCONV ls_receive (const void *buf, size_t len, uint16_t id)
{
	const unsigned char *b = buf; if (!len) return;
	if (getenv ("VXPOC_NETDBG") && b[0] != 'I') fprintf (stderr, "[netls] got %c (%u bytes) from %u, peer %u role %d\n", b[0], (unsigned) len, (unsigned) id, (unsigned) ls_peer, ls_role);
	if (ls_role == 1) { if (ls_peer == 0xFFFF) { ls_peer = id; ls_state = 1; } else if (id != ls_peer) return; }   /* (its first packet may come before
	                                              RetroArch says it plays: connected) */
	switch (b[0]) {
	case 'P': if (ls_hello_n + len > ls_hello_cap) { ls_hello_cap = (ls_hello_n + len) * 2; ls_hello = realloc (ls_hello, ls_hello_cap); }
	          memcpy (ls_hello + ls_hello_n, b + 1, len - 1); ls_hello_n += len - 1; break;
	case 'Q': if (len < 2) break;
	          if (ls_hello_n + len > ls_hello_cap) { ls_hello_cap = (ls_hello_n + len) * 2; ls_hello = realloc (ls_hello, ls_hello_cap); }
	          memcpy (ls_hello + ls_hello_n, b + 2, len - 2); ls_hello_n += len - 2;
	          if (b[1] == 'H' && ls_role == 1) { if (ls_peer_ok) ls_on_hello (ls_hello, ls_hello_n);
	                                             else { free (ls_hello_wait); ls_hello_wait = malloc (ls_hello_n + 1); memcpy (ls_hello_wait, ls_hello, ls_hello_n); ls_hello_wait_n = ls_hello_n; } }
	          else if (b[1] == 'G' && ls_role == 2) ls_on_game (ls_hello, ls_hello_n);
	          ls_hello_n = 0; break;
	case 'I': if (ls_state == 2) ls_on_inputs (b + 1, len - 1); break;
	case 'C': if (ls_state == 2) ls_on_check (b + 1, len - 1); break;
	case 'X': ls_state = 0; ls_cfg_on = 0; ls_say ("the other side left", 240); break;
	}
}
static void RETRO_CALLCONV ls_stop (void) { ls_send = NULL; ls_rx = NULL; ls_role = 0; ls_cfg_on = 0; if (ls_state) ls_log ("netplay ended - playing alone"); ls_state = 0; }
static bool RETRO_CALLCONV ls_connected (uint16_t id)
{
	if (getenv ("VXPOC_NETDBG")) fprintf (stderr, "[netls] connected %u (peer %u)\n", (unsigned) id, (unsigned) ls_peer);
	if (ls_peer != 0xFFFF && ls_peer != id) return false;      /* (two players: one joins) */
	ls_peer = id; ls_peer_ok = 1; if (!ls_state) ls_state = 1;
	if (ls_hello_wait) { unsigned char *h = ls_hello_wait; ls_hello_wait = NULL; ls_on_hello (h, ls_hello_wait_n); free (h); }
	return true;
}
static void RETRO_CALLCONV ls_disconnected (uint16_t id)
{
	if (id != ls_peer) return; ls_peer = 0xFFFF; ls_peer_ok = 0; ls_cfg_on = 0;
	if (ls_state) ls_say ("player 2 left - playing alone", 240); ls_state = 0;
}
/* (test: VXPOC_LSSOLO=1 - an online game with nobody: both players' inputs made up here (VXPOC_LSFUZZ), an empty card,
 *  seed 12345 - its checks (VXPOC_NETDBG) the same on every system if the game runs the same everywhere) */
static void netls_solo_tick (void)
{
	if (ls_solo || ls_state || !ls_avail || !getenv ("VXPOC_LSSOLO")) return;
	ls_solo = 1; ls_seed = 12345; ls_mode = ls_mode_now (); ls_jitfast = 0; ls_intro_skip = 0; ls_dacofs = 0; snprintf (ls_carthw, sizeof ls_carthw, "1");
	ls_only_free (); ls_me = 0; ls_begin ();
}
static const struct retro_netpacket_callback ls_cb = { ls_start, ls_receive, ls_stop, NULL, ls_connected, ls_disconnected, LS_PROTO };
static void netls_load (const char *path)              /* (retro_load_game: a UVMC2 / PiTrex game plays online in lockstep) */
{
	const char *e = strrchr (path, '.');
	ls_avail = e && (!vx_stricmp (e, ".um2") || !vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit"));
	if (ls_avail && !env_cb (RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, (void *) &ls_cb) && !getenv ("VXPOC_LSSOLO")) ls_avail = 0;
}

/* ---- the core: its settings while in step (game_env), each game frame's input (emu_loop) */
static void netls_env (void)
{
	if (!ls_cfg_on) return;
	static char e1[64], e2[64], e3[64], e4[96];
	snprintf (e1, sizeof e1, "VT_POWERSEED=%u", (unsigned) ls_seed); vx_putenv (e1);
	snprintf (e2, sizeof e2, "VT_HBLATCH=%u", (unsigned) (ls_seed & 7)); vx_putenv (e2);
	snprintf (e3, sizeof e3, "VT_DACOFS=%.6f", ls_dacofs); vx_putenv (e3);
	snprintf (e4, sizeof e4, "VT_CARTHW=%s", ls_carthw); vx_putenv (e4);
	vx_putenv ("VT_THREADS="); vx_putenv ("VT_USBHID="); km_on = 0;
	vx_putenv (ls_jitfast ? "VT_JIT=" : "VT_JIT=exact");
	if (getenv ("VXPOC_LSJIT")) { static char ej[40]; snprintf (ej, sizeof ej, "VT_JIT=%s", getenv ("VXPOC_LSJIT")); vx_putenv (ej); }   /* (test) */
	vx_putenv (ls_mode == 1 ? "VT_UVM2_FAST=1" : "VT_UVM2_FAST="); vx_putenv (ls_mode == 2 ? "VT_UVM2_DEV=1" : "VT_UVM2_DEV=");
	vx_putenv ("VT_IMAGER="); vx_putenv ("VT_LIGHTPEN="); vx_putenv ("VT_SPEECHDEV="); lightpen = 0;
}
static int netls_frame (unsigned char *m)            /* (before a game frame in step: 1 and its input in m, or 0 - not yet) */
{
	while (ls_have[ls_me] <= ls_f + LS_DELAY) {          /* (this side's input LS_DELAY frames on: the controller now) */
		unsigned char *x = ls_in[ls_me][ls_have[ls_me] % LS_RING]; x[0] = ls_pad[0]; x[1] = ls_pad[1]; x[2] = ls_pad[2]; x[3] = ls_reset_req ? 4 : 0; ls_reset_req = 0;
		if (getenv ("VXPOC_LSFUZZ")) { uint32_t r = (ls_have[ls_me] / 7 + 1) * 2654435761u ^ (ls_me ? 0x5bd1e995u : 0x1b873593u); r ^= r >> 13; r *= 0x85ebca6bu; r ^= r >> 16;   /* (test: each side
		                                              presses its own way - both games stay the same only if the inputs get across) */
			x[0] = (unsigned char) (r & 0xff); x[1] = (unsigned char) (r >> 8); x[2] = (unsigned char) ((r >> 16) & 15); }
		ls_have[ls_me]++; ls_send_inputs (); }
	const int o = ls_me ^ 1;
	if (ls_have[o] <= ls_f && ls_rx) { double t0 = vt_wall ();
		do { ls_rx (); if (ls_have[o] > ls_f) break;
#ifdef _WIN32
			Sleep (1);
#else
			usleep (1000);
#endif
		} while (vt_wall () - t0 < LS_WAIT_MS / 1000.0 && ls_state == 2); }
	if (ls_solo) while (ls_have[o] <= ls_f + LS_DELAY) {   /* (test: the other player made up here too) */
		uint32_t r = (ls_have[o] / 7 + 1) * 2654435761u ^ 0x5bd1e995u; r ^= r >> 13; r *= 0x85ebca6bu; r ^= r >> 16;
		unsigned char *x = ls_in[o][ls_have[o] % LS_RING]; x[0] = (unsigned char) r; x[1] = (unsigned char) (r >> 8); x[2] = (unsigned char) ((r >> 16) & 15); x[3] = 0; ls_have[o]++; }
	if (ls_state != 2 || ls_have[o] <= ls_f) return 0;
	const unsigned char *a = ls_in[0][ls_f % LS_RING], *b = ls_in[1][ls_f % LS_RING];
	m[0] = a[0]; m[1] = a[1]; m[2] = a[2]; m[3] = (unsigned char) (0x40 | ((a[3] | b[3]) & 4)); m[4] = b[0]; m[5] = b[1]; m[6] = b[2]; m[7] = 0;
	return 1;
}
static void netls_after (void)                       /* (a game frame run in step: the RAM's check now and then) */
{
	ls_f++;
	if (ls_f <= 30 && getenv ("VXPOC_NETDBG")) { extern unsigned char *vx_ram; extern int vt_uvm2; extern uint32_t uvm2_sig (void);   /* (test: the first frames, part by part) */
		const unsigned char *a = ls_in[0][(ls_f - 1) % LS_RING], *b = ls_in[1][(ls_f - 1) % LS_RING];
		fprintf (stderr, "[netls] f %u in %02x%02x%02x %02x%02x%02x ram %08x cyc %llu uvm2 %08x\n", (unsigned) ls_f, a[0], a[1], a[2], b[0], b[1], b[2],
		         vx_ram ? ~crc32_up (0xFFFFFFFFu, vx_ram, 1024) : 0, (unsigned long long) (total_cycles - ls_cyc0), vt_uvm2 ? uvm2_sig () : 0); }
	if (ls_f % LS_CHECK == 0) { uint32_t c = ls_ram_crc (); ls_check (0, ls_f, c);
		unsigned char b[9]; b[0] = 'C'; put32 (b + 1, ls_f); put32 (b + 5, c); ls_put (0, b, 9); }
}
