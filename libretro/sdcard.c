/* sdcard.c - the desktop's fatimg.py in C, for the RetroArch core (included by libretro_vx.c): a folder as a cartridge's
 * SD card - a FAT32 image built from the folder before a game starts (long file names, subfolders; no partition
 * table, 512-byte sectors, 2 FATs, 4 KB clusters - as fatimg.build), and what the game wrote copied back into the
 * folder afterwards (new and changed files; nothing in the folder is ever deleted - as fatimg.sync_back).
 * A list of the folder's files (names, sizes, times) beside the image: the image is built again only when it changed.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#define sdc_stat _stat64
typedef struct _stat64 sdc_st;
#else
#include <sys/stat.h>
#include <dirent.h>
#define sdc_stat stat
typedef struct stat sdc_st;
#endif

#define SDC_SECTOR 512
#define SDC_EOC 0x0FFFFFFFu
typedef struct { char name[256]; int dir; long long size; long long mtime; } sdc_ent;

static int sdc_cmp (const void *a, const void *b) { return vx_stricmp (((const sdc_ent *) a)->name, ((const sdc_ent *) b)->name); }
/* (netplay, netls.c: a card the same on both sides, as the desktop's fatimg.build (only, fixed_time) - only the files both
 *  have alike (their paths below the card's folder, '/' between), every time and the volume's serial one fixed time) */
static char **sdc_only; static int sdc_nonly; static long long sdc_fixed; static char sdc_root[1800];
static int sdc_keep (const char *path, int dir)
{
	if (!sdc_only) return 1;
	size_t rl = strlen (sdc_root); if (strncmp (path, sdc_root, rl)) return 0;
	char rel[1100]; snprintf (rel, sizeof rel, "%s", path + rl + (path[rl] ? 1 : 0)); for (char *q = rel; *q; q++) if (*q == '\\') *q = '/';
	size_t n = strlen (rel);
	for (int i = 0; i < sdc_nonly; i++) { const char *o = sdc_only[i];
		if (!dir && !strcmp (o, rel)) return 1;
		if (dir && !strncmp (o, rel, n) && o[n] == '/') return 1; }
	return 0;
}
static int sdc_list (const char *path, sdc_ent **out)        /* (the folder's entries, sorted as fatimg: by name, any case) */
{
	int n = 0, cap = 0; sdc_ent *v = NULL; *out = NULL;
#ifdef _WIN32
	char pat[1800]; snprintf (pat, sizeof pat, "%s\\*", path);
	struct __finddata64_t fd; intptr_t h = _findfirst64 (pat, &fd); if (h == -1) return 0;
	do { const char *nm = fd.name;
#else
	DIR *d = opendir (path); if (!d) return 0;
	struct dirent *de;
	while ((de = readdir (d))) { const char *nm = de->d_name;
#endif
		if (!strcmp (nm, ".") || !strcmp (nm, "..") || strlen (nm) > 255) continue;
		char p[2048]; snprintf (p, sizeof p, "%s" SEP "%s", path, nm); sdc_st st; if (sdc_stat (p, &st)) continue;
		if (!sdc_keep (p, (st.st_mode & S_IFMT) == S_IFDIR)) continue;
		if (n == cap) { cap = cap ? cap * 2 : 32; v = realloc (v, (size_t) cap * sizeof *v); }
		snprintf (v[n].name, sizeof v[n].name, "%s", nm); v[n].dir = (st.st_mode & S_IFMT) == S_IFDIR;
		v[n].size = v[n].dir ? 0 : (long long) st.st_size; v[n].mtime = (long long) st.st_mtime; n++;
#ifdef _WIN32
	} while (_findnext64 (h, &fd) == 0); _findclose (h);
#else
	} closedir (d);
#endif
	if (n) qsort (v, (size_t) n, sizeof *v, sdc_cmp);
	*out = v; return n;
}
static long long sdc_tree (const char *path, FILE *man, const char *rel)   /* (the files' total size; their list to man) */
{
	sdc_ent *v; int n = sdc_list (path, &v); long long t = 0;
	for (int i = 0; i < n; i++) { char p[2048], r[1024]; snprintf (p, sizeof p, "%s" SEP "%s", path, v[i].name);
		snprintf (r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", v[i].name);
		if (v[i].dir) t += sdc_tree (p, man, r);
		else { t += v[i].size; if (man) fprintf (man, "%s|%lld|%lld\n", r, v[i].size, v[i].mtime); } }
	free (v); return t;
}

/* ---- the names (fatimg _short_ok, _short_name, _lfn_entries) */
static int sdc_shortch (int c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr ("$%'-_@~`!(){}^#&", c); }
static int sdc_short_ok (const char *nm)
{
	const char *dot = strchr (nm, '.'); size_t bl = dot ? (size_t) (dot - nm) : strlen (nm), el = dot ? strlen (dot + 1) : 0;
	if (!bl || bl > 8 || el > 3 || (dot && strchr (dot + 1, '.'))) return 0;
	for (const char *q = nm; *q; q++) if (q != dot && !sdc_shortch ((unsigned char) *q)) return 0;
	return 1;
}
typedef struct { unsigned char s[11]; } sdc_sn;
static int sdc_used (const sdc_sn *u, int nu, const unsigned char *s) { for (int i = 0; i < nu; i++) if (!memcmp (u[i].s, s, 11)) return 1; return 0; }
static int sdc_short (const char *nm, sdc_sn *used, int *nu, unsigned char out[11])   /* (-> 1 when it needs a long name) */
{
	if (sdc_short_ok (nm)) {
		memset (out, ' ', 11); const char *dot = strchr (nm, '.'); size_t bl = dot ? (size_t) (dot - nm) : strlen (nm);
		memcpy (out, nm, bl); if (dot) memcpy (out + 8, dot + 1, strlen (dot + 1));
		if (!sdc_used (used, *nu, out)) { memcpy (used[(*nu)++].s, out, 11); return 0; } }
	char b[300], e[8] = ""; const char *lead = nm; while (*lead == '.') lead++;
	const char *dot = *lead ? strrchr (nm, '.') : NULL; if (dot && dot < lead) dot = NULL;
	int k = 0; for (const char *q = nm; *q && (!dot || q < dot) && k < 290; q++) { int c = toupper ((unsigned char) *q); if (isalnum (c) || strchr ("_-$", c)) b[k++] = (char) c; }
	b[k] = 0; if (!k) strcpy (b, "X");
	if (dot) { k = 0; for (const char *q = dot + 1; *q && k < 3; q++) { int c = toupper ((unsigned char) *q); if (isalnum (c) || strchr ("_-$", c)) e[k++] = (char) c; } e[k] = 0; if (!k) strcpy (e, "X"); }
	for (int n = 1; n < 999999; n++) {
		char tail[12]; int tl = snprintf (tail, sizeof tail, "~%d", n); int bl = (int) strlen (b); if (bl > 8 - tl) bl = 8 - tl;
		memset (out, ' ', 11); memcpy (out, b, (size_t) bl); memcpy (out + bl, tail, (size_t) tl); memcpy (out + 8, e, strlen (e));
		if (!sdc_used (used, *nu, out)) { memcpy (used[(*nu)++].s, out, 11); return 1; } }
	return 1;
}
static int sdc_utf16 (const char *s, unsigned short *u, int max)     /* (UTF-8 -> UTF-16 code units) */
{
	int n = 0; const unsigned char *p = (const unsigned char *) s;
	while (*p && n < max - 2) { unsigned c = *p++;
		if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63); p += 3; c -= 0x10000; u[n++] = (unsigned short) (0xD800 | c >> 10); u[n++] = (unsigned short) (0xDC00 | (c & 0x3ff)); continue; }
		if (c >= 0xE0 && p[0] && p[1]) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
		else if (c >= 0xC0 && p[0]) { c = (c & 31) << 6 | (p[0] & 63); p += 1; }
		u[n++] = (unsigned short) c; }
	return n;
}
static int sdc_needs_lfn (const char *nm) { for (const char *q = nm; *q; q++) if (*q >= 'a' && *q <= 'z') return 1; return 0; }
static int sdc_lfn (const char *nm, const unsigned char s11[11], unsigned char *out)   /* (-> entries written, on-disk order) */
{
	unsigned short u[600]; int n = sdc_utf16 (nm, u, 560); u[n++] = 0; while (n % 13) u[n++] = 0xFFFF;
	int parts = n / 13; unsigned char ck = 0; for (int i = 0; i < 11; i++) ck = (unsigned char) (((ck & 1) << 7) + (ck >> 1) + s11[i]);
	for (int i = 0; i < parts; i++) {
		unsigned char *e = out + (size_t) (parts - 1 - i) * 32; const unsigned short *c = u + i * 13;
		e[0] = (unsigned char) (i + 1 + (i == parts - 1 ? 0x40 : 0)); e[11] = 0x0F; e[12] = 0; e[13] = ck; e[26] = e[27] = 0;
		for (int k = 0; k < 5; k++) { e[1 + 2 * k] = (unsigned char) c[k]; e[2 + 2 * k] = (unsigned char) (c[k] >> 8); }
		for (int k = 0; k < 6; k++) { e[14 + 2 * k] = (unsigned char) c[5 + k]; e[15 + 2 * k] = (unsigned char) (c[5 + k] >> 8); }
		for (int k = 0; k < 2; k++) { e[28 + 2 * k] = (unsigned char) c[11 + k]; e[29 + 2 * k] = (unsigned char) (c[11 + k] >> 8); } }
	return parts;
}
static void sdc_entry (unsigned char *e, const unsigned char s11[11], int attr, uint32_t cl, uint32_t size, long long mt)
{
	time_t tt = (time_t) (sdc_fixed ? sdc_fixed : mt); struct tm *lt = sdc_fixed ? gmtime (&tt) : localtime (&tt);   /* (fixed: the same on
	                                              both sides, whatever their time zones) */ int y = lt ? lt->tm_year + 1900 : 1980; if (y < 1980) y = 1980;
	unsigned t = lt ? (unsigned) (lt->tm_hour << 11 | lt->tm_min << 5 | lt->tm_sec / 2) : 0, d = lt ? (unsigned) ((y - 1980) << 9 | (lt->tm_mon + 1) << 5 | lt->tm_mday) : 0x21;
	memcpy (e, s11, 11); e[11] = (unsigned char) attr; e[12] = e[13] = 0;
	e[14] = (unsigned char) t; e[15] = (unsigned char) (t >> 8); e[16] = (unsigned char) d; e[17] = (unsigned char) (d >> 8); e[18] = e[16]; e[19] = e[17];
	e[20] = (unsigned char) (cl >> 16); e[21] = (unsigned char) (cl >> 24); e[22] = e[14]; e[23] = e[15]; e[24] = e[16]; e[25] = e[17];
	e[26] = (unsigned char) cl; e[27] = (unsigned char) (cl >> 8); e[28] = (unsigned char) size; e[29] = (unsigned char) (size >> 8); e[30] = (unsigned char) (size >> 16); e[31] = (unsigned char) (size >> 24);
}

/* ---- the image (fatimg.build) */
typedef struct { FILE *f; uint32_t *fat; uint32_t nclus, next, spc, data_start; int bad; } sdc_img;
static uint32_t sdc_alloc (sdc_img *m, long long nbytes)
{
	const long long cs = (long long) m->spc * SDC_SECTOR; uint32_t n = (uint32_t) ((nbytes + cs - 1) / cs); if (!n) n = 1;
	uint32_t first = m->next; if (first + n > m->nclus + 2) { m->bad = 1; return 0; }
	for (uint32_t k = 0; k < n; k++) m->fat[first + k] = k < n - 1 ? first + k + 1 : SDC_EOC;
	m->next += n; return first;
}
static void sdc_put (sdc_img *m, uint32_t cl, const void *b, size_t n)
{
	if (!cl || !n) return;
	if (fseek (m->f, ((long long) m->data_start + (long long) (cl - 2) * m->spc) * SDC_SECTOR, SEEK_SET) == 0) fwrite (b, 1, n, m->f);
}
static size_t sdc_dir_bytes (const sdc_ent *v, int n, int root)      /* (room for a folder's entries: every name with a long one) */
{
	size_t k = root ? 1 : 2; for (int i = 0; i < n; i++) k += 1 + (strlen (v[i].name) + 1 + 12) / 13;
	return (k + 1) * 32;
}
static void sdc_make_dir (sdc_img *m, const char *path, uint32_t self, uint32_t parent, int root, size_t room)
{
	sdc_ent *v; int n = sdc_list (path, &v);
	unsigned char *raw = calloc (1, room + 32); size_t at = 0; time_t now = time (NULL);
	sdc_sn *used = malloc (sizeof (sdc_sn) * (size_t) (n + 4)); int nu = 0;
	if (!root) { static const unsigned char D1[11] = ".          ", D2[11] = "..         ";
		sdc_entry (raw + at, D1, 0x10, self, 0, now); at += 32; sdc_entry (raw + at, D2, 0x10, parent == 2 ? 0 : parent, 0, now); at += 32; }
	else { memcpy (raw, "VECTREX SD ", 11); raw[11] = 0x08; at += 32; }
	for (int i = 0; i < n && !m->bad; i++) {
		char p[2048]; snprintf (p, sizeof p, "%s" SEP "%s", path, v[i].name);
		unsigned char s11[11]; int need = sdc_short (v[i].name, used, &nu, s11) || sdc_needs_lfn (v[i].name);
		uint32_t cl = 0; uint32_t size = 0; int attr = 0x20;
		if (v[i].dir) {
			sdc_ent *sv; int sn = sdc_list (p, &sv); size_t sroom = sdc_dir_bytes (sv, sn, 0); free (sv);
			cl = sdc_alloc (m, (long long) sroom); attr = 0x10;
			if (!m->bad) sdc_make_dir (m, p, cl, self, 0, sroom);
		} else {
			FILE *f = fopen (p, "rb"); long long sz = v[i].size;
			if (f && sz > 0) { cl = sdc_alloc (m, sz);
				if (!m->bad) { unsigned char *buf = malloc (1 << 20); long long done = 0; size_t k;
					fseek (m->f, ((long long) m->data_start + (long long) (cl - 2) * m->spc) * SDC_SECTOR, SEEK_SET);   /* (the clusters: one run) */
					while (buf && (k = fread (buf, 1, 1 << 20, f)) > 0 && done < sz) { fwrite (buf, 1, k, m->f); done += (long long) k; }
					free (buf); } }
			if (f) fclose (f); size = (uint32_t) (sz > 0 ? sz : 0); }
		if (need) at += 32 * (size_t) sdc_lfn (v[i].name, s11, raw + at);
		sdc_entry (raw + at, s11, attr, cl, size, v[i].mtime); at += 32; }
	sdc_put (m, self, raw, room);
	free (raw); free (used); free (v);
}
static int sdc_build (const char *folder, const char *img)
{
	const long long content = sdc_tree (folder, NULL, "");
	long long mb = (content * 2 + (32LL << 20)) >> 20; if (mb < 64) mb = 64; if (mb < 272) mb = 272;   /* (4 KB clusters: >= 65525 of them) */
	const uint32_t total = (uint32_t) (mb * 2048); uint32_t spc = 8; while (total / spc > 4000000u) spc *= 2;
	const uint32_t reserved = 32, nfats = 2, clusters = total / spc, fatsz = (clusters * 4 + 8 + SDC_SECTOR - 1) / SDC_SECTOR;
	sdc_img m = { 0 }; m.spc = spc; m.data_start = reserved + nfats * fatsz; m.nclus = (total - m.data_start) / spc; m.next = 2;
	m.fat = calloc ((size_t) m.nclus + 2, 4); if (!m.fat) return 0;
	m.fat[0] = 0x0FFFFFF8u; m.fat[1] = SDC_EOC;
	m.f = fopen (img, "w+b"); if (!m.f) { free (m.fat); return 0; }
	{ /* (the whole size: zero - sparse where the system can) */
#ifdef _WIN32
	  _chsize_s (_fileno (m.f), (long long) total * SDC_SECTOR);
#else
	  if (ftruncate (fileno (m.f), (off_t) total * SDC_SECTOR)) {}
#endif
	}
	sdc_ent *v; int n = sdc_list (folder, &v); size_t room = sdc_dir_bytes (v, n, 1); free (v);
	uint32_t root = sdc_alloc (&m, (long long) room);
	if (root == 2) sdc_make_dir (&m, folder, 2, 0, 1, room); else m.bad = 1;
	unsigned char bs[SDC_SECTOR] = { 0 }, fsi[SDC_SECTOR] = { 0 };
	memcpy (bs, "\xEB\x58\x90" "MSWIN4.1", 11);
#define W16(p, o, v) ((p)[o] = (unsigned char) (v), (p)[(o) + 1] = (unsigned char) ((v) >> 8))
#define W32(p, o, v) (W16 (p, o, (v) & 0xffff), W16 (p, (o) + 2, (uint32_t) (v) >> 16))
	W16 (bs, 11, SDC_SECTOR); bs[13] = (unsigned char) spc; W16 (bs, 14, reserved); bs[16] = (unsigned char) nfats; bs[21] = 0xF8;
	W16 (bs, 24, 63); W16 (bs, 26, 255); W32 (bs, 32, total); W32 (bs, 36, fatsz); W32 (bs, 44, 2); W16 (bs, 48, 1); W16 (bs, 50, 6);
	bs[64] = 0x80; bs[66] = 0x29; W32 (bs, 67, (uint32_t) (sdc_fixed ? sdc_fixed : time (NULL))); memcpy (bs + 71, "VECTREX SD FAT32   ", 19); bs[510] = 0x55; bs[511] = 0xAA;
	W32 (fsi, 0, 0x41615252u); W32 (fsi, 484, 0x61417272u); W32 (fsi, 488, m.nclus - (m.next - 2)); W32 (fsi, 492, m.next); W32 (fsi, 508, 0xAA550000u);
	fseek (m.f, 0, SEEK_SET); fwrite (bs, 1, SDC_SECTOR, m.f); fwrite (fsi, 1, SDC_SECTOR, m.f);
	fseek (m.f, 6 * SDC_SECTOR, SEEK_SET); fwrite (bs, 1, SDC_SECTOR, m.f); fwrite (fsi, 1, SDC_SECTOR, m.f);
	for (uint32_t k = 0; k < nfats; k++) { fseek (m.f, (long long) (reserved + k * fatsz) * SDC_SECTOR, SEEK_SET);
		for (uint32_t i = 0; i < m.nclus + 2; i++) { unsigned char b4[4]; W32 (b4, 0, m.fat[i]); fwrite (b4, 1, 4, m.f); } }
#undef W16
#undef W32
	int ok = !m.bad && !ferror (m.f); fclose (m.f); free (m.fat);
	if (!ok) remove (img);
	return ok;
}

/* ---- what the game wrote, back into the folder (fatimg._read_image, sync_back) */
typedef struct { FILE *f; unsigned char *fat; uint32_t nent, data_start, csize, bps, spc; } sdc_rd;
static unsigned char *sdc_chain (sdc_rd *r, uint32_t cl, long long size, long long *got)   /* (-> the chain's bytes, size or all) */
{
	unsigned char *buf = NULL; long long n = 0, cap = 0; uint32_t steps = 0;
	while (cl >= 2 && cl < r->nent && steps++ < r->nent) {
		if (n + r->csize > cap) { cap = (n + r->csize) * 2; buf = realloc (buf, (size_t) cap); }
		fseek (r->f, ((long long) r->data_start + (long long) (cl - 2) * r->spc) * r->bps, SEEK_SET);
		if (fread (buf + n, 1, r->csize, r->f) != r->csize) break;
		n += r->csize; if (size >= 0 && n >= size) break;
		cl = (r->fat[cl * 4] | r->fat[cl * 4 + 1] << 8 | r->fat[cl * 4 + 2] << 16 | (uint32_t) r->fat[cl * 4 + 3] << 24) & 0x0FFFFFFF;
		if (cl >= 0x0FFFFFF8) break; }
	*got = size >= 0 && n > size ? size : n; return buf;
}
static void sdc_mkdirs (const char *path)                  /* (every folder of path, below the card's folder) */
{
	char p[2048]; snprintf (p, sizeof p, "%s", path);
	for (char *q = p + 1; *q; q++) if (*q == '/' || *q == '\\') { char c = *q; *q = 0; vx_mkdir (p); *q = c; }
}
static long long sdc_older_than;                     /* (> 0: only files in the folder older than this - the card - are replaced) */
static int sdc_back_walk (sdc_rd *r, uint32_t cl, const char *folder, const char *rel, int depth)
{
	if (depth > 32) return 0;
	long long n; unsigned char *raw = sdc_chain (r, cl, -1, &n); int written = 0;
	unsigned short lfn[20][13]; int lfn_n = 0;
	for (long long i = 0; raw && i + 32 <= n; i += 32) {
		const unsigned char *e = raw + i; if (e[0] == 0) break;
		if (e[0] == 0xE5) { lfn_n = 0; continue; }
		if (e[11] == 0x0F) { int o = (e[0] & 0x1f) - 1; if (o >= 0 && o < 20) { static const int P[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
				for (int k = 0; k < 13; k++) lfn[o][k] = (unsigned short) (e[P[k]] | e[P[k] + 1] << 8); if (o + 1 > lfn_n) lfn_n = o + 1; } continue; }
		if (e[11] & 0x08) { lfn_n = 0; continue; }
		char name[600]; int k = 0;
		if (lfn_n) { for (int o = 0; o < lfn_n; o++) for (int j = 0; j < 13; j++) { unsigned c = lfn[o][j]; if (!c || c == 0xFFFF) goto done;
				if (c < 0x80) name[k++] = (char) c; else if (c < 0x800) { name[k++] = (char) (0xC0 | c >> 6); name[k++] = (char) (0x80 | (c & 63)); }
				else { name[k++] = (char) (0xE0 | c >> 12); name[k++] = (char) (0x80 | (c >> 6 & 63)); name[k++] = (char) (0x80 | (c & 63)); }
				if (k > 580) goto done; }
		  done: name[k] = 0; }
		else { int b = 8; while (b > 0 && e[b - 1] == ' ') b--; int x = 3; while (x > 0 && e[8 + x - 1] == ' ') x--;
			for (int j = 0; j < b; j++) name[k++] = (char) ((e[12] & 0x08) ? tolower (e[j]) : e[j]);
			if (x) { name[k++] = '.'; for (int j = 0; j < x; j++) name[k++] = (char) ((e[12] & 0x10) ? tolower (e[8 + j]) : e[8 + j]); }
			name[k] = 0; }
		lfn_n = 0;
		if (!name[0] || !strcmp (name, ".") || !strcmp (name, "..") || strchr (name, '/') || strchr (name, '\\')) continue;
		const uint32_t first = (uint32_t) (e[20] | e[21] << 8) << 16 | (uint32_t) (e[26] | e[27] << 8), size = (uint32_t) (e[28] | e[29] << 8 | e[30] << 16 | (uint32_t) e[31] << 24);
		char r2[1200]; snprintf (r2, sizeof r2, "%s%s%s", rel, *rel ? SEP : "", name);
		if (e[11] & 0x10) { if (first >= 2) written += sdc_back_walk (r, first, folder, r2, depth + 1); continue; }
		long long got = 0; unsigned char *blob = first >= 2 && size ? sdc_chain (r, first, size, &got) : NULL;
		char dst[2400]; snprintf (dst, sizeof dst, "%s" SEP "%s", folder, r2);
		int same = 0; FILE *o = fopen (dst, "rb");
		if (o && sdc_older_than > 0) { sdc_st st; if (!sdc_stat (dst, &st) && (long long) st.st_mtime > sdc_older_than) same = 1; }   /* (the folder's
		                                              copy is newer than the card: put there since - it stays) */
		if (same) { fclose (o); o = NULL; }
		if (!same && o) { fseek (o, 0, SEEK_END); long long os_ = ftell (o);
			if (os_ == got) { unsigned char *ob = malloc ((size_t) (got ? got : 1)); fseek (o, 0, SEEK_SET);
				same = ob && (got == 0 || fread (ob, 1, (size_t) got, o) == (size_t) got) && (got == 0 || !memcmp (ob, blob, (size_t) got)); free (ob); }
			fclose (o); }
		if (!same) { sdc_mkdirs (dst); o = fopen (dst, "wb"); if (o) { if (got) fwrite (blob, 1, (size_t) got, o); fclose (o); written++;
				fprintf (stderr, "[vectrex_emu] SD card: %s written back\n", r2); } }
		free (blob); }
	free (raw); return written;
}
static int sdc_sync_back (const char *img, const char *folder)
{
	FILE *f = fopen (img, "rb"); if (!f) return 0;
	unsigned char bs[SDC_SECTOR]; if (fread (bs, 1, SDC_SECTOR, f) != SDC_SECTOR) { fclose (f); return 0; }
	sdc_rd r = { 0 }; r.f = f; r.bps = (uint32_t) (bs[11] | bs[12] << 8); r.spc = bs[13];
	const uint32_t reserved = (uint32_t) (bs[14] | bs[15] << 8), nfats = bs[16], fatsz = (uint32_t) (bs[36] | bs[37] << 8 | bs[38] << 16 | (uint32_t) bs[39] << 24),
	               root = (uint32_t) (bs[44] | bs[45] << 8 | bs[46] << 16 | (uint32_t) bs[47] << 24);
	if (r.bps != 512 || !r.spc || !fatsz || fatsz > (64u << 20) / 512) { fclose (f); return 0; }
	r.fat = malloc ((size_t) fatsz * r.bps); r.nent = fatsz * r.bps / 4; r.data_start = reserved + nfats * fatsz; r.csize = r.spc * r.bps;
	fseek (f, (long long) reserved * r.bps, SEEK_SET);
	int w = 0; if (r.fat && fread (r.fat, 1, (size_t) fatsz * r.bps, f) == (size_t) fatsz * r.bps) w = sdc_back_walk (&r, root, folder, "", 0);
	free (r.fat); fclose (f); return w;
}

/* ---- a game's card: the image built from its folder (only when the folder changed), the last run's writes first */
static int sdc_prepare (const char *folder, const char *img)
{
	/* (a game that did not end cleanly: what it wrote, kept - not over files put in the folder since) */
	{ sdc_st st; sdc_older_than = sdc_stat (img, &st) ? 0 : (long long) st.st_mtime;
	  if (sdc_older_than) sdc_sync_back (img, folder);
	  sdc_older_than = 0; }
	char man[1900], cur[1900]; snprintf (man, sizeof man, "%s.list", img); snprintf (cur, sizeof cur, "%s.new", man);
	FILE *c = fopen (cur, "wb"); if (c) { sdc_tree (folder, c, ""); fclose (c); }
	int same = 0; FILE *a = fopen (man, "rb"), *b = fopen (cur, "rb"), *t = fopen (img, "rb");
	if (a && b && t) { same = 1; int x, y; do { x = fgetc (a); y = fgetc (b); if (x != y) { same = 0; break; } } while (x != EOF); }
	if (a) fclose (a); if (b) fclose (b); if (t) fclose (t);
	if (same) { remove (cur); return 1; }
	int ok = sdc_build (folder, img);
	remove (man); if (ok) rename (cur, man); else remove (cur);
	fprintf (stderr, "[vectrex_emu] SD card %s built from %s%s\n", img, folder, ok ? "" : " - FAILED");
	return ok;
}
