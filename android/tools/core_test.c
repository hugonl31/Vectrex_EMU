/* core_test.c - regression test for the Android core library (core_api.c): reads a --live input
 * stream (as the desktop program writes to the core's stdin) and writes the same "VF" frame stream
 * the original `vtest BIOS CART --live` writes, through vx_frame. Both outputs must be identical:
 *   vtest bios.bin cart.bin --live /dev/null < in.bin > a.bin
 *   core_test bios.bin cart.bin in.bin > b.bin  &&  cmp a.bin b.bin */
#include "../app/src/main/cpp/core_api.c"

static unsigned char *slurp (const char *fn, size_t *n)
{
	FILE *f = fopen (fn, "rb"); if (!f) { perror (fn); exit (2); }
	static unsigned char b[1048576 + 1]; *n = fread (b, 1, sizeof b, f); fclose (f);
	unsigned char *r = malloc (*n + 1); memcpy (r, b, *n); return r;
}
static void w16 (FILE *f, unsigned v) { fputc (v & 255, f); fputc ((v >> 8) & 255, f); }

int main (int argc, char **argv)
{
	if (argc < 4) { fprintf (stderr, "usage: core_test bios cart input\n"); return 2; }
	size_t nb, nc; unsigned char *b = slurp (argv[1], &nb), *c = slurp (argv[2], &nc);
	if (vx_load (b, nb, c, nc, NULL)) { fprintf (stderr, "bad bios\n"); return 2; }
	FILE *in = fopen (argv[3], "rb"); if (!in) { perror (argv[3]); return 2; }
	static vx_frame_t fr; unsigned char m[8];
	while (fread (m, 1, 4, in) == 4) {
		if (m[3] & 0x80) break;
		if ((m[3] & 0x40) && fread (m + 4, 1, 3, in) != 3) break;
		double ramp = -1;
		if (m[3] & 0x20) { unsigned char g[2]; if (fread (g, 1, 2, in) != 2) break; ramp = (g[0] | g[1] << 8) / 100.0; m[3] &= ~0x20; }
		vx_frame (m, ramp, &fr);
		fputc ('V', stdout); fputc ('F', stdout); w16 (stdout, fr.nlines);
		w16 (stdout, fr.two_channels ? fr.nsamples | 0x8000 : fr.nsamples);
		w16 (stdout, fr.frame_t0 & 0xffff); w16 (stdout, fr.frame_t0 >> 16);
		for (int i = 0; i < fr.nlines; i++) {
			vx_line_t *l = &fr.lines[i];
			w16 (stdout, l->x0); w16 (stdout, l->y0); w16 (stdout, l->x1); w16 (stdout, l->y1);
			fputc (l->bright, stdout); w16 (stdout, l->energy); w16 (stdout, l->t0);
		}
		fwrite (fr.pcm, 2, fr.two_channels ? fr.nsamples * 2 : fr.nsamples, stdout);
	}
	vx_close ();
	return 0;
}
