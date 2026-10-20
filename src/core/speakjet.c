/* speakjet.c - the VecVox speech accessory: a Magnevation SpeakJet on controller port 2 (included by vecx.c,
 * after sp0256.c: the serial link and the SP0256 are there).
 *
 * The VecVox (Alex Herbert) takes SpeakJet codes on the same serial link as the VecVoice: 128-199 phonemes,
 * 0-6 pauses, control codes (7 fast, 8 slow, 14 stress, 15 relax, 20 volume, 21 speed, 22 pitch in Hz, 23
 * bend, 26 repeat, 30 delay, 31 reset ...) into a 64-byte buffer; its ready line = room in the buffer
 * ("0 ... the input buffer is full", Alex Herbert's AtariVox Programmer's Guide, 2004).
 *
 * How the SpeakJet makes its sounds was never published (a programmed chip), so the VecVox speaks with the
 * SP0256: every phoneme as the nearest AL2 allophone(s), stretched to the SpeakJet's length, at the
 * SpeakJet's pitch - songs keep their notes and timing (vt_vecvoice == 2). (A formant synthesizer modelled
 * on recordings was tried on 2026-10-19; the owner preferred this.)
 */

/* the 72 phonemes (128-199): the AL2 allophones that say it (0xff = end), the SpeakJet's length (ms) */
static const struct { unsigned char a[3]; unsigned short ms; } sj_ph[72] = {
	{{19,255},70}, {{12,255},70}, {{20,255},70}, {{7,255},70},          /* IY IH EY EH */
	{{26,255},70}, {{15,255},70}, {{15,255},70}, {{24,255},70},         /* AY(hat) AX UX OH */
	{{24,255},200}, {{53,255},70}, {{30,255},70}, {{31,255},70},        /* AW(father) OW UH UW */
	{{16,255},70}, {{11,255},70}, {{56,255},70}, {{44,255},70},         /* MM NE NO NGE */
	{{44,255},70}, {{45,255},70}, {{45,255},70}, {{46,255},70},         /* NGO LE LO WW */
	{{14,255},70}, {{60,255},200}, {{47,255},200}, {{59,255},190},      /* RR IYRR EYRR AXRR */
	{{58,255},200}, {{58,255},185}, {{20,255},165}, {{6,255},200},      /* AWRR OWRR EYIY OHIY */
	{{5,255},225}, {{6,255},185}, {{25,7,255},170}, {{62,255},140},     /* OWIY OHIH IYEH EHLL */
	{{49,31,255},180}, {{32,255},170}, {{12,46,255},170}, {{32,255},200},/* IYUW AXUW IHWW AYWW */
	{{53,255},131}, {{10,255},70}, {{35,255},70}, {{43,255},70},        /* OWWW JH VV ZZ */
	{{38,255},70}, {{54,255},70}, {{63,255},45}, {{28,255},45},         /* ZH DH BE BO */
	{{28,255},10}, {{28,255},10}, {{33,255},45}, {{21,255},45},         /* EB OB DE DO */
	{{21,255},10}, {{21,255},10}, {{36,255},55}, {{61,255},55},         /* ED OD GE GO */
	{{34,255},55}, {{34,255},55}, {{50,255},70}, {{27,255},70},         /* EG OG CH HE */
	{{57,255},70}, {{48,255},70}, {{40,255},70}, {{55,255},40},         /* HO WH FF SE */
	{{55,255},40}, {{37,255},50}, {{29,255},40}, {{13,255},50},         /* SO SH TH TT */
	{{17,255},70}, {{17,55,255},170}, {{42,255},55}, {{8,255},55},      /* TU TS KE KO */
	{{41,255},55}, {{41,255},45}, {{9,255},99}, {{9,255},99},           /* EK OK PE PO */
};
static unsigned char sj_q[64]; static int sj_qh, sj_qn;      /* the SpeakJet's input buffer */
static int sj_vol = 96, sj_speed = 114, sj_pitch = 88, sj_arg = -1, sj_rep, sj_next = 100;   /* (sj_next: % length) */
static int sj_pend[3], sj_pn, sj_pi; static long sj_sil;     /* allophones to say, samples of silence */
static double sj_stress = 1.0;

static void sj_reset (void)
{
	sj_qh = sj_qn = 0; sj_vol = 96; sj_speed = 114; sj_pitch = 88; sj_arg = -1; sj_rep = 0; sj_next = 100;
	sj_pn = sj_pi = 0; sj_sil = 0; sj_stress = 1.0; sj_per = 0; sj_scale = 1.0;
}

static void sj_code (unsigned c)
{
	static const int pause_ms[7] = { 0, 100, 200, 700, 30, 60, 90 };
	if (sj_arg >= 0) {                                           /* a control code's value */
		switch (sj_arg) {
		case 20: sj_vol = c & 127; break;
		case 21: sj_speed = c & 127; break;
		case 22: sj_pitch = (int) c; break;
		case 26: sj_rep = (int) c; break;
		case 30: sj_sil += (long) c * 100; break;               /* (delay: 10 ms steps) */
		}
		sj_arg = -1; return;
	}
	if (c >= 128 && c < 200) {
		int k = (int) c - 128, n = sj_rep > 1 ? sj_rep : 1;
		double spd = sj_speed < 10 ? 10 : sj_speed;
		/* (samples at 10 kHz): length x 1.4 x (114 / speed)^2 - fitted to the owner's reference recording of
		 * VecVox Xmas song 2 (Merry.wav, 2026-10-19: 83 % of the notes on their pitch, 25.9 s / 25.2 s) */
		double target = sj_ph[k].ms * 10.0 * 1.4 * (114.0 / spd) * (114.0 / spd) * sj_next / 100.0;
		double native = 0;
		sj_pn = 0;
		for (int r = 0; r < n; r++)
			for (int i = 0; i < 3 && sj_ph[k].a[i] != 255 && sj_pn < 3; i++) sj_pend[sj_pn++] = sj_ph[k].a[i];
		for (int i = 0; i < sj_pn; i++) native += sp_native[sj_pend[i]];
		sj_scale = native > 0 ? target * n / native : 1.0;
		int hz = (int) (sj_pitch * sj_stress + 0.5);
		sj_per = hz > 0 ? (10000 + hz / 2) / hz : 0;
		sj_pi = 0; sj_rep = 0; sj_next = 100; sj_stress = 1.0;
		return;
	}
	if (c < 7) { sj_sil += pause_ms[c] * 10L; return; }
	switch (c) {
	case 7: sj_next = 50; break;                                /* fast: the next phoneme */
	case 8: sj_next = 150; break;                               /* slow */
	case 14: sj_stress = 1.12; break;                           /* stress / relax: the next phoneme's pitch */
	case 15: sj_stress = 0.9; break;
	case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 28: case 29: case 30:
		sj_arg = (int) c; break;                                /* (a value follows) */
	case 31: { int q = sj_qn, h = sj_qh; sj_reset (); sj_qn = q; sj_qh = h; } break;   /* (reset: the defaults, the buffer kept) */
	default:                                                    /* 200-254 sound effects: a short rest */
		if (c >= 200) sj_sil += 700;
		break;
	}
}

/* the VecVox's next 10 kHz sample */
static int16_t sj_sample (void)
{
	for (int guard = 0; guard < 64; guard++) {
		if (sj_sil > 0) { sj_sil--; return 0; }
		if (sp_busy ()) return (int16_t) (sp_sample () * sj_vol / 96);
		if (sj_pi < sj_pn) { sp_lrq = 0; sp_ald = sj_pend[sj_pi++] << 4; continue; }
		if (!sj_qn) { sj_per = 0; return 0; }
		unsigned c = sj_q[sj_qh]; sj_qh = (sj_qh + 1) & 63; sj_qn--;
		sj_code (c);
	}
	return 0;
}

/* the speech device: its next sample, a byte from the serial link, its ready line (1 = takes the next) */
static int speech_rate (void) { return 10000; }
static int16_t speech_sample (void) { return vt_vecvoice == 2 ? sj_sample () : sp_sample (); }
static void speech_byte (unsigned b)
{
	if (getenv ("VT_SPLOG")) fprintf (stderr, "byte %02X\n", b & 0xff);
	if (vt_vecvoice == 2) { if (sj_qn < 64) { sj_q[(sj_qh + sj_qn) & 63] = (unsigned char) b; sj_qn++; } }
	else sp_ald_w (b);
}
static int speech_ready (void) { return vt_vecvoice == 2 ? sj_qn < 32 : sp_lrq; }

/* every allophone's own length, measured once by speaking it */
static void sp_measure (void)
{
	int keep = vt_vecvoice; vt_vecvoice = 1;
	for (int a = 0; a < 64; a++) {
		sp_reset (); sp_ald_w ((unsigned) a);
		int n = 0;
		while (sp_busy () && n < 40000) { sp_sample (); n++; }
		sp_native[a] = n;
	}
	sp_reset (); vt_vecvoice = keep;
}

/* after the ROM is loaded: which device ("vecvox" = the VecVox, else the VecVoice) */
void sp_start (const char *dev)
{
	sp_measure ();
	vt_vecvoice = (dev && !strcmp (dev, "vecvox")) ? 2 : 1;
	sp_reset (); sj_reset ();
}
