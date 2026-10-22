// scene.cpp - see scene.h. Every number here is the desktop program's (src/frontend/vecrender.py),
// fitted there to photos, videos and measurements of a real Vectrex; the names are kept.
#include "scene.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace vx {

static const double ALG_MAX_X = 33000, ALG_MAX_Y = 41000;
static const double PI = 3.14159265358979323846;
// the beam (vecrender.py BEAM_*)
static const double BEAM_W0 = 0.30, BEAM_WZ = 0.18, BEAM_SPEED = 0.35, BEAM_LINE = 1.15, BEAM_FADE = 0.3;
static const double BEAM_HALO_W = 1.6, BEAM_HALO_C = 18.0;
static const double BEAM_DOTC = 4.0, BEAM_DOTMAX = 1.6, BEAM_DOTW = 1.0, BEAM_DOTWK = 0.002;
static const double BEAM_BLOOM_E0 = 20.0, BEAM_BLOOM_E1 = 38.0, BEAM_BLOOM_W = 2.0, BEAM_BLOOM_H = 2.5;
static const double BEAM_LOWZ = 0.5, BEAM_LOWP = 1.7, BEAM_LOWW = 0.45, BEAM_Z0 = 27 / 127.0;
static const double BEAM_Z0DOT = 23 / 127.0, BEAM_DOTLOWP = 1.0, BEAM_DOTLOWC = 40.0;
static const double BEAM_PEAK = 0.62;
// the phosphor (PHOS_*), the tube (CURVE_K), the overlay (ART_*)
static const double PHOS_GAIN = 1.0 / (1.0 - 0.40), PHOS_HOLD = 0.021, PHOS_HOLD_OFF = 0.021;
static const double PHOS_GCAP = 1.5, PHOS_GAGE = 0.035;
static const double CURVE_K = 0.07, ART_PARALLAX = 0.008, ART_ROOM_MID = 0.075;
static const double WOBBLE_SPEED = 2.62;
static const double CRT_FADE = 0.3;                // the tube warming up after power on (vectrex_emu.py crt_fx)
// the overlay's scratches (vecrender.py SCR_*: the owner's recipe, from previews and his real sheet)
static const double SCR_TPM = 2048 / 207.0;        // texels per mm of the scratch textures
static const int SCR_COUNTS[10] = { 9843, 24276, 8177, 3480, 2900, 2418, 684, 112, 8, 2 };   // steps 1 (fine wear) .. 10
static const double SCR_LEN0 = 5.0, SCR_LEN1 = 80.0;
static const float SCR_TIERS[4] = { 15.0f, 35.0f, 0.04f, 0.005f };
static const double SCR_PEN[3] = { 0.45, 0.85, 1.5 };
static const double SCR_BASE[3] = { 1.8145, 1.202, 0.5043 };
static const double SCR_FINE_D0 = 0.3, SCR_FINE_D1 = 1.0, SCR_FINE_W = 0.0376, SCR_FINE_1M = 18.53;
static const double SCR_SCRIBBLE[2] = { 0.04, 1.0 };
static const double SCR_STEP_W = 0.0538;
static const double SCR_GLINT_SIG[2][2] = { { 4.0, 1.0 }, { 12.0, 0.7 } };   // (sigma in quarter px, weight)
static const double SCR_GLINT_K = 2.0, ART_GLOW_K = 0.718;
static const double SCR_REF = 4 * SCR_FINE_1M * 15.42 * 0.65 * 0.7628 * SCR_FINE_W;
// the glint's light at eighth size (the desktop: quarter size with these sigmas): its kernels are 16 and 48
// screen px wide and the glint field is smooth - at eighth size it looks the same, for an eighth of the work
static const int HARM_DIV = 8;

static void beam_z(double zi, double &k, double &w)   // (brightness factor, width factor) for zi = Z / 127
{
    if (zi >= BEAM_LOWZ) { k = 0.55 + 0.45 * zi; w = 1.0; return; }
    double r = std::max(0.0, (zi - BEAM_Z0) / (BEAM_LOWZ - BEAM_Z0));
    k = (0.55 + 0.45 * BEAM_LOWZ) * std::pow(r, BEAM_LOWP);
    w = BEAM_LOWW + (1.0 - BEAM_LOWW) * zi / BEAM_LOWZ;
}
static double beam_zdot(double zi)
{
    double r = std::max(0.0, (zi - BEAM_Z0DOT) / (BEAM_LOWZ - BEAM_Z0DOT));
    return (0.55 + 0.45 * BEAM_LOWZ) * std::pow(r, BEAM_DOTLOWP);
}

// the mains hum (HUM_AXES, mains_cycles, hum_coeffs): (weight, harmonic, phase) per axis
static const double HUM[2][3][3] = { { { 1.0, 1, 0.3 }, { 0.55, 2, 1.7 }, { 0.25, 3, 4.1 } },
                                     { { 1.0, 2, 2.2 }, { 0.7, 1, 0.9 }, { 0.3, 3, 3.3 } } };
static double mains_cycles(double t, double speed, double hz)
{
    return hz * t + speed * (0.002 * t + 0.15 * std::sin(2 * PI * t / 61.0)
                             + 0.08 * std::sin(2 * PI * t / 23.0 + 1.0) + 0.04 * std::sin(2 * PI * t / 9.7 + 2.0));
}
struct Hum { double a0, k50, P[2][4], Q[2][4]; };
static void hum_coeffs(double t, double strength, double speed, double hz, Hum &h)
{
    h.a0 = 2 * PI * mains_cycles(t, speed, hz);
    h.k50 = 2 * PI * hz / 1.5e6;
    double breathe = 0.75 + 0.25 * std::sin(2 * PI * 0.029 * t) * std::sin(2 * PI * 0.047 * t + 1.0);
    for (int ax = 0; ax < 2; ax++) {
        double ss = 0; for (auto &e : HUM[ax]) ss += e[0] * e[0];
        double norm = std::sqrt(ss / 2.0), k = 160.0 * strength * breathe / norm;
        for (int i = 0; i < 4; i++) h.P[ax][i] = h.Q[ax][i] = 0;
        for (auto &e : HUM[ax]) { int hh = (int) e[1]; h.P[ax][hh] += k * e[0] * std::cos(e[2]); h.Q[ax][hh] += k * e[0] * std::sin(e[2]); }
    }
}
static inline void woff(const Hum &h, double t, double &dx, double &dy)
{
    double a = h.a0 + t * h.k50, s1 = std::sin(a), c1 = std::cos(a);
    double s2 = 2 * s1 * c1, c2 = c1 * c1 - s1 * s1, s3 = s2 * c1 + c2 * s1, c3 = c2 * c1 - s2 * s1;
    dx = h.P[0][1] * s1 + h.Q[0][1] * c1 + h.P[0][2] * s2 + h.Q[0][2] * c2 + h.P[0][3] * s3 + h.Q[0][3] * c3;
    dy = h.P[1][1] * s1 + h.Q[1][1] * c1 + h.P[1][2] * s2 + h.Q[1][2] * c2 + h.P[1][3] * s3 + h.Q[1][3] * c3;
}

void Scene::set_screen(int w, int h)
{
    if (w == sw && h == sh) return;
    sw = w; sh = h;
    young.clear(); last.clear();
    layout();
}

void Scene::layout()
{
    // the picture fitted and centred (the desktop's resize); the images cover it and a small margin
    // (the bloom and the curve reach a little past its edges), not the whole screen: on a 16:9 screen
    // that is less than half the pixels to age, show and blur every display frame
    s = std::min(sw / ALG_MAX_X, sh / ALG_MAX_Y);
    pw = ALG_MAX_X * s; ph = ALG_MAX_Y * s;
    px0 = (sw - pw) / 2; py0 = (sh - ph) / 2;
    double m = 0.03 * ph;
    tx0 = std::max(0.0, px0 - m); ty0 = std::max(0.0, py0 - m);
    tws = std::min((double) sw, px0 + pw + m) - tx0; ths = std::min((double) sh, py0 + ph + m) - ty0;
    rs = std::max(0.25, std::min(1.0, (double) look.render_scale));
}

double Scene::hold() const
{
    double per = 0.02;
    if (!periods.empty()) per = *std::max_element(periods.begin(), periods.end());
    per = std::min(0.1, per);
    return look.trail_on ? std::max(PHOS_HOLD, per + 0.001) : std::max(PHOS_HOLD_OFF, per + 0.002);
}

void Scene::build(const std::vector<Line> &in0, std::vector<Instance> &out, bool do_load, float cgain, double cs, double cw)
{
    out.clear();
    if (in0.empty()) return;
    std::vector<Line> lines;
    lines.reserve(in0.size() + 64);
    if (look.halo_on && !look.even_beam)                 // the resting spots' halos, drawn first (BEAM_HALO_W)
        for (auto &l : in0)
            if (l.x0 == l.x1 && l.y0 == l.y1 && l.energy >= 64 && (l.bright & 127)) { Line h = l; h.bright = (l.bright & 127) | 256; lines.push_back(h); }
    lines.insert(lines.end(), in0.begin(), in0.end());

    Hum hum; bool wob = look.wob_on && look.wob_strength > 0;
    if (wob) hum_coeffs(frame_t0 / 1.5e6, look.wob_strength, WOBBLE_SPEED, look.mains_hz, hum);
    double load = 0;                                     // brightness bending: this frame's beam load, smoothed
    for (auto &l : lines) load += (l.energy / 8.0) * ((l.bright & 127) / 127.0);
    load /= 30000.0;
    if (load_f < 0) load_f = load;
    if (do_load) load_f += (load - load_f) * 0.3;
    bool bend = look.bend_on && look.bend_strength > 0;
    double gs = 1, gsh = 0, cxm = ALG_MAX_X / 2, cym = ALG_MAX_Y / 2;
    if (bend) {
        double amt = look.bend_strength * load_f * 10.0;   // game px at the screen edge
        gs = 1.0 + (look.bend_type != 1 ? amt / 128.0 : 0.0);
        gsh = look.bend_type != 0 ? amt * 160.0 * 0.5 : 0.0;
    }
    const double sc = s * rs;                            // image px per emulator unit
    const double ox = (px0 - tx0) * rs, oy = (py0 - ty0) * rs;
    const double gp = 160.0 * sc;                        // image px per game pixel
    out.reserve(lines.size());
    for (auto &l : lines) {
        double x0 = l.x0, y0 = l.y0, x1 = l.x1, y1 = l.y1;
        int b = l.bright;
        double cyc = l.energy / 8.0;                     // beam energy (cycles at full level)
        if (bend) {
            x0 = cxm + (x0 - cxm) * gs + gsh; x1 = cxm + (x1 - cxm) * gs + gsh;
            y0 = cym + (y0 - cym) * gs + gsh; y1 = cym + (y1 - cym) * gs + gsh;
        }
        if (cs != 1.0) {                                 // (switching off: the picture shrinking into the centre)
            x0 = cxm + (x0 - cxm) * cs; x1 = cxm + (x1 - cxm) * cs;
            y0 = cym + (y0 - cym) * cs; y1 = cym + (y1 - cym) * cs;
        }
        if (wob) {                                       // the hum: offsets at the line's start and end
            double dx0, dy0, dx1, dy1;
            woff(hum, l.t0, dx0, dy0); woff(hum, l.t0 + cyc, dx1, dy1);
            x0 += dx0; y0 += dy0; x1 += dx1; y1 += dy1;
        }
        double ax = ox + x0 * sc, ay = oy + y0 * sc, bx = ox + x1 * sc, by = oy + y1 * sc;
        double dx = bx - ax, dy = by - ay, lp = std::sqrt(dx * dx + dy * dy);
        double zi = (b & 127) / 127.0, zk, zw, k;
        beam_z(zi, zk, zw);
        double fw = (BEAM_W0 + BEAM_WZ * zi) * zw * cw;
        if (b & 256) {                                   // a resting spot's halo
            double bl = std::min(1.0, std::max(0.0, (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0)));
            fw = BEAM_HALO_W * zw * (1.0 + (BEAM_BLOOM_W - 1.0) * bl) * cw;
            k = look.halo_k * std::min(1.0, cyc / BEAM_HALO_C) * (1.0 + (BEAM_BLOOM_H - 1.0) * bl);
        } else if (look.even_beam) {                     // an ideal display: lines and points alike
            k = BEAM_LINE;
        } else if (lp / gp > 0.05) {
            double e = cyc / (lp / gp);                  // cycles per game pixel
            k = BEAM_LINE * (1.0 - BEAM_SPEED * std::exp(-e / 0.8));
        } else {                                         // the beam resting: a spot
            k = BEAM_DOTMAX * std::min(1.0, cyc / BEAM_DOTC);
            if (zi < BEAM_LOWZ) zk = std::max(zk, beam_zdot(zi) * std::min(1.0, cyc / BEAM_DOTLOWC));
            fw = fw * BEAM_DOTW + BEAM_DOTWK * std::min(cyc, 80.0);
            double bl = std::min(1.0, std::max(0.0, (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0)));
            fw *= 1.0 + (BEAM_BLOOM_W - 1.0) * bl;
        }
        double sg = std::max(0.6, fw * gp / 2.355);      // sigma, image px
        k *= zk * BEAM_PEAK * cgain;
        if (b & 128) k *= BEAM_FADE;
        out.push_back({ (float) ax, (float) ay, (float) bx, (float) by, (float) k, (float) sg, (float) (l.t0 / 1.5e6), 0.f });
    }
}

void Scene::add_frame(std::vector<Line> lines, double t_start, double length, uint64_t t0abs)
{
    if (length > 0) { periods.push_back((float) length); if (periods.size() > 12) periods.erase(periods.begin()); }
    double t = t_start - power_on_t, f = std::min(1.0, std::max(0.0, t / CRT_FADE));
    crt_gain = (float) std::pow(f * f * (3.0 - 2.0 * f), 2.6);   // slow start, then up to full brightness
    frame_t0 = t0abs;
    if (!lines.empty()) last_lines = lines;
    std::vector<Instance> b;
    build(lines, b, true, crt_gain);
    last = b;
    if (!b.empty()) {
        float tmax = 0; for (auto &i : b) tmax = std::max(tmax, i.tl);
        young.push_back({ std::move(b), t_start, t_start + tmax, -1e9 });
    }
}

void Scene::make_plan(double T, FramePlan &p)
{
    layout();
    p.screen_w = sw; p.screen_h = sh;
    p.tw = std::max(8, (int) std::ceil(tws * rs)); p.th = std::max(8, (int) std::ceil(ths * rs));
    p.qw = std::max(4, p.tw / 4); p.qh = std::max(4, p.th / 4);
    p.phosphor = look.phosphor;
    p.inst.clear(); p.batches.clear();
    p.even = look.even_beam;
    p.cap_vp[0] = (float) p.tw; p.cap_vp[1] = (float) p.th; p.cap_vp[2] = look.even_beam ? 1.f : 0.f; p.cap_vp[3] = 0;
    double dt = T_prev < 0 ? 0.0 : std::min(0.25, std::max(0.0, T - T_prev));
    T_prev = T;
    const double pht = ph * rs;                          // the picture's height in image px (the desktop's window h)
    if (!look.phosphor) {                                // crisp: the newest frame as it is
        young.clear();
        if (!last.empty()) { p.batches.push_back({ 0, (uint32_t) last.size(), 0.f, 0.f, 0.f, 1.f }); p.inst = last; }
    } else {
        // 1. the fresh light: each young frame's lines drawn since the last display frame (switching off:
        // all that is left of them at once - the beam stopped)
        bool flush = off_flush; off_flush = false;
        for (auto it = young.begin(); it != young.end();) {
            Batch bt{ (uint32_t) p.inst.size(), (uint32_t) it->b.size(), 1.f, (float) (T - it->t0), flush ? 1e9f : (float) (T - it->done), (float) PHOS_GAIN };
            p.inst.insert(p.inst.end(), it->b.begin(), it->b.end());
            p.batches.push_back(bt);
            it->done = T;
            if (!flush && T < it->t1) ++it; else it = young.erase(it);
        }
        if (off) {                                       // switching off: lines lit now (the zap, the collapse)
            std::vector<Instance> b; bool mx = false;
            if (streak) {
                if (!zap_lit) { zap_lit = true; build(streaks, b, false, 1.2f); mx = true; }   // (one even line: no bright joints)
            } else {
                double t = T - off_t, c = 0.05;          // (to 30 % in 0.05 s, then only its afterglow)
                if (t < c) {
                    double q = t / c, sc = 1.0 - 0.7 * q;
                    build(off_lines, b, false, (float) (0.15 + 0.85 * q * q), sc, 1.0 / std::pow(sc, 0.15));
                }
            }
            if (!b.empty()) {
                Batch bt{ (uint32_t) p.inst.size(), (uint32_t) b.size(), 2.f, 0.f, 1.f, (float) PHOS_GAIN, mx };
                p.inst.insert(p.inst.end(), b.begin(), b.end());
                p.batches.push_back(bt);
            }
        }
        // 2. the phosphor image aged (update.frag: a, b, c), 3. what it shows (show.frag)
        double hd = hold(), hz = dt > 0.004 ? 1.0 / dt : 60.0;
        float *u = p.update;
        u[0] = (float) std::exp(-dt); u[1] = 1.f; u[2] = look.trail_on ? look.phos_spread : 0.f; u[3] = look.trail_on ? look.phos_grow : 0.f;
        u[4] = (float) PHOS_GCAP; u[5] = (float) std::max(1.0, std::min(4.0, std::round(pht / 820.0 * 60.0 / hz)));
        u[6] = (float) std::exp(-PHOS_GAGE); u[7] = (float) hd;
        u[8] = std::max(0.05f, look.trail); u[9] = look.trail_on ? 0.f : 1.f; u[10] = 1.f / p.tw; u[11] = 1.f / p.th;
        float *v = p.show;
        v[0] = (float) hd; v[1] = std::max(0.05f, look.trail); v[2] = look.trail_on ? 0.f : 1.f;
        v[3] = look.trail_on ? (float) (look.phos_smooth * pht / 820.0) : 0.f; v[4] = 1.f / p.tw; v[5] = 1.f / p.th; v[6] = v[7] = 0;
    }
    // 4. the bloom: H, V, H, V at quarter size (the first reads the full-size picture)
    for (int i = 0; i < 4; i++) {
        p.blur[i][0] = (i & 1) ? 0.f : 1.f / p.qw; p.blur[i][1] = (i & 1) ? 1.f / p.qh : 0.f; p.blur[i][2] = p.blur[i][3] = 0;
    }
    p.blur_passes = look.phosphor && look.bloom > 0 ? 4 : 0;
    // 5. the screen (final.frag: a, b, c)
    float *f = p.fin;
    f[0] = f[1] = f[2] = f[3] = 0;                        // (vp: the backend's)
    f[4] = (float) (px0 + pw / 2); f[5] = (float) (py0 + ph / 2); f[6] = (float) (0.5 * ph);
    f[7] = look.curve_on && look.phosphor ? (float) CURVE_K : 0.f;
    f[8] = (float) tx0; f[9] = (float) ty0; f[10] = (float) tws; f[11] = (float) ths;
    f[12] = look.phosphor ? look.bloom : 0.f; f[13] = look.phosphor ? 1.f : 0.f; f[14] = f[15] = 0;
    // 6. the overlay: its height the picture's, its pixels square, nudged, lowered a little (parallax)
    p.art = look.art_on && art_w > 0 && art_h > 0;
    if (p.art) {
        double hh = ph * look.art_scale, ww = hh * art_w / (double) art_h;
        double cx = px0 + pw / 2 + look.art_dx * ph, cy = py0 + ph / 2 + (look.art_dy + (look.art_parallax ? ART_PARALLAX : 0.0)) * ph;
        p.art_rect[0] = (float) (cx - ww / 2); p.art_rect[1] = (float) (cy - hh / 2);
        p.art_rect[2] = (float) (cx + ww / 2); p.art_rect[3] = (float) (cy + hh / 2);
        double r = std::max(0.0, (double) look.art_room);
        float room = (float) (ART_ROOM_MID * std::pow(r / ART_ROOM_MID, 2.0)), prt = (float) (look.art_print * (r / ART_ROOM_MID));
        for (int ps = 0; ps < 2; ps++) { p.art_a[ps][0] = (float) ps; p.art_a[ps][1] = look.art_tint; p.art_a[ps][2] = room; p.art_a[ps][3] = prt; }
    }
    // 7. the sheet's scratches and plastic glow (vecrender.py _scratches, _harmonics)
    bool scr = look.scr_on && scr_ready;
    p.sheet = sheet_ok && p.art && (scr || look.art_glow);
    if (p.sheet) {
        p.qw4 = std::max(4, sw / 4); p.qh4 = std::max(4, sh / 4);
        p.qw8 = std::max(4, sw / HARM_DIV); p.qh8 = std::max(4, sh / HARM_DIV);
        float *q = p.sheet_pc;
        for (int i = 0; i < 4; i++) q[i] = p.art_rect[i];
        q[4] = look.art_tint; q[5] = p.art_a[0][2]; q[6] = p.art_a[0][3];
        q[7] = (float) (art_sheet > 0 ? art_sheet / art_w : 846.0 / 1360.0);
        q[8] = (float) SCR_REF; q[9] = (float) (look.scr_glint * SCR_BASE[2]); q[10] = scr ? (float) (2.5 * 0.85 * 0.5) : 0.f; q[11] = scr ? 1.f : 0.f;
        // the glint's angle lobe |sin|^4 (Glint width, fixed at x1 as on the desktop): its harmonics a2, a4
        static double a2 = 0, a4 = 0;
        if (a2 == 0) {
            double m0 = 0, m2 = 0, m4 = 0; const int n = 720;
            for (int i = 0; i < n; i++) { double x = (i + 0.5) * PI / n, f = std::pow(std::fabs(std::cos(x)), 4.0); m0 += f; m2 += f * std::cos(2 * x); m4 += f * std::cos(4 * x); }
            a2 = 2 * m2 / m0; a4 = 2 * m4 / m0;
        }
        q[12] = (float) a2; q[13] = (float) a4; q[14] = scr ? (float) SCR_GLINT_K : 0.f;
        q[15] = look.art_glow ? (float) (ART_GLOW_K * look.art_glow_k) : 0.f;
        q[16] = (float) SCR_PEN[0]; q[17] = (float) SCR_PEN[1]; q[18] = (float) SCR_PEN[2]; q[19] = 1.f;
        for (int i = 0; i < 6; i++) {                    // the light blurred at quarter size: once (M), three times (F)
            p.sblur[i][0] = (i & 1) ? 0.f : 1.f / p.qw4; p.sblur[i][1] = (i & 1) ? 1.f / p.qh4 : 0.f; p.sblur[i][2] = p.sblur[i][3] = 0;
        }
        double k8 = 4.0 / HARM_DIV;                      // (the sigmas, given in quarter px, at the harmonics' size)
        double s1 = SCR_GLINT_SIG[0][0] * k8, s2 = SCR_GLINT_SIG[1][0] * k8;
        double rr = std::max(0.0, std::min(2.0, (double) look.scr_reach));
        double w1 = SCR_GLINT_SIG[0][1] * (1.5 - 0.5 * rr), w2 = SCR_GLINT_SIG[1][1] * rr;
        for (int part = 0; part < 3; part++) { p.harm_h[part][0] = (float) part; p.harm_h[part][1] = (float) s1; p.harm_h[part][2] = (float) s2; p.harm_h[part][3] = 1.f / p.qw8; }
        for (int part = 0; part < 2; part++) {
            float *v = p.harm_v[part];
            v[0] = (float) part; v[1] = (float) s1; v[2] = (float) s2; v[3] = 1.f / p.qh8; v[4] = (float) w1; v[5] = (float) w2; v[6] = v[7] = 0;
        }
    }
}

// switching off (vectrex_emu.py crt_off): the zap - the last vector the beam was drawing, cut off: one
// smooth freehand stroke anywhere within the last picture (6 in 10 are 5-8 cm long on the tube, 3 in 10
// 2-5 cm, 1 in 10 a squiggle: a small open loop with a longer curved tail) - or (3 in 10) the collapse
void Scene::power_off(double T)
{
    off = true; off_flush = true; zap_lit = false; off_t = T;
    std::mt19937 rnd((unsigned) (T * 1000.0));
    auto uni = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(rnd); };
    auto rnd01 = [&]() { return uni(0.0, 1.0); };
    off_lines.clear();
    for (auto &l : last_lines)                           // (only what was on screen)
        if (0 < l.x0 && l.x0 < ALG_MAX_X && 0 < l.x1 && l.x1 < ALG_MAX_X && 0 < l.y0 && l.y0 < ALG_MAX_Y && 0 < l.y1 && l.y1 < ALG_MAX_Y) off_lines.push_back(l);
    streak = rnd01() < 0.7;
    const double cm = ALG_MAX_Y / 24.0;                  // (the tube is about 24 cm tall)
    auto bendf = [&]() { return (rnd01() < 0.5 ? -1.0 : 1.0) * uni(0.8, 3.2); };
    double roll = rnd01();
    double length = roll < 0.6 ? uni(5.0, 8.0) * cm : roll < 0.9 ? uni(2.0, 5.0) * cm : uni(4.0, 7.0) * cm;
    bool squiggle = roll >= 0.9;
    double k0 = bendf(), k1 = bendf();
    double kw = rnd01() < 0.5 ? 0.0 : uni(1.5, 4.0), fw = uni(0.6, 1.6), pw = uni(0, 6.3);
    double loop = (rnd01() < 0.5 ? -1.0 : 1.0) * uni(1.4, 1.9) * 2 * PI;
    double x = uni(0.08, 0.92) * ALG_MAX_X, y = uni(0.08, 0.92) * ALG_MAX_Y, a = uni(0, 2 * PI);
    const int n = 80; double step = length / n;
    std::vector<std::pair<double, double>> pts{ { x, y } };
    for (int i = 0; i < n; i++) {
        double u = i / (double) n, da;
        if (squiggle && u < 0.35) da = loop * (u / 0.35) * 2.0 / (0.35 * n);   // a small loop, tightening, then the tail
        else da = (k0 * (1.0 - u) * (1.0 - u) + k1 * u * u + kw * std::sin(2 * PI * fw * u + pw)) / n;
        a += da; x += step * std::cos(a); y += step * std::sin(a);
        pts.push_back({ x, y });
    }
    // within the visible picture: shifted back inside, or (too big) made smaller - still one smooth stroke
    double bx0 = 0.06 * ALG_MAX_X, bx1 = 0.94 * ALG_MAX_X, by0 = 0.05 * ALG_MAX_Y, by1 = 0.95 * ALG_MAX_Y;
    auto box = [&](double &x0, double &x1, double &y0, double &y1) {
        x0 = y0 = 1e18; x1 = y1 = -1e18;
        for (auto &q : pts) { x0 = std::min(x0, q.first); x1 = std::max(x1, q.first); y0 = std::min(y0, q.second); y1 = std::max(y1, q.second); }
    };
    double mnx, mxx, mny, mxy; box(mnx, mxx, mny, mxy);
    double k = std::min({ 1.0, (bx1 - bx0) / std::max(1.0, mxx - mnx), (by1 - by0) / std::max(1.0, mxy - mny) });
    double cx0 = (mnx + mxx) / 2, cy0 = (mny + mxy) / 2;
    for (auto &q : pts) { q.first = cx0 + (q.first - cx0) * k; q.second = cy0 + (q.second - cy0) * k; }
    box(mnx, mxx, mny, mxy);
    double dx = std::max(0.0, bx0 - mnx) - std::max(0.0, mxx - bx1), dy = std::max(0.0, by0 - mny) - std::max(0.0, mxy - by1);
    streaks.clear();
    for (size_t i = 0; i + 1 < pts.size(); i++)
        streaks.push_back({ (float) (int) (pts[i].first + dx), (float) (int) (pts[i].second + dy), (float) (int) (pts[i + 1].first + dx),
                            (float) (int) (pts[i + 1].second + dy), 90, 40.f, 0 });
}

// the scratch maker's draws (vecrender.py _scr_make): the fine wear into A (chan 0) and B (chan 1), the nine
// steps into B (chan 2) and C (chan 3); each step's count by Wear and Wear type, more where hands hold the sheet
std::vector<ScrDraw> Scene::scratch_draws() const
{
    std::vector<ScrDraw> out;
    // the handled sheet's density: its mean over the sheet and its maximum (_scr_handled_mean)
    double tot = 0, mx = 0;
    for (int yi = 0; yi < 64; yi++)
        for (int xi = 0; xi < 48; xi++) {
            double x = xi / 47.0, y = yi / 63.0, ex = std::min(x, 1 - x), ey = std::min(y, 1 - y);
            double mid = std::exp(-((x - 0.5) * (x - 0.5) / 0.08 + (y - 0.62) * (y - 0.62) / 0.12));
            double edge = std::exp(-ex / 0.06) * 0.6, corner = 1 - 0.7 * std::exp(-(ex * ex + ey * ey) / 0.02);
            double d = (0.25 + mid + edge) * corner; tot += d; mx = std::max(mx, d);
        }
    const double hmean = tot / (64 * 48) / mx, hmax = mx;
    // the seed: the user's, varied per overlay (by its file name: the sum of (i + 1) x its code points)
    long sum = 0; size_t i = 0; int idx = 0;
    while (i < art_name.size()) {
        unsigned char c = art_name[i]; long cp; int n = 1;
        if (c < 0x80) cp = c; else if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else { cp = c & 7; n = 4; }
        for (int k = 1; k < n && i + k < art_name.size(); k++) cp = (cp << 6) | (art_name[i + k] & 63);
        i += n; sum += (long) (++idx) * cp;
    }
    const double sd = (double) (((long) look.scr_seed * 7919 + sum) % 9973 + 1);
    const double dmg[3] = { look.scr_fine, look.scr_medium, look.scr_large };
    auto amount = [&](int k, double &kn, double &kd) {  // step k (1-10): (number x, depth x)
        double base = k <= 5 ? SCR_BASE[0] : SCR_BASE[1];
        int g = k <= 3 ? 0 : k <= 7 ? 1 : 2;
        kn = base * look.scr_wear * dmg[g]; kd = std::sqrt(std::max(0.0, kn));
    };
    auto draw = [&](int tex, double n, int stepid, int mode, int place, double L0, double L1, double d0, double d1, double wv, int chan) {
        double cand = n / (place ? hmean : 1.0);
        long whole = (long) cand;
        double fr = std::fmod(sd * 0.6180339 + stepid * 0.37, 1.0);
        long c = whole + (fr < cand - whole ? 1 : 0);
        for (long b = 0; b < c; b += 4096) {
            ScrDraw d{ tex, {} };
            float *q = d.pc;
            q[0] = (float) (b / 4096); q[1] = (float) std::min(4096L, c - b); q[2] = (float) sd; q[3] = (float) stepid;
            q[4] = (float) mode; q[5] = (float) place; q[6] = (float) L0; q[7] = (float) L1;
            q[8] = (float) d0; q[9] = (float) d1; q[10] = (float) wv; q[11] = 0.f;
            q[12] = (float) chan; q[13] = (float) hmax; q[14] = (float) (SCR_SCRIBBLE[0] * look.scr_scrib); q[15] = (float) SCR_SCRIBBLE[1];
            for (int k = 0; k < 4; k++) q[16 + k] = SCR_TIERS[k];
            out.push_back(d);
        }
    };
    double kn, kd;
    amount(1, kn, kd);
    double fine_n = SCR_COUNTS[0] * kn, fw = SCR_FINE_W * SCR_TPM * kd;
    for (int c = 0; c < 2; c++) draw(c, fine_n, 0, 0, 0, SCR_LEN0, SCR_LEN1, SCR_FINE_D0, SCR_FINE_D1, fw, c);
    for (int c = 2; c <= 3; c++)
        for (int st = 0; st < 9; st++) {
            amount(st + 2, kn, kd);
            if (kn <= 0) continue;
            double depth = (0.02 + 1.58 * std::pow(st / 8.0, 3)) * (st >= 6 ? 1.1 : 1.0);
            draw(c - 1, SCR_COUNTS[st + 1] * kn, st + 1, 1, 1, SCR_LEN0, SCR_LEN1, 0.75, 1.25, depth * kd * SCR_STEP_W * SCR_TPM, c);
        }
    return out;
}

}  // namespace vx
