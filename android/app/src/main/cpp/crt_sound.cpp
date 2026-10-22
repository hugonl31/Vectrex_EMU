// crt_sound.cpp - see crt_sound.h; the numbers are the desktop's (measured on the owner's Vectrex).
#include "crt_sound.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vx {

static const double RATE = 44100.0;
static const double BUZZ_GAIN = 47.5;                  // empty-screen buzz ~34 dB below the game sound, +40 %
static const double POWER_SND_GAIN = 1.6;
static const double VOICE_BOOST = 1.5;

Biquad::Biquad(char kind, double f0, double q, double gain_db)
{
    double w = 2 * M_PI * f0 / RATE, cw = std::cos(w), sw = std::sin(w), al = sw / (2 * q);
    double b[3], a[3];
    if (kind == 'l') { b[0] = (1 - cw) / 2; b[1] = 1 - cw; b[2] = (1 - cw) / 2; a[0] = 1 + al; a[1] = -2 * cw; a[2] = 1 - al; }
    else if (kind == 'h') { b[0] = (1 + cw) / 2; b[1] = -(1 + cw); b[2] = (1 + cw) / 2; a[0] = 1 + al; a[1] = -2 * cw; a[2] = 1 - al; }
    else if (kind == 's') {
        double A = std::pow(10.0, gain_db / 40.0), sa = 2 * std::sqrt(A) * al;
        b[0] = A * ((A + 1) - (A - 1) * cw + sa); b[1] = 2 * A * ((A - 1) - (A + 1) * cw); b[2] = A * ((A + 1) - (A - 1) * cw - sa);
        a[0] = (A + 1) + (A - 1) * cw + sa; a[1] = -2 * ((A - 1) + (A + 1) * cw); a[2] = (A + 1) + (A - 1) * cw - sa;
    } else {
        double A = std::pow(10.0, gain_db / 40.0);
        b[0] = 1 + al * A; b[1] = -2 * cw; b[2] = 1 - al * A; a[0] = 1 + al / A; a[1] = -2 * cw; a[2] = 1 - al / A;
    }
    b0 = b[0] / a[0]; b1 = b[1] / a[0]; b2 = b[2] / a[0]; a1 = a[1] / a[0]; a2 = a[2] / a[0];
}

CrtSound::CrtSound()
{
    // the buzz as recorded on the owner's Vectrex: a comb of frame-rate harmonics, nearly all between
    // 1.2 and 4.8 kHz, with resonances near 1.45, 2.75 and 3.5 kHz
    buzz_f = { Biquad('h', 1300, 0.7), Biquad('h', 1300, 0.7), Biquad('p', 1450, 4.0, 6.0), Biquad('p', 2750, 5.0, 5.0),
               Biquad('p', 3800, 3.0, 7.0), Biquad('l', 4700, 0.8), Biquad('l', 4700, 0.8) };
    // the built-in speaker: little below ~250 Hz, fairly even up to ~5 kHz, down above ~6.5 kHz
    spk = { Biquad('h', 220, 0.9), Biquad('h', 175, 0.7), Biquad('p', 270, 1.3, 2.0), Biquad('p', 900, 1.0, 4.0),
            Biquad('l', 6200, 0.7), Biquad('l', 8000, 0.7) };
    voice_spk = { Biquad('h', 140, 0.7), Biquad('p', 1800, 1.0, 2.0), Biquad('l', 7500, 0.7) };
    voice_bass = { Biquad('h', 40, 0.7), Biquad('s', 150, 0.7, 6.0) };   // (an arcade cabinet's warm bass)
}

void CrtSound::process(const int16_t *pcm, int n, const vx_line_t *lines, int nlines, const int16_t *speech, int16_t *out)
{
    o.assign(n, 0.0);
    for (int i = 0; i < n; i++) o[i] = pcm[i] * volume;            // (the volume: the game sound only)
    if (buzz_on && buzz_vol > 0 && n > 0) {
        act.assign(n, 0.0);
        const double k = RATE / 1.5e6;                             // samples per CPU cycle
        for (int j = 0; j < nlines; j++) {
            const vx_line_t &l = lines[j];
            double e = l.energy / 8.0 * ((l.bright & 127) / 127.0);
            int i0 = (int) (l.t0 * k), i1 = (int) ((l.t0 + l.energy / 8.0) * k);
            if (i0 >= n) continue;
            i1 = std::min(n - 1, std::max(i0, i1));
            double w = e / (i1 - i0 + 1);
            for (int i = i0; i <= i1; i++) act[i] += w;
        }
        // the frame's start: the BIOS zeroes the integrators and recalibrates every frame, even with
        // nothing on screen, so the buzz never stops (its pitch is the frame rate)
        for (int i = 0; i < std::min(n, 8); i++) act[i] += 300.0 * (1.0 - i / 8.0);
        double g = buzz_vol * BUZZ_GAIN;
        for (int i = 0; i < n; i++) {                              // high pass (~300 Hz): a buzz, not a thump
            double x = act[i] * g;
            yp = 0.957 * (yp + x - xp); xp = x;
            double y = yp;
            for (auto &f : buzz_f) y = f.run(y);
            o[i] += y;
        }
    }
    if (speaker_on)
        for (int i = 0; i < n; i++) {
            double v = o[i];
            for (auto &f : spk) v = f.run(v);
            o[i] = 32767.0 * std::tanh(v / 26000.0);              // the driver's limited excursion
        }
    if (speech) {                                                  // (the speech module: not through the
        auto &chain = voice_full ? voice_bass : voice_spk;         //  Vectrex's volume, buzz or speaker)
        for (int i = 0; i < n; i++) {
            double s = speech[i] * voice_vol * 2.0 * VOICE_BOOST;  // (the core sends it at half scale)
            for (auto &f : chain) s = f.run(s);
            double v = o[i] + s;
            if (v > 24000) v = 24000 + 8767 * std::tanh((v - 24000) / 8767);
            else if (v < -24000) v = -24000 - 8767 * std::tanh((-v - 24000) / 8767);
            o[i] = v;
        }
    }
    if (!overlay.empty()) {                                        // the power switch's sound (not through the volume)
        size_t m = std::min((size_t) n, overlay.size());
        for (size_t i = 0; i < m; i++) o[i] += overlay[i] * POWER_SND_GAIN;
        overlay.erase(overlay.begin(), overlay.begin() + m);
    }
    for (int i = 0; i < n; i++) out[i] = (int16_t) std::max(-32768.0, std::min(32767.0, o[i]));
}

std::vector<int16_t> load_wav(const uint8_t *d, size_t n)
{
    std::vector<int16_t> out;
    if (n < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return out;
    size_t p = 12; int ch = 0, bits = 0;
    while (p + 8 <= n) {
        uint32_t sz = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t) d[p + 7] << 24;
        if (!memcmp(d + p, "fmt ", 4) && p + 24 <= n) { ch = d[p + 10] | d[p + 11] << 8; bits = d[p + 22] | d[p + 23] << 8; }
        else if (!memcmp(d + p, "data", 4) && ch == 1 && bits == 16) {
            size_t m = std::min((size_t) sz, n - p - 8) / 2;
            out.resize(m); memcpy(out.data(), d + p + 8, m * 2);
            break;
        }
        p += 8 + sz + (sz & 1);
    }
    if (out.empty()) return out;
    int pk = 1; for (auto v : out) pk = std::max(pk, std::abs((int) v));
    size_t k = 0; while (k < out.size() && std::abs((int) out[k]) <= pk * 0.05) k++;
    out.erase(out.begin(), out.begin() + (k > 88 ? k - 88 : 0));
    return out;
}

}  // namespace vx
