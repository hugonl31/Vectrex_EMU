// scene.cpp - see scene.h. Every number here is the desktop program's (src/frontend/vecrender.py),
// fitted there to photos, videos and measurements of a real Vectrex; the names are kept.
#include "scene.h"
#include <algorithm>
#include <cmath>

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

void Scene::build(const std::vector<Line> &in0, std::vector<Instance> &out, bool do_load)
{
    out.clear();
    if (in0.empty()) return;
    std::vector<Line> lines;
    lines.reserve(in0.size() + 64);
    if (look.halo_on)                                    // the resting spots' halos, drawn first (BEAM_HALO_W)
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
        if (wob) {                                       // the hum: offsets at the line's start and end
            double dx0, dy0, dx1, dy1;
            woff(hum, l.t0, dx0, dy0); woff(hum, l.t0 + cyc, dx1, dy1);
            x0 += dx0; y0 += dy0; x1 += dx1; y1 += dy1;
        }
        double ax = ox + x0 * sc, ay = oy + y0 * sc, bx = ox + x1 * sc, by = oy + y1 * sc;
        double dx = bx - ax, dy = by - ay, lp = std::sqrt(dx * dx + dy * dy);
        double zi = (b & 127) / 127.0, zk, zw, k;
        beam_z(zi, zk, zw);
        double fw = (BEAM_W0 + BEAM_WZ * zi) * zw;
        if (b & 256) {                                   // a resting spot's halo
            double bl = std::min(1.0, std::max(0.0, (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0)));
            fw = BEAM_HALO_W * zw * (1.0 + (BEAM_BLOOM_W - 1.0) * bl);
            k = look.halo_k * std::min(1.0, cyc / BEAM_HALO_C) * (1.0 + (BEAM_BLOOM_H - 1.0) * bl);
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
        k *= zk * BEAM_PEAK * crt_gain;
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
    std::vector<Instance> b;
    build(lines, b, true);
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
    p.cap_vp[0] = (float) p.tw; p.cap_vp[1] = (float) p.th; p.cap_vp[2] = p.cap_vp[3] = 0;
    double dt = T_prev < 0 ? 0.0 : std::min(0.25, std::max(0.0, T - T_prev));
    T_prev = T;
    const double pht = ph * rs;                          // the picture's height in image px (the desktop's window h)
    if (!look.phosphor) {                                // crisp: the newest frame as it is
        young.clear();
        if (!last.empty()) { p.batches.push_back({ 0, (uint32_t) last.size(), 0.f, 0.f, 0.f, 1.f }); p.inst = last; }
    } else {
        // 1. the fresh light: each young frame's lines drawn since the last display frame
        for (auto it = young.begin(); it != young.end();) {
            Batch bt{ (uint32_t) p.inst.size(), (uint32_t) it->b.size(), 1.f, (float) (T - it->t0), (float) (T - it->done), (float) PHOS_GAIN };
            p.inst.insert(p.inst.end(), it->b.begin(), it->b.end());
            p.batches.push_back(bt);
            it->done = T;
            if (T < it->t1) ++it; else it = young.erase(it);
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
}

}  // namespace vx
