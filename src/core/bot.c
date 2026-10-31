/* bot.c - a simulated player for balancing Steel Siege (included by vtest.c).
 *
 * Script command:  bot SYMFILE MAXFRAMES [SKILL]
 *   Plays from the current state until the level is cleared, the game is over, or MAXFRAMES pass.
 *   Reads the game's RAM through the symbol file (Ents, PlrIX, ...), so it follows code changes.
 *   SKILL 1..3 (default 2): reaction smoothing and aim error, roughly novice / decent / good.
 *   SKILL 0: never dodges shots (raw threat). SKILL 9: never dodges shots and never fires; the
 *   deaths per minute then measure how hard the robots press when they are not being removed.
 * Output: one line
 *   BOT result=clear|gameover|timeout frames=N deaths=D wave=W left=R bosshp=H saved=S
 *
 * Policy, per frame: a danger field (robots by distance, shots by predicted closest approach over
 * the next frames, walls, the boss body) pushes the ship; when nothing is close it is pulled to
 * the nearest human, and it drifts to line up a target on one of the 8 firing lines. It fires the
 * 8-way direction nearest the bearing of the best target (with a little aim error).
 */
#include <math.h>

static unsigned bs_saved, bs_ents, bs_entend, bs_plrix, bs_plriy, bs_state, bs_lives, bs_wave, bs_req, bs_boss,
                bs_humans, bs_plralive, bs_fencet, bs_waveflags, bs_darkt;
/* type numbers come from the symbol file (Arena 2 has more robot types, its shots start at 24) */
static int T_SPARK = 14, T_SHIELD = -1, T_SNIPER = -1, T_POST = -1, T_PICKUP = -1, T_HSEG = -1, arena2;
static int bot_lookup (const char *symfile)
{
	FILE *f = fopen (symfile, "r");
	if (!f) { perror (symfile); return 0; }
	char l[256], name[128]; unsigned v;
	while (fgets (l, sizeof l, f)) {
		if (sscanf (l, "%127s EQU $%x", name, &v) != 2) continue;
		if (!strcmp (name, "Ents")) bs_ents = v;
		else if (!strcmp (name, "EntEnd")) bs_entend = v;
		else if (!strcmp (name, "PlrIX")) bs_plrix = v;
		else if (!strcmp (name, "PlrIY")) bs_plriy = v;
		else if (!strcmp (name, "GameState")) bs_state = v;
		else if (!strcmp (name, "Lives")) bs_lives = v;
		else if (!strcmp (name, "Wave")) bs_wave = v;
		else if (!strcmp (name, "RequiredLeft")) bs_req = v;
		else if (!strcmp (name, "Boss")) bs_boss = v;
		else if (!strcmp (name, "Humans")) bs_humans = v;
		else if (!strcmp (name, "PlrAlive")) bs_plralive = v;
		else if (!strcmp (name, "SavedCount")) bs_saved = v;
		else if (!strcmp (name, "FenceT")) bs_fencet = v;
		else if (!strcmp (name, "WaveFlags")) bs_waveflags = v;
		else if (!strcmp (name, "DarkT")) bs_darkt = v;
		else if (!strcmp (name, "T_SPARK")) T_SPARK = v;
		else if (!strcmp (name, "T_SHIELD")) T_SHIELD = v;
		else if (!strcmp (name, "T_SNIPER")) T_SNIPER = v;
		else if (!strcmp (name, "T_POST")) T_POST = v;
		else if (!strcmp (name, "T_PICKUP")) T_PICKUP = v;
		else if (!strcmp (name, "T_HSEG")) T_HSEG = v;
		else if (!strcmp (name, "A2Entry")) arena2 = 1;
	}
	fclose (f);
	return bs_ents && bs_entend && bs_plrix && bs_state && bs_boss && bs_humans;
}
static int rd (unsigned a) { return e6809_read8 (a & 0xFFFF); }
static int rds (unsigned a) { int v = rd (a); return v > 127 ? v - 256 : v; }

#define T_HULK 2
#define T_ELECTRODE 9
#define ARENA_L (-98)
#define ARENA_R 98
#define ARENA_B (-124)
#define ARENA_T 110

static double brand (void) { return rand () / (double) RAND_MAX; }

/* what the bot "sees": a ring of arena snapshots, read 'lag' frames late (reaction time) */
#define SNAP_N 16
#define SNAP_E 40
typedef struct { int n; int t[SNAP_E], fl[SNAP_E], et[SNAP_E], ea[SNAP_E], slot[SNAP_E];
                 double x[SNAP_E], y[SNAP_E], vx[SNAP_E], vy[SNAP_E];
                 int boss, bvuln; double bx, by; } snap_t;
static snap_t snaps[SNAP_N];
static void take_snap (snap_t *sn)
{
	unsigned end = (rd (bs_entend) << 8) | rd (bs_entend + 1);
	sn->n = 0;
	for (unsigned e = bs_ents; e < end && sn->n < SNAP_E; e += 8) {
		int t = rd (e); if (!t) continue;
		int k = sn->n++;
		sn->t[k] = t; sn->fl[k] = rd (e + 1);
		sn->x[k] = rds (e + 2); sn->y[k] = rds (e + 4);
		sn->vx[k] = rds (e + 6) / 16.0; sn->vy[k] = rds (e + 7) / 16.0;
		sn->et[k] = rd (e + 6); sn->ea[k] = rd (e + 7); sn->slot[k] = (e - bs_ents) / 8;
	}
	sn->boss = rd (bs_boss);
	sn->bvuln = rd (bs_boss + 10);
	sn->bx = rds (bs_boss + 2); sn->by = rds (bs_boss + 3);
}

static void bot_run (const char *symfile, long maxframes, int skill)
{
	if (!bot_lookup (symfile)) { printf ("BOT error: symbols\n"); return; }
	double smooth = skill >= 3 ? 0.55 : skill == 2 ? 0.35 : 0.2;   /* how fast the stick follows */
	double aimerr = skill >= 3 ? 0.10 : skill == 2 ? 0.25 : 0.45;  /* radians of bearing noise */
	int lag = skill >= 3 ? 4 : skill == 2 ? 8 : 12;                /* reaction time in frames */
	double seen = skill >= 3 ? 60 : skill == 2 ? 45 : skill == 1 ? 35 : 0;   /* shots noticed within (px);
	                                                               skill 0 ignores shots: raw threat */
	for (int i = 0; i < SNAP_N; i++) snaps[i].n = 0, snaps[i].boss = 0;
	double jx = 0, jy = 0;
	int deaths = 0, shots = 0, prevstate = rd (bs_state), firehold = 0;
	int cause[32] = { 0 };                 /* deaths by the nearest thing (0 boss, 31 unknown) */
	double lpx = 0, lpy = 0;
	long reqsum = 0, reqn = 0, playf = 0;  /* robots alive, averaged over play; frames in play */
	long f;
	const char *res = "timeout";
	for (f = 0; f < maxframes; f++) {
		int st = rd (bs_state);
		if (st == 3 || st == 4) playf++;
		if (st == 4 && prevstate != 4) {
			deaths++;
			snap_t *c = &snaps[(f + SNAP_N - 1) % SNAP_N];   /* the arena one frame before */
			int best = 31; double bd = 16;
			for (int k = 0; k < c->n; k++) {
				double d = hypot (c->x[k] - lpx, c->y[k] - lpy);
				if (d < bd && c->t[k] < 31) { bd = d; best = c->t[k]; }
			}
			if (c->boss && hypot (c->bx - lpx, c->by - lpy) < 26) best = 0;
			cause[best]++;
		}
		prevstate = st;
		if (st == 5) { res = "clear"; break; }
		if (st == 6) { res = "gameover"; break; }
		int btn = 0;
		if (st == 3) {
			double px = rds (bs_plrix), py = rds (bs_plriy);
			lpx = px; lpy = py;
			reqsum += rd (bs_req); reqn++;
			double fx = 0, fy = 0;                 /* desired movement */
			double bestd = 1e9, tx = 0, ty = 0; int have = 0;
			take_snap (&snaps[f % SNAP_N]);
			snap_t *sn = &snaps[(f + SNAP_N - (f >= lag ? lag : f)) % SNAP_N];
			int fence = bs_fencet ? rd (bs_fencet) : 255;
			int dark = arena2 && bs_waveflags && (rd (bs_waveflags) & 0x40) && rd (bs_darkt) >= 8;
			double cellx = 0, celly = 0, celld = 1e9;
			for (int k = 0; k < sn->n; k++) {
				int t = sn->t[k], fl = sn->fl[k];
				if (fl & 0x80) continue;                            /* materialising / caged */
				double x = sn->x[k], y = sn->y[k];
				double dx = px - x, dy = py - y, d = sqrt (dx * dx + dy * dy) + 0.01;
				if (dark && t < T_SPARK && t != T_POST && (fabs (dx) > 42 || fabs (dy) > 42)) continue;
				if (t == T_PICKUP) {                                 /* a power cell: worth a detour */
					if (d < celld) { celld = d; cellx = x; celly = y; }
					continue;
				}
				if (t == T_POST && (sn->ea[k] & 1) && (fence < 130 || fence >= 170)) {
					/* a lit (or about to light) laser fence: keep off the line */
					for (int j = 0; j < sn->n; j++) if (sn->slot[j] == sn->et[k] && sn->t[j] == T_POST) {
						double x2 = sn->x[j], y2 = sn->y[j], lx = x2 - x, ly = y2 - y, ll = lx * lx + ly * ly + 0.01;
						double u = ((px - x) * lx + (py - y) * ly) / ll; if (u < 0) u = 0; if (u > 1) u = 1;
						double cx = x + lx * u, cy = y + ly * u, ex = px - cx, ey = py - cy, ed = hypot (ex, ey) + 0.01;
						if (ed < 14) { double w = (14 - ed) / 14 * 3.0; fx += ex / ed * w; fy += ey / ed * w; }
					}
				}
				if (t == T_SNIPER && (fl & 0x20)) {
					/* an aiming line: step out of it */
					double vx = (signed char) sn->et[k], vy = (signed char) sn->ea[k], vl = hypot (vx, vy) + 0.01;
					double u = ((px - x) * vx + (py - y) * vy) / vl;
					if (u > 0) {
						double cx = x + vx / vl * u, cy = y + vy / vl * u, ex = px - cx, ey = py - cy, ed = hypot (ex, ey) + 0.01;
						if (ed < 12) { double w = (12 - ed) / 12 * 3.0; if (ed < 0.5) { ex = -vy; ey = vx; ed = vl; } fx += ex / ed * w; fy += ey / ed * w; }
					}
				}
				if (t >= T_SPARK) {                                  /* a shot: predicted approach */
					if (d > seen) continue;
					double vx = sn->vx[k], vy = sn->vy[k];
					x += vx * lag; y += vy * lag;                    /* rough extrapolation */
					dx = px - x; dy = py - y;
					double vv = vx * vx + vy * vy, tc = 0;
					if (vv > 0.001) { tc = (dx * vx + dy * vy) / vv; if (tc < 0) tc = 0; if (tc > 20) tc = 20; }
					double cx = x + vx * tc, cy = y + vy * tc;
					double ex = px - cx, ey = py - cy, ed = sqrt (ex * ex + ey * ey) + 0.01;
					if (ed < 12 && tc < 15) {
						double w = (12 - ed) / 12 * 3.0 / (1 + tc / 6);
						if (ed < 0.5) { ex = -vy; ey = vx; ed = sqrt (ex * ex + ey * ey) + 0.01; }
						fx += ex / ed * w; fy += ey / ed * w;
					}
				} else {                                             /* a robot */
					double r = t == T_HULK ? 30 : 22;
					if (d < r) { double w = (r - d) / r * 2.0; fx += dx / d * w; fy += dy / d * w; }
					int target = t != T_HULK && t != T_ELECTRODE;
					if (t == T_HSEG && !(fl & 8)) target = 0;           /* only the Hydra's tail takes hits */
					if (t == T_SHIELD) {                                 /* shield in front of us: go round */
						static const double nx[4] = { 1, 0, -1, 0 }, ny[4] = { 0, 1, 0, -1 };
						int fc = fl & 3;
						if ((dx * nx[fc] + dy * ny[fc]) > 0.6 * d) {
							target = 0;
							if (d < 90) { fx += -ny[fc] * 0.6; fy += nx[fc] * 0.6; }
						}
					}
					if (target && d < bestd) { bestd = d; tx = x; ty = y; have = 1; }
				}
			}
			/* boss */
			if (sn->boss) {
				double x = sn->bx, y = sn->by;
				double dx = px - x, dy = py - y, d = sqrt (dx * dx + dy * dy) + 0.01;
				if (d < 45) { double w = (45 - d) / 45 * 2.5; fx += dx / d * w; fy += dy / d * w; }
				if (!arena2 || sn->bvuln) { tx = x; ty = y; have = 1; bestd = d; }
			}
			/* walls */
			if (px < ARENA_L + 16) fx += (ARENA_L + 16 - px) / 16.0;
			if (px > ARENA_R - 16) fx -= (px - (ARENA_R - 16)) / 16.0;
			if (py < ARENA_B + 16) fy += (ARENA_B + 16 - py) / 16.0;
			if (py > ARENA_T - 16) fy -= (py - (ARENA_T - 16)) / 16.0;
			double danger = sqrt (fx * fx + fy * fy);
			if (danger < 0.3) {
				/* calm: go for the nearest human, otherwise line up the target */
				double hb = 1e9, hx = 0, hy = 0;
				for (int i = 0; i < 8; i++) {
					unsigned h = bs_humans + i * 6;
					if (!rd (h)) continue;
					double x = rds (h + 1), y = rds (h + 2), d = hypot (px - x, py - y);
					if (d < hb) { hb = d; hx = x; hy = y; }
				}
				if (celld < 80) { hb = celld; hx = cellx; hy = celly; }
				if (hb < 1e8) { fx += (hx - px) / (hb + 1) * 0.8; fy += (hy - py) / (hb + 1) * 0.8; }
				else if (have) {
					/* move to the nearest of the 8 firing lines through the target, keep ~45 px away */
					double dx = px - tx, dy = py - ty;
					if (fabs (dx) < fabs (dy)) fx += -dx / 30.0; else fy += -dy / 30.0;
					double d = hypot (dx, dy);
					if (d < 40) { fx += dx / d * 0.3; fy += dy / d * 0.3; }
					if (d > 110) { fx -= dx / d * 0.5; fy -= dy / d * 0.5; }   /* far: close in */
				}
			}
			double m = hypot (fx, fy);
			double wx = 0, wy = 0;
			if (m > 0.05) { wx = fx / m * 127; wy = fy / m * 127; if (m < 0.5) { wx *= m * 2; wy *= m * 2; } }
			jx += (wx - jx) * smooth; jy += (wy - jy) * smooth;
			alg_jch0 = (unsigned) ((int) jx + 128); alg_jch1 = (unsigned) ((int) jy + 128);
			/* fire at the target (skill 9: never - measures how hard the robots press) */
			if (skill != 9 && have && bestd < 140) {
				double a = atan2 (ty - py, tx - px) + (brand () * 2 - 1) * aimerr;
				int oct = ((int) floor ((a + M_PI / 8) / (M_PI / 4)) + 8) % 8;   /* 0 right, 2 up */
				static const int mask[8] = { 8, 8 | 2, 2, 1 | 2, 1, 1 | 4, 4, 8 | 4 };
				btn = mask[oct];
				if (!firehold) shots++;
				firehold = 1;
			} else firehold = 0;
		} else { alg_jch0 = alg_jch1 = 128; jx = jy = 0; }
		snd_regs[14] = 0xff & ~btn;
		run_frame ();
	}
	alg_jch0 = alg_jch1 = 128; snd_regs[14] = 0xff;
	printf ("BOT result=%s frames=%ld deaths=%d wave=%d left=%d bosshp=%d saved=%d\n", res, f, deaths,
	        rd (bs_wave), rd (bs_req), rd (bs_boss) ? rd (bs_boss + 5) : -1, bs_saved ? rd (bs_saved) : -1);
	printf ("BOT alive %.1f play %ld\n", reqn ? reqsum / (double) reqn : 0.0, playf);
	printf ("BOT causes");
	for (int i = 0; i < 32; i++) if (cause[i]) printf (" %d:%d", i, cause[i]);
	printf ("\n");
}
