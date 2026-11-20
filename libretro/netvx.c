/* netvx.c - Vectrex_EMU's libretro core: online play the desktop's way, for the own iOS app (and the tests on a PC): the
 * same packets as the desktop's netplay.py (online version NV_VER), so a phone and a PC play each other, meet at the
 * same meeting point (tools/vxrelay.py: codes, the game room, the relay) and find each other's games on a network
 * without the internet (the LAN announcements). RetroArch has its own netplay (and netls.c for the UVMC2 / PiTrex):
 * this is for frontends without one - the app calls vx_net_* (below).
 *   Lockstep, as the desktop: both sides power the same game on with the host's settings (the power-on seed, the intro,
 * the cartridge hardware, R302's DAC offset - through netls.c's ls_cfg), and each game frame runs only with both
 * players' controllers for it; each side's input runs `delay` frames after it was read (the host measures the round
 * trip: 3-12 frames), every packet repeats the last NV_KEEP inputs. A thread of its own takes the packets in (the
 * game, the meeting point, the chat, the pause); the game frames (emu_loop) only read what it has.
 *   The UVMC2 and PiTrex games too: their SD cards compared first (the host's list, which files player 2 has alike - the
 * card built of those, dated as the desktop's), the slower UVMC2 mode, the PiTrex JIT fast only between the same
 * processor type. The sync check sends the desktop's own state checksum and compares the other side's.
 * Included by libretro_vx.c. Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#ifdef _WIN32                                    /* (windows.h is in already, with winsock 1: its calls only - the tests) */
#pragma comment (lib, "ws2_32.lib")
typedef int socklen_t;
typedef SOCKET nv_sock_t;
#define NV_BADSOCK INVALID_SOCKET
#define nv_closesock closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <ifaddrs.h>                             /* (each network's own broadcast address: an ad-hoc one - a phone's Wi-Fi Direct group, a hotspot - too) */
#include <net/if.h>
typedef int nv_sock_t;
#define NV_BADSOCK (-1)
#define nv_closesock close
#endif

#define NV_VER 11                    /* (netplay.py NET_VER: both sides the same - raise it with the desktop's) */
#define NV_PORT 7744                 /* (a direct game: the host's port) */
#define NV_LANPORT 7743              /* (the announcements of a host on this network) */
#define NV_SERVER "172.233.60.40"    /* (the meeting point, tools/vxrelay.py) */
#define NV_SPORT 7745
#define NV_KEEP 16                   /* inputs repeated in each packet */
#define NV_RING 512
#define NV_TIMEOUT 6.0               /* seconds without the other side: "no connection" said - the game waits for it, only Leave ends it */
#define NV_PUNCH 3.0                 /* seconds of trying the direct way before the relay */
#define NV_CHATMAX 200
#ifndef NV_ARCH                              /* (a test build may say another: the iPhone's, arm64) */
#if defined(__aarch64__) || defined(_M_ARM64)
#define NV_ARCH "arm64"
#else
#define NV_ARCH "x64"
#endif
#endif

enum { NV_IDLE, NV_MEET, NV_WAIT, NV_AGREED, NV_PLAY, NV_ENDED };
typedef struct { signed char jx, jy; unsigned char b, fl; } nv_in;

static nv_sock_t nv_s = NV_BADSOCK, nv_ls = NV_BADSOCK;   /* (the game's socket; the LAN listener's) */
static int nv_role, nv_phase, nv_mode;          /* (role 1 host, 2 join; mode 0 internet public, 1 internet private, 2 this network) */
static volatile int nv_quit_th, nv_th_on;
static struct sockaddr_in nv_peer, nv_srv, nv_cand[2]; static int nv_have_peer, nv_ncand, nv_relay, nv_locked;
static char nv_code[8], nv_gamer[16], nv_key[48], nv_peer_name[16], nv_err[160], nv_note[160];
static char nv_game[1024];
/* joining a running game (app to app): the host's state sent, compressed (nv_lz), player 2 loads it into the same game -
 * no reset: as a controller plugged into port 2 of a running Vectrex. Only when both say they can ("hot" in the game,
 * "\0hotjoin" in player 2's yes), no SD card (a UVMC2 / PiTrex has no save state) and not relayed: else both start anew as before */
static int nv_hot_can, nv_hot, nv_hot_total, nv_hot_got, nv_hot_last, nv_hot_apply, nv_take_req;
static unsigned char *nv_hz, *nv_hbuf, *nv_hmask, *nv_hraw; static size_t nv_hzn, nv_hrawn; static double nv_t_hot;
static char ls_port2[24]; static unsigned long long nv_hctr[5];   /* (the host's frame counters, with its state) */                        /* (online: the host's port 2 device, both sides - libretro_vx.c game_env) */
/* another game during a game (the iOS app, both sides): the host picks it, player 2 checks it has it, then a flag in the
 * host's own input switches both in the same game frame - the connection and the frame count go on. Only between apps
 * that both say they can ("regame" in the game, "\0regame" in player 2's yes); the desktop's online play can not */
static int nv_rg_can, nv_rg_state, nv_rg_switched, nv_reset_n, nv_reset_by; static unsigned nv_rg_seq, nv_rg_seen = 0xffffu;
static int nv_rgm_peer, nv_x_got; static unsigned nv_x_seq; static double nv_x_until, nv_t_x;   /* (a switch to or from a card game: player 2 can
	(its "\0rgmode"); the host's word to switch now ('X'), out of a streamed game, which has no input frames to carry it) */
static void nv_switch_now (void);
static char nv_rg_game[1024], nv_rg_rom[1100], nv_rg_msg[160]; static double nv_t_rg, nv_rg_t0; static uint32_t nv_rg_done_f = 0xffffffffu;                      /* (the game: the host's json - offered or agreed) */
static char nv_rom_md5[33], nv_rom_name[256]; static long long nv_rom_size; static int nv_skip_boot; static char nv_carthw[32];
static double nv_dacofs;
static int nv_delay = 3, nv_accepted, nv_go, nv_left, nv_lost, nv_peer_pause, nv_pause, nv_pause_seq;
/* (the pause: each side's own - 0 playing, 1 a menu, 2 another app - and the game stands while either is paused: one side's
 *  going on no longer ended the other's (a menu closed here went on while the other player was in another app). Each side
 *  sends its own now and then, its number with it: a lost one is sent again) */
static int nv_mypause, nv_theirpause, nv_their_seq = -1; static double nv_t_z;
static void nv_pause_upd (void) { nv_pause = nv_mypause || nv_theirpause; nv_peer_pause = nv_theirpause != 0; }
/* a cartridge with an SD card (the UVMC2): the host's card list ([path, size, md5], zlib JSON, CARD_CHUNK pieces), player 2's
 * answer (which files it has alike), the files both play from - the card built of those only, every file dated
 * NV_CARD_TIME (the desktop's netplay.CARD_TIME) - and the UVMC2 mode both play in (the slower of the two) */
#define NV_CARD_CHUNK 480
/* ---- the host's saves for both (2026-11-20): a flash cartridge's flash (Vectorblade's calibration, high scores), Last
 * Human's save chip - sent to player 2 at the start (and with a game switch), both play with them: each player's own
 * parted the games, none (as they came) lost the calibration. No unit value travels: R302 (the one the game sees) is the
 * factory's on both online. Player 2's own files are never written: the host's go to vectrex_emu_online.* only, made anew
 * at each start - a crash leaves nothing behind. "VXS1", then records: type (1 flash, 2 save chip), length, the runs
 * where it differs from the ROM / an empty chip ([offset 4][length 2][bytes]) */
#define NV_SV_MAX 65536
static unsigned char nv_sv[NV_SV_MAX], nv_cur[NV_SV_MAX], nv_svmask[NV_SV_MAX / NV_CARD_CHUNK + 2];
static size_t nv_svn, nv_curn; static int nv_sv_id, nv_sv_state, nv_sv_peer, nv_sv_need, nv_sv_cidx, nv_sv_total, nv_sv_got, nv_sv_ids;
static char nv_sv_md5[33], nv_cur_md5[33]; static double nv_t_sv;   /* (nv_sv: the host's offered / player 2's coming; nv_cur: the one played with) */
#define NV_CARD_TIME 1104537600
typedef struct { char rel[256]; long long size; char md5[33]; } nv_cfile;
static nv_cfile *nv_mine; static int nv_nmine;               /* (this side's card files) */
static nv_cfile *nv_theirs; static int nv_ntheirs;           /* (the host's list, at player 2) */
static unsigned char *nv_cz; static size_t nv_czn;           /* (the host: its list, compressed) */
static unsigned char *nv_cparts[512]; static size_t nv_cpartn[512]; static int nv_cgot, nv_ctotal;   /* (player 2: the pieces) */
static unsigned char nv_cbits[512]; static int nv_cbitn;     /* (player 2's answer: a bit a file) */
static int nv_card, nv_card_done, nv_ready, nv_cidx; static double nv_t_card;
static char **nv_only; static int nv_nonly;                  /* (the files both play from) */
static char nv_uvm2[8] = "exact"; static int nv_jitfast;   /* (the PiTrex JIT fast: both the same processor type) */
static double nv_last_heard, nv_last_direct, nv_last_relayed, nv_punch_t0, nv_last_send, nv_wait_t0, nv_rtt;
/* ---- rollback (2026-11-20): a frame whose other-side input is not here yet runs at once with a guess - that side's last
 * input (a moment's button - RESET, a game switch, a port taken - never guessed); the state before it kept. The real input
 * different: back to that frame's state, the frames since run again (netvx_rollback, emu_loop). The game runs on through a
 * network's pauses instead of standing (lockstep: every pause longer than the input delay stood both games - the iPad and
 * the iPhone 8-10 times in 30 s, up to 316 ms; Wi-Fi, 4G alike). At most NV_RB_W frames on guesses, then it waits as before.
 * 6809 games only (save states); a UVMC2 / PiTrex stays in lockstep. The packets are the same: an older side plays on in
 * lockstep against it. The sync check: only frames both sides ran with real input */
#define NV_RB_W 20
#define NV_RB_SLOTS 24
static unsigned char *nv_rb_buf[NV_RB_SLOTS], *nv_rb_tmp; static size_t nv_rb_len[NV_RB_SLOTS], nv_rb_cap[NV_RB_SLOTS]; static uint32_t nv_rb_fr[NV_RB_SLOTS];
static int nv_rb_on, nv_rb_peer; static uint32_t nv_conf, nv_fd;   /* (nv_fd: frames below have their inputs decided - run, or running now) */ static int64_t nv_rb_from = -1; static nv_in nv_rlast; static int64_t nv_rlast_f = -1;
static nv_in nv_used[512]; static unsigned char nv_used_pred[512]; static uint32_t nv_psum[512][2]; static unsigned char nv_psum_ok[512]; static int nv_cur_conf;
static long nv_n_rb, nv_n_rbf, nv_n_pred; static int64_t nv_reset_lastf = -1;
static int nv_rb_ports[NV_RB_SLOTS][2]; static unsigned long long nv_rb_ctr[NV_RB_SLOTS][5];   /* (with each saved frame: the ports taken,
                                                 the frame counters outside the state - as the running game's state sent to a joiner) */
static int nv_in_differ (const nv_in *a, const nv_in *b) { return a->jx != b->jx || a->jy != b->jy || a->b != b->b || a->fl != b->fl; }
static int nv_stream, nv_stream_peer, nv_st_tgt = 2, nv_st_buf, nv_stq_n; static double nv_st_calm, nv_st_gap_max, nv_st_gap_recent; static long nv_st_n_dry;   /* (run dry: how often, the longest gap between two frames in) */   /* (line streaming - below) */ static long nv_st_n_got, nv_st_n_drop, nv_st_n_lost, nv_st_n_ref, nv_st_n_new, nv_st_n_two, nv_st_n_fix;
static void nv_st_on_piece (const unsigned char *b, size_t n); static void nv_st_on_input (const unsigned char *b, size_t n); static void nv_ports_take (int side, unsigned char fl); static void nv_st_clear (void);
static size_t vx_save_to (unsigned char *d); static bool vx_load_from (const void *d, size_t sz); static void vx_state_init (void); static void nv_sum_send (uint32_t f, uint32_t h); static void vx_rb_keep (int back);
static int nv_dly = 3, nv_their_dly, nv_st_run, nv_st_ep; static double nv_st_t0, nv_calm_t0;   /* (this side's input delay now - it grows when the
						  game stands (a Wi-Fi's pauses), and comes back to the agreed one when calm; the other side's, sent with its inputs) */
static uint32_t nv_f, nv_lhave;                  /* (the frame now; this side's inputs made up to) */
static nv_in nv_local[NV_RING], nv_remote[NV_RING]; static uint32_t nv_rhave_lo; static unsigned char nv_rgot[NV_RING];
static uint32_t nv_rgot_f[NV_RING];
static int nv_ports[2] = { 1, 2 };              /* (the port each side holds: host, join - netplay.py port_inputs) */
static double nv_adv, nv_adv_them; static int32_t nv_their_f = -1;
/* the chat */
typedef struct { uint16_t k; char t[NV_CHATMAX + 1]; double t0; } nv_line;
static nv_line nv_cout[16]; static int nv_ncout; static uint16_t nv_cseq, nv_cnext;
static char nv_cin[16][NV_CHATMAX + 1]; static int nv_ncin;      /* (the other player's lines, for the app) */
static char nv_cheld[64][NV_CHATMAX + 1]; static uint16_t nv_cheld_k[64]; static int nv_ncheld;
/* the game room and the network's games */
static char nv_room[8192]; static int nv_room_state; static double nv_room_t0;   /* (0 none, 1 asking, 2 there, 3 no answer) */
typedef struct { char ip[48]; int port; char name[128], md5[33], gamer[16]; long long size; int nver; double seen; } nv_lan;
static nv_lan nv_lans[16]; static int nv_nlans, nv_lan_on;

#ifdef _WIN32
static CRITICAL_SECTION nv_mx; static int nv_mx_ok;
static void nv_lock (void) { if (!nv_mx_ok) { InitializeCriticalSection (&nv_mx); nv_mx_ok = 1; } EnterCriticalSection (&nv_mx); }
static void nv_unlock (void) { LeaveCriticalSection (&nv_mx); }
static HANDLE nv_th;
static void nv_sleep_ms (int ms) { Sleep (ms); }
#else
static pthread_mutex_t nv_mx = PTHREAD_MUTEX_INITIALIZER;
static void nv_lock (void) { pthread_mutex_lock (&nv_mx); }
static void nv_unlock (void) { pthread_mutex_unlock (&nv_mx); }
static pthread_t nv_th;
static void nv_sleep_ms (int ms) { usleep ((useconds_t) ms * 1000); }
#endif
static void nv_log (const char *s) { fprintf (stderr, "[vectrex_emu] online: %s\n", s); }
static int pz_inflate (unsigned char *out, size_t outlen, const unsigned char *in, size_t inlen);   /* (pxfetch.c) */
static void nv_on_sum (const unsigned char *b, size_t n);
static void nv_jesc (char *out, size_t n, const char *s);

/* ---- MD5 (RFC 1321): the ROM's identity, as the desktop's rom_md5 */
typedef struct { uint32_t a, b, c, d; uint64_t n; unsigned char buf[64]; } nv_md5;
static void nv_md5_block (nv_md5 *m, const unsigned char *p)
{
	static const uint32_t K[64] = { 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
		0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9,
		0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0,
		0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
	static const int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
	uint32_t w[16]; for (int i = 0; i < 16; i++) w[i] = (uint32_t) p[4 * i] | ((uint32_t) p[4 * i + 1] << 8) | ((uint32_t) p[4 * i + 2] << 16) | ((uint32_t) p[4 * i + 3] << 24);
	uint32_t a = m->a, b = m->b, c = m->c, d = m->d;
	for (int i = 0; i < 64; i++) {
		uint32_t f; int g;
		if (i < 16) { f = (b & c) | (~b & d); g = i; } else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
		else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; } else { f = c ^ (b | ~d); g = (7 * i) & 15; }
		const uint32_t t = d; d = c; c = b; const uint32_t x = a + f + K[i] + w[g]; b = b + ((x << R[i]) | (x >> (32 - R[i]))); a = t; }
	m->a += a; m->b += b; m->c += c; m->d += d;
}
static void nv_md5_init (nv_md5 *m) { m->a = 0x67452301; m->b = 0xefcdab89; m->c = 0x98badcfe; m->d = 0x10325476; m->n = 0; }
static void nv_md5_add (nv_md5 *m, const unsigned char *p, size_t n)
{
	while (n) { const size_t k = m->n & 63, t = 64 - k < n ? 64 - k : n; memcpy (m->buf + k, p, t); m->n += t; p += t; n -= t; if ((m->n & 63) == 0) nv_md5_block (m, m->buf); }
}
static void nv_md5_hex (nv_md5 *m, char *out)
{
	const uint64_t bits = m->n * 8; unsigned char pad = 0x80, z = 0, L[8];
	nv_md5_add (m, &pad, 1); while ((m->n & 63) != 56) nv_md5_add (m, &z, 1);
	for (int i = 0; i < 8; i++) L[i] = (unsigned char) (bits >> (8 * i)); nv_md5_add (m, L, 8);
	const uint32_t v[4] = { m->a, m->b, m->c, m->d };
	for (int i = 0; i < 16; i++) sprintf (out + 2 * i, "%02x", (unsigned) ((v[i / 4] >> (8 * (i % 4))) & 0xff));
	out[32] = 0;
}
/* the MD5 of a file (32 hex digits) and its size; 0 when it can not be read */
int vx_net_md5_file (const char *path, char *out33, long long *size)
{
	FILE *f = fopen (path, "rb"); if (!f) return 0;
	nv_md5 m; nv_md5_init (&m); unsigned char b[16384]; size_t n; long long t = 0;
	while ((n = fread (b, 1, sizeof b, f)) > 0) { nv_md5_add (&m, b, n); t += (long long) n; }
	fclose (f); nv_md5_hex (&m, out33); if (size) *size = t; return 1;
}

/* ---- the SD card: this side's files, the list as the desktop's card_manifest, zlib (stored blocks out, inflate in) */
static int nv_cmp_rel (const void *a, const void *b)
{
	const char *x = ((const nv_cfile *) a)->rel, *y = ((const nv_cfile *) b)->rel;
	for (;; x++, y++) { const int c = tolower ((unsigned char) *x), d = tolower ((unsigned char) *y); if (c != d || !c) return c - d; }
}
static const char *nv_card_folder (const char *game)       /* (<system>/vectrex_emu/uvmc2 for a .um2) */
{
	static char fo[1800]; const char *sys = NULL; env_cb (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys);
	const char *e = strrchr (game, '.'); int px = e && (!vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit"));
	snprintf (fo, sizeof fo, "%s" SEP "vectrex_emu" SEP "%s", sys ? sys : ".", px ? "pitrex" : "uvmc2"); return fo;
}
static void nv_card_scan (const char *path, const char *rel)
{
	sdc_ent *v; int n = sdc_list (path, &v);
	for (int i = 0; i < n; i++) { char pp[2048], r[1024]; snprintf (pp, sizeof pp, "%s" SEP "%s", path, v[i].name); snprintf (r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", v[i].name);
		if (v[i].dir) { nv_card_scan (pp, r); continue; }
		if (strlen (r) > 250) continue;
		nv_cfile x; memset (&x, 0, sizeof x); snprintf (x.rel, sizeof x.rel, "%s", r);
		if (!vx_net_md5_file (pp, x.md5, &x.size)) continue;
		nv_mine = realloc (nv_mine, sizeof *nv_mine * (size_t) (nv_nmine + 1)); nv_mine[nv_nmine++] = x; }
	free (v);
}
static void nv_card_mine (const char *game)
{
	free (nv_mine); nv_mine = NULL; nv_nmine = 0; char **so = sdc_only; sdc_only = NULL;
	nv_card_scan (nv_card_folder (game), ""); sdc_only = so;
	if (nv_nmine > 1) qsort (nv_mine, (size_t) nv_nmine, sizeof *nv_mine, nv_cmp_rel);
}
static uint32_t nv_adler (const unsigned char *b, size_t n) { uint32_t a = 1, c = 0; for (size_t i = 0; i < n; i++) { a = (a + b[i]) % 65521; c = (c + a) % 65521; } return (c << 16) | a; }
static void nv_card_pack (void)                              /* (the host's list as zlib: stored blocks - Python's zlib reads them) */
{
	size_t cap = 64 + (size_t) nv_nmine * 360; char *j = malloc (cap); size_t o = 0; if (!j) return;
	o += (size_t) snprintf (j + o, cap - o, "[");
	for (int i = 0; i < nv_nmine; i++) { char r[600]; nv_jesc (r, sizeof r, nv_mine[i].rel); o += (size_t) snprintf (j + o, cap - o, "%s[%s, %lld, \"%s\"]", i ? ", " : "", r, nv_mine[i].size, nv_mine[i].md5); }
	o += (size_t) snprintf (j + o, cap - o, "]");
	free (nv_cz); nv_cz = malloc (o + o / 65535 * 5 + 16); size_t z = 0; nv_cz[z++] = 0x78; nv_cz[z++] = 0x01;
	size_t at = 0; do { const size_t k = o - at > 65535 ? 65535 : o - at; nv_cz[z++] = at + k >= o ? 1 : 0;
		nv_cz[z++] = (unsigned char) k; nv_cz[z++] = (unsigned char) (k >> 8); nv_cz[z++] = (unsigned char) ~k; nv_cz[z++] = (unsigned char) (~k >> 8);
		memcpy (nv_cz + z, j + at, k); z += k; at += k; } while (at < o);
	const uint32_t ad = nv_adler ((const unsigned char *) j, o); nv_cz[z++] = (unsigned char) (ad >> 24); nv_cz[z++] = (unsigned char) (ad >> 16); nv_cz[z++] = (unsigned char) (ad >> 8); nv_cz[z++] = (unsigned char) ad;
	nv_czn = z; free (j);
}
static int nv_card_unpack (void)                             /* (player 2: the host's pieces -> its list) */
{
	size_t n = 0; for (int i = 0; i < nv_ctotal; i++) n += nv_cpartn[i];
	unsigned char *z = malloc (n + 1); if (!z) return 0; size_t at = 0; for (int i = 0; i < nv_ctotal; i++) { memcpy (z + at, nv_cparts[i], nv_cpartn[i]); at += nv_cpartn[i]; }
	const size_t cap = 4 << 20; char *j = calloc (1, cap); int rc = j && n > 6 ? pz_inflate ((unsigned char *) j, cap - 1, z + 2, n - 6) : 1, ok = rc == 0 || rc == -12; free (z);   /* (-12: all of it, the buffer bigger - fine) */
	if (!ok) { free (j); return 0; }
	free (nv_theirs); nv_theirs = NULL; nv_ntheirs = 0;
	for (const char *p = j; (p = strchr (p, '[')) != NULL; ) {     /* ([[path, size, md5], ...]) */
		p++; while (*p == ' ') p++; if (*p != '"') continue;
		nv_cfile x; memset (&x, 0, sizeof x); size_t i = 0; p++;
		while (*p && *p != '"' && i + 1 < sizeof x.rel) { if (*p == '\\' && p[1]) p++; x.rel[i++] = *p++; } while (*p && *p != '"') p++; if (*p) p++;
		while (*p == ',' || *p == ' ') p++; x.size = strtoll (p, (char **) &p, 10); while (*p == ',' || *p == ' ') p++;
		if (*p == '"') { p++; size_t k = 0; while (*p && *p != '"' && k < 32) x.md5[k++] = *p++; }
		nv_theirs = realloc (nv_theirs, sizeof *nv_theirs * (size_t) (nv_ntheirs + 1)); nv_theirs[nv_ntheirs++] = x; }
	free (j); return 1;
}
static void nv_only_set (const unsigned char *bits, int n, const nv_cfile *list)
{
	for (int i = 0; i < nv_nonly; i++) free (nv_only[i]); free (nv_only); nv_only = NULL; nv_nonly = 0;
	for (int i = 0; i < n; i++) if (bits[i >> 3] >> (i & 7) & 1) { nv_only = realloc (nv_only, sizeof (char *) * (size_t) (nv_nonly + 1)); nv_only[nv_nonly++] = strdup (list[i].rel); }
}

/* ---- tiny JSON: the few values the packets carry */
static int nv_jstr (const char *j, const char *key, char *out, size_t n)
{
	char k[64]; snprintf (k, sizeof k, "\"%s\"", key); const char *p = strstr (j, k); if (!p) return 0;
	p += strlen (k); while (*p == ' ' || *p == ':') p++; if (*p != '"') return 0; p++;
	size_t i = 0; while (*p && *p != '"' && i + 1 < n) { if (*p == '\\' && p[1]) p++; out[i++] = *p++; } out[i] = 0; return 1;
}
static int nv_jnum (const char *j, const char *key, double *v)
{
	char k[64]; snprintf (k, sizeof k, "\"%s\"", key); const char *p = strstr (j, k); if (!p) return 0;
	p += strlen (k); while (*p == ' ' || *p == ':') p++;
	if (!strncmp (p, "true", 4)) { *v = 1; return 1; } if (!strncmp (p, "false", 5) || !strncmp (p, "null", 4)) { *v = 0; return 1; }
	char *e; const double d = strtod (p, &e); if (e == p) return 0; *v = d; return 1;
}
static void nv_jesc (char *out, size_t n, const char *s)   /* (a string as JSON: quotes and backslashes escaped) */
{
	size_t i = 0; if (n < 3) return; out[i++] = '"';
	for (; *s && i + 3 < n; s++) { if (*s == '"' || *s == '\\') out[i++] = '\\'; if ((unsigned char) *s >= 32) out[i++] = *s; }
	out[i++] = '"'; out[i] = 0;
}

/* ---- the time, the socket */
static double nv_now (void) { return vt_wall (); }
static void nv_nonblock (nv_sock_t s)
{
#ifdef _WIN32
	u_long one = 1; ioctlsocket (s, FIONBIO, &one);
#else
	fcntl (s, F_SETFL, fcntl (s, F_GETFL, 0) | O_NONBLOCK);
#endif
}
static nv_sock_t nv_udp (int port, int bcast)
{
#ifdef _WIN32
	static int wsa; if (!wsa) { WSADATA d; WSAStartup (MAKEWORD (2, 2), &d); wsa = 1; }
#endif
	nv_sock_t s = socket (AF_INET, SOCK_DGRAM, 0); if (s == NV_BADSOCK) return s;
	int one = 1; setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const char *) &one, sizeof one);
	if (bcast) setsockopt (s, SOL_SOCKET, SO_BROADCAST, (const char *) &one, sizeof one);
	{ int big = 1 << 20; setsockopt (s, SOL_SOCKET, SO_RCVBUF, (const char *) &big, sizeof big); setsockopt (s, SOL_SOCKET, SO_SNDBUF, (const char *) &big, sizeof big); }   /* (streamed
	                                                 frames: a card game's intro, 10+ pieces a frame in bursts - the default buffers dropped them: the Nova to the iPad 48-82 of 250 lost) */
	struct sockaddr_in a; memset (&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl (INADDR_ANY); a.sin_port = htons ((unsigned short) port);
	if (bind (s, (struct sockaddr *) &a, sizeof a) != 0) { nv_closesock (s); return NV_BADSOCK; }
	nv_nonblock (s); return s;
}
static void nv_log (const char *m);
static void nv_raw (const void *b, size_t n, const struct sockaddr_in *to)
{
	if (nv_s == NV_BADSOCK) return;
	if (sendto (nv_s, (const char *) b, (int) n, 0, (const struct sockaddr *) to, sizeof *to) < 0) {   /* (said once an address: an iPhone without
	                                                              the local network's permission can not send to the other's home address) */
		static uint32_t said[4]; static int ns; const uint32_t a = to->sin_addr.s_addr; int k = 0; for (int i = 0; i < ns; i++) k |= said[i] == a;
		if (!k) { if (ns < 4) said[ns++] = a; char m[96]; snprintf (m, sizeof m, "can not send to %s (error %d)", inet_ntoa (to->sin_addr),
#ifdef _WIN32
			WSAGetLastError ()
#else
			errno
#endif
			); nv_log (m); } }
}
static void nv_send (int kind, const void *pay, size_t n)   /* (a game packet: straight to the other side, or through the relay) */
{
	unsigned char b[1300]; { const size_t cap = (nv_relay && nv_code[0]) ? 600 : 1240; if (n > cap) n = cap; }   /* (through the meeting point: its packets' size; direct: up to 1240 - the streamed lines' pieces) */
	{ static double gap = -1; if (gap < 0) { const char *e = getenv ("VX_NET_GAP"); gap = e ? atof (e) : 0; }   /* (test: the link silent
	                                              `gap` s of every 1.5 s - a Wi-Fi's pauses) */
	  if (gap > 0 && kind == 'I' && fmod (nv_now (), 1.5) < gap) return; }
	if (nv_relay && nv_code[0]) { const size_t c = strlen (nv_code); memcpy (b, "VXR1X", 5); memcpy (b + 5, nv_code, c);
		memcpy (b + 5 + c, "VXN1", 4); b[9 + c] = (unsigned char) kind; if (n) memcpy (b + 10 + c, pay, n); nv_raw (b, 10 + c + n, &nv_srv); return; }
	if (!nv_have_peer) return;
	memcpy (b, "VXN1", 4); b[4] = (unsigned char) kind; if (n) memcpy (b + 5, pay, n); nv_raw (b, 5 + n, &nv_peer);
}
static void nv_send_to (int kind, const void *pay, size_t n, const struct sockaddr_in *to)
{
	unsigned char b[640]; if (n > 600) n = 600; memcpy (b, "VXN1", 4); b[4] = (unsigned char) kind; if (n) memcpy (b + 5, pay, n); nv_raw (b, 5 + n, to);
}
static void nv_srvsend (const char *kind_and_body) { nv_raw (kind_and_body, strlen (kind_and_body), &nv_srv); }
static void nv_put16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
static unsigned nv_get16 (const unsigned char *p) { return (unsigned) p[0] | ((unsigned) p[1] << 8); }
static void nv_put32 (unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char) (v >> (8 * i)); }
static uint32_t nv_get32 (const unsigned char *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

/* this device's address on its network (the one that reaches the internet): the joiner's second way to the host */
static void nv_lan_addr (char *ip, size_t n)
{
	snprintf (ip, n, "127.0.0.1");
	nv_sock_t s = socket (AF_INET, SOCK_DGRAM, 0); if (s == NV_BADSOCK) return;
	struct sockaddr_in a; memset (&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons (53); a.sin_addr.s_addr = inet_addr ("8.8.8.8");
	if (connect (s, (struct sockaddr *) &a, sizeof a) == 0) { struct sockaddr_in me; socklen_t l = sizeof me;
		if (getsockname (s, (struct sockaddr *) &me, &l) == 0) snprintf (ip, n, "%s", inet_ntoa (me.sin_addr)); }
	nv_closesock (s);
}
static int nv_my_port (void) { struct sockaddr_in me; socklen_t l = sizeof me; return nv_s != NV_BADSOCK && getsockname (nv_s, (struct sockaddr *) &me, &l) == 0 ? ntohs (me.sin_port) : 0; }
static int nv_addr (struct sockaddr_in *a, const char *ip, int port)
{
	memset (a, 0, sizeof *a); a->sin_family = AF_INET; a->sin_port = htons ((unsigned short) port);
	if (!strcmp (ip, "255.255.255.255")) { a->sin_addr.s_addr = htonl (INADDR_BROADCAST); return 1; }
	const unsigned long v = inet_addr (ip); if (v != INADDR_NONE) { a->sin_addr.s_addr = (uint32_t) v; return 1; }
	struct hostent *h = gethostbyname (ip); if (!h || h->h_addrtype != AF_INET || !h->h_addr_list[0]) return 0;
	memcpy (&a->sin_addr, h->h_addr_list[0], 4); return 1;
}
static int nv_same (const struct sockaddr_in *a, const struct sockaddr_in *b) { return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port; }

/* ---- the inputs: each packet the last NV_KEEP frames' of this side (netplay.py _send_inputs) */
static void nv_send_inputs (void)
{
	unsigned char b[5 + 4 * NV_KEEP + 3]; const uint32_t hi = nv_lhave ? nv_lhave - 1 : 0, lo = hi + 1 >= NV_KEEP ? hi + 1 - NV_KEEP : 0;
	const int n = nv_lhave ? (int) (hi - lo + 1) : 0;
	nv_put32 (b, lo); b[4] = (unsigned char) n;
	for (int i = 0; i < n; i++) { const nv_in *x = &nv_local[(lo + i) % NV_RING]; b[5 + 4 * i] = (unsigned char) x->jx; b[6 + 4 * i] = (unsigned char) x->jy; b[7 + 4 * i] = x->b; b[8 + 4 * i] = x->fl; }
	int adv = (int) (nv_adv * 16); if (adv > 2000) adv = 2000; if (adv < -2000) adv = -2000; nv_put16 (b + 5 + 4 * n, (unsigned) (adv & 0xffff));
	b[5 + 4 * n + 2] = (unsigned char) nv_dly;   /* (this side's delay now: the other side's sync reckons with it - an older one reads no further) */
	nv_send ('I', b, 5 + 4 * n + 3); nv_last_send = nv_now ();
}
/* the clock sync, as the PC's (netplay.py pace_s): the side ahead of the other slows down a little - at most 1 %, from
 * half a frame ahead - so neither runs into the other's inputs and stands (a stutter: measured and sent, but not used
 * until 2026-11-20 - the iPad and the iPhone stood and raced). Each side sees itself ahead by the one-way delay too: half
 * the difference of the two is how far ahead it really is. Returns the share of this refresh's time to leave out */
static long nv_n_wait, nv_n_stood, nv_n_frames;     /* (the app's log: frames played, waited for a moment, stood - since last asked) */
/* the app's log line: the delay, how far ahead each side sees itself, the frames since the last call */
static unsigned long long nv_dg_tc; static double nv_dg_t;
int vx_net_diag (char *o, int n)
{
	nv_lock (); const int r = snprintf (o, (size_t) n, "delay %d, round trip %.0f ms, ahead %.1f (them %.1f), %ld frames, %ld waited, %ld stood, %ld guessed, %ld rollbacks (%ld frames again), %.2f s emulated in %.2f s%s",
		nv_dly, nv_rtt > 0 ? nv_rtt * 1000 : 0.0, nv_adv, nv_adv_them, nv_n_frames, nv_n_wait, nv_n_stood, nv_n_pred, nv_n_rb, nv_n_rbf, (double) (total_cycles - nv_dg_tc) / 1.5e6, nv_now () - nv_dg_t, nv_rb_on ? "" : " - lockstep"); nv_dg_tc = total_cycles; nv_dg_t = nv_now ();
	nv_n_wait = nv_n_stood = nv_n_frames = nv_n_pred = nv_n_rb = nv_n_rbf = 0;
	if (nv_stream && r > 0 && r < n) { const int r2 = snprintf (o + r, (size_t) (n - r), " - streamed: %ld frames came, %ld left out, %ld lost, %ld pieces repaired; %ld of %ld refreshes a new frame (%ld more than one); buffer %d, ran dry %ld, longest gap between frames in %.0f ms", nv_st_n_got, nv_st_n_drop, nv_st_n_lost, nv_st_n_fix, nv_st_n_new, nv_st_n_ref, nv_st_n_two, nv_st_tgt, nv_st_n_dry, nv_st_gap_max * 1000); nv_st_n_got = nv_st_n_drop = nv_st_n_lost = nv_st_n_ref = nv_st_n_new = nv_st_n_two = nv_st_n_fix = nv_st_n_dry = 0; nv_st_gap_max = 0; nv_unlock (); return r2 > 0 ? r + r2 : r; }
	nv_unlock (); return r;
}
double netvx_pace (void)
{
	if (nv_stream && nv_role == 2) {                              /* (streamed, player 2: frames waiting beyond the buffer - up to 25 % faster) */
		nv_lock (); const int over = nv_stq_n - (nv_st_tgt + 1); nv_unlock ();
		if (over <= 0) return 0.0; const double u = -0.06 * over; return u > -0.25 ? u : -0.25; }
	nv_lock (); const double ahead = (nv_adv - nv_adv_them) / 2.0; const uint32_t f = nv_f; nv_unlock ();
	if (f < 100) return 0.0;
	if (ahead < -1.5) { const double u = (ahead + 1.5) * 0.02; return u > -0.10 ? u : -0.10; }   /* (behind - a moment's halt, the overlay switched on the
	                                                 iPad: 18 frames behind, 36 s to come back at the other's 1 %): up to 10 % faster, a few seconds */
	if (ahead <= 0.5) return 0.0;
	const double s = (ahead - 0.5) * 0.005; return s < 0.01 ? s : 0.01;
}
static void nv_on_inputs (const unsigned char *b, size_t n)
{
	if (n < 5) return; const uint32_t lo = nv_get32 (b); const int k = b[4];
	for (int i = 0; i < k && 5 + 4 * (size_t) i + 4 <= n; i++) { const uint32_t f = lo + (uint32_t) i;
		if (f + NV_RING / 2 < nv_f || f >= nv_f + NV_RING / 2) continue;   /* (far off: not this game's frames) */
		const int r = f % NV_RING; if (nv_rgot[r] && nv_rgot_f[r] == f) continue;
		nv_remote[r].jx = (signed char) b[5 + 4 * i]; nv_remote[r].jy = (signed char) b[6 + 4 * i]; nv_remote[r].b = b[7 + 4 * i]; nv_remote[r].fl = b[8 + 4 * i];
		nv_rgot[r] = 1; nv_rgot_f[r] = f;
		if ((int64_t) f > nv_rlast_f) { nv_rlast = nv_remote[r]; nv_rlast_f = f; }   /* (the guess for frames not here yet) */
		if (nv_rb_on && f < nv_fd && nv_used_pred[r] && nv_in_differ (&nv_remote[r], &nv_used[r]) && (nv_rb_from < 0 || (int64_t) f < nv_rb_from))
			nv_rb_from = f; }   /* (a frame run with a wrong guess: back to it - emu_loop) */
	if (n >= 5 + 4 * (size_t) k + 3 && b[5 + 4 * k + 2]) nv_their_dly = b[5 + 4 * k + 2];
	const int32_t tf = (int32_t) (lo + (uint32_t) k) - 1 - (nv_their_dly ? nv_their_dly : nv_delay);
	if (tf > nv_their_f) { nv_their_f = tf; nv_adv += 0.05 * (((double) nv_f - tf) - nv_adv);
		if (n >= 5 + 4 * (size_t) k + 2) nv_adv_them = (double) (int16_t) nv_get16 (b + 5 + 4 * k) / 16.0; }
}

/* ---- the chat: lines until the other side confirms them */
static void nv_send_chat (double now)
{
	for (int i = 0; i < nv_ncout; ) {
		if (now - nv_cout[i].t0 > 15.0) { memmove (nv_cout + i, nv_cout + i + 1, (size_t) (nv_ncout - i - 1) * sizeof *nv_cout); nv_ncout--; continue; }
		unsigned char b[2 + NV_CHATMAX]; const size_t n = strlen (nv_cout[i].t); nv_put16 (b, nv_cout[i].k); memcpy (b + 2, nv_cout[i].t, n); nv_send ('T', b, 2 + n); i++; }
}
static void nv_on_chat (const unsigned char *b, size_t n)
{
	if (n < 2) return; const unsigned k = nv_get16 (b); unsigned char a[2]; nv_put16 (a, k); nv_send ('A', a, 2);
	if (((k - nv_cnext) & 0xffff) >= 0x8000) return;           /* (shown already) */
	for (int i = 0; i < nv_ncheld; i++) if (nv_cheld_k[i] == k) return;
	if (nv_ncheld < 64) { size_t m = n - 2 > NV_CHATMAX ? NV_CHATMAX : n - 2; memcpy (nv_cheld[nv_ncheld], b + 2, m); nv_cheld[nv_ncheld][m] = 0; nv_cheld_k[nv_ncheld++] = (uint16_t) k; }
	for (int found = 1; found; ) { found = 0;
		for (int i = 0; i < nv_ncheld; i++) if (nv_cheld_k[i] == nv_cnext) {
			if (nv_ncin == 16) { memmove (nv_cin, nv_cin + 1, sizeof nv_cin[0] * 15); nv_ncin--; }
			snprintf (nv_cin[nv_ncin++], NV_CHATMAX + 1, "%s", nv_cheld[i]);
			memmove (nv_cheld + i, nv_cheld + i + 1, sizeof nv_cheld[0] * (size_t) (nv_ncheld - i - 1)); memmove (nv_cheld_k + i, nv_cheld_k + i + 1, sizeof nv_cheld_k[0] * (size_t) (nv_ncheld - i - 1));
			nv_ncheld--; nv_cnext = (uint16_t) (nv_cnext + 1); found = 1; break; } }
}

/* ---- the host's Vectrex for both: the package made (the host), and used at a game's start (both) */
static unsigned char *nv_file (const char *p, size_t *n, size_t cap)   /* (a whole file, at most cap bytes; NULL: none) */
{
	FILE *f = fopen (p, "rb"); if (!f) return NULL; unsigned char *b = malloc (cap + 1); if (!b) { fclose (f); return NULL; }
	*n = fread (b, 1, cap + 1, f); fclose (f); if (*n > cap) { free (b); return NULL; } return b;
}
static size_t nv_sv_rec (unsigned char *o, size_t k, int type, const unsigned char *img, const unsigned char *base, size_t len)
{   /* (a record: the runs where img differs from base - [offset 4][length 2][bytes]; 0: no room) */
	const size_t k0 = k; if (k + 9 > NV_SV_MAX) return 0; o[k] = (unsigned char) type; nv_put32 (o + k + 1, (uint32_t) len); k += 9;
	uint32_t runs = 0; size_t i = 0;
	while (i < len) { if (img[i] == base[i]) { i++; continue; }
		size_t j = i, last = i; while (j < len && j - i < 60000) { if (img[j] != base[j]) last = j; else if (j - last > 8) break; j++; } j = last + 1;   /* (to its last differing byte) */
		if (k + 6 + (j - i) > NV_SV_MAX) return 0;
		nv_put32 (o + k, (uint32_t) i); o[k + 4] = (unsigned char) ((j - i) & 255); o[k + 5] = (unsigned char) ((j - i) >> 8); memcpy (o + k + 6, img + i, j - i); k += 6 + (j - i); runs++; i = j; }
	nv_put32 (o + k0 + 5, runs); return k;
}
static int nv_sv_lasthuman (const char *rom)          /* (Last Human: its save chip - libretro_vx.c game_env alike) */
{
	unsigned char h[96] = { 0 }; FILE *c = fopen (rom, "rb"); if (!c) return 0; if (fread (h, 1, 96, c)) {} fclose (c);
	return !memcmp (h, "g GCE", 5) && memmem_title (h, 96, "LAST") && memmem_title (h, 96, "HUMAN");
}
static size_t nv_sv_flashlen (size_t n, const char *hw)   /* (a flash cartridge's flash size - vtest.c's rule; 0: none) */
{
	if (!strcmp (hw, "vb")) return n > 65536 ? n : 262144;
	if (!strcmp (hw, "hb512")) return 524288;
	if (strcmp (hw, "auto") && strcmp (hw, "1")) return 0;
	return n == 524288 ? 524288 : (n > 65536 && n <= 262144) ? n : 0;
}
static size_t nv_sv_build (const char *rom, const char *hw, unsigned char *o)
{
	size_t k = 4; memcpy (o, "VXS1", 4);
	size_t n = 0; unsigned char *r = nv_file (rom, &n, 1 << 20); if (!r) return k;
	const size_t L = nv_sv_flashlen (n, hw && hw[0] ? hw : "auto");
	if (L) { char p[1900]; save_path (rom, ".sav", p, sizeof p); size_t m = 0; unsigned char *s = nv_file (p, &m, L);
		if (s && m == L) { unsigned char *base = malloc (L); if (base) { memset (base, 0xff, L); memcpy (base, r, n < L ? n : L);
			const size_t k2 = nv_sv_rec (o, k, 1, s, base, L); if (k2) k = k2; else nv_log ("the flash's saves too many to send: both without"); free (base); } }
		free (s); }
	if (nv_sv_lasthuman (rom)) { char p[1900]; save_path (rom, ".ds2431", p, sizeof p); size_t m = 0; unsigned char *s = nv_file (p, &m, 128);
		if (s && m == 128) { unsigned char base[128]; memset (base, 0xff, 128); const size_t k2 = nv_sv_rec (o, k, 2, s, base, 128); if (k2) k = k2; }
		free (s); }
	free (r); return k;
}
/* (a game's start online - libretro_vx.c game_env: the flash, the save chip of the package both agreed; none: the
 *  cartridge as it came, no save chip - as the PC online) */
static void nv_sv_apply (const char *path)
{
	static char ef[1950], ed[1950]; vx_putenv ("VT_FLASH_CLEAN=1"); vx_putenv ("VT_FLASH_IMG="); vx_putenv ("VT_DS2431=");
	nv_lock (); size_t n = nv_curn; unsigned char *b = n ? malloc (n) : NULL; if (b) memcpy (b, nv_cur, n); char md[33]; snprintf (md, sizeof md, "%s", nv_cur_md5); nv_unlock ();
	if (!b) return;
	char mine[33] = ""; long long sz; const int same = vx_net_md5_file (path, mine, &sz) && !vx_stricmp (mine, md);   /* (the saves: for this game only) */
	const char *sv = NULL; env_cb (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sv); const char *dir = sv && *sv ? sv : g_dir;
	size_t k = 4; int used = 0;
	while (n >= 4 && !memcmp (b, "VXS1", 4) && k + 9 <= n) {
		const int type = b[k]; const size_t len = nv_get32 (b + k + 1); const uint32_t runs = nv_get32 (b + k + 5); k += 9;
		if (type != 1 && type != 2) { if (k + len > n) break; k += len; continue; }   /* (another kind: passed over) */
		unsigned char *img = NULL; size_t rn = 0; unsigned char *r = NULL;
		if (type == 1 && same && (r = nv_file (path, &rn, 1 << 20)) && len <= (1 << 20) && (img = malloc (len))) { memset (img, 0xff, len); memcpy (img, r, rn < len ? rn : len); }
		else if (type == 2 && same && len == 128 && (img = malloc (128))) memset (img, 0xff, 128);
		for (uint32_t q = 0; q < runs && k + 6 <= n; q++) { const size_t o = nv_get32 (b + k), l = (size_t) b[k + 4] | ((size_t) b[k + 5] << 8); k += 6;
			if (k + l > n) { k = n; break; } if (img && o + l <= len) memcpy (img + o, b + k, l); k += l; }
		if (img) { char p[1900]; snprintf (p, sizeof p, "%s" SEP "vectrex_emu_online%s", dir, type == 1 ? ".sav" : ".ds2431"); FILE *f = fopen (p, "wb");
			if (f) { fwrite (img, 1, len, f); fclose (f);
				if (type == 1) { snprintf (ef, sizeof ef, "VT_FLASH_IMG=%s", p); vx_putenv (ef); used |= 2; } else { snprintf (ed, sizeof ed, "VT_DS2431=%s", p); vx_putenv (ed); used |= 4; } } }
		free (img); free (r); }
	free (b);
	fprintf (stderr, "[vectrex_emu] online: the host's saves -%s%s%s\n", used & 2 ? " flash" : "", used & 4 ? " save chip" : "", used & 6 ? "" : " none for this game");
}
/* (the host: the package for the game it offers - sent once player 2 can take it) */
static void nv_sv_offer (const char *rom)
{
	nv_svn = nv_sv_build (rom, nv_carthw, nv_sv); nv_sv_id = (++nv_sv_ids) & 0x7fff; if (!nv_sv_id) nv_sv_id = ++nv_sv_ids;
	long long sz; if (!vx_net_md5_file (rom, nv_sv_md5, &sz)) nv_sv_md5[0] = 0; nv_sv_state = 0; nv_sv_cidx = 0;
}
static void nv_sv_take (void)                         /* (the package both have now: the one played with) */
{
	memcpy (nv_cur, nv_sv, nv_svn); nv_curn = nv_svn; snprintf (nv_cur_md5, sizeof nv_cur_md5, "%s", nv_sv_md5);
}
static void nv_sv_want (const char *game)             /* (player 2: a game offered - its package coming?) */
{
	double v; nv_sv_need = nv_jnum (game, "sv", &v) && v > 0 && v <= NV_SV_MAX ? (int) v : 0;
	nv_sv_id = nv_jnum (game, "svi", &v) ? (int) v : 0; nv_sv_state = 0; nv_sv_total = 0; nv_sv_got = 0; nv_svn = 0;
	if (!nv_jstr (game, "md5", nv_sv_md5, sizeof nv_sv_md5)) nv_sv_md5[0] = 0;
}

/* ---- the game the host offers: the ROM's identity and what must be the same on both sides */
static void nv_make_game (void)
{
	char nm[300], hn[40]; nv_jesc (nm, sizeof nm, nv_rom_name); nv_jesc (hn, sizeof hn, nv_gamer);
	char bm[33] = ""; { long long bz; if (!g_bios[0] || !vx_net_md5_file (g_bios, bm, &bz)) bm[0] = 0; }   /* (the BIOS: both the same - another
	                                              one (Mine Storm II, a patched one) parts the games from the first frame) */
	uint32_t seed = (uint32_t) (nv_now () * 1e6) ^ (uint32_t) rand () ^ ((uint32_t) rand () << 15); seed &= 0x7fffffff;
	snprintf (nv_game, sizeof nv_game, "{\"md5\": \"%s\", \"size\": %lld, \"name\": %s, \"seed\": %u, \"nver\": %d, \"arch\": \"" NV_ARCH "\", "
		"\"skip_boot\": %s, \"cart_hw\": \"%s\", \"dacofs\": %.6f, \"delay\": %d, \"rtt_ms\": %d, \"relay\": %s, \"regame\": true, \"hot\": %s, \"port2\": \"%s\", \"bios\": \"%s\", \"sv\": %u, \"svi\": %d, \"rb\": true, \"stream\": %s, \"host_name\": %s%s%s%s}",
		nv_rom_md5, nv_rom_size, nm, (unsigned) seed, NV_VER, nv_skip_boot ? "true" : "false", nv_carthw, nv_dacofs, nv_delay,
		(int) (nv_rtt * 1000 + 0.5), nv_relay ? "true" : "false", "false", optv ("vectrex_emu_port2"), bm, (unsigned) (nv_card ? 0 : nv_svn), nv_sv_id, nv_card ? "true" : "false", hn, nv_card ? ", \"card\": true, \"uvm2\": \"" : "", nv_card ? nv_uvm2 : "", nv_card ? "\"" : "");
}
static size_t nv_lz (const unsigned char *b, size_t n, unsigned char *out)   /* (out: n + n / 127 + 16 bytes) */
{
	static int tab[1 << 16]; for (int i = 0; i < (1 << 16); i++) tab[i] = -1;
	size_t i = 0, o = 0, ls = 0, lc = 0;
	#define NV_FLUSH() do { if (lc) { out[o++] = (unsigned char) lc; memcpy (out + o, b + ls, lc); o += lc; lc = 0; } } while (0)
	while (i < n) {
		size_t best = 0, off = 0;
		if (i + 3 <= n) { const unsigned h = ((unsigned) b[i] * 2654435761u ^ (unsigned) b[i + 1] * 40503u ^ b[i + 2]) & 0xffff; const int j = tab[h]; tab[h] = (int) i;
			if (j >= 0 && i - (size_t) j < 65535) { size_t l = 0; while (i + l < n && l < 65535 && b[(size_t) j + l] == b[i + l]) l++; if (l >= 4) { best = l; off = i - (size_t) j; } } }
		if (best) { NV_FLUSH (); out[o++] = 0x80; out[o++] = (unsigned char) off; out[o++] = (unsigned char) (off >> 8); out[o++] = (unsigned char) best; out[o++] = (unsigned char) (best >> 8); i += best; ls = i; }
		else { if (!lc) ls = i; lc++; i++; if (lc == 127) NV_FLUSH (); } }
	NV_FLUSH ();
	#undef NV_FLUSH
	return o;
}
static int nv_unlz (const unsigned char *in, size_t n, unsigned char *out, size_t cap)   /* (0: exactly cap bytes out) */
{
	size_t i = 0, o = 0;
	while (i < n) { const unsigned t = in[i++];
		if (t & 0x80) { if (i + 4 > n) return -1; const size_t off = in[i] | (size_t) in[i + 1] << 8, len = in[i + 2] | (size_t) in[i + 3] << 8; i += 4;
			if (!off || off > o || o + len > cap) return -1; for (size_t k = 0; k < len; k++, o++) out[o] = out[o - off]; }
		else { if (i + t > n || o + t > cap) return -1; memcpy (out + o, in + i, t); i += t; o += t; } }
	return o == cap ? 0 : -1;
}
static void nv_hot_free (void)
{
	free (nv_hz); nv_hz = NULL; nv_hzn = 0; free (nv_hbuf); nv_hbuf = NULL; free (nv_hmask); nv_hmask = NULL; free (nv_hraw); nv_hraw = NULL; nv_hrawn = 0;
	nv_hot = nv_hot_total = nv_hot_got = nv_hot_last = nv_hot_apply = 0;
}
static nv_in *nv_rlast_zero_ (void) { return &nv_rlast; }
static void nv_reset_frames (void)
{
	nv_f = 0; nv_lhave = 0; nv_wait_t0 = 0; nv_their_f = -1; nv_adv = nv_adv_them = 0;
	nv_dly = nv_delay; nv_their_dly = 0; nv_st_run = nv_st_ep = 0; nv_st_t0 = nv_calm_t0 = 0;
	nv_conf = 0; nv_fd = 0; nv_rb_from = -1; nv_reset_lastf = -1; memset (nv_rlast_zero_ (), 0, sizeof nv_rlast); nv_rlast_f = nv_delay > 0 ? nv_delay - 1 : -1;
	for (int q = 0; q < NV_RB_SLOTS; q++) nv_rb_fr[q] = 0xffffffffu; memset (nv_psum_ok, 0, sizeof nv_psum_ok); memset (nv_used_pred, 0, sizeof nv_used_pred);
	for (uint32_t f = 0; f < (uint32_t) nv_delay; f++) {        /* (the first frames: no input, the same on both sides) */
		nv_local[f].jx = nv_local[f].jy = 0; nv_local[f].b = nv_local[f].fl = 0;
		nv_remote[f] = nv_local[f]; nv_rgot[f] = 1; nv_rgot_f[f] = f; }
	nv_lhave = (uint32_t) nv_delay; nv_ports[0] = 1; nv_ports[1] = 2;
}

/* ---- the meeting point's packets */
static void nv_on_server (int kind, const char *body, size_t n)
{
	char j[2048]; if (n >= sizeof j) n = sizeof j - 1; memcpy (j, body, n); j[n] = 0;
	if (kind == 'C') { snprintf (nv_code, sizeof nv_code, "%s", j); if (nv_phase == NV_MEET) nv_phase = NV_WAIT; nv_log ("the meeting point gave the code"); }
	else if (kind == 'P') {                                      /* (the other side's addresses: public and its network's) */
		const char *p; nv_ncand = 0;
		for (int w = 0; w < 2; w++) { p = strstr (j, w ? "\"lan\"" : "\"pub\""); if (!p) continue; p = strchr (p, '['); if (!p) continue;
			char ip[64]; int port; if (sscanf (p, "[ \"%63[^\"]\" , %d", ip, &port) == 2 || sscanf (p, "[\"%63[^\"]\", %d", ip, &port) == 2)
				if (nv_addr (&nv_cand[nv_ncand], ip, port)) nv_ncand++; }
		char nm[16]; if (nv_jstr (j, nv_role == 2 ? "gamer" : "name", nm, sizeof nm) && !nv_peer_name[0]) snprintf (nv_peer_name, sizeof nv_peer_name, "%s", nm);   /* (the host's: its gamer name - "name" there is its game's) */
		if (nv_ncand && !nv_have_peer) { nv_peer = nv_cand[0]; nv_have_peer = 1; }
		{ char m[160]; int k = snprintf (m, sizeof m, "the other side's ways:"); for (int i = 0; i < nv_ncand && k < 140; i++)
			k += snprintf (m + k, sizeof m - (size_t) k, " %s:%d", inet_ntoa (nv_cand[i].sin_addr), ntohs (nv_cand[i].sin_port)); nv_log (m); }
		nv_punch_t0 = nv_now (); nv_locked = 1; }
	else if (kind == 'E') { snprintf (nv_err, sizeof nv_err, "meeting point: %s", j); nv_log (nv_err); if (nv_phase < NV_AGREED) nv_phase = NV_ENDED; }
	else if (kind == 'l') { snprintf (nv_room, sizeof nv_room, "%s", j); nv_room_state = 2; }
}
static int nv_known (const struct sockaddr_in *src)
{
	if (!nv_locked) return 1;
	if (nv_have_peer && nv_peer.sin_addr.s_addr == src->sin_addr.s_addr) return 1;
	for (int i = 0; i < nv_ncand; i++) if (nv_cand[i].sin_addr.s_addr == src->sin_addr.s_addr) return 1;
	return 0;
}
/* the path after the punching window: the way the other side's last packet came */
static void nv_path (void)
{
	if (!nv_code[0] || !nv_ncand) return;
	if (nv_last_direct && nv_last_direct >= nv_last_relayed) { if (nv_relay) nv_log ("the direct way works: no relay"); nv_relay = 0; }
	else if (nv_last_relayed) { if (!nv_relay) nv_log ("through the relay"); nv_relay = 1; }
}

/* ---- the game packets */
static double nv_ping_t[8]; static int nv_ping_i, nv_ping_got; static double nv_ping_max, nv_ping_sent; static uint32_t nv_ping_seen;
static double nv_g_sent;
static void nv_on_packet (int kind, const unsigned char *b, size_t n, const struct sockaddr_in *src, int relayed)
{
	nv_last_heard = nv_now ();
	if (nv_lost && !nv_left && kind != 'Q') { nv_lost = 0; nv_log ("the other side is heard again"); }   /* (a gap, not a leave: on) */
	if (!relayed) { nv_last_direct = nv_last_heard;
		for (int i = 0; i < nv_ncand; i++) if (nv_same (src, &nv_cand[i]) && !(nv_have_peer && nv_same (src, &nv_peer))) { nv_peer = *src; nv_have_peer = 1; nv_log ("a direct connection"); }
		if (nv_locked && nv_have_peer && src->sin_addr.s_addr == nv_peer.sin_addr.s_addr && src->sin_port != nv_peer.sin_port && (kind == 'I' || kind == 'Z' || kind == 'S'))
			nv_peer = *src; }                                    /* (its router gave it another port: followed) */
	else nv_last_relayed = nv_last_heard;
	if (nv_role == 1 && nv_phase == NV_WAIT && kind == 'H' && nv_rtt == -2) {   /* the host: a player says hello (the first) */
		if (!relayed) { nv_peer = *src; nv_have_peer = 1; } nv_locked = 1;
		const unsigned ver = n >= 2 ? nv_get16 (b) : 1;
		if (n > 2 && !nv_peer_name[0]) { size_t m = n - 2 > 12 ? 12 : n - 2; memcpy (nv_peer_name, b + 2, m); nv_peer_name[m] = 0; }
		if (ver != NV_VER) { unsigned char v[2]; nv_put16 (v, NV_VER); for (int i = 0; i < 5; i++) nv_send ('V', v, 2);
			snprintf (nv_err, sizeof nv_err, "the other player has another version (online %u, this %d)", ver, NV_VER); nv_phase = NV_ENDED; return; }
		nv_path ();                                              /* (still waiting: the round trip first) */
		nv_ping_i = 0; nv_ping_got = 0; nv_ping_max = 0; nv_ping_sent = 0; nv_ping_seen = 0; nv_accepted = -1; nv_g_sent = 0; nv_rtt = -1;
		nv_log ("a player came: measuring the round trip"); return; }
	if (kind == 'H' || kind == 'h') return;                      /* (more hellos: the way is open) */
	switch (kind) {
	case 'P': if (nv_role == 2) { nv_path (); nv_send ('p', b, n); } break;          /* (the host measuring the round trip) */
	case 'p': if (nv_role == 1 && n >= 12 && nv_rtt == -1) { const uint32_t k = nv_get32 (b); double t0; memcpy (&t0, b + 4, 8);
			/* (any answer counts once, also one later than the next ping - it carries its own send time: through a hotel's
			 *  relay every answer came after 0.5 s, none counted, 100 ms was guessed and the game ran at 17 frames a second) */
			if (k < 32 && !(nv_ping_seen >> k & 1u)) { nv_ping_seen |= 1u << k; const double r = nv_now () - t0; if (r > nv_ping_max) nv_ping_max = r;
				if (nv_ping_got < 8) nv_ping_t[nv_ping_got] = r; nv_ping_got++;
				if ((int) k == nv_ping_i) { nv_ping_i++; nv_ping_sent = 0; } } } break;
	case 'G': if (nv_role == 2 && nv_phase <= NV_WAIT) { size_t m = n < sizeof nv_game ? n : sizeof nv_game - 1; memcpy (nv_game, b, m); nv_game[m] = 0;
			double v; if (nv_jnum (nv_game, "nver", &v) && (int) v != NV_VER) { static const unsigned char no[] = "\0version";
				for (int i = 0; i < 5; i++) nv_send ('N', no, 8); snprintf (nv_err, sizeof nv_err, "the host has another version (online %d, this %d)", (int) v, NV_VER); nv_phase = NV_ENDED; break; }
			if (nv_jnum (nv_game, "delay", &v)) nv_delay = (int) v; if (nv_delay < 1) nv_delay = 1; if (nv_delay > 32) nv_delay = 32;
			char hn[16]; if (!nv_peer_name[0] && nv_jstr (nv_game, "host_name", hn, sizeof hn)) snprintf (nv_peer_name, sizeof nv_peer_name, "%s", hn);
			nv_card = nv_jnum (nv_game, "card", &v) && v != 0; nv_card_done = !nv_card; nv_cbitn = 0; nv_ctotal = 0; nv_cgot = 0;
			{ char hm[8] = "exact"; nv_jstr (nv_game, "uvm2", hm, sizeof hm); const int mine = ls_mode_now (), theirs = !strcmp (hm, "dev") ? 2 : !strcmp (hm, "fast") ? 1 : 0;
			  const int r0 = mine == 2 ? 2 : mine == 1 ? 1 : 0, rank = (r0 == 0 || theirs == 0) ? 0 : (r0 == 1 || theirs == 1) ? 1 : 2;   /* (the slower: exact < fast < dev) */
			  snprintf (nv_uvm2, sizeof nv_uvm2, "%s", rank == 2 ? "dev" : rank == 1 ? "fast" : "exact"); }
			{ char nm2[256]; if (nv_jstr (nv_game, "name", nm2, sizeof nm2)) snprintf (nv_rom_name, sizeof nv_rom_name, "%s", nm2); }
			{ char ar[16] = ""; nv_jitfast = nv_jstr (nv_game, "arch", ar, sizeof ar) && !strcmp (ar, NV_ARCH); }
			nv_rg_can = strstr (nv_game, "\"regame\": true") != NULL; nv_hot_can = strstr (nv_game, "\"hot\": true") != NULL;
			nv_sv_want (nv_game); nv_rb_peer = strstr (nv_game, "\"rb\": true") != NULL; nv_stream_peer = strstr (nv_game, "\"stream\": true") != NULL;
			nv_phase = NV_AGREED; nv_accepted = -1; nv_log ("the host's game came: the app looks for the ROM"); } break;
	case 'A': if (nv_role == 1 && nv_phase == NV_WAIT && nv_rtt >= 0) { nv_phase = NV_AGREED; nv_accepted = 1;
			for (size_t i = 0; i + 6 < n; i++) if (!memcmp (b + i, "\0uvm2:", 6)) { char m[8] = ""; size_t k = 0; while (i + 6 + k < n && k < 7 && b[i + 6 + k]) { m[k] = (char) b[i + 6 + k]; k++; } m[k] = 0;
				if (!strcmp (m, "exact") || !strcmp (m, "fast") || !strcmp (m, "dev")) snprintf (nv_uvm2, sizeof nv_uvm2, "%s", m); break; }
			nv_jitfast = 0; for (size_t i = 0; i + 9 <= n; i++) if (!memcmp (b + i, "\0jit:fast", 9)) nv_jitfast = 1;
			nv_rg_can = 0; for (size_t i = 0; i + 7 <= n; i++) if (!memcmp (b + i, "\0regame", 7)) nv_rg_can = 1;
			nv_rgm_peer = 0; for (size_t i = 0; i + 7 <= n; i++) if (!memcmp (b + i, "\0rgmode", 7)) nv_rgm_peer = 1;
			nv_sv_peer = 0; for (size_t i = 0; i + 5 <= n; i++) if (!memcmp (b + i, "\0save", 5)) nv_sv_peer = 1;
			nv_rb_peer = 0; for (size_t i = 0; i + 3 <= n; i++) if (!memcmp (b + i, "\0rb", 3)) nv_rb_peer = 1;
			nv_stream = 0; if (nv_card) for (size_t i = 0; i + 7 <= n; i++) if (!memcmp (b + i, "\0stream", 7)) nv_stream = 1;
			nv_hot_can = 0; for (size_t i = 0; i + 8 <= n; i++) if (!memcmp (b + i, "\0hotjoin", 8)) nv_hot_can = 0;   /* (a join starts both anew - the owner's choice: the running game's state (up to 2 MB) stalled a phone's Wi-Fi Direct link and parted the games at frame 0, iPad with the Nova, 2026-11-19) */   /* (relayed: 2 MB through the meeting point took 15 s, the host's game standing - both start anew) */
			if (nv_card && nv_stream) { nv_card_done = 1; nv_ready = 1; nv_curn = 0; nv_log ("player 2 has the game: the host plays it and sends its lines (streamed)"); }
			else if (nv_card) { nv_card_done = 0; nv_cidx = 0; nv_curn = 0; nv_log ("player 2 has the game: the SD cards compared"); }
			else if (nv_svn && nv_sv_peer) { nv_card_done = 1; nv_sv_state = 1; nv_sv_cidx = 0; nv_log ("player 2 has the game: the host's saves sent"); }
			else { nv_card_done = 1; nv_ready = 1; nv_curn = 0; nv_log ("player 2 has the game: both start (no saves: the cartridges as they came)"); } }
		else if (n >= 2) { const unsigned k = nv_get16 (b); for (int i = 0; i < nv_ncout; i++) if (nv_cout[i].k == k) { memmove (nv_cout + i, nv_cout + i + 1, sizeof *nv_cout * (size_t) (nv_ncout - i - 1)); nv_ncout--; break; } }
		break;
	case 'N': if (nv_role == 1 && nv_phase == NV_WAIT) { char why[64] = ""; size_t m = n < 63 ? n : 63; memcpy (why, b, m); why[m] = 0;
			snprintf (nv_err, sizeof nv_err, n == 5 && !memcmp (b, "\0bios", 5) ? "player 2's BIOS (bios.bin) is not yours - online both need the same one"
				: n >= 2 && !b[0] ? "the other player has another version" : "player 2 does not have the game"); nv_phase = NV_ENDED; } break;
	case 'V': snprintf (nv_err, sizeof nv_err, "the host has another version of Vectrex_EMU"); nv_phase = NV_ENDED; break;
	case 'I': if (nv_role == 2 && nv_card && nv_cbitn && !nv_card_done) { nv_card_done = 1; nv_ready = 1; }
		if (nv_phase == NV_PLAY || nv_phase == NV_AGREED) nv_on_inputs (b, n); break;
	case 'T': nv_on_chat (b, n); break;
	case 'L': if (nv_phase == NV_PLAY && nv_stream) nv_st_on_piece (b, n); break;   /* (line streaming: a piece of the host's frame) */
	case 'u': if (nv_phase == NV_PLAY && nv_stream) nv_st_on_input (b, n); break;   /* (line streaming: player 2's controller) */
	case 'X': if (nv_role == 2 && nv_phase == NV_PLAY && nv_stream && nv_rg_state == 2 && n >= 2 && nv_get16 (b) == nv_rg_seq) nv_x_got = 1; break;   /* (the host
	                                                 switched out of its streamed game: this side too - netvx_stream_guest) */
	case 'W': if (nv_role == 2 && nv_sv_need && n > 10 && (int) nv_get16 (b) == nv_sv_id) {   /* (player 2: a piece of the host's package) */
			if (nv_sv_state == 2) { unsigned char a[2]; nv_put16 (a, (unsigned) nv_sv_id); nv_send ('w', a, 2); break; }
			const int i = (int) nv_get16 (b + 2), t = (int) nv_get16 (b + 4); const size_t len = nv_get32 (b + 6), m = n - 10;
			if (t <= 0 || i >= t || len != (size_t) nv_sv_need || len > NV_SV_MAX || (size_t) t != (len + NV_CARD_CHUNK - 1) / NV_CARD_CHUNK || m > NV_CARD_CHUNK) break;
			if (nv_sv_total != t) { nv_sv_total = t; nv_sv_got = 0; memset (nv_svmask, 0, sizeof nv_svmask); }
			if (nv_svmask[i] || (size_t) i * NV_CARD_CHUNK + m > len) break;
			memcpy (nv_sv + (size_t) i * NV_CARD_CHUNK, b + 10, m); nv_svmask[i] = 1; nv_sv_got++;
			if (nv_sv_got == t) { nv_svn = len;
				if (memcmp (nv_sv, "VXS1", 4)) { nv_sv_total = 0; nv_log ("the host's package came damaged: again"); break; }
				nv_sv_state = 2; if (nv_phase == NV_AGREED) nv_sv_take ();   /* (a game switch's: taken when both switch - vx_net_regame_answer) */
				if (nv_phase == NV_AGREED && nv_accepted == 1 && !nv_card) nv_ready = 1;
				unsigned char a[2]; nv_put16 (a, (unsigned) nv_sv_id); for (int q = 0; q < 3; q++) nv_send ('w', a, 2);
				char t2[96]; snprintf (t2, sizeof t2, "the host's saves here (%u bytes)", (unsigned) len); nv_log (t2); } } break;
	case 'w': if (nv_role == 1 && n >= 2 && (int) nv_get16 (b) == nv_sv_id && nv_sv_state == 1) {   /* (the host: player 2 has the package) */
			nv_sv_state = 2; if (nv_phase == NV_AGREED) { nv_sv_take (); nv_ready = 1; nv_log ("player 2 has the host's saves: both start"); } } break;
	case 'J': if (nv_role == 2 && nv_hot_can && !nv_hot_apply && !nv_hraw && n > 4) {
			const int i = (int) nv_get16 (b), t = (int) nv_get16 (b + 2); if (t <= 0 || i >= t) break;
			if (nv_hot_total != t) { free (nv_hbuf); free (nv_hmask); nv_hbuf = malloc ((size_t) t * NV_CARD_CHUNK); nv_hmask = calloc ((size_t) t, 1); nv_hot_total = t; nv_hot_got = 0; nv_hot_last = 0;
				if (!nv_hbuf || !nv_hmask) { nv_hot_total = 0; break; } }
			if (nv_hmask[i] || n - 4 > NV_CARD_CHUNK) break;
			memcpy (nv_hbuf + (size_t) i * NV_CARD_CHUNK, b + 4, n - 4); nv_hmask[i] = 1; nv_hot_got++; if (i == t - 1) nv_hot_last = (int) (n - 4);
			if (nv_hot_got == t) {
				const size_t zn = (size_t) (t - 1) * NV_CARD_CHUNK + (size_t) nv_hot_last; const unsigned char *z = nv_hbuf;
				const size_t raw = zn >= 4 ? (size_t) nv_get32 (z) : 0; nv_hraw = raw ? malloc (raw) : NULL;
				if (zn >= 4 + 40) memcpy (nv_hctr, z + zn - 40, 40);
				if (!nv_hraw || zn < 4 + 40 || nv_unlz (z + 4, zn - 4 - 40, nv_hraw, raw)) { free (nv_hraw); nv_hraw = NULL; memset (nv_hmask, 0, (size_t) t); nv_hot_got = 0; nv_log ("the host's game came damaged: again"); break; }
				nv_hrawn = raw; nv_hot_apply = 1; char m[96]; snprintf (m, sizeof m, "the host's running game here (%zu bytes): loaded at the next refresh", raw); nv_log (m); } } break;
	case 'j': if (nv_role == 1 && nv_hot == 1) { nv_hot = 2; free (nv_hz); nv_hz = NULL; nv_hzn = 0; nv_log ("player 2 has the running game: on together"); } break;
	case 'R': if (nv_role == 2 && nv_phase == NV_PLAY && n > 2) { const unsigned q = nv_get16 (b); if (q == nv_rg_seen) break; nv_rg_seen = q;
			size_t m = n - 2 < sizeof nv_rg_game - 1 ? n - 2 : sizeof nv_rg_game - 1; memcpy (nv_rg_game, b + 2, m); nv_rg_game[m] = 0;
			nv_rg_seq = q; nv_rg_state = 1; nv_sv_want (nv_rg_game); nv_log ("the host picked another game: the app looks for the ROM"); } break;
	case 'r': if (nv_role == 1 && nv_rg_state == 1 && n >= 3 && nv_get16 (b) == nv_rg_seq) {
			if (b[2]) { nv_rg_state = 2; if (nv_svn && nv_sv_peer) nv_sv_take (); else nv_curn = 0; nv_log ("player 2 has the next game: both switch"); }
			else { char nm[200] = ""; nv_jstr (nv_rg_game, "name", nm, sizeof nm); snprintf (nv_rg_msg, sizeof nv_rg_msg, "the other player does not have %s", nm);
				nv_rg_state = 0; nv_log (nv_rg_msg); } } break;
	case 'S': nv_on_sum (b, n); break;
	case 'c': if (nv_role == 1 && nv_card && !nv_card_done && n >= 4) {   /* (player 2's answer: the files it has alike) */
			const int k = (int) nv_get32 (b); nv_only_set (b + 4, k < nv_nmine ? k : nv_nmine, nv_mine);
			for (int i = 0; i < 5; i++) nv_send ('K', NULL, 0);
			nv_card_done = 1; nv_ready = 1; char m[96]; snprintf (m, sizeof m, "SD card: %d of %d files alike - both start", nv_nonly, nv_nmine); nv_log (m); } break;
	case 'C': if (nv_role == 2 && nv_card && !nv_card_done && n >= 4) {   /* (the host's card list, a piece) */
			const int i = (int) nv_get16 (b), t = (int) nv_get16 (b + 2); if (t <= 0 || t > 512 || i >= t) break;
			if (nv_ctotal != t) { for (int q = 0; q < 512; q++) { free (nv_cparts[q]); nv_cparts[q] = NULL; } nv_ctotal = t; nv_cgot = 0; }
			if (!nv_cparts[i]) { nv_cparts[i] = malloc (n - 4 + 1); memcpy (nv_cparts[i], b + 4, n - 4); nv_cpartn[i] = n - 4; nv_cgot++; }
			if (nv_cgot == nv_ctotal && !nv_cbitn) {
				if (!nv_card_unpack ()) { for (int q = 0; q < 512; q++) { free (nv_cparts[q]); nv_cparts[q] = NULL; } nv_ctotal = 0; nv_log ("the card list came damaged: again"); break; }
				nv_card_mine (nv_rom_name);                     /* (which of the host's files this side has, the same to the byte) */
				memset (nv_cbits, 0, sizeof nv_cbits); const int m = nv_ntheirs < 4096 ? nv_ntheirs : 4096;
				for (int q = 0; q < m; q++) for (int r = 0; r < nv_nmine; r++)
					if (!nv_cmp_rel (&nv_theirs[q], &nv_mine[r]) && nv_theirs[q].size == nv_mine[r].size && !strcmp (nv_theirs[q].md5, nv_mine[r].md5)) { nv_cbits[q >> 3] |= (unsigned char) (1 << (q & 7)); break; }
				nv_cbitn = m; nv_only_set (nv_cbits, m, nv_theirs); char t2[96]; snprintf (t2, sizeof t2, "SD card: %d of the host's %d files alike", nv_nonly, m); nv_log (t2); } } break;
	case 'K': if (nv_role == 2 && nv_card && nv_cbitn && !nv_card_done) { nv_card_done = 1; nv_ready = 1; nv_log ("the host has the answer: both start"); } break;
	case 'Z': if (n >= 3) { const unsigned seq = nv_get16 (b + 1);   /* (the other side's own pause: the newest of its own) */
			if (nv_their_seq < 0 || ((seq - (unsigned) nv_their_seq) & 0xffff) < 0x8000) {
				nv_their_seq = (int) seq; nv_theirpause = b[0] ? (n >= 4 && b[3] ? b[3] : 1) : 0; nv_pause_upd (); } } break;
	case 'Q': nv_lost = 1; nv_left = 1; nv_log ("the other player left"); if (nv_phase < NV_PLAY) nv_phase = NV_ENDED; break;
	default: break; }
}

/* ---- the LAN: a host's announcements, the games heard */
static void nv_lan_beacon (void)
{
	nv_sock_t b = nv_s; if (b == NV_BADSOCK) return;
	char nm[300], gm[40], j[640]; nv_jesc (nm, sizeof nm, nv_rom_name); nv_jesc (gm, sizeof gm, nv_gamer);
	const int n = snprintf (j, sizeof j, "VXL1{\"name\": %s, \"md5\": \"%s\", \"size\": %lld, \"port\": %d, \"gamer\": %s, \"veclink\": false, \"nver\": %d}",
		nm, nv_rom_md5, nv_rom_size, nv_my_port (), gm, NV_VER);
	struct sockaddr_in a; nv_addr (&a, "255.255.255.255", NV_LANPORT);
	int one = 1; setsockopt (b, SOL_SOCKET, SO_BROADCAST, (const char *) &one, sizeof one);
	sendto (b, j, n, 0, (struct sockaddr *) &a, sizeof a);
	char ip[64]; nv_lan_addr (ip, sizeof ip); unsigned a1, a2, a3, a4;   /* (each network's own broadcast address: a /24 assumed, as the desktop) */
	if (sscanf (ip, "%u.%u.%u.%u", &a1, &a2, &a3, &a4) == 4) { char bc[32]; snprintf (bc, sizeof bc, "%u.%u.%u.255", a1, a2, a3);
		if (nv_addr (&a, bc, NV_LANPORT)) sendto (b, j, n, 0, (struct sockaddr *) &a, sizeof a); }
#ifndef _WIN32
	/* (and every other network of this device: an ad-hoc one has no way to the internet - a phone's Wi-Fi Direct group (p2p0),
	 *  its hotspot - and 255.255.255.255 leaves by the main one only) */
	{ struct ifaddrs *ia = NULL; if (getifaddrs (&ia) == 0) { for (struct ifaddrs *i = ia; i; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
		struct sockaddr_in t; memset (&t, 0, sizeof t); t.sin_family = AF_INET; t.sin_port = htons (NV_LANPORT);
		if ((i->ifa_flags & IFF_BROADCAST) && i->ifa_broadaddr && i->ifa_broadaddr->sa_family == AF_INET) t.sin_addr = ((struct sockaddr_in *) i->ifa_broadaddr)->sin_addr;
		else if (i->ifa_netmask) t.sin_addr.s_addr = ((struct sockaddr_in *) i->ifa_addr)->sin_addr.s_addr | ~((struct sockaddr_in *) i->ifa_netmask)->sin_addr.s_addr;
		else continue;
		sendto (b, j, n, 0, (struct sockaddr *) &t, sizeof t); }
		freeifaddrs (ia); } }
#endif
}
static void nv_lan_take (void)
{
	if (nv_ls == NV_BADSOCK) return;
	for (;;) { unsigned char b[2048]; struct sockaddr_in src; socklen_t sl = sizeof src;
		const int n = (int) recvfrom (nv_ls, (char *) b, sizeof b - 1, 0, (struct sockaddr *) &src, &sl); if (n <= 0) break;
		b[n] = 0; if (n < 5 || memcmp (b, "VXL1", 4)) continue;
		const char *j = (const char *) b + 4; char ip[48]; snprintf (ip, sizeof ip, "%s", inet_ntoa (src.sin_addr)); double v;
		int port = nv_jnum (j, "port", &v) ? (int) v : NV_PORT, k;
		for (k = 0; k < nv_nlans; k++) if (!strcmp (nv_lans[k].ip, ip) && nv_lans[k].port == port) break;
		if (k == nv_nlans) { if (nv_nlans == 16) continue; nv_nlans++; }
		nv_lan *g = &nv_lans[k]; snprintf (g->ip, sizeof g->ip, "%s", ip); g->port = port;
		if (!nv_jstr (j, "name", g->name, sizeof g->name)) g->name[0] = 0; if (!nv_jstr (j, "md5", g->md5, sizeof g->md5)) g->md5[0] = 0;
		if (!nv_jstr (j, "gamer", g->gamer, sizeof g->gamer)) g->gamer[0] = 0;
		g->size = nv_jnum (j, "size", &v) ? (long long) v : 0; g->nver = nv_jnum (j, "nver", &v) ? (int) v : NV_VER; g->seen = nv_now (); }
	for (int k = 0; k < nv_nlans; ) if (nv_now () - nv_lans[k].seen > 3.5) { memmove (nv_lans + k, nv_lans + k + 1, sizeof *nv_lans * (size_t) (nv_nlans - k - 1)); nv_nlans--; } else k++;
}

/* ---- the thread: the packets in, the handshake's timers */
static void nv_pump (void)
{
	if (nv_s == NV_BADSOCK) return;
	for (;;) { unsigned char b[2048]; struct sockaddr_in src; socklen_t sl = sizeof src;
		const int n = (int) recvfrom (nv_s, (char *) b, sizeof b, 0, (struct sockaddr *) &src, &sl); if (n <= 0) break;
		if (n >= 5 && !memcmp (b, "VXR1", 4)) {
			if (src.sin_addr.s_addr != nv_srv.sin_addr.s_addr) continue;   /* (not from the meeting point) */
			if (b[4] == 'F' && n >= 10 && !memcmp (b + 5, "VXN1", 4)) { nv_on_packet (b[9], b + 10, (size_t) n - 10, &src, 1); continue; }
			nv_on_server (b[4], (const char *) b + 5, (size_t) n - 5); continue; }
		if (n < 5 || memcmp (b, "VXN1", 4)) continue;
		if (!nv_known (&src)) continue;                           /* (not from the other player) */
		nv_on_packet (b[4], b + 5, (size_t) n - 5, &src, 0); }
}
static double nv_t_srv, nv_t_hello, nv_t_beacon, nv_t_punch, nv_t_pause, nv_t_ka;
static void nv_hello (void)
{
	unsigned char h[14]; nv_put16 (h, NV_VER); const size_t m = strlen (nv_gamer) > 12 ? 12 : strlen (nv_gamer); memcpy (h + 2, nv_gamer, m);
	if (nv_ncand) for (int i = 0; i < nv_ncand; i++) nv_send_to ('H', h, 2 + m, &nv_cand[i]);
	else if (nv_have_peer) nv_send_to ('H', h, 2 + m, &nv_peer);
	if (nv_code[0] && nv_ncand && nv_now () - nv_punch_t0 > NV_PUNCH * 0.5) {   /* (through the relay too: some routers never let it through) */
		unsigned char r[64]; const size_t c = strlen (nv_code); memcpy (r, "VXR1X", 5); memcpy (r + 5, nv_code, c); memcpy (r + 5 + c, "VXN1H", 5); memcpy (r + 10 + c, h, 2 + m); nv_raw (r, 12 + c + m, &nv_srv); }
}
static char nv_me[64];
static void nv_tick (void)
{
	const double now = nv_now ();
	const char *me = nv_me;
	if (nv_phase == NV_MEET || (nv_phase == NV_WAIT && nv_role == 1 && nv_mode < 2)) {   /* the meeting point */
		if (nv_role == 1 && !nv_code[0] && now - nv_t_srv > 1.0) { nv_t_srv = now;
			char nm[300], gm[40], j[900]; nv_jesc (nm, sizeof nm, nv_rom_name); nv_jesc (gm, sizeof gm, nv_gamer);
			snprintf (j, sizeof j, "VXR1R{\"name\": %s, \"md5\": \"%s\", \"size\": %lld, \"lan\": [\"%s\", %d], \"ver\": %d, \"key\": \"%s\", \"pub\": %d, \"gamer\": %s}",
				nm, nv_rom_md5, nv_rom_size, me, nv_my_port (), NV_VER, nv_key, nv_mode == 0 ? 1 : 0, gm);
			nv_srvsend (j); }
		else if (nv_role == 1 && nv_code[0] && now - nv_t_ka > 2.0) { char k[32]; snprintf (k, sizeof k, "VXR1K%s", nv_code); nv_srvsend (k); nv_t_ka = now; }
		else if (nv_role == 2 && !nv_ncand && now - nv_t_srv > 1.0) { nv_t_srv = now;
			char j[400]; snprintf (j, sizeof j, "VXR1J%s{\"lan\": [\"%s\", %d], \"name\": \"%s\", \"key\": \"%s\"}", nv_code, me, nv_my_port (), nv_gamer, nv_key); nv_srvsend (j); }
		if (nv_role == 2 && nv_ncand && nv_phase == NV_MEET) nv_phase = NV_WAIT; }
	if (nv_role == 1 && nv_phase == NV_WAIT && nv_ncand && now - nv_t_punch > 0.1 && nv_rtt != -1) {   /* (the host: opening the way) */
		nv_t_punch = now; for (int i = 0; i < nv_ncand; i++) nv_send_to ('h', NULL, 0, &nv_cand[i]); if (now - nv_punch_t0 > NV_PUNCH) nv_path (); }
	if (nv_role == 1 && nv_mode == 2 && nv_phase == NV_WAIT && now - nv_t_beacon > 1.0 && nv_rtt != -1) { nv_t_beacon = now; nv_lan_beacon (); }
	if (nv_role == 2 && nv_phase == NV_WAIT && now - nv_t_hello > 0.2) { nv_t_hello = now; nv_hello (); if (nv_ncand && now - nv_punch_t0 > NV_PUNCH) nv_path ();
		if (now - nv_last_heard > 20.0 && nv_last_heard > 0) { snprintf (nv_err, sizeof nv_err, "the host does not answer"); nv_phase = NV_ENDED; } }
	/* the host: the round trip (8 pings, the slowest), then the game until player 2 answers */
	if (nv_role == 1 && nv_phase == NV_WAIT && nv_rtt == -1 && nv_have_peer + nv_relay) {
		const int pings = nv_ping_got >= 4 ? 8 : 24;           /* (fewer than 4 answers: on, up to 24 - a slow path still measured) */
		if (nv_ping_i < pings && nv_ping_got < 8 && (nv_ping_sent == 0 || now - nv_ping_sent > 0.5)) {
			if (nv_ping_sent) nv_ping_i++;                       /* (no answer in 0.5 s: the next) */
			if (nv_ping_i < pings) { unsigned char p[12]; nv_put32 (p, (uint32_t) nv_ping_i); memcpy (p + 4, &now, 8); nv_send ('P', p, 12); nv_ping_sent = now; } }
		if ((nv_ping_i >= pings || nv_ping_got >= 8) && now - nv_ping_sent > (nv_ping_got ? 0.0 : 1.0)) {
			/* (the round trip: the second slowest of the 8 - one late answer (a phone's Wi-Fi waking up) set 12 frames for the
			 *  whole game. The delay covers the way there (half the round trip, 50 game frames a second) and 2 frames of
			 *  unevenness: the input is there when the other side's game needs it) */
			const int g = nv_ping_got < 8 ? nv_ping_got : 8; double t[8]; for (int i = 0; i < g; i++) t[i] = nv_ping_t[i];
			for (int i = 0; i < g; i++) for (int j = i + 1; j < g; j++) if (t[j] > t[i]) { const double x = t[i]; t[i] = t[j]; t[j] = x; }
			nv_rtt = g >= 4 ? t[1] : g ? t[0] : 0.3;             /* (no answer at all: a slow path assumed, not a fast one) */
			nv_delay = (int) (nv_rtt * 25.0 + 0.999) + 2 + (nv_relay ? 2 : 0); if (nv_delay < 3) nv_delay = 3;
			{ const int p2p = nv_have_peer && nv_peer.sin_addr.s_addr == htonl (0x7f000001u);   /* (the player through the iOS app's bridge: Apple's
			                                                 peer-to-peer Wi-Fi - fast, but it stops a few hundred ms now and then while the radio
			                                                 visits the ordinary Wi-Fi: 28 ms, slowest 343, 3 frames - both games stood and raced, the
			                                                 iPad / iPhone 2026-11-19. Room for its pauses: 6 frames, 8 when one answer was slow) */
			  if (p2p) { const int f = nv_ping_max > 0.15 ? 8 : 6; if (nv_delay < f) nv_delay = f; } }
			if (nv_delay > 12) nv_delay = 12;   /* (relayed: 2 frames more - its way varies more) */
			nv_make_game (); char m[160]; snprintf (m, sizeof m, "round trip %.0f ms (slowest %.0f, %d of %d answered)%s: %d frames of input delay", nv_rtt * 1000, nv_ping_max * 1000, nv_ping_got, nv_ping_i, nv_relay ? ", relayed" : "", nv_delay); nv_log (m); } }
	if (nv_role == 1 && nv_phase == NV_WAIT && nv_rtt >= 0 && now - nv_g_sent > 0.05) { nv_g_sent = now; nv_send ('G', nv_game, strlen (nv_game)); }
	if (nv_role == 1 && nv_sv_state == 1 && nv_svn && (nv_phase == NV_AGREED || (nv_phase == NV_PLAY && nv_rg_state == 1)) && now - nv_t_sv > 0.004) {
		nv_t_sv = now; const int t = (int) ((nv_svn + NV_CARD_CHUNK - 1) / NV_CARD_CHUNK), i = nv_sv_cidx++ % t;   /* (the host's package, piece by piece, again and again) */
		unsigned char pk[10 + NV_CARD_CHUNK]; nv_put16 (pk, (unsigned) nv_sv_id); nv_put16 (pk + 2, (unsigned) i); nv_put16 (pk + 4, (unsigned) t); nv_put32 (pk + 6, (uint32_t) nv_svn);
		const size_t k = nv_svn - (size_t) i * NV_CARD_CHUNK < NV_CARD_CHUNK ? nv_svn - (size_t) i * NV_CARD_CHUNK : NV_CARD_CHUNK; memcpy (pk + 10, nv_sv + (size_t) i * NV_CARD_CHUNK, k); nv_send ('W', pk, 10 + k); }
	if (nv_role == 1 && nv_phase == NV_AGREED && nv_card && !nv_card_done && nv_czn && now - nv_t_card > 0.004) {   /* (the host: its card list, piece by piece) */
		nv_t_card = now; const int t = (int) ((nv_czn + NV_CARD_CHUNK - 1) / NV_CARD_CHUNK); const int i = nv_cidx % t; nv_cidx++;
		unsigned char pk[4 + NV_CARD_CHUNK]; nv_put16 (pk, (unsigned) i); nv_put16 (pk + 2, (unsigned) t);
		const size_t k = nv_czn - (size_t) i * NV_CARD_CHUNK < NV_CARD_CHUNK ? nv_czn - (size_t) i * NV_CARD_CHUNK : NV_CARD_CHUNK; memcpy (pk + 4, nv_cz + (size_t) i * NV_CARD_CHUNK, k); nv_send ('C', pk, 4 + k); }
	if (nv_role == 2 && nv_card && nv_cbitn && !nv_card_done && now - nv_t_card > 0.05) {   /* (player 2: its answer, until the host starts) */
		nv_t_card = now; unsigned char pk[4 + 512]; nv_put32 (pk, (uint32_t) nv_cbitn); const size_t nb = (size_t) (nv_cbitn + 7) / 8; memcpy (pk + 4, nv_cbits, nb); nv_send ('c', pk, 4 + nb); }
	if (nv_role == 1 && nv_hot == 1 && nv_hzn && now - nv_t_hot > 0.003) { nv_t_hot = now;
		const int t = (int) ((nv_hzn + NV_CARD_CHUNK - 1) / NV_CARD_CHUNK);
		for (int q = 0; q < 6; q++) { const int i = nv_cidx % t; nv_cidx++; unsigned char pk[4 + NV_CARD_CHUNK]; nv_put16 (pk, (unsigned) i); nv_put16 (pk + 2, (unsigned) t);
			const size_t k = nv_hzn - (size_t) i * NV_CARD_CHUNK < NV_CARD_CHUNK ? nv_hzn - (size_t) i * NV_CARD_CHUNK : NV_CARD_CHUNK; memcpy (pk + 4, nv_hz + (size_t) i * NV_CARD_CHUNK, k); nv_send ('J', pk, 4 + k); } }
	/* playing or paused: the inputs and the chat go on, the pause is said now and then */
	if (nv_role == 1 && nv_phase == NV_PLAY && now < nv_x_until && now - nv_t_x > 0.1) { nv_t_x = now; unsigned char pk[2]; nv_put16 (pk, nv_x_seq); nv_send ('X', pk, 2); }
	if (nv_phase == NV_PLAY && nv_rg_state && now - nv_t_rg > 0.1) { nv_t_rg = now;
		if (nv_role == 1 && nv_rg_state == 1) { unsigned char pk[600]; const size_t k = strlen (nv_rg_game); nv_put16 (pk, nv_rg_seq); memcpy (pk + 2, nv_rg_game, k); nv_send ('R', pk, 2 + k);
			if (now - nv_rg_t0 > 8.0) { snprintf (nv_rg_msg, sizeof nv_rg_msg, "the other player did not answer"); nv_rg_state = 0; } }
		if (nv_role == 2 && nv_rg_state == 2 && (!nv_sv_need || nv_sv_state == 2)) { unsigned char pk[3]; nv_put16 (pk, nv_rg_seq); pk[2] = 1; nv_send ('r', pk, 3); } }
	if (nv_phase == NV_PLAY) {
		if (now - nv_last_send > 0.03) nv_send_inputs ();
		if (nv_ncout && now - nv_t_pause > 0.15) { nv_send_chat (now); nv_t_pause = now; }
		if (now - nv_t_z > 0.25) { unsigned char z[4]; z[0] = nv_mypause ? 1 : 0; nv_put16 (z + 1, (unsigned) nv_pause_seq); z[3] = (unsigned char) nv_mypause; nv_send ('Z', z, 4); nv_t_z = now; }
		if (now - nv_last_heard > NV_TIMEOUT) { if (!nv_lost) nv_log ("connection lost"); nv_lost = 1; } }
	if (nv_lan_on) nv_lan_take ();
	if (nv_room_state == 1 && now - nv_t_srv > 0.5) { nv_t_srv = now; nv_raw ("VXR1L\0", 6, &nv_srv); if (now - nv_room_t0 > 3.0) nv_room_state = 3; }
}
#ifdef _WIN32
static DWORD WINAPI nv_main (LPVOID u)
#else
static void *nv_main (void *u)
#endif
{
	(void) u;
	while (!nv_quit_th) {
		if (nv_s != NV_BADSOCK) { fd_set r; FD_ZERO (&r); FD_SET (nv_s, &r); nv_sock_t mx = nv_s;
			if (nv_ls != NV_BADSOCK) { FD_SET (nv_ls, &r); if (nv_ls > mx) mx = nv_ls; }
			struct timeval tv = { 0, 3000 }; select ((int) mx + 1, &r, NULL, NULL, &tv); }
		else nv_sleep_ms (5);
		nv_lock (); nv_pump (); nv_tick (); nv_unlock (); }
	nv_th_on = 0;
	return 0;
}
static void nv_thread (void)
{
	if (nv_th_on) return; nv_quit_th = 0; nv_th_on = 1;
#ifdef _WIN32
	nv_th = CreateThread (NULL, 0, nv_main, NULL, 0, NULL); if (!nv_th) nv_th_on = 0;
#else
	if (pthread_create (&nv_th, NULL, nv_main, NULL)) nv_th_on = 0; else pthread_detach (nv_th);
#endif
}

/* ---- line streaming (2026-11-20): a card game (UVMC2, PiTrex) - its frame takes 4-12 ms on a fast PC (VecDoom up to 26):
 * no rollback (it would run several again in one refresh), and lockstep stood both at every network pause. The host plays
 * it as offline (its own settings, no card compared) and sends each frame's lines and sound; player 2 does not run it - it
 * shows the frames as they come (a phone needs no power for VecDoom then) and sends its controller. The host never waits;
 * player 2 sees the game one network trip late. An older side (the PC today): the lockstep as before */
#ifndef AQ
#define AQ 65536
#endif
static int16_t aq[AQ * 2]; static int aq_r, aq_w; static long aq_frames; static int emu_stood; static unsigned long long run_t;   /* (defined in
                                                 libretro_vx.c further down - the sound queue, the time to reach) */
static int aq_fill (void); static void frame_push (unsigned long long p0);
#define NV_ST_MAXL 6000
#define NV_ST_MAXA 1200
#define NV_ST_MAXC 240                                         /* (pieces a frame at most, the repair ones with them) */
#define NV_ST_GROUP 4                                          /* (one repair piece for each 4: one lost of them comes back) */
static unsigned char nv_st_raw[64 + NV_ST_MAXL * 26 + NV_ST_MAXA], nv_st_z[sizeof nv_st_raw + sizeof nv_st_raw / 127 + 64];
typedef struct { uint32_t fno; int nd, got, done; size_t zlen, rawn, csz; unsigned char mask[NV_ST_MAXC]; unsigned char *buf, *par; } nv_stasm_t;
static nv_stasm_t nv_sta[6];
typedef struct { uint32_t fno; unsigned long long p0, tc; int nl, na; line_t *L; int16_t *a; } nv_stfr_t;
static nv_stfr_t nv_stq[16]; static int nv_st_started; static uint32_t nv_st_last; static long nv_st_n_ref, nv_st_n_new, nv_st_n_two, nv_st_n_fix;
static nv_in nv_st_in; static uint32_t nv_st_in_seq; static int nv_st_in_ok; static unsigned char nv_st_resets, nv_st_resets_done, nv_st_take_seq, nv_st_take_val, nv_st_take_done;
static unsigned char nv_st_my_resets, nv_st_my_take_seq, nv_st_my_take_val; static uint32_t nv_st_out_seq; static uint32_t nv_st_fno;
static void nv_st_put16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
static unsigned nv_st_get16 (const unsigned char *p) { return (unsigned) p[0] | ((unsigned) p[1] << 8); }
/* (the lines, small: a stroke that goes on where the last ended is one flag; the rest differences, 7 bits a byte) */
static unsigned char *nv_vput (unsigned char *o, unsigned long long v) { while (v >= 0x80) { *o++ = (unsigned char) (v | 0x80); v >>= 7; } *o++ = (unsigned char) v; return o; }
static unsigned char *nv_zput (unsigned char *o, long long v) { return nv_vput (o, v < 0 ? ((unsigned long long) (-(v + 1)) << 1) | 1 : (unsigned long long) v << 1); }
static const unsigned char *nv_vget (const unsigned char *i, const unsigned char *e, unsigned long long *v)
{ unsigned long long r = 0; int sh = 0; while (i < e) { const unsigned char c = *i++; r |= (unsigned long long) (c & 0x7f) << sh; if (!(c & 0x80)) { *v = r; return i; } sh += 7; if (sh > 63) break; } *v = 0; return NULL; }
static const unsigned char *nv_zget (const unsigned char *i, const unsigned char *e, long long *v)
{ unsigned long long u; i = nv_vget (i, e, &u); *v = (u & 1) ? -(long long) (u >> 1) - 1 : (long long) (u >> 1); return i; }
/* (the sound: 8-bit mu-law, 22 kHz) */
static unsigned char nv_mu_enc (int x)
{
	const int sign = x < 0 ? 0x80 : 0; if (x < 0) x = -x; if (x > 32635) x = 32635; x += 132;
	int e = 7; for (int m = 0x4000; !(x & m) && e > 0; m >>= 1) e--;
	return (unsigned char) ~(sign | (e << 4) | ((x >> (e + 3)) & 15));
}
static int nv_mu_dec (unsigned char u) { u = (unsigned char) ~u; const int e = (u >> 4) & 7, m = u & 15, x = (((m << 3) + 132) << e) - 132; return u & 0x80 ? -x : x; }
static size_t nv_st_pack (unsigned char *o0, unsigned long long p0, int nl, int na, int a0)
{
	unsigned char *o = o0; nv_put32 (o, nv_st_fno); memcpy (o + 4, &p0, 8); { const unsigned long long tc = total_cycles; memcpy (o + 12, &tc, 8); }
	nv_st_put16 (o + 20, (unsigned) nl); nv_st_put16 (o + 22, (unsigned) na); o += 24;
	long px = 0, py = 0; unsigned char pc = 0, prgb = 0; unsigned long long pend = 0;
	for (int i = 0; i < nl; i++) { const line_t *l = &last[i];
		const int cont = l->x0 == px && l->y0 == py, samec = l->c == pc, samergb = l->rgb == prgb, sameT = (unsigned long long) l->t0 == pend;
		*o++ = (unsigned char) (cont | samec << 1 | samergb << 2 | sameT << 3);
		if (!cont) { o = nv_zput (o, (long long) l->x0 - px); o = nv_zput (o, (long long) l->y0 - py); }
		o = nv_zput (o, (long long) l->x1 - l->x0); o = nv_zput (o, (long long) l->y1 - l->y0);
		if (!samec) *o++ = l->c; if (!samergb) *o++ = l->rgb;
		o = nv_vput (o, l->cyc); if (!sameT) o = nv_zput (o, (long long) l->t0 - (long long) pend);
		px = l->x1; py = l->y1; pc = l->c; prgb = l->rgb; pend = (unsigned long long) l->t0 + l->cyc; }
	{ int i = a0; for (int k = 0; k < na; k++) { *o++ = nv_mu_enc (aq[2 * i]); i = (i + 2) % AQ; } }
	return (size_t) (o - o0);
}
/* (the host, after each frame: its lines and the sound it made - from the sound queue's a0 on - in pieces, a repair piece
 *  (all four XORed) after each four: one lost of them is made again there) */
static void netvx_stream_send (unsigned long long p0, int a0)
{
	const int nl = nlast < NV_ST_MAXL ? nlast : NV_ST_MAXL;
	int na = 0; { int i = a0; while (i != aq_w && na < NV_ST_MAXA * 2) { i = (i + 1) % AQ; na++; } } na /= 2;
	const size_t raw = nv_st_pack (nv_st_raw, p0, nl, na, a0);
	size_t zn = nv_lz (nv_st_raw, raw, nv_st_z + 1); const unsigned char *z; if (zn < raw) { nv_st_z[0] = 1; z = nv_st_z; zn++; }   /* (packed when smaller) */
	else { nv_st_z[0] = 0; memmove (nv_st_z + 1, nv_st_raw, raw); z = nv_st_z; zn = raw + 1; }
	{ static double sr, sz, sl; static long c; sr += raw; sz += zn; sl += nl; if (++c % 250 == 0 && getenv ("VX_STSIZE")) { fprintf (stderr, "STSIZE lines %.0f raw %.0f sent %.0f bytes a frame%c", sl / 250, sr / 250, sz / 250, 10); sr = sz = sl = 0; } }
	nv_lock ();
	const size_t C = (nv_relay && nv_code[0]) ? 560 : 1180;      /* (through the meeting point: its packets' size) */
	const int nd = (int) ((zn + C - 1) / C), ng = (nd + NV_ST_GROUP - 1) / NV_ST_GROUP;
	if (nd + ng > NV_ST_MAXC) { nv_unlock (); nv_st_fno++; return; }
	unsigned char pk[18 + 1180], par[1180];
	for (int g = 0; g < ng; g++) { memset (par, 0, C);
		for (int i = g * NV_ST_GROUP; i < nd && i < (g + 1) * NV_ST_GROUP; i++) {
			const size_t k = zn - (size_t) i * C < C ? zn - (size_t) i * C : C;
			nv_put32 (pk, nv_st_fno); nv_st_put16 (pk + 4, (unsigned) i); nv_st_put16 (pk + 6, (unsigned) nd); nv_put32 (pk + 8, (uint32_t) zn); nv_put32 (pk + 12, (uint32_t) raw); nv_st_put16 (pk + 16, (unsigned) C);
			memcpy (pk + 18, z + (size_t) i * C, k); for (size_t q = 0; q < k; q++) par[q] ^= z[(size_t) i * C + q]; nv_send ('L', pk, 18 + k); }
		nv_put32 (pk, nv_st_fno); nv_st_put16 (pk + 4, (unsigned) (nd + g)); nv_st_put16 (pk + 6, (unsigned) nd); nv_put32 (pk + 8, (uint32_t) zn); nv_put32 (pk + 12, (uint32_t) raw); nv_st_put16 (pk + 16, (unsigned) C);
		memcpy (pk + 18, par, C); nv_send ('L', pk, 18 + C); }
	nv_unlock (); nv_st_fno++;
}
static int nv_st_unpack (const unsigned char *r, size_t rn, nv_stfr_t *q)
{
	if (rn < 24) return 0; const unsigned char *e = r + rn;
	q->fno = nv_get32 (r); memcpy (&q->p0, r + 4, 8); memcpy (&q->tc, r + 12, 8); q->nl = (int) nv_st_get16 (r + 20); q->na = (int) nv_st_get16 (r + 22);
	if (q->nl > NV_ST_MAXL || q->na > NV_ST_MAXA) return 0;
	q->L = malloc (sizeof (line_t) * (size_t) (q->nl ? q->nl : 1)); q->a = malloc (sizeof (int16_t) * (size_t) (q->na ? q->na : 1)); if (!q->L || !q->a) { free (q->L); free (q->a); return 0; }
	const unsigned char *i = r + 24; long px = 0, py = 0; unsigned char pc = 0, prgb = 0; unsigned long long pend = 0;
	for (int k = 0; k < q->nl; k++) { line_t *l = &q->L[k]; long long v; unsigned long long u;
		if (!i || i >= e) goto bad; const unsigned char f = *i++;
		if (f & 1) { l->x0 = px; l->y0 = py; } else { i = nv_zget (i, e, &v); l->x0 = px + (long) v; if (!i) goto bad; i = nv_zget (i, e, &v); l->y0 = py + (long) v; if (!i) goto bad; }
		i = nv_zget (i, e, &v); if (!i) goto bad; l->x1 = l->x0 + (long) v; i = nv_zget (i, e, &v); if (!i) goto bad; l->y1 = l->y0 + (long) v;
		if (f & 2) l->c = pc; else { if (i >= e) goto bad; l->c = *i++; }
		if (f & 4) l->rgb = prgb; else { if (i >= e) goto bad; l->rgb = *i++; }
		i = nv_vget (i, e, &u); if (!i) goto bad; l->cyc = (unsigned) u;
		if (f & 8) l->t0 = (unsigned) pend; else { i = nv_zget (i, e, &v); if (!i) goto bad; l->t0 = (unsigned) ((long long) pend + v); }
		px = l->x1; py = l->y1; pc = l->c; prgb = l->rgb; pend = (unsigned long long) l->t0 + l->cyc; }
	if (!i || i + q->na > e) goto bad;
	for (int k = 0; k < q->na; k++) q->a[k] = (int16_t) nv_mu_dec (i[k]);
	return 1;
bad: free (q->L); free (q->a); return 0;
}
/* (player 2, the network's thread: a piece of a frame; a lost one made again from its group's repair piece; a whole
 *  frame into the queue, in order) */
static void nv_st_on_piece (const unsigned char *b, size_t n)
{
	if (n < 19 || nv_role != 2 || !nv_stream) return;
	const uint32_t f = nv_get32 (b); const int i = (int) nv_st_get16 (b + 4), nd = (int) nv_st_get16 (b + 6); const size_t zn = nv_get32 (b + 8), rawn = nv_get32 (b + 12), C = nv_st_get16 (b + 16);
	const int ng = (nd + NV_ST_GROUP - 1) / NV_ST_GROUP;
	if (nv_st_started && f <= nv_st_last) return;              /* (too late: newer shown) */
	if (nd <= 0 || i >= nd + ng || nd + ng > NV_ST_MAXC || C < 64 || C > 1180 || zn > (size_t) nd * C || zn > sizeof nv_st_z || rawn > sizeof nv_st_raw || n - 18 > C) return;
	nv_stasm_t *a = NULL; for (int k = 0; k < 6; k++) if (nv_sta[k].buf && nv_sta[k].fno == f && nv_sta[k].nd == nd) a = &nv_sta[k];
	if (!a) { int o = 0; for (int k = 1; k < 6; k++) if (!nv_sta[k].buf || (nv_sta[o].buf && nv_sta[k].fno < nv_sta[o].fno)) o = k;   /* (the oldest's place) */
		a = &nv_sta[o]; if (a->buf && !a->done) nv_st_n_lost++;
		if (!a->buf) { a->buf = malloc ((size_t) NV_ST_MAXC * 1180); a->par = malloc ((size_t) NV_ST_MAXC / NV_ST_GROUP * 1180 + 1180); } if (!a->buf || !a->par) return;
		a->fno = f; a->nd = nd; a->got = 0; a->done = 0; a->zlen = zn; a->rawn = rawn; a->csz = C; memset (a->mask, 0, sizeof a->mask); }
	if (a->done || a->mask[i] || a->csz != C) return;
	a->mask[i] = 1;
	if (i < nd) { memcpy (a->buf + (size_t) i * C, b + 18, n - 18); if (n - 18 < C) memset (a->buf + (size_t) i * C + (n - 18), 0, C - (n - 18)); a->got++; }
	else memcpy (a->par + (size_t) (i - nd) * C, b + 18, C);
	/* (a group with one piece missing and its repair piece here: the missing one is the others XORed with it) */
	for (int g = 0; g < ng && a->got < nd; g++) { int miss = -1, nm = 0; const int g0 = g * NV_ST_GROUP, g1 = g0 + NV_ST_GROUP < nd ? g0 + NV_ST_GROUP : nd;
		if (!a->mask[nd + g]) continue;
		for (int k = g0; k < g1; k++) if (!a->mask[k]) { miss = k; nm++; }
		if (nm != 1) continue;
		unsigned char *d = a->buf + (size_t) miss * C; memcpy (d, a->par + (size_t) g * C, C);
		for (int k = g0; k < g1; k++) if (k != miss) for (size_t q = 0; q < C; q++) d[q] ^= a->buf[(size_t) k * C + q];
		a->mask[miss] = 1; a->got++; nv_st_n_fix++; }
	if (a->got < nd) return;
	a->done = 1;
	static unsigned char raw[sizeof nv_st_raw]; nv_stfr_t fr; int ok;
	if (a->buf[0] == 1) ok = !nv_unlz (a->buf + 1, a->zlen - 1, raw, a->rawn) && nv_st_unpack (raw, a->rawn, &fr);
	else ok = a->zlen - 1 == a->rawn && nv_st_unpack (a->buf + 1, a->rawn, &fr);
	if (!ok) { nv_st_n_lost++; return; }
	if (nv_stq_n == 16) { free (nv_stq[0].L); free (nv_stq[0].a); memmove (nv_stq, nv_stq + 1, sizeof nv_stq[0] * 15); nv_stq_n--; nv_st_n_drop++; }
	int at = nv_stq_n; while (at > 0 && nv_stq[at - 1].fno > f) at--;
	if (at < nv_stq_n) memmove (nv_stq + at + 1, nv_stq + at, sizeof nv_stq[0] * (size_t) (nv_stq_n - at));
	nv_stq[at] = fr; nv_stq_n++; nv_st_n_got++;
	{ static double last_in; const double t = nv_now (); const double g = last_in > 0 ? t - last_in : 0; last_in = t;   /* (the longest wait for a frame: the log, and the
	                                                 buffer's size - Apple's peer-to-peer Wi-Fi stops for 110-130 ms about 4 times a second) */
	  if (g < 5.0) { if (g > nv_st_gap_max) nv_st_gap_max = g; nv_st_gap_recent = g > nv_st_gap_recent ? g : nv_st_gap_recent * 0.995; } }
}
/* (player 2, emu_loop: the frames come in their time - two waiting at most (a third: the oldest left out), none: the
 *  picture stands; and this side's controller to the host) */
static void netvx_stream_guest (void)
{
	nv_lock ();
	{ unsigned char pk[11]; nv_put32 (pk, ++nv_st_out_seq); int jx = (int) ls_pad[0] - 128, jy = (int) ls_pad[1] - 128;
	  if (ls_reset_req) { nv_st_my_resets++; ls_reset_req = 0; }
	  if (nv_take_req) { nv_st_my_take_seq++; nv_st_my_take_val = (unsigned char) nv_take_req; nv_take_req = 0; }
	  pk[4] = (unsigned char) (signed char) (jx < -127 ? -127 : jx > 127 ? 127 : jx); pk[5] = (unsigned char) (signed char) (jy < -127 ? -127 : jy > 127 ? 127 : jy);
	  pk[6] = ls_pad[2]; pk[7] = nv_st_my_resets; pk[8] = nv_st_my_take_seq; pk[9] = nv_st_my_take_val; pk[10] = 0; nv_send ('u', pk, 11); }
	if (nv_x_got) { nv_x_got = 0; if (nv_rg_state == 2) nv_switch_now (); nv_unlock (); run_t = total_cycles; emu_stood = 1; return; }   /* (the host switched) */
	if (nv_pause) { nv_unlock (); run_t = total_cycles; emu_stood = 1; return; }
	/* (Apple's peer-to-peer Wi-Fi brings the frames in bursts: up to the buffer + 6 kept, each shown when its time comes, one extra a
	 *  refresh while more than 3 wait - more than 3 left out at once was 10-30 frames in 5 s and the sound running dry) */
	while (nv_stq_n > nv_st_tgt + 6) { free (nv_stq[0].L); free (nv_stq[0].a); memmove (nv_stq, nv_stq + 1, sizeof nv_stq[0] * (size_t) (nv_stq_n - 1)); nv_stq_n--; nv_st_n_drop++; }
	/* (a small playout buffer: frames shown only once nv_st_tgt are here - a late one no longer stands the picture and makes it
	 *  jump after (iPhone from iPad, Apple's peer-to-peer Wi-Fi: choppy, 19 frames left out in 5 s); run dry: one frame more,
	 *  up to 8 - or at once what the longest recent wait needs; 6 s without: one less, down to 2) */
	if (!nv_st_started && !nv_st_buf) { nv_st_buf = 1; nv_st_tgt = 2; }
	if (nv_st_buf) {
		if (nv_stq_n >= nv_st_tgt) { nv_st_buf = 0; nv_st_calm = nv_now (); if (nv_st_started) run_t = total_cycles; }
		else { nv_unlock (); nv_st_n_ref++; run_t = total_cycles; emu_stood = nv_st_started; return; } }
	if (nv_now () - nv_st_calm > 6.0 && nv_st_tgt > 2 && nv_st_tgt > (int) (nv_st_gap_recent / 0.02) + 1) { nv_st_tgt--; nv_st_calm = nv_now (); }   /* (not below what the recent waits need) */
	int k = 0;
	while (nv_stq_n > 0 && (!nv_st_started || total_cycles < run_t) && (k < 1 || !nv_st_started)) {   /* (one a refresh at most: two in one
	                                                 picture flickered - a game drawing on alternate frames; ahead of the buffer, netvx_pace plays a little faster) */
		nv_stfr_t q = nv_stq[0]; memmove (nv_stq, nv_stq + 1, sizeof nv_stq[0] * (size_t) (nv_stq_n - 1)); nv_stq_n--;
		nv_unlock ();
		memcpy (last, q.L, sizeof (line_t) * (size_t) q.nl); nlast = q.nl; total_cycles = q.tc;
		frame_push (q.p0);
		for (int i = 0; i < q.na; i++) for (int r = 0; r < 2; r++) if (aq_fill () < AQ - 1) { aq[2 * aq_w] = aq[2 * aq_w + 1] = q.a[i]; aq_w = (aq_w + 1) % AQ; }
		aq_frames++;
		free (q.L); free (q.a);
		nv_lock (); nv_st_last = q.fno; nv_f = q.fno + 1; nv_n_frames++; if (!nv_st_started) { nv_st_started = 1; run_t = total_cycles; } k++; }
	nv_unlock ();
	nv_st_n_ref++; if (k) nv_st_n_new++; if (k > 1) nv_st_n_two++;
	{ static double due_t;                     /* (one due and none here for more than a frame (20 ms): run dry - wait for a fuller buffer;
	                                                 a few ms late is the host's own rhythm: counted so, it grew to 6 frames, 120 ms behind) */
	  if (k || !nv_st_started) due_t = 0;
	  else if (total_cycles < run_t && !nv_stq_n) { const double t = nv_now (); if (!due_t) due_t = t;
		else if (t - due_t > 0.02) { due_t = 0; nv_lock (); nv_st_buf = 1;   /* (one more - or at once as many as the longest recent wait needs, up to 8) */
			{ int need = (int) (nv_st_gap_recent / 0.02) + 1; if (need < nv_st_tgt + 1) need = nv_st_tgt + 1; nv_st_tgt = need > 8 ? 8 : need; }
			nv_st_calm = t; nv_st_n_dry++; nv_unlock (); } } }
	if (!k) { if (total_cycles < run_t) run_t = total_cycles; emu_stood = nv_st_started; }   /* (none shown: the picture as it was - the time held
	                                                 only when one was due and none is here; moved up to the last frame's end each refresh, it
	                                                 played every frame in two refreshes (17 ms, not its 20-28) and ran the buffer dry 10-18 times in 5 s) */
}
/* (the host, before each frame: its controller and player 2's last - a RESET or a port taken by player 2 once each) */
static int netvx_stream_input (unsigned char *m)
{
	nv_lock ();
	if (nv_role == 1 && nv_rg_state == 2) {                      /* (player 2 has the next game: switched here now, the word sent - no frames to carry it) */
		nv_x_seq = nv_rg_seq; nv_x_until = nv_now () + 3.0; nv_t_x = 0; nv_switch_now (); nv_unlock (); return 0; }
	if (nv_pause) { nv_unlock (); return 0; }
	nv_in mine, theirs = nv_st_in_ok ? nv_st_in : (nv_in) { 0, 0, 0, 0 };
	{ int jx = (int) ls_pad[0] - 128, jy = (int) ls_pad[1] - 128; mine.jx = (signed char) (jx < -127 ? -127 : jx > 127 ? 127 : jx); mine.jy = (signed char) (jy < -127 ? -127 : jy > 127 ? 127 : jy);
	  mine.b = ls_pad[2]; mine.fl = (unsigned char) (ls_reset_req ? 4 : 0); ls_reset_req = 0;
	  if (nv_take_req) { mine.fl |= nv_take_req == 1 ? 0x10 : 0x20; nv_take_req = 0; } }
	theirs.fl = 0;
	if (nv_st_resets != nv_st_resets_done) { nv_st_resets_done = nv_st_resets; theirs.fl |= 4; }
	if (nv_st_take_seq != nv_st_take_done) { nv_st_take_done = nv_st_take_seq; theirs.fl |= nv_st_take_val == 1 ? 0x10 : 0x20; }
	const nv_in *hi = &mine, *jo = &theirs;
	if ((hi->fl | jo->fl) & 4) { nv_reset_n++; nv_reset_by = hi->fl & 4 ? 1 : 2; }
	nv_ports_take (0, hi->fl); nv_ports_take (1, jo->fl);
	const nv_in *p1 = nv_ports[0] == 1 ? hi : nv_ports[1] == 1 ? jo : NULL, *p2 = nv_ports[0] == 2 ? hi : nv_ports[1] == 2 ? jo : NULL;
	m[0] = (unsigned char) ((p1 ? p1->jx : 0) + 128); m[1] = (unsigned char) ((p1 ? p1->jy : 0) + 128); m[2] = p1 ? p1->b : 0;
	m[3] = (unsigned char) (0x40 | ((hi->fl | jo->fl) & 4)); m[4] = (unsigned char) ((p2 ? p2->jx : 0) + 128); m[5] = (unsigned char) ((p2 ? p2->jy : 0) + 128); m[6] = p2 ? p2->b : 0; m[7] = 0;
	nv_n_frames++; nv_f++; nv_unlock ();   /* (the frame's number: the status, as in lockstep) */
	return 1;
}
static void nv_st_on_input (const unsigned char *b, size_t n)   /* (the host: player 2's controller - the newest) */
{
	if (n < 10 || nv_role != 1 || !nv_stream) return;
	const uint32_t s = nv_get32 (b); if (nv_st_in_ok && (int32_t) (s - nv_st_in_seq) <= 0) return;
	nv_st_in_seq = s; nv_st_in_ok = 1; nv_st_in.jx = (signed char) b[4]; nv_st_in.jy = (signed char) b[5]; nv_st_in.b = b[6];
	nv_st_resets = b[7]; if (b[8] != nv_st_take_seq) { nv_st_take_seq = b[8]; nv_st_take_val = b[9]; }
}
static void nv_st_clear (void)
{
	nv_stream = nv_stream_peer = 0; nv_st_in_ok = 0; nv_st_resets = nv_st_resets_done = 0; nv_st_take_seq = nv_st_take_done = 0; nv_st_fno = 0; nv_st_started = 0; nv_st_last = 0;
	nv_st_my_resets = nv_st_my_take_seq = 0; for (int k = 0; k < nv_stq_n; k++) { free (nv_stq[k].L); free (nv_stq[k].a); } nv_stq_n = 0;
	for (int k = 0; k < 6; k++) nv_sta[k].done = 1;
}

/* ---- the game frames (emu_loop): 1 and the frame's input (m: the core's 8 bytes) once both players' are there */
static int netvx_playing (void) { return nv_phase == NV_PLAY && nv_go; }
static void nv_ports_take (int side, unsigned char fl)            /* (netplay.py port_inputs: 'take port' 0x10 / 0x20) */
{
	const int k = fl & 0x10 ? 1 : fl & 0x20 ? 2 : 0; if (!k) return;
	nv_ports[side] = k; if (nv_ports[side ^ 1] == k) nv_ports[side ^ 1] = 3 - k;   /* (the other player: pushed to the other port - the PC alike) */
}
static int netvx_frame (unsigned char *m)
{
	nv_lock ();
	while (nv_lhave <= nv_f + (uint32_t) nv_dly) {              /* (this side's input `delay` frames on: the controller now) */
		nv_in *x = &nv_local[nv_lhave % NV_RING]; int jx = (int) ls_pad[0] - 128, jy = (int) ls_pad[1] - 128;
		x->jx = (signed char) (jx < -127 ? -127 : jx > 127 ? 127 : jx); x->jy = (signed char) (jy < -127 ? -127 : jy > 127 ? 127 : jy);
		x->b = ls_pad[2]; x->fl = (unsigned char) (ls_reset_req ? 4 : 0); ls_reset_req = 0;
		if (nv_role == 1 && nv_rg_state == 2) { x->fl |= 8; nv_rg_state = 3; }
		if (nv_take_req) { x->fl |= nv_take_req == 1 ? 0x10 : 0x20; nv_take_req = 0; }   /* (the host: the next game from this frame on, both) */
		nv_lhave++; }
	if (nv_now () - nv_last_send > 0.008) nv_send_inputs ();
	const int r = nv_f % NV_RING; int got = nv_rgot[r] && nv_rgot_f[r] == nv_f;
	{ static int no = -1; if (no < 0) no = getenv ("VX_NO_ROLLBACK") != NULL; nv_rb_on = vx_ok () && !no; }   /* (6809 games: rollback; a UVMC2 / PiTrex: lockstep - test: off) */
	if (nv_rb_on && nv_rb_peer && nv_dly > 2) nv_dly = 2;        /* (both with rollback: 2 frames of input delay - the guess covers the rest) */
	while (nv_conf < nv_f) {                                      /* (the frames both sides' real inputs are in for, in order: the sync check's sums sent) */
		const int q = nv_conf % NV_RING; if (!(nv_rgot[q] && nv_rgot_f[q] == nv_conf)) break;
		if (nv_used_pred[q] && nv_in_differ (&nv_remote[q], &nv_used[q])) { if (nv_rb_from < 0 || (int64_t) nv_conf < nv_rb_from) nv_rb_from = nv_conf; break; }   /* (a wrong guess: the rollback first) */
		if (nv_psum_ok[q] && nv_psum[q][0] == nv_conf) nv_sum_send (nv_psum[q][0], nv_psum[q][1]); nv_psum_ok[q] = 0;
		nv_conf++; }
	const int can_guess = !got && nv_rb_on && !nv_pause && nv_rlast_f >= 0 && (int64_t) nv_f - (int64_t) nv_conf < NV_RB_W && nv_rb_from < 0;
	static double wait_end;                                       /* (the other side's input late: a moment's wait, then the picture stands -
	                                                                 at most 12 ms in 50: a wait in each run of a refresh (the app runs 2-4 to catch up)
	                                                                 held the iPhone at 15 pictures a second, the iPad at 20) */
	if (!got && !can_guess && !nv_lost && !nv_pause && nv_now () - wait_end > 0.05) {
		const double t0 = nv_now (); nv_unlock ();
		do { nv_sleep_ms (1); nv_lock (); nv_pump (); got = nv_rgot[r] && nv_rgot_f[r] == nv_f; nv_unlock (); } while (!got && nv_now () - t0 < 0.012);
		wait_end = nv_now (); nv_lock (); nv_n_wait++; }
	if (!got && !can_guess && !nv_pause) nv_n_stood++;
	/* (the delay to the link: the game stood 3 times in 5 s, or 6 frames at once (a longer stand: a quarter of it more) - one frame more, up to 12; 15 s without
	 *  standing - one less, down to the agreed one. A Wi-Fi's (and Apple's peer-to-peer's) pauses are longer some evenings
	 *  than the pings showed: 6 frames stood 10-20 times in 5 s, the iPad / iPhone 2026-11-20. Frames are numbered: each
	 *  side's delay its own, the extra frames the controller as it is) */
	if (nv_f > 60 && !nv_pause && !nv_lost && !nv_rg_state && !(nv_rb_on && nv_rb_peer)) { const double t = nv_now ();   /* (both with rollback: the delay stays -
	                                                 against one in lockstep (the PC) it grows as before: that side waits for this one's input) */
		if (!got) { if (!nv_st_run++) { if (t - nv_st_t0 > 5.0) { nv_st_t0 = t; nv_st_ep = 0; } nv_st_ep++; }
			if ((nv_st_ep >= 3 || nv_st_run == 6) && nv_dly < 12) { nv_dly++; nv_st_ep = 0; nv_st_t0 = t;
				char m[64]; snprintf (m, sizeof m, "input delay %d frames (the link pauses)", nv_dly); nv_log (m); }
			nv_calm_t0 = t; }
		else { if (nv_st_run >= 8 && nv_dly < 12) {   /* (a long stand just over: as much more at once - half of it) */
				nv_dly += nv_st_run / 4; if (nv_dly > 12) nv_dly = 12; nv_st_ep = 0; nv_st_t0 = t;
				char m[64]; snprintf (m, sizeof m, "input delay %d frames (the link paused)", nv_dly); nv_log (m); }
			nv_st_run = 0; if (!nv_calm_t0) nv_calm_t0 = t;
			if (t - nv_calm_t0 > 15.0 && nv_dly > nv_delay) { nv_dly--; nv_calm_t0 = t; char m[64]; snprintf (m, sizeof m, "input delay %d frames (calm)", nv_dly); nv_log (m); } } }
	if ((!got && !can_guess) || nv_pause) { if (!nv_wait_t0) nv_wait_t0 = nv_now (); nv_unlock (); return 0; }
	nv_wait_t0 = 0;
	nv_in guess; const nv_in *mine = &nv_local[nv_f % NV_RING], *theirs = &nv_remote[r];
	if (!got) { guess = nv_rlast; guess.fl = 0; theirs = &guess; }   /* (the guess: the other side's last input, no moment's button) */
	const nv_in *hi = nv_role == 1 ? mine : theirs, *jo = nv_role == 1 ? theirs : mine;   /* (host, joiner) */
	if ((hi->fl & 8) && nv_f != nv_rg_done_f && nv_rg_rom[0] && nv_rb_on && !(got && nv_conf == nv_f)) { nv_unlock (); return 0; }   /* (a game switch:
	                                                 only on a frame both are sure of - no rollback across it) */
	if (!got) { const int q = nv_f % NV_RB_SLOTS;                 /* (a guess: this frame's state kept first) */
		{ static size_t tn; const size_t z = retro_serialize_size (); if (z > tn) { free (nv_rb_tmp); nv_rb_tmp = malloc (z + 16); tn = nv_rb_tmp ? z : 0; } }   /* (as large as this game's state can be) */
		const size_t n = nv_rb_tmp ? vx_save_to (nv_rb_tmp) : 0;
		if (n && nv_rb_cap[q] < n) { unsigned char *p = realloc (nv_rb_buf[q], n); if (p) { nv_rb_buf[q] = p; nv_rb_cap[q] = n; } }
		if (!n || nv_rb_cap[q] < n) { nv_unlock (); return 0; }   /* (no room: it waits, as without rollback) */
		memcpy (nv_rb_buf[q], nv_rb_tmp, n); nv_rb_len[q] = n; nv_rb_fr[q] = nv_f; nv_rb_ports[q][0] = nv_ports[0]; nv_rb_ports[q][1] = nv_ports[1];
		nv_rb_ctr[q][0] = frame_start_cycles; nv_rb_ctr[q][1] = last_busframe; nv_rb_ctr[q][2] = last_recal; nv_rb_ctr[q][3] = idle_cycles; nv_rb_ctr[q][4] = idle_frame;
		nv_n_pred++; }
	nv_n_frames++;
	if (nv_rb_on) { nv_used[r] = *theirs; nv_used_pred[r] = (unsigned char) !got; }
	nv_cur_conf = got && nv_conf == nv_f; if (nv_cur_conf) nv_conf++; nv_fd = nv_f + 1;
	{ static int lg = -1; if (lg < 0) lg = getenv ("VX_RBLOG") != NULL; if (lg) fprintf (stderr, "RUN %u %s %d,%d,%d,%d conf %u\n", nv_f, got ? "real" : "GUESS", theirs->jx, theirs->jy, theirs->b, theirs->fl, nv_conf); }
	if ((hi->fl & 8) && nv_f != nv_rg_done_f && nv_rg_rom[0]) {     /* (the next game: switched before this frame, on both sides) */
		nv_rg_done_f = nv_f; { char m[64]; snprintf (m, sizeof m, "the next game at frame %u", (unsigned) nv_f); nv_log (m); }
		for (int q = 0; q < NV_RB_SLOTS; q++) nv_rb_fr[q] = 0xffffffffu; nv_rb_from = -1; nv_conf = nv_f;   /* (the last game's states: gone) */
		nv_switch_now ();                                          /* (a card game next: streamed from here) */
		nv_unlock (); return 0; }
	if (((hi->fl | jo->fl) & 4) && (int64_t) nv_f > nv_reset_lastf) { nv_reset_lastf = nv_f; nv_reset_n++; nv_reset_by = hi->fl & 4 ? 1 : 2; }   /* (RESET: who - said on
	                                                 both screens, once - not again when the frame is run again) */
	nv_ports_take (0, hi->fl); nv_ports_take (1, jo->fl);
	const nv_in *p1 = nv_ports[0] == 1 ? hi : nv_ports[1] == 1 ? jo : NULL, *p2 = nv_ports[0] == 2 ? hi : nv_ports[1] == 2 ? jo : NULL;
	m[0] = (unsigned char) ((p1 ? p1->jx : 0) + 128); m[1] = (unsigned char) ((p1 ? p1->jy : 0) + 128); m[2] = p1 ? p1->b : 0;
	m[3] = (unsigned char) (0x40 | ((hi->fl | jo->fl) & 4)); m[4] = (unsigned char) ((p2 ? p2->jx : 0) + 128); m[5] = (unsigned char) ((p2 ? p2->jy : 0) + 128); m[6] = p2 ? p2->b : 0; m[7] = 0;
	nv_unlock ();
	return 1;
}
/* the sync check, the desktop's (live.c, netplay.py mark): every 25th frame both send the state's checksum after it - the
 * console RAM's FNV-1a ^ the cycle count x 2654435761 (^ the UVMC2's / PiTrex's) - the first frame they differ: out of step */
static uint32_t nv_sums[8][2]; static int nv_nsums; static uint32_t nv_their[32][2]; static int nv_ntheir; static int32_t nv_desync = -1;
static int nv_nmatch; static uint32_t nv_last_match = 0xffffffffu;   /* (frames checked alike: the status) */
/* a game switch, both sides (nv_lock held): the host's next game on - a card game streamed (the host plays it as offline,
 * player 2 runs nothing), a 6809 game in step (rollback); from or to a streamed game the frames count from 0 again */
static void nv_switch_now (void)
{
	const int was_stream = nv_stream; double v; char ch[32];
	snprintf (nv_game, sizeof nv_game, "%s", nv_rg_game);
	if (nv_role == 2) { if (nv_sv_need && nv_sv_state == 2) nv_sv_take (); else nv_curn = 0; }   /* (player 2: the next game's package - the host took it at the yes) */
	ls_seed = nv_jnum (nv_game, "seed", &v) ? (uint32_t) v : 0; ls_intro_skip = nv_jnum (nv_game, "skip_boot", &v) ? (int) v : 0;
	ls_dacofs = nv_jnum (nv_game, "dacofs", &v) ? v : 0;
	if (!nv_jstr (nv_game, "cart_hw", ch, sizeof ch)) snprintf (ch, sizeof ch, "auto");
	snprintf (ls_carthw, sizeof ls_carthw, "%s", !strcmp (ch, "auto") ? "1" : !strcmp (ch, "off") ? "0" : ch);
	{ char nm2[256]; if (nv_jstr (nv_game, "name", nm2, sizeof nm2)) snprintf (nv_rom_name, sizeof nv_rom_name, "%s", nm2); }
	snprintf (sw_next, sizeof sw_next, "%s", nv_rg_rom); nv_rg_rom[0] = 0; nv_rg_state = 0; nv_rg_switched++;
	nv_card = nv_jnum (nv_game, "card", &v) && v != 0; ls_me = nv_role == 1 ? 0 : 1;
	if (nv_role == 2) nv_rb_peer = strstr (nv_game, "\"rb\": true") != NULL;   /* (the host runs the next game on guesses too: both the short delay) */
	if (nv_card) {                                               /* (streamed: each side as offline - the host its own settings) */
		nv_stream = 1; ls_cfg_on = 0; nv_st_started = 0; nv_st_fno = 0; nv_st_last = 0; nv_st_in_ok = 0;
		for (int k = 0; k < nv_stq_n; k++) { free (nv_stq[k].L); free (nv_stq[k].a); } nv_stq_n = 0; for (int k = 0; k < 6; k++) nv_sta[k].done = 1;
	} else { nv_stream = 0; ls_cfg_on = 1; }
	if (was_stream || nv_stream) { nv_reset_frames (); nv_nsums = nv_ntheir = 0; nv_desync = -1; nv_nmatch = 0; nv_last_match = 0xffffffffu; nv_rg_done_f = 0xffffffffu; }
	char m[320]; snprintf (m, sizeof m, "the next game: %s%s", nv_rom_name, nv_stream ? " (streamed: the host plays it, its lines to player 2)" : was_stream ? " (in step again)" : ""); nv_log (m);
}
static void nv_compare (void)
{
	for (int i = 0; i < nv_ntheir; i++) for (int k = 0; k < nv_nsums; k++) if (nv_their[i][0] == nv_sums[k][0]) {
		if (nv_their[i][1] != nv_sums[k][1]) { if (nv_desync < 0) { nv_desync = (int32_t) nv_sums[k][0]; char m[64]; snprintf (m, sizeof m, "out of step at frame %d", (int) nv_desync); nv_log (m); } }
		else { nv_nmatch++; nv_their[i][0] = 0xffffffffu; } }   /* (alike: counted once, that sum done) */
}
static void nv_on_sum (const unsigned char *b, size_t n)
{
	if (n < 8) return; if (nv_ntheir == 32) { memmove (nv_their, nv_their + 1, sizeof nv_their[0] * 31); nv_ntheir--; }
	nv_their[nv_ntheir][0] = nv_get32 (b); nv_their[nv_ntheir][1] = nv_get32 (b + 4); nv_ntheir++; nv_compare ();
}
static void nv_sum_send (uint32_t f, uint32_t h)
{
	if (nv_nsums == 8) { memmove (nv_sums, nv_sums + 1, sizeof nv_sums[0] * 7); nv_nsums--; }
	nv_sums[nv_nsums][0] = f; nv_sums[nv_nsums][1] = h; nv_nsums++;
	unsigned char p[8]; nv_put32 (p, f); nv_put32 (p + 4, h); nv_send ('S', p, 8); nv_compare ();
}
/* (emu_loop, before the frames: a wrong guess - the state of its frame back, the frames since to be run again; the frame
 *  number they were up to (0: none)) */
static uint32_t netvx_rollback (void)
{
	nv_lock ();
	if (nv_rb_from < 0 || !nv_rb_on) { nv_unlock (); return 0; }
	const uint32_t g = (uint32_t) nv_rb_from, was = nv_f; nv_rb_from = -1; const int q = g % NV_RB_SLOTS;
	if (g >= was || nv_rb_fr[q] != g || !nv_rb_len[q]) { nv_unlock (); nv_log ("rollback: that frame's state is gone"); return 0; }
	nv_unlock ();
	vx_rb_keep (0); const int ok = vx_load_from (nv_rb_buf[q], nv_rb_len[q]); vx_rb_keep (1);   /* (now's time, sound and switches kept) */
	if (!ok) { nv_log ("rollback: the state could not be loaded"); return 0; }
	nv_lock ();
	nv_ports[0] = nv_rb_ports[q][0]; nv_ports[1] = nv_rb_ports[q][1];
	frame_start_cycles = nv_rb_ctr[q][0]; last_busframe = nv_rb_ctr[q][1]; last_recal = nv_rb_ctr[q][2]; idle_cycles = nv_rb_ctr[q][3]; idle_frame = nv_rb_ctr[q][4];
	nv_f = g; nv_fd = g; nv_n_rb++; if (getenv ("VX_RBLOG")) fprintf (stderr, "ROLLBACK to %u from %u\n", g, was); nv_n_rbf += (long) (was - g);
	nv_unlock ();
	return was;
}
static void netvx_after (void)
{
	nv_lock (); if (!nv_rb_on) nv_rgot[nv_f % NV_RING] = 0;      /* (rollback: kept - a frame may be run again) */
	static int sumn = -1; if (sumn < 0) { const char *e = getenv ("VX_SUMN"); sumn = e ? atoi (e) : 25; if (sumn < 1) sumn = 25; }   /* (test: another period) */
	if (nv_f % (uint32_t) sumn == 0) { extern unsigned char (*e6809_read8) (unsigned address); extern int vt_uvm2, vt_pitrex; extern uint32_t uvm2_sig (void), pitrex_sig (void);
		uint32_t h = 2166136261u; for (unsigned a = 0xC800; a < 0xCC00; a++) h = (h ^ e6809_read8 (a)) * 16777619u;
		h ^= (uint32_t) total_cycles * 2654435761u; if (vt_uvm2) h ^= uvm2_sig (); else if (vt_pitrex) h ^= pitrex_sig ();
		if (nv_cur_conf) nv_sum_send (nv_f, h);
		else { const int q = nv_f % NV_RING; nv_psum[q][0] = nv_f; nv_psum[q][1] = h; nv_psum_ok[q] = 1; } }   /* (run on a guess: sent once sure) */
	nv_f++;
	nv_unlock ();
}

/* ---- the app's side */
static void nv_close_socks (void)
{
	if (nv_s != NV_BADSOCK) { nv_closesock (nv_s); nv_s = NV_BADSOCK; }
}
static void nv_clear (void)
{
	nv_role = 0; nv_phase = NV_IDLE; nv_have_peer = nv_ncand = nv_relay = nv_locked = 0; nv_code[0] = nv_peer_name[0] = nv_err[0] = nv_note[0] = 0;
	nv_game[0] = 0; nv_accepted = 0; nv_go = 0; nv_left = nv_lost = 0; nv_pause = nv_peer_pause = 0; nv_pause_seq = 0; nv_mypause = nv_theirpause = 0; nv_their_seq = -1; nv_t_z = 0;
	nv_svn = nv_curn = 0; nv_sv_state = nv_sv_need = nv_sv_peer = nv_sv_total = nv_sv_got = 0; nv_cur_md5[0] = 0; nv_st_clear ();
	nv_last_heard = nv_last_direct = nv_last_relayed = 0; nv_punch_t0 = 0; nv_rtt = 0; nv_delay = 3; nv_ncout = 0; nv_ncin = 0; nv_ncheld = 0; nv_cseq = nv_cnext = 0;
	nv_t_srv = nv_t_hello = nv_t_beacon = nv_t_punch = nv_t_pause = nv_t_ka = 0; nv_ping_i = nv_ping_got = 0; nv_g_sent = 0;
	nv_hot_free (); nv_hot_can = 0; nv_take_req = 0; ls_port2[0] = 0;
	nv_rg_can = nv_rg_state = 0; nv_rg_rom[0] = nv_rg_msg[0] = 0; nv_rg_seen = 0xffffu; nv_rg_done_f = 0xffffffffu;
	nv_card = nv_card_done = nv_ready = 0; nv_jitfast = 0; nv_cbitn = 0; nv_ctotal = nv_cgot = 0; nv_cidx = 0; snprintf (nv_uvm2, sizeof nv_uvm2, "exact");
	for (int q = 0; q < 512; q++) { free (nv_cparts[q]); nv_cparts[q] = NULL; }
	memset (nv_rgot, 0, sizeof nv_rgot); nv_f = 0; nv_lhave = 0; nv_nsums = 0; nv_ntheir = 0; nv_desync = -1; nv_nmatch = 0; nv_last_match = 0xffffffffu;
}
/* host: mode 0 the internet's game room (public), 1 the internet with a code only (private), 2 this network (no
 * internet: the game announced on it). rom: the game's file; skip_boot, cart_hw ('auto', 'off' or a board), dacofs:
 * what both sides must have alike. 1: started (the phase and the code: vx_net_status) */
int vx_net_host (int mode, const char *gamer, const char *key, const char *rom, int skip_boot, const char *cart_hw, double dacofs)
{
	nv_lock (); nv_close_socks (); nv_clear ();
	if (!vx_net_md5_file (rom, nv_rom_md5, &nv_rom_size)) { snprintf (nv_err, sizeof nv_err, "the game's file can not be read"); nv_phase = NV_ENDED; nv_unlock (); return 0; }
	const char *b = rom; for (const char *q = rom; *q; q++) if (*q == '/' || *q == '\\') b = q + 1; snprintf (nv_rom_name, sizeof nv_rom_name, "%s", b);
	snprintf (nv_gamer, sizeof nv_gamer, "%s", gamer ? gamer : ""); snprintf (nv_key, sizeof nv_key, "%s", key ? key : "");
	nv_skip_boot = skip_boot; snprintf (nv_carthw, sizeof nv_carthw, "%s", cart_hw && cart_hw[0] ? cart_hw : "auto"); nv_dacofs = dacofs;
	nv_role = 1; nv_mode = mode;
	{ const char *e = strrchr (rom, '.'); nv_card = e && (!vx_stricmp (e, ".um2") || !vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit"));
	  if (nv_card) { const int m = ls_mode_now (); snprintf (nv_uvm2, sizeof nv_uvm2, "%s", m == 2 ? "dev" : m == 1 ? "fast" : "exact");
		nv_card_mine (rom); nv_card_pack (); char t[96]; snprintf (t, sizeof t, "a card game: its card %d files, UVMC2 mode %s", nv_nmine, nv_uvm2); nv_log (t); } }
	if (!nv_card) nv_sv_offer (rom); else nv_svn = 0;   /* (the host's saves for player 2: its calibration) */
	nv_s = nv_udp (mode == 2 ? NV_PORT : 0, mode == 2);
	if (nv_s == NV_BADSOCK && mode == 2) nv_s = nv_udp (0, 1);   /* (the port taken: another one - the announcement says which) */
	if (nv_s == NV_BADSOCK) { snprintf (nv_err, sizeof nv_err, "no network"); nv_phase = NV_ENDED; nv_unlock (); return 0; }
	nv_lan_addr (nv_me, sizeof nv_me);                         /* (after the socket: on Windows the network is started there - before it, 127.0.0.1) */
	nv_addr (&nv_srv, NV_SERVER, NV_SPORT);
	nv_phase = mode == 2 ? NV_WAIT : NV_MEET; nv_locked = 0; nv_rtt = -2;   /* (-2: no player yet) */
	nv_last_heard = nv_now ();
	nv_unlock (); nv_thread (); nv_log (mode == 2 ? "hosting on this network" : "hosting: asking the meeting point for a code");
	return 1;
}
/* join: a five-letter code (the meeting point) or an address ("192.168.1.20" or "host:port", a game on this network) */
int vx_net_join (const char *where, const char *gamer, const char *key)
{
	nv_lock (); nv_close_socks (); nv_clear ();
	snprintf (nv_gamer, sizeof nv_gamer, "%s", gamer ? gamer : ""); snprintf (nv_key, sizeof nv_key, "%s", key ? key : "");
	nv_role = 2; nv_s = nv_udp (0, 0); nv_addr (&nv_srv, NV_SERVER, NV_SPORT); nv_lan_addr (nv_me, sizeof nv_me);
	if (nv_s == NV_BADSOCK) { snprintf (nv_err, sizeof nv_err, "no network"); nv_phase = NV_ENDED; nv_unlock (); return 0; }
	int code = strlen (where) == 5; for (const char *q = where; *q; q++) if (!((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z'))) code = 0;
	if (code) { for (int i = 0; i < 5; i++) nv_code[i] = (char) toupper ((unsigned char) where[i]); nv_code[5] = 0; nv_mode = 0; nv_phase = NV_MEET; }
	else { char ip[128]; int port = NV_PORT; snprintf (ip, sizeof ip, "%s", where); char *c = strchr (ip, ':'); if (c) { *c = 0; port = atoi (c + 1); }
		if (!nv_addr (&nv_peer, ip, port)) { snprintf (nv_err, sizeof nv_err, "address not found"); nv_phase = NV_ENDED; nv_unlock (); return 0; }
		nv_have_peer = 1; nv_locked = 1; nv_mode = 2; nv_phase = NV_WAIT; }
	nv_last_heard = 0; nv_unlock (); nv_thread (); nv_log ("joining");
	return 1;
}
/* the joiner: the host's game found (rom: its file) or not; 1 when both may start */
void vx_net_accept (int ok)
{
	nv_lock ();
	if (nv_role == 2 && nv_phase == NV_AGREED) {
		int bad_bios = 0;
		if (ok) { char hb[40], mb[33] = ""; long long bz;   /* (the host's BIOS: the same here, or no game) */
			if (nv_jstr (nv_game, "bios", hb, sizeof hb) && hb[0] && g_bios[0] && vx_net_md5_file (g_bios, mb, &bz) && vx_stricmp (hb, mb)) { ok = 0; bad_bios = 1; } }
		unsigned char why[64]; size_t wn = 0; if (bad_bios) { memcpy (why, "\0bios", 5); wn = 5; }
		if (ok) { if (nv_jitfast) { memcpy (why, "\0jit:fast", 9); wn = 9; } memcpy (why + wn, "\0regame", 7); wn += 7; memcpy (why + wn, "\0hotjoin", 8); wn += 8;
			memcpy (why + wn, "\0save", 5); wn += 5;   /* (it takes the host's saves) */
			memcpy (why + wn, "\0rb", 3); wn += 3;     /* (it runs on guesses: rollback) */
			memcpy (why + wn, "\0rgmode", 7); wn += 7; /* (it switches to and from card games: streamed / in step) */
			if (nv_card && nv_stream_peer) { memcpy (why + wn, "\0stream", 7); wn += 7; nv_stream = 1; }   /* (a card game: the host's lines - nothing run here) */
			memcpy (why + wn, "\0uvm2:", 6); wn += 6; const size_t k = strlen (nv_uvm2); memcpy (why + wn, nv_uvm2, k); wn += k; }
		for (int i = 0; i < 5; i++) nv_send (ok ? 'A' : 'N', why, wn);
		if (!ok) { snprintf (nv_err, sizeof nv_err, bad_bios ? "your BIOS (bios.bin) is not the host's - online both need the same one" : "you do not have the game"); nv_phase = NV_ENDED; } else { nv_accepted = 1;
			if (!nv_sv_need) nv_curn = 0; else if (nv_sv_state == 2) nv_sv_take ();   /* (the host's package: all here, or the start waits for it) */
			if ((!nv_card || nv_stream) && (!nv_sv_need || nv_sv_state == 2)) nv_ready = 1; } }
	nv_unlock ();
}
/* both: the game agreed - its settings for the power-on (the app loads the ROM after this, then vx_net_go) */
void vx_net_prepare (void)
{
	nv_lock (); double v; char ch[32];
	if (nv_stream) { ls_cfg_on = 0; ls_me = nv_role == 1 ? 0 : 1; nv_reset_frames (); nv_phase = NV_PLAY; nv_go = 0; nv_last_heard = nv_now ();
		nv_st_started = 0; nv_stq_n = 0; nv_st_fno = 0; nv_unlock (); nv_log (nv_role == 1 ? "streamed: this side plays, its lines to player 2" : "streamed: the host's lines shown here"); return; }
	ls_cfg_on = 1; ls_seed = nv_jnum (nv_game, "seed", &v) ? (uint32_t) v : 0; ls_intro_skip = nv_jnum (nv_game, "skip_boot", &v) ? (int) v : 0;
	ls_dacofs = nv_jnum (nv_game, "dacofs", &v) ? v : 0; ls_jitfast = nv_jitfast; ls_mode = !strcmp (nv_uvm2, "dev") ? 2 : !strcmp (nv_uvm2, "fast") ? 1 : 0;
	ls_me = nv_role == 1 ? 0 : 1;
	if (nv_card) { static char *none_[1] = { "" }; sdc_only = nv_nonly ? nv_only : none_; sdc_nonly = nv_nonly; sdc_fixed = NV_CARD_TIME; }   /* (until the game is on: vx_net_go) */
	if (!nv_jstr (nv_game, "cart_hw", ch, sizeof ch)) snprintf (ch, sizeof ch, "auto");
	snprintf (ls_carthw, sizeof ls_carthw, "%s", !strcmp (ch, "auto") ? "1" : !strcmp (ch, "off") ? "0" : ch);
	if (!nv_jstr (nv_game, "port2", ls_port2, sizeof ls_port2)) ls_port2[0] = 0;   /* (the host's; the desktop's game has none: this side's own) */
	nv_reset_frames (); nv_phase = NV_PLAY; nv_go = 0; nv_last_heard = nv_now ();
	nv_unlock ();
}
/* the host, playing: another game for both (1: offered - the switch follows when player 2 has it; 0: not possible, the
 * reason in the status' regame_msg) */
int vx_net_regame (const char *rom, int skip_boot, const char *cart_hw, double dacofs)
{
	nv_lock (); int ok = 0;
	const char *e = strrchr (rom, '.'); const int card = e && (!vx_stricmp (e, ".um2") || !vx_stricmp (e, ".img") || !vx_stricmp (e, ".pit"));
	if (nv_role != 1 || nv_phase != NV_PLAY) snprintf (nv_rg_msg, sizeof nv_rg_msg, "only the host changes the game");
	else if (!nv_rg_can) snprintf (nv_rg_msg, sizeof nv_rg_msg, "the other player's Vectrex_EMU can not change games: leave and host again");
	else if ((card || nv_card) && !nv_rgm_peer) snprintf (nv_rg_msg, sizeof nv_rg_msg, "the other player's Vectrex_EMU can not switch to or from a card game: update it");
	else if (nv_rg_state) snprintf (nv_rg_msg, sizeof nv_rg_msg, "a game change is on its way already");
	else if (!vx_net_md5_file (rom, nv_rom_md5, &nv_rom_size)) snprintf (nv_rg_msg, sizeof nv_rg_msg, "the game's file can not be read");
	else {
		const char *bn = rom; for (const char *q = rom; *q; q++) if (*q == '/' || *q == '\\') bn = q + 1; snprintf (nv_rom_name, sizeof nv_rom_name, "%s", bn);
		nv_skip_boot = skip_boot; snprintf (nv_carthw, sizeof nv_carthw, "%s", cart_hw && cart_hw[0] ? cart_hw : "auto"); nv_dacofs = dacofs;
		nv_sv_offer (rom); nv_sv_state = nv_sv_peer && nv_svn ? 1 : 0;   /* (the next game's package: sent while player 2 answers) */
		{ const int was = nv_card; char gm0[sizeof nv_game], um0[sizeof nv_uvm2]; memcpy (gm0, nv_game, sizeof gm0); memcpy (um0, nv_uvm2, sizeof um0);
		  nv_card = card; if (card) { const int m = ls_mode_now (); snprintf (nv_uvm2, sizeof nv_uvm2, "%s", m == 2 ? "dev" : m == 1 ? "fast" : "exact"); }
		  nv_make_game (); snprintf (nv_rg_game, sizeof nv_rg_game, "%s", nv_game);   /* (the next game's: "card" in it - the game now kept as it is) */
		  memcpy (nv_game, gm0, sizeof gm0); memcpy (nv_uvm2, um0, sizeof um0); nv_card = was; }
		if (card) nv_svn = 0, nv_sv_state = 0;   /* (a card game: no saves package) */
		snprintf (nv_rg_rom, sizeof nv_rg_rom, "%s", rom);
		nv_rg_seq = (nv_rg_seq + 1) & 0xffffu; nv_rg_state = 1; nv_t_rg = 0; nv_rg_t0 = nv_now (); nv_rg_msg[0] = 0; ok = 1;
		char m[300]; snprintf (m, sizeof m, "another game offered: %s", nv_rom_name); nv_log (m); }
	nv_unlock (); return ok;
}
/* player 2: the host's next game here (path) - or not (NULL) */
void vx_net_regame_answer (const char *path)
{
	nv_lock ();
	if (nv_role == 2 && nv_rg_state == 1) {
		if (path && path[0]) { snprintf (nv_rg_rom, sizeof nv_rg_rom, "%s", path); nv_rg_state = 2; nv_t_rg = 0;
			if (!nv_sv_need) nv_curn = 0; }   /* (with a package: taken below once it is all here) */
		else { unsigned char pk[3]; nv_put16 (pk, nv_rg_seq); pk[2] = 0; for (int i = 0; i < 5; i++) nv_send ('r', pk, 3); nv_rg_state = 0; } }
	nv_unlock ();
}
/* the host, player 2 has agreed and joins its running game: this moment's state packed and sent, both on from it (1) -
 * 0: not possible (the status' "hot" 0: start both anew - vx_net_prepare, the load, vx_net_go) */
int vx_net_go_hot (void)
{
	const size_t n = retro_serialize_size (); if (!n) return 0;
	unsigned char *st = malloc (n), *z = st ? malloc (4 + n + n / 127 + 16 + 40) : NULL;
	if (!st || !z || !retro_serialize (st, n)) { free (st); free (z); return 0; }
	nv_put32 (z, (uint32_t) n); size_t zn = 4 + nv_lz (st, n, z + 4); free (st);
	{ unsigned long long c[5] = { frame_start_cycles, last_busframe, last_recal, idle_cycles, idle_frame }; memcpy (z + zn, c, sizeof c); zn += sizeof c; }
	nv_lock ();
	free (nv_hz); nv_hz = z; nv_hzn = zn; nv_hot = 1; nv_cidx = 0; nv_t_hot = 0; ls_cfg_on = 1; ls_port2[0] = 0;
	nv_reset_frames (); nv_phase = NV_PLAY; nv_go = 1; nv_last_heard = nv_now ();
	char m[128]; snprintf (m, sizeof m, "player 2 joins the running game: %zu bytes packed to %zu, %d pieces", n, zn, (int) ((zn + NV_CARD_CHUNK - 1) / NV_CARD_CHUNK)); nv_log (m);
	nv_unlock ();
	return 1;
}
/* player 2: the host's running game wanted - its settings now; the app loads the same game, the state follows (netvx_hot_apply) */
void vx_net_prepare_hot (void)
{
	nv_lock ();
	ls_cfg_on = 1; ls_intro_skip = 0; if (!nv_jstr (nv_game, "port2", ls_port2, sizeof ls_port2)) ls_port2[0] = 0;
	{ char ch[32]; if (!nv_jstr (nv_game, "cart_hw", ch, sizeof ch)) snprintf (ch, sizeof ch, "auto"); snprintf (ls_carthw, sizeof ls_carthw, "%s", !strcmp (ch, "auto") ? "1" : !strcmp (ch, "off") ? "0" : ch); }
	nv_unlock ();
}
/* (each refresh, before the emulation, after a game switch: the host's state in - both on from it) */
static void netvx_hot_apply (void)
{
	if (!nv_hot_apply || nv_role != 2 || sw_next[0]) return;
	nv_hot_apply = 0;
	const int ok = retro_unserialize (nv_hraw, nv_hrawn);
	nv_lock ();
	free (nv_hraw); nv_hraw = NULL; nv_hrawn = 0;
	if (ok) { frame_start_cycles = nv_hctr[0]; last_busframe = nv_hctr[1]; last_recal = nv_hctr[2]; idle_cycles = nv_hctr[3]; idle_frame = nv_hctr[4];
		nv_reset_frames (); nv_phase = NV_PLAY; nv_go = 1; nv_last_heard = nv_now (); for (int i = 0; i < 5; i++) nv_send ('j', NULL, 0); nv_log ("in the host's running game: on together"); }
	else { snprintf (nv_err, sizeof nv_err, "the host's game could not be loaded here"); nv_log (nv_err); }
	nv_unlock ();
}
void vx_net_take_port (int k) { nv_lock (); if (k == 1 || k == 2) nv_take_req = k; nv_unlock (); }
void vx_net_go (void) { nv_lock (); nv_go = 1; nv_last_heard = nv_now (); if (nv_card) { sdc_only = NULL; sdc_nonly = 0; sdc_fixed = 0; } nv_unlock (); nv_log ("playing"); }
void vx_net_leave (void)
{
	nv_lock ();
	if (nv_s != NV_BADSOCK) { for (int i = 0; i < 3; i++) nv_send ('Q', NULL, 0);
		if (nv_role == 1 && nv_code[0]) { char d[32]; snprintf (d, sizeof d, "VXR1D%s", nv_code); nv_srvsend (d); } }
	nv_close_socks (); nv_clear (); ls_cfg_on = 0;
	nv_unlock ();
}
void vx_net_chat (const char *text)
{
	nv_lock ();
	if (text && text[0] && nv_ncout < 16) { nv_line *l = &nv_cout[nv_ncout++]; l->k = nv_cseq++; snprintf (l->t, sizeof l->t, "%s", text); l->t0 = nv_now (); nv_send_chat (nv_now ()); }
	nv_unlock ();
}
void vx_net_pause (int on)
{
	nv_lock (); const int w = on <= 0 ? 0 : on > 2 ? 1 : on;   /* (1: a menu, 2: another app) */
	if (w != nv_mypause) { nv_mypause = w; nv_pause_seq = (nv_pause_seq + 1) & 0xffff; nv_pause_upd ();
		unsigned char z[4]; z[0] = w ? 1 : 0; nv_put16 (z + 1, (unsigned) nv_pause_seq); z[3] = (unsigned char) w; nv_send ('Z', z, 4); nv_t_z = nv_now (); }
	nv_unlock ();
}
void vx_net_room (void)                                            /* (the game room: asked now, its answer in vx_net_status) */
{
	nv_lock ();
	if (nv_s == NV_BADSOCK) { nv_s = nv_udp (0, 0); nv_addr (&nv_srv, NV_SERVER, NV_SPORT); }
	nv_room_state = 1; nv_t_srv = 0; nv_room_t0 = nv_now (); nv_room[0] = 0; nv_unlock (); nv_thread ();
}
void vx_net_lan (int on)                                           /* (games on this network: listened for while on) */
{
	nv_lock ();
	if (on && nv_ls == NV_BADSOCK) nv_ls = nv_udp (NV_LANPORT, 1);
	if (!on && nv_ls != NV_BADSOCK) { nv_closesock (nv_ls); nv_ls = NV_BADSOCK; nv_nlans = 0; }
	nv_lan_on = on && nv_ls != NV_BADSOCK; nv_unlock ();
	if (on) nv_thread ();
}
/* everything the app shows, as JSON: the phase (0 alone, 1 meeting point, 2 waiting, 3 agreed, 4 playing, 5 ended), the
 * role, the code, the other player, the game, the relay, the input delay, a problem, the chat lines new since the last
 * call, the game room's answer and the network's games */
/* a player name reserved at the meeting point for this device's key (as the desktop's claim_name: the app's first start,
 * a name changed in Online) - waits up to 3 s, on the caller's thread (an app's background one). old: the name before,
 * set free. out: the name as the meeting point keeps it. 1 reserved, 0 another player's, -1 not allowed, -2 no answer */
int vx_net_claim (const char *name, const char *key, const char *old, char *out, int outn)
{
	if (out && outn > 0) out[0] = 0;
	if (!name || !*name || !key || !*key) return -1;
	nv_sock_t s = socket (AF_INET, SOCK_DGRAM, 0); if (s == NV_BADSOCK) return -2;
	struct sockaddr_in a; if (!nv_addr (&a, NV_SERVER, NV_SPORT)) { nv_closesock (s); return -2; }
	char nm[64], ky[200], od[64], q[400]; nv_jesc (nm, sizeof nm, name); nv_jesc (ky, sizeof ky, key); nv_jesc (od, sizeof od, old && *old ? old : "");
	const int qn = snprintf (q, sizeof q, "VXR1N{\"name\": %s, \"key\": %s, \"old\": %s}", nm, ky, old && *old ? od : "null");
	const double t0 = nv_now (); double asked = -10; int r = -2;
	while (nv_now () - t0 < 3.0) {
		if (nv_now () - asked > 1.0) { sendto (s, q, qn, 0, (struct sockaddr *) &a, sizeof a); asked = nv_now (); }
		fd_set rs; FD_ZERO (&rs); FD_SET (s, &rs); struct timeval tv = { 0, 200000 };
		if (select ((int) s + 1, &rs, NULL, NULL, &tv) <= 0) continue;
		char b[1024]; const int n = (int) recv (s, b, sizeof b - 1, 0); if (n < 5) continue; b[n] = 0;
		if (!memcmp (b, "VXR1n", 5)) { double ok = 0; nv_jnum (b + 5, "ok", &ok);
			if (ok != 0) { r = 1; if (out) nv_jstr (b + 5, "name", out, (size_t) outn); }
			else { char why[64] = ""; nv_jstr (b + 5, "why", why, sizeof why); r = !strcmp (why, "taken") ? 0 : -1; }
			break; }
		if (!memcmp (b, "VXR1E", 5)) { r = -1; break; } }
	nv_closesock (s);
	{ char m[160]; snprintf (m, sizeof m, "name %s: %s", name, r == 1 ? "reserved" : r == 0 ? "another player's" : r == -1 ? "not allowed" : "the meeting point did not answer"); nv_log (m); }
	return r;
}
/* the name the meeting point keeps for this device's key (the apps make the key from the device: a fresh install gets its
 * name back) - up to 3 s, on the caller's thread. 1 found (out), 0 none, -2 no answer */
int vx_net_whoami (const char *key, char *out, int outn)
{
	if (out && outn > 0) out[0] = 0;
	if (!key || !*key) return 0;
	nv_sock_t s = socket (AF_INET, SOCK_DGRAM, 0); if (s == NV_BADSOCK) return -2;
	struct sockaddr_in a; if (!nv_addr (&a, NV_SERVER, NV_SPORT)) { nv_closesock (s); return -2; }
	char ky[200], q[260]; nv_jesc (ky, sizeof ky, key); const int qn = snprintf (q, sizeof q, "VXR1W{\"key\": %s}", ky);
	const double t0 = nv_now (); double asked = -10; int r = -2;
	while (nv_now () - t0 < 3.0) {
		if (nv_now () - asked > 1.0) { sendto (s, q, qn, 0, (struct sockaddr *) &a, sizeof a); asked = nv_now (); }
		fd_set rs; FD_ZERO (&rs); FD_SET (s, &rs); struct timeval tv = { 0, 200000 };
		if (select ((int) s + 1, &rs, NULL, NULL, &tv) <= 0) continue;
		char b[512]; const int n = (int) recv (s, b, sizeof b - 1, 0); if (n < 5) continue; b[n] = 0;
		if (!memcmp (b, "VXR1w", 5)) { char nm[64] = ""; r = nv_jstr (b + 5, "name", nm, sizeof nm) && nm[0] ? 1 : 0; if (r && out) snprintf (out, (size_t) outn, "%s", nm); break; } }
	nv_closesock (s);
	{ char m[120]; snprintf (m, sizeof m, "this device's name: %s", r == 1 ? out : r == 0 ? "none yet" : "the meeting point did not answer"); nv_log (m); }
	return r;
}
int vx_net_playing_now (void) { return netvx_playing (); }
int vx_net_port (void) { return nv_my_port (); }       /* (the game's UDP port here: the iOS app's peer-to-peer bridge hands its packets to it) */
const char *vx_net_status (void)
{
	static char out[16384]; char t[256], g[1100];
	nv_lock ();
	size_t o = (size_t) snprintf (out, sizeof out, "{\"phase\": %d, \"role\": %d, \"mode\": %d, \"code\": \"%s\", \"relay\": %d, \"delay\": %d, \"rtt\": %d, "
		"\"lost\": %d, \"left\": %d, \"paused\": %d, \"peer_paused\": %d, \"peer_why\": %d, \"my_pause\": %d, \"waiting\": %.2f, \"frame\": %u, \"accepted\": %d, \"desync\": %d, \"checked\": %d, \"ready\": %d, \"card\": %d, \"card_files\": %d, \"uvm2\": \"%s\", \"stream\": %d",
		nv_phase, nv_role, nv_mode, nv_code, nv_relay, nv_delay, (int) (nv_rtt > 0 ? nv_rtt * 1000 : 0), nv_lost, nv_left, nv_pause, nv_peer_pause, nv_theirpause, nv_mypause,
		nv_wait_t0 ? nv_now () - nv_wait_t0 : 0.0, (unsigned) nv_f, nv_accepted, (int) nv_desync, nv_nmatch, nv_ready, nv_card, nv_nonly, nv_uvm2, nv_stream);
	nv_jesc (t, sizeof t, nv_peer_name); o += (size_t) snprintf (out + o, sizeof out - o, ", \"peer\": %s", t);
	nv_jesc (t, sizeof t, nv_err); o += (size_t) snprintf (out + o, sizeof out - o, ", \"error\": %s", t);
	o += (size_t) snprintf (out + o, sizeof out - o, ", \"game\": %s", nv_game[0] ? nv_game : "null");
	nv_jesc (t, sizeof t, nv_rg_msg); nv_rg_msg[0] = 0;
	{ char gm[40]; nv_jesc (gm, sizeof gm, nv_gamer);
	  o += (size_t) snprintf (out + o, sizeof out - o, ", \"hot\": %d, \"hot_got\": %d, \"hot_total\": %d, \"ports\": [%d, %d], \"me\": %s",
		nv_hot_can && !nv_card, nv_role == 1 ? (nv_hot == 2 ? 1 : 0) : nv_hot_got, nv_role == 1 ? (nv_hot ? 1 : 0) : nv_hot_total, nv_ports[0], nv_ports[1], gm); }
	o += (size_t) snprintf (out + o, sizeof out - o, ", \"regame_msg\": %s, \"regame_offer\": %s, \"regame_n\": %d, \"reset_n\": %d, \"reset_by\": %d",
		t, nv_role == 2 && nv_rg_state == 1 && nv_rg_game[0] ? nv_rg_game : "null", nv_rg_switched, nv_reset_n, nv_reset_by);
	o += (size_t) snprintf (out + o, sizeof out - o, ", \"chat\": [");
	for (int i = 0; i < nv_ncin; i++) { nv_jesc (g, sizeof g, nv_cin[i]); o += (size_t) snprintf (out + o, sizeof out - o, "%s%s", i ? ", " : "", g); }
	nv_ncin = 0;
	o += (size_t) snprintf (out + o, sizeof out - o, "], \"room_state\": %d, \"room\": %s, \"lan\": [", nv_room_state, nv_room_state == 2 && nv_room[0] ? nv_room : "null");
	for (int i = 0; i < nv_nlans && o < sizeof out - 600; i++) { char nm[300], gm[40]; nv_jesc (nm, sizeof nm, nv_lans[i].name); nv_jesc (gm, sizeof gm, nv_lans[i].gamer);
		o += (size_t) snprintf (out + o, sizeof out - o, "%s{\"ip\": \"%s\", \"port\": %d, \"name\": %s, \"md5\": \"%s\", \"size\": %lld, \"gamer\": %s, \"nver\": %d}",
			i ? ", " : "", nv_lans[i].ip, nv_lans[i].port, nm, nv_lans[i].md5, nv_lans[i].size, gm, nv_lans[i].nver); }
	o += (size_t) snprintf (out + o, sizeof out - o, "], \"sums\": [");
	for (int i = 0; i < nv_nsums; i++) o += (size_t) snprintf (out + o, sizeof out - o, "%s[%u, %u]", i ? ", " : "", (unsigned) nv_sums[i][0], (unsigned) nv_sums[i][1]);
	snprintf (out + o, sizeof out - o, "]}");
	nv_unlock ();
	return out;
}
