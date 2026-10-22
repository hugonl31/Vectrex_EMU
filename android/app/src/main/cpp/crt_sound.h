// crt_sound.h - the Vectrex's own sound around the game's (src/frontend/vectrex_emu.py CrtSound): the
// BUZZ (the beam and deflection currents leak into the audio amplifier, so it follows what is drawn,
// repeating at the frame rate), the small built-in speaker, the speech module's own amplifier, and the
// power switch. (The worn volume knob's crackle is the desktop's; here the volume is a setting.)
#pragma once
#include "core_api.h"
#include <cstdint>
#include <vector>

namespace vx {

struct Biquad {                     // RBJ audio-cookbook biquad: 'l'owpass, 'h'ighpass, 'p'eak, low 's'helf
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    Biquad(char kind, double f0, double q, double gain_db = 0.0);
    inline double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

struct CrtSound {
    bool buzz_on = true; double buzz_vol = 0.0912;
    bool speaker_on = true;
    double volume = 1.0;
    double voice_vol = 1.0; bool voice_full = true;
    std::vector<int16_t> snd_on, snd_off;               // the power switch (sounds/on.wav, off.wav)
    std::vector<float> overlay;                         // a power sound still playing
    CrtSound();
    // one frame: pcm (n mono samples, the console), the frame's lines, speech (n samples, or null) -> out
    void process(const int16_t *pcm, int n, const vx_line_t *lines, int nlines, const int16_t *speech, int16_t *out);
    void power_on() { overlay.assign(snd_on.begin(), snd_on.end()); }
private:
    std::vector<Biquad> buzz_f, spk, voice_spk, voice_bass;
    double xp = 0, yp = 0;
    std::vector<double> act, o, hp, sp;
    size_t ov_pos = 0;
};

// a 16-bit mono WAV, its silence before the sound skipped (vectrex_emu.py load_wav)
std::vector<int16_t> load_wav(const uint8_t *data, size_t n);

}  // namespace vx
