// scene.h - the picture's logic, the same for both graphics backends: the beam model (each line a
// Gaussian capsule, its brightness and width from the beam energy), the phosphor in real time (game
// frames light up at their own moments, the afterglow fades as measured), the mains hum wobble and the
// brightness bending - ported from src/frontend/vecrender.py (GLBloomRenderer). Each display frame it
// makes a FramePlan: the instance data and every pass's parameters; a backend only executes it.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

namespace vx {

struct Line {                       // a beam line (the core's, see core_api.h); t0 may pass 65535 (joined pieces)
    float x0, y0, x1, y1;
    int bright;                     // Z 0..127, bit 7 = fading out, bit 8 (here) = a resting spot's halo
    float energy;                   // 1/8 cycles
    uint32_t t0;                    // start cycle in the (joined) frame
};

struct Instance { float ax, ay, bx, by, k, sg, tl, pad; };      // capsule.vert's two vec4 attributes
struct Batch { uint32_t first, count; float timed, age0, dtw, gain; };

struct Look {                       // the Tube / Display settings (vecrender.py defaults: the owner's choices)
    bool phosphor = true;
    bool trail_on = true; float trail = 0.6f;
    float bloom = 0.55f;
    bool wob_on = true; float wob_strength = 0.1074f; int mains_hz = 50;
    bool bend_on = true; float bend_strength = 0.6f; int bend_type = 0;  // grow, shift, grow+shift
    bool curve_on = true;
    bool halo_on = true; float halo_k = 0.014f;
    float phos_grow = 0.5f, phos_smooth = 0.8f, phos_spread = 0.0f;
    bool art_on = true; float art_tint = 1.0f, art_room = 0.0648f, art_print = 0.36f; bool art_parallax = true;
    float art_dx = 0.f, art_dy = 0.f, art_scale = 1.f;
    float render_scale = 1.0f;      // the phosphor images' resolution (Android: below 1 for slow GPUs)
};

struct FramePlan {
    int screen_w = 0, screen_h = 0;
    int tw = 0, th = 0, qw = 0, qh = 0;                 // the picture images (full and quarter size)
    bool phosphor = true;
    std::vector<Instance> inst;
    std::vector<Batch> batches;                         // the fresh light (phosphor) or the crisp lines
    float cap_vp[4];                                    // capsule pass: target size
    float update[16], show[12];                         // (after the vp vec4: see the shaders)
    float blur[4][4];                                   // per blur pass: dir (the first reads the full-size picture)
    int blur_passes = 4;
    float fin[16];                                      // final.frag (vp filled by the backend)
    bool art = false; float art_rect[4]; float art_a[2][4];
};

class Scene {
public:
    Look look;
    void set_screen(int w, int h);
    // a game frame that started at real time t_start (s) and lasts `length` s; t0abs = its first
    // cycle (absolute, 64-bit): the hum's phase
    void add_frame(std::vector<Line> lines, double t_start, double length, uint64_t t0abs);
    void make_plan(double T, FramePlan &p);             // the screen at real time T
    void clear() { young.clear(); last.clear(); T_prev = -1; }
    void set_art_size(int w, int h) { art_w = w; art_h = h; }
    // the tube warming up after power on (vectrex_emu.py crt_fx)
    double power_on_t = -1e9;
private:
    struct Young { std::vector<Instance> b; double t0, t1, done; };
    std::deque<Young> young;
    std::vector<Instance> last;
    std::vector<float> periods;
    double T_prev = -1;
    double load_f = -1;
    float crt_gain = 1.f;
    int sw = 0, sh = 0;
    // the picture on the screen and in the images
    double s = 1, px0 = 0, py0 = 0, pw = 0, ph = 0;     // screen px
    double tx0 = 0, ty0 = 0, tws = 0, ths = 0;          // the images' rectangle on the screen
    double rs = 1;                                      // image px per screen px
    int art_w = 0, art_h = 0;
    uint64_t frame_t0 = 0;
    void layout();
    void build(const std::vector<Line> &in, std::vector<Instance> &out, bool do_load);
    double hold() const;
};

}  // namespace vx
