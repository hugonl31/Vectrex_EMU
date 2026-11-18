/* pxfetch.c - Vectrex_EMU's libretro core: the PiTrex's SD card files fetched by the core itself (the desktop's Vectrex >
 * Get games & extras, getter.py chain_pitrex): the ROM browser's "PiTrex: get it" while <system>/vectrex_emu/pitrex has
 * no pitrex.img. Malban's newest PiTrex_sd_... zip (his download page, then the known ones - plain http, as his site
 * serves them), into <system>/vectrex_emu/pitrex: one folder round it all left out, pitrex.img written last (there: the
 * folder is whole). On a thread of its own; the browser shows how far it is. Its own small inflate (RFC 1951, after
 * Mark Adler's puff) and HTTP/1.0 over a socket (Windows: WinINet) - nothing the system may not have.
 * Included by libretro_vx.c before browser.c.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <setjmp.h>
#ifdef _WIN32
#include <wininet.h>
#include <direct.h>
#pragma comment(lib, "wininet.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#endif

static volatile int px_st, px_pct, px_redo;   /* (0 not started, 1 fetching, 2 unpacking, 3 done, 4 failed; its %; the browser to scan again) */
static char px_dir[1100];                     /* (<system>/vectrex_emu/pitrex) */

/* ---- inflate (RFC 1951): the whole of one entry, in memory */
typedef struct { const unsigned char *in; size_t inlen, incnt; unsigned char *out; size_t outlen, outcnt; long bitbuf; int bitcnt; jmp_buf env; } pz_st;
typedef struct { short *count, *symbol; } pz_huff;
static int pz_bits (pz_st *s, int need)
{
	long val = s->bitbuf;
	while (s->bitcnt < need) { if (s->incnt == s->inlen) longjmp (s->env, 1); val |= (long) s->in[s->incnt++] << s->bitcnt; s->bitcnt += 8; }
	s->bitbuf = val >> need; s->bitcnt -= need;
	return (int) (val & ((1L << need) - 1));
}
static int pz_stored (pz_st *s)
{
	s->bitbuf = 0; s->bitcnt = 0;
	if (s->incnt + 4 > s->inlen) return 2;
	const unsigned len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
	if (s->in[s->incnt + 2] != (~len & 0xff) || s->in[s->incnt + 3] != ((~len >> 8) & 0xff)) return -2;
	s->incnt += 4;
	if (s->incnt + len > s->inlen) return 2;
	if (s->outcnt + len > s->outlen) return 1;
	memcpy (s->out + s->outcnt, s->in + s->incnt, len); s->outcnt += len; s->incnt += len;
	return 0;
}
static int pz_decode (pz_st *s, const pz_huff *h)
{
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= 15; len++) {
		code |= pz_bits (s, 1);
		const int count = h->count[len];
		if (code - count < first) return h->symbol[index + (code - first)];
		index += count; first += count; first <<= 1; code <<= 1;
	}
	return -10;
}
static int pz_construct (pz_huff *h, const short *length, int n)
{
	short offs[16]; int left = 1;
	for (int len = 0; len <= 15; len++) h->count[len] = 0;
	for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
	if (h->count[0] == n) return 0;
	for (int len = 1; len <= 15; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
	offs[1] = 0;
	for (int len = 1; len < 15; len++) offs[len + 1] = (short) (offs[len] + h->count[len]);
	for (int sym = 0; sym < n; sym++) if (length[sym]) h->symbol[offs[length[sym]]++] = (short) sym;
	return left;
}
static int pz_codes (pz_st *s, const pz_huff *lc, const pz_huff *dc)
{
	static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
	                                 4097, 6145, 8193, 12289, 16385, 24577 };
	static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
	int symbol;
	do {
		symbol = pz_decode (s, lc); if (symbol < 0) return symbol;
		if (symbol < 256) { if (s->outcnt == s->outlen) return 1; s->out[s->outcnt++] = (unsigned char) symbol; }
		else if (symbol > 256) {
			symbol -= 257; if (symbol >= 29) return -10;
			size_t len = (size_t) (lbase[symbol] + pz_bits (s, lext[symbol]));
			symbol = pz_decode (s, dc); if (symbol < 0) return symbol; if (symbol >= 30) return -11;
			const size_t dist = (size_t) (dbase[symbol] + pz_bits (s, dext[symbol]));
			if (dist > s->outcnt) return -11;
			if (s->outcnt + len > s->outlen) return 1;
			while (len--) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
		}
	} while (symbol != 256);
	return 0;
}
static int pz_fixed (pz_st *s)
{
	static int made; static short lcnt[16], lsym[288], dcnt[16], dsym[30]; static pz_huff lc = { lcnt, lsym }, dc = { dcnt, dsym };
	if (!made) { short l[288]; int i = 0;
		for (; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
		pz_construct (&lc, l, 288); for (i = 0; i < 30; i++) l[i] = 5; pz_construct (&dc, l, 30); made = 1; }
	return pz_codes (s, &lc, &dc);
}
static int pz_dynamic (pz_st *s)
{
	static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	short lengths[320], lcnt[16], lsym[288], dcnt[16], dsym[30]; pz_huff lc = { lcnt, lsym }, dc = { dcnt, dsym };
	const int nlen = pz_bits (s, 5) + 257, ndist = pz_bits (s, 5) + 1, ncode = pz_bits (s, 4) + 4; int index, err;
	if (nlen > 286 || ndist > 30) return -3;
	for (index = 0; index < ncode; index++) lengths[order[index]] = (short) pz_bits (s, 3);
	for (; index < 19; index++) lengths[order[index]] = 0;
	if (pz_construct (&lc, lengths, 19) != 0) return -4;
	index = 0;
	while (index < nlen + ndist) {
		int symbol = pz_decode (s, &lc); if (symbol < 0) return symbol;
		if (symbol < 16) lengths[index++] = (short) symbol;
		else { short len = 0;
			if (symbol == 16) { if (!index) return -5; len = lengths[index - 1]; symbol = 3 + pz_bits (s, 2); }
			else if (symbol == 17) symbol = 3 + pz_bits (s, 3);
			else symbol = 11 + pz_bits (s, 7);
			if (index + symbol > nlen + ndist) return -6;
			while (symbol--) lengths[index++] = len; }
	}
	if (!lengths[256]) return -9;
	err = pz_construct (&lc, lengths, nlen); if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -7;
	err = pz_construct (&dc, lengths + nlen, ndist); if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -8;
	return pz_codes (s, &lc, &dc);
}
static int pz_inflate (unsigned char *out, size_t outlen, const unsigned char *in, size_t inlen)
{
	pz_st s; s.out = out; s.outlen = outlen; s.outcnt = 0; s.in = in; s.inlen = inlen; s.incnt = 0; s.bitbuf = 0; s.bitcnt = 0;
	int err = 0, last;
	if (setjmp (s.env)) return 2;
	do { last = pz_bits (&s, 1); const int type = pz_bits (&s, 2);
		err = type == 0 ? pz_stored (&s) : type == 1 ? pz_fixed (&s) : type == 2 ? pz_dynamic (&s) : -1;
		if (err) break; } while (!last);
	return err ? err : s.outcnt == outlen ? 0 : -12;
}

/* ---- HTTP: the body into a file, or into memory (mem: up to *len bytes); redirects followed. 0: done */
static void px_progress (long long got, long long total, int share, int base)
{ if (total > 0) { const int p = base + (int) (got * share / total); if (p != px_pct) { px_pct = p; px_redo |= 2; } } }
#ifdef _WIN32
static int px_get (const char *url, FILE *f, char *mem, size_t *len, int share)
{
	HINTERNET h = InternetOpenA ("Vectrex_EMU", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0); if (!h) return -1;
	HINTERNET u = InternetOpenUrlA (h, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
	if (!u) { InternetCloseHandle (h); return -1; }
	DWORD code = 0, cl = sizeof code; HttpQueryInfoA (u, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &cl, NULL);
	DWORD tot = 0; cl = sizeof tot; if (!HttpQueryInfoA (u, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &tot, &cl, NULL)) tot = 0;
	char buf[65536]; DWORD n; long long got = 0; int r = code == 200 ? 0 : -1;
	while (!r && InternetReadFile (u, buf, sizeof buf, &n) && n) {
		if (f) { if (fwrite (buf, 1, n, f) != n) r = -1; px_progress (got + n, tot, share, 0); }
		else { if (got + n > (long long) *len) { r = -1; break; } memcpy (mem + got, buf, n); }
		got += n; }
	if (!f) *len = (size_t) got;
	InternetCloseHandle (u); InternetCloseHandle (h);
	return r;
}
#else
static int px_get (const char *url, FILE *f, char *mem, size_t *len, int share)
{
	char cur[1100]; snprintf (cur, sizeof cur, "%s", url);
	for (int hop = 0; hop < 6; hop++) {
		if (strncmp (cur, "http://", 7)) return -1;               /* (http only: no TLS here - Malban's site serves both) */
		char host[256], port[8] = "80"; const char *p = cur + 7, *sl = strchr (p, '/'); size_t hl = sl ? (size_t) (sl - p) : strlen (p);
		if (hl >= sizeof host) return -1; memcpy (host, p, hl); host[hl] = 0; const char *path = sl ? sl : "/";
		char *co = strchr (host, ':'); if (co) { *co = 0; snprintf (port, sizeof port, "%s", co + 1); }
		struct addrinfo hints, *ai = NULL; memset (&hints, 0, sizeof hints); hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo (host, port, &hints, &ai) || !ai) return -1;
		int s = -1; for (struct addrinfo *a = ai; a; a = a->ai_next) {
			s = socket (a->ai_family, a->ai_socktype, a->ai_protocol); if (s < 0) continue;
			struct timeval tv = { 30, 0 }; setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt (s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#ifdef SO_NOSIGPIPE
			{ int one = 1; setsockopt (s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
			if (!connect (s, a->ai_addr, a->ai_addrlen)) break; close (s); s = -1; }
		freeaddrinfo (ai); if (s < 0) return -1;
		char rq[1400]; const int rn = snprintf (rq, sizeof rq, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Vectrex_EMU\r\nConnection: close\r\n\r\n", path, host);
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
		if (send (s, rq, (size_t) rn, MSG_NOSIGNAL) != rn) { close (s); return -1; }
		/* the head: the status, Location, Content-Length */
		char hd[8192]; size_t hn = 0; char *end = NULL;
		while (!end && hn < sizeof hd - 1) { const ssize_t n = recv (s, hd + hn, sizeof hd - 1 - hn, 0); if (n <= 0) break; hn += (size_t) n; hd[hn] = 0; end = strstr (hd, "\r\n\r\n"); }
		if (!end) { close (s); return -1; }
		int code = 0; sscanf (hd, "HTTP/%*s %d", &code); long long tot = 0; char loc[1100] = "";
		for (char *l = strstr (hd, "\r\n"); l && l < end; l = strstr (l + 2, "\r\n")) {
			if (!strncasecmp (l + 2, "Content-Length:", 15)) tot = atoll (l + 17);
			if (!strncasecmp (l + 2, "Location:", 9)) { const char *v = l + 11; while (*v == ' ') v++; size_t k = 0; while (v[k] && v[k] != '\r' && k < sizeof loc - 1) { loc[k] = v[k]; k++; } loc[k] = 0; } }
		if (code >= 300 && code < 400 && loc[0]) { close (s);
			if (loc[0] == '/') { char b[1100]; snprintf (b, sizeof b, "http://%s%s", host, loc); snprintf (cur, sizeof cur, "%s", b); } else snprintf (cur, sizeof cur, "%s", loc);
			continue; }
		if (code != 200) { close (s); return -1; }
		long long got = 0; int r = 0; char buf[65536]; const size_t first = hn - (size_t) (end + 4 - hd);
		if (first) { if (f) r = fwrite (end + 4, 1, first, f) != first; else if (first > *len) r = 1; else memcpy (mem, end + 4, first); got = (long long) first; }
		while (!r) { const ssize_t n = recv (s, buf, sizeof buf, 0); if (n < 0) { r = 1; break; } if (!n) break;
			if (f) { if (fwrite (buf, 1, (size_t) n, f) != (size_t) n) r = 1; px_progress (got + n, tot, share, 0); }
			else { if (got + n > (long long) *len) { r = 1; break; } memcpy (mem + got, buf, (size_t) n); }
			got += n; }
		close (s);
		if (!f) *len = (size_t) got;
		return r || (tot && got != tot) ? -1 : 0;
	}
	return -1;
}
#endif

/* ---- the zip: its central directory; each file inflated into the folder */
static unsigned px_u16 (const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned px_u32 (const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned) p[3] << 24); }
static void px_mkdirs (char *p)                 /* (each folder of the path; p's last part the file) */
{
	for (char *c = p + 1; *c; c++) if (*c == '/' || *c == '\\') { const char k = *c; *c = 0;
#ifdef _WIN32
		_mkdir (p);
#else
		mkdir (p, 0755);
#endif
		*c = k; }
}
static int px_unzip (const char *zp, const char *dir)   /* (0: whole; pitrex.img last) */
{
	FILE *z = fopen (zp, "rb"); if (!z) return -1;
	fseek (z, 0, SEEK_END); const long zs = ftell (z); unsigned char tail[65558]; const long tn = zs < (long) sizeof tail ? zs : (long) sizeof tail;
	fseek (z, zs - tn, SEEK_SET); if (fread (tail, 1, (size_t) tn, z) != (size_t) tn) { fclose (z); return -1; }
	long eo = -1; for (long i = tn - 22; i >= 0; i--) if (px_u32 (tail + i) == 0x06054b50) { eo = i; break; }
	if (eo < 0) { fclose (z); return -1; }
	const unsigned cnt = px_u16 (tail + eo + 10), csz = px_u32 (tail + eo + 12), coff = px_u32 (tail + eo + 16);
	unsigned char *cd = malloc (csz); if (!cd) { fclose (z); return -1; }
	fseek (z, (long) coff, SEEK_SET); if (fread (cd, 1, csz, z) != csz) { free (cd); fclose (z); return -1; }
	/* one folder round it all: left out (getter.py's strip); pitrex.img somewhere in it */
	char strip[300] = ""; int one = 1, has_img = -1; unsigned o = 0;
	for (unsigned i = 0; i < cnt && o + 46 <= csz; i++) { const unsigned nl = px_u16 (cd + o + 28);
		char nm[600]; snprintf (nm, sizeof nm, "%.*s", (int) nl, (const char *) cd + o + 46); const char *sl = strchr (nm, '/');
		if (!sl) one = 0; else if (!strip[0]) snprintf (strip, sizeof strip, "%.*s", (int) (sl - nm + 1), nm); else if (strncmp (nm, strip, strlen (strip))) one = 0;
		o += 46 + nl + px_u16 (cd + o + 30) + px_u16 (cd + o + 32); }
	if (!one) strip[0] = 0;
	int r = 0; long long done = 0, all = 0;
	for (int pass = 0; pass < 3 && !r; pass++) {        /* (0: the sizes, 1: all but pitrex.img, 2: pitrex.img) */
		o = 0;
		for (unsigned i = 0; i < cnt && o + 46 <= csz && !r; i++) {
			const unsigned char *e = cd + o; const unsigned meth = px_u16 (e + 10), cs = px_u32 (e + 20), us = px_u32 (e + 24), nl = px_u16 (e + 28), lo = px_u32 (e + 42);
			char nm[600]; snprintf (nm, sizeof nm, "%.*s", (int) nl, (const char *) e + 46); o += 46 + nl + px_u16 (e + 30) + px_u16 (e + 32);
			const char *rel = nm + strlen (strip); const size_t rl = strlen (rel);
			if (!rl || rel[rl - 1] == '/' || strstr (rel, "..")) continue;
			const int img = !strcmp (rel, "pitrex.img"); if (img && has_img < 0) has_img = 1;
			if (pass == 0) { all += us; continue; }
			if ((pass == 2) != img) continue;
			if (meth != 0 && meth != 8) { r = -1; break; }
			unsigned char lh[30]; fseek (z, (long) lo, SEEK_SET); if (fread (lh, 1, 30, z) != 30 || px_u32 (lh) != 0x04034b50) { r = -1; break; }
			fseek (z, (long) lo + 30 + px_u16 (lh + 26) + px_u16 (lh + 28), SEEK_SET);
			unsigned char *in = malloc (cs ? cs : 1), *out = meth ? malloc (us ? us : 1) : in;
			if (!in || !out || fread (in, 1, cs, z) != cs) r = -1;
			else if (meth && pz_inflate (out, us, in, cs)) r = -1;
			if (!r) { char p[1800]; snprintf (p, sizeof p, "%s/%s", dir, rel); px_mkdirs (p); FILE *w = fopen (p, "wb");
				if (!w || fwrite (out, 1, us, w) != us) r = -1; if (w) fclose (w); }
			if (out != in) free (out); free (in);
			done += us; if (all) { const int pc = (int) (done * 100 / all); if (pc != px_pct) { px_pct = pc; px_redo |= 2; } }
		}
		if (pass == 0 && has_img < 0) r = -1;           /* (no pitrex.img: not the PiTrex's files) */
	}
	free (cd); fclose (z);
	return r;
}

/* ---- the whole: the sources in getter.py's order, each until one is whole */
static void px_run (void)
{
	char zp[1200]; snprintf (zp, sizeof zp, "%s.download.zip", px_dir);
	char urls[8][300]; int nu = 0;
	{ size_t n = 2 << 20; char *pg = malloc (n + 1);                 /* (his download page: the newest PiTrex_sd_... first) */
	  if (pg && !px_get ("http://vide.malban.de/pitrex/pitrex-baremetal-download", NULL, pg, &n, 0)) { pg[n] = 0; int best = 0;
		for (char *q = strstr (pg, "download_id="); q; q = strstr (q + 1, "download_id=")) { const int id = atoi (q + 12);
			const char *z = strstr (q, "PiTrex_sd_");          /* (its file named within 1500 characters after it) */
			if (z && z - q < 1500 && id > best) best = id; }
		if (best) snprintf (urls[nu++], 300, "http://vide.malban.de/?sdm_process_download=1&download_id=%d", best); }
	  free (pg); }
	snprintf (urls[nu++], 300, "http://vide.malban.de/?sdm_process_download=1&download_id=7074");
	snprintf (urls[nu++], 300, "http://vectrex.malban.de/pitrex/PiTrex_sd_Jul29_25.zip");
	for (int i = 0; i < nu; i++) {
		px_st = 1; px_pct = 0; px_redo |= 2;
		FILE *f = fopen (zp, "wb"); if (!f) break;
		const int g = px_get (urls[i], f, NULL, NULL, 100); fclose (f);
		fprintf (stderr, "[vectrex_emu] PiTrex: %s %s\n", urls[i], g ? "failed" : "fetched");
		if (g) continue;
		px_st = 2; px_pct = 0; px_redo |= 2;
		{ char d[1200]; snprintf (d, sizeof d, "%s/x", px_dir); px_mkdirs (d); }
		const int u = px_unzip (zp, px_dir);
		fprintf (stderr, "[vectrex_emu] PiTrex: unpacked %s\n", u ? "- not whole" : "into its folder");
		if (!u) { remove (zp); px_st = 3; px_redo |= 1; return; }
	}
	remove (zp); px_st = 4; px_redo |= 2;
}
#ifdef _WIN32
static DWORD WINAPI px_thr (LPVOID a) { (void) a; px_run (); return 0; }
#else
static void *px_thr (void *a) { (void) a; px_run (); return NULL; }
#endif
static void px_start (const char *sys)            /* (the browser's "PiTrex: get it") */
{
	if (px_st == 1 || px_st == 2) return;
	snprintf (px_dir, sizeof px_dir, "%s/vectrex_emu/pitrex", sys && *sys ? sys : ".");
	px_st = 1; px_pct = 0; px_redo |= 2;
#ifdef _WIN32
	HANDLE t = CreateThread (NULL, 0, px_thr, NULL, 0, NULL); if (t) CloseHandle (t); else px_st = 4;
#else
	pthread_t t; if (pthread_create (&t, NULL, px_thr, NULL)) px_st = 4; else pthread_detach (t);
#endif
}
