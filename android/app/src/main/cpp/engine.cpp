// engine.cpp - the Android app's native side: the emulation thread (the core, one game frame at a
// time, paced by real time and the sound queue as the desktop's Runner), the render thread (the
// phosphor picture at the display's refresh, Vulkan or OpenGL ES), the sound (AAudio) and the JNI
// calls of EmulatorActivity.java.
#include "backend.h"
#include "core_api.h"
#include "crt_sound.h"
#include "scene.h"
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VectrexEMU", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VectrexEMU", __VA_ARGS__)

using namespace vx;

namespace {

const int RATE = 44100;

double now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- the sound: a ring the AAudio callback drains
struct Ring {
    static const uint32_t N = 1 << 15;                   // (0.74 s)
    int16_t buf[N];
    std::atomic<uint32_t> w{ 0 }, r{ 0 };
    uint32_t size() const { return w.load(std::memory_order_acquire) - r.load(std::memory_order_acquire); }
    void push(const int16_t *d, int n)
    {
        uint32_t wi = w.load(std::memory_order_relaxed);
        uint32_t room = N - (wi - r.load(std::memory_order_acquire));
        if ((uint32_t) n > room) n = (int) room;
        for (int i = 0; i < n; i++) buf[(wi + i) & (N - 1)] = d[i];
        w.store(wi + n, std::memory_order_release);
    }
    int pop(int16_t *d, int n)
    {
        uint32_t ri = r.load(std::memory_order_relaxed);
        uint32_t have = w.load(std::memory_order_acquire) - ri;
        int k = (int) std::min<uint32_t>(have, (uint32_t) n);
        for (int i = 0; i < k; i++) d[i] = buf[(ri + i) & (N - 1)];
        r.store(ri + k, std::memory_order_release);
        return k;
    }
};

struct QFrame { std::vector<Line> lines; double t_start, len; uint64_t t0; bool power; };

struct Engine {
    std::map<std::string, std::string> conf;
    std::vector<uint8_t> bios, cart, art_rgba; int art_w = 0, art_h = 0; double art_sheet = 0; std::string art_name;
    std::string sav;
    CrtSound crt;
    Look look;
    double line_gap = 12.3;
    bool skip_boot = false;

    std::atomic<bool> running{ false }, paused{ false };
    std::atomic<bool> app_paused{ false }, user_paused{ false };   // (the app in the background / the player's pause)
    std::mutex cm;                                      // (the settings: conf, changed live from the in-game menu)
    std::atomic<int> conf_version{ 0 };
    // the clock both threads keep time by: real time, standing still while paused (a paused picture stays as
    // it is - its afterglow does not fade - and the game goes on where it stopped: vectrex_emu.py pause_acc)
    std::mutex km; double paused_total = 0, pause_start = -1;
    double clock()
    {
        std::lock_guard<std::mutex> g(km);
        return (pause_start >= 0 ? pause_start : now()) - paused_total;
    }
    void set_paused()
    {
        bool p = app_paused || user_paused;
        std::lock_guard<std::mutex> g(km);
        if (p && pause_start < 0) pause_start = now();
        else if (!p && pause_start >= 0) { paused_total += now() - pause_start; pause_start = -1; }
        paused = p;
    }
    std::mutex sm; std::string stats;                    // (the on-screen display: once a second)
    std::atomic<int> in_jx[2], in_jy[2], in_btn[2];
    std::atomic<bool> p2_seen{ false }, reset_req{ false };
    std::atomic<bool> off{ false };                     // (switching off: the picture's zap, the switch's sound)
    std::atomic<double> power_t{ -1.0 };                // (when the Vectrex was switched on: the tube warms up from
                                                        //  then - not in the frame queue, whose oldest frames go when
                                                        //  the screen is not there yet)

    std::thread emu_thr, ren_thr;
    std::mutex qm; std::deque<QFrame> queue;
    Ring ring;
    AAudioStream *stream = nullptr;
    std::atomic<long> underruns{ 0 };
    std::atomic<int> burst{ 256 };                      // (the sound device's burst, samples: the queue keeps 2+)
    std::atomic<bool> sounding{ false };                // (the first frame's sound is in: underruns count from now)

    std::mutex wm; ANativeWindow *window = nullptr; std::atomic<bool> window_ok{ false };
    std::string renderer_name;

    std::string get(const char *k, const char *d) { auto it = conf.find(k); return it == conf.end() ? d : it->second; }
    double num(const char *k, double d) { auto it = conf.find(k); return it == conf.end() ? d : atof(it->second.c_str()); }
    bool flag(const char *k, bool d) { auto it = conf.find(k); return it == conf.end() ? d : it->second == "1" || it->second == "true"; }

    void configure()
    {
        // the core's analog model: a real Vectrex, or the perfect one (vectrex_emu.py Core)
        if (flag("ideal", false)) setenv("VT_ZIDEAL", "1", 1);
        else { setenv("VT_ZREFK", "0.38", 1); setenv("VT_CALSHIFT", "2.9", 1); }
        std::string hw = get("cart_hw", "auto");
        setenv("VT_CARTHW", hw == "auto" ? "1" : hw == "off" ? "0" : hw.c_str(), 1);
        char seed[16]; snprintf(seed, sizeof seed, "%d", (int) (now() * 1000) & 0x7fffffff);
        setenv("VT_POWERSEED", seed, 1);                 // (the RAM's random state at every power-on)
        std::string sp = get("speech", "off"), rom = get("speech_rom", "");
        if (sp != "off" && !rom.empty()) {
            setenv("VT_VECVOICE", rom.c_str(), 1); setenv("VT_SPEECHDEV", sp.c_str(), 1); setenv("VT_SPEECHOUT", "1", 1);
        }
        skip_boot = flag("skip_boot", false);
        line_gap = num("line_gap", 12.3);
        look = make_look();
        set_sound();
        for (int i = 0; i < 2; i++) { in_jx[i] = 0; in_jy[i] = 0; in_btn[i] = 0; }
    }
    // the look from the settings (the defaults for what is not set)
    Look make_look()
    {
        Look L;
        L.phosphor = flag("phosphor", L.phosphor); L.trail_on = flag("trail_on", L.trail_on); L.trail = (float) num("trail", L.trail);
        L.bloom = (float) num("bloom", L.bloom); L.wob_on = flag("wob_on", L.wob_on); L.wob_strength = (float) num("wob_strength", L.wob_strength);
        L.mains_hz = (int) num("mains_hz", L.mains_hz); L.bend_on = flag("bend_on", L.bend_on); L.bend_strength = (float) num("bend_strength", L.bend_strength);
        L.bend_type = (int) num("bend_type", L.bend_type); L.curve_on = flag("curve_on", L.curve_on);
        L.halo_on = flag("halo_on", L.halo_on); L.halo_k = (float) num("halo_k", L.halo_k);
        L.phos_grow = (float) num("phos_grow", L.phos_grow); L.phos_smooth = (float) num("phos_smooth", L.phos_smooth);
        L.art_on = flag("art_on", L.art_on); L.art_tint = (float) num("art_tint", L.art_tint); L.art_room = (float) num("art_room", L.art_room);
        L.art_print = (float) num("art_print", L.art_print); L.art_parallax = flag("art_parallax", L.art_parallax);
        L.render_scale = (float) num("render_scale", L.render_scale);
        L.even_beam = flag("even_beam", L.even_beam);
        L.art_glow = flag("art_glow", L.art_glow); L.art_glow_k = (float) num("art_glow_k", L.art_glow_k);
        L.scr_on = flag("scr_on", L.scr_on); L.scr_wear = (float) num("scr_wear", L.scr_wear);
        L.scr_fine = (float) num("scr_fine", L.scr_fine); L.scr_medium = (float) num("scr_medium", L.scr_medium);
        L.scr_large = (float) num("scr_large", L.scr_large); L.scr_glint = (float) num("scr_glint", L.scr_glint);
        L.scr_reach = (float) num("scr_reach", L.scr_reach); L.scr_scrib = (float) num("scr_scrib", L.scr_scrib);
        L.scr_seed = (int) num("scr_seed", L.scr_seed);
        L.art_dx = (float) num("art_dx", L.art_dx); L.art_dy = (float) num("art_dy", L.art_dy); L.art_scale = (float) num("art_scale", L.art_scale);
        L.phos_full = get("phos_area", "screen") != "picture";
        L.glint_div = get("glint_res", "4") == "8" ? 8 : 4;
        return L;
    }
    void set_sound()
    {
        static const CrtSound d0;
        crt.buzz_on = d0.buzz_on; crt.buzz_vol = d0.buzz_vol; crt.speaker_on = d0.speaker_on; crt.volume = d0.volume;
        crt.voice_vol = d0.voice_vol; crt.voice_full = d0.voice_full;
        crt.buzz_on = flag("buzz_on", crt.buzz_on); crt.buzz_vol = num("buzz_vol", crt.buzz_vol);
        crt.speaker_on = flag("speaker_on", crt.speaker_on); crt.volume = num("volume", crt.volume);
        crt.voice_vol = num("voice_vol", crt.voice_vol); crt.voice_full = flag("voice_full", crt.voice_full);
    }

    // ------------------------------------------------------------ sound
    // The stream runs at the device's own rate (48 kHz mostly): only then does Android give the low-latency
    // path (small bursts, no mixer in between); the core's 44.1 kHz is resampled here (cubic Hermite).
    int out_rate = RATE;
    double rs_step = 1.0, rs_pos = 0.0;                 // (input samples per output sample; the phase)
    float rs_h[4] = { 0, 0, 0, 0 };                     // (the last input samples: x-1, x0, x1, x2)
    int16_t rs_in[16384];
    // (the stream: stereo float, the format the fast path takes on most devices; the sound itself is mono)
    static aaudio_data_callback_result_t audio_cb(AAudioStream *, void *user, void *data, int32_t n)
    {
        Engine *e = (Engine *) user;
        float *out = (float *) data;
        if (e->paused) { memset(out, 0, n * 2 * sizeof(float)); return AAUDIO_CALLBACK_RESULT_CONTINUE; }
        // (the input samples shifted in before this callback's outputs: the phase after the last output may
        //  pass 1 - that sample is shifted in at the start of the next callback, from its own buffer)
        int need = (int) (e->rs_pos + (n - 1) * e->rs_step);
        if (need > (int) (sizeof e->rs_in / 2)) need = sizeof e->rs_in / 2;
        int k = e->ring.pop(e->rs_in, need);
        if (k < need) { memset(e->rs_in + k, 0, (need - k) * 2); if (e->running && e->sounding) e->underruns++; }
        float *h = e->rs_h; double pos = e->rs_pos; int j = 0;
        for (int i = 0; i < n; i++) {
            while (pos >= 1.0 && j < need) { h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = e->rs_in[j++]; pos -= 1.0; }
            float t = (float) pos;                       // (between h[1] and h[2])
            float c0 = h[1], c1 = 0.5f * (h[2] - h[0]), c2 = h[0] - 2.5f * h[1] + 2.f * h[2] - 0.5f * h[3];
            float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
            float y = (((c3 * t + c2) * t + c1) * t + c0) * (1.f / 32768.f);
            y = y > 1.f ? 1.f : y < -1.f ? -1.f : y;
            out[2 * i] = out[2 * i + 1] = y;
            pos += e->rs_step;
        }
        e->rs_pos = pos;
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    static void audio_err(AAudioStream *, void *user, aaudio_result_t err)
    {
        Engine *e = (Engine *) user;                     // (a headset plugged in / out: open the stream again)
        LOGI("audio stream error %d: reopening", err);
        std::thread([e]() { e->close_audio(); e->open_audio(); }).detach();
    }
    void open_audio()
    {
        AAudioStreamBuilder *b = nullptr;
        if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(b, 2);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);   // (at the device's rate)
        AAudioStreamBuilder_setDataCallback(b, audio_cb, this);
        AAudioStreamBuilder_setErrorCallback(b, audio_err, this);
        if (AAudioStreamBuilder_openStream(b, &stream) != AAUDIO_OK) stream = nullptr;
        AAudioStreamBuilder_delete(b);
        if (stream) {
            out_rate = AAudioStream_getSampleRate(stream);
            if (out_rate <= 0) out_rate = RATE;
            rs_step = RATE / (double) out_rate; rs_pos = 0.0;
            int b = AAudioStream_getFramesPerBurst(stream);
            AAudioStream_setBufferSizeInFrames(stream, 2 * std::max(b, 1));   // (two bursts: low latency, no glitches)
            if (b > 0) burst = (int) std::ceil(b * rs_step);                // (in the core's samples, for the queue)
            AAudioStream_requestStart(stream);
            LOGI("audio: %d Hz stereo float (the core's 44100 resampled), burst %d, %s", out_rate, b,
                 AAudioStream_getPerformanceMode(stream) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ? "low latency" : "normal latency");
        }
    }
    void close_audio()
    {
        if (stream) { AAudioStream_requestStop(stream); AAudioStream_close(stream); stream = nullptr; }
    }

    // ------------------------------------------------------------ the game loop (vectrex_emu.py Runner)
    void emu_loop()
    {
        static vx_frame_t fr;
        std::vector<int16_t> mono, speech, out;
        std::vector<Line> lines;
        const double low = std::max(0.012, 2.5 * burst / RATE);  // (the queue's low water mark: 2.5 bursts)
        { std::vector<int16_t> z((size_t) (std::max(0.04, low + 0.02) * RATE), 0); ring.push(z.data(), (int) z.size()); }   // (a head start)
        double t_next = clock();
        uint64_t ft_hi = 0; uint32_t ft_last = 0;
        bool first = true; double gap = line_gap; int seen = conf_version;
        struct { std::vector<Line> lines; double t_start, len; uint64_t t0; bool power, on; } pend{ {}, 0, 0, 0, false, false };
        while (running) {
            if (paused) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            double send_gap = first ? gap : -1.0;
            if (seen != conf_version) {                           // (a setting changed in the in-game menu)
                std::lock_guard<std::mutex> g(cm);
                seen = conf_version; set_sound();
                double ng = num("line_gap", 12.3);
                if (ng != gap) { gap = ng; send_gap = ng; }
            }
            if (off) {                                            // switched off: only the switch's sound, then silence
                static bool clicked = false;
                if (!clicked) { clicked = true; crt.power_off(); t_next = clock(); }
                int16_t z[RATE / 50]; crt.idle(RATE / 50, z); ring.push(z, RATE / 50);
                t_next += 0.02;
                while (running && clock() < t_next) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            unsigned char m[8];
            int jx = in_jx[0], jy = in_jy[0], btn = in_btn[0];
            int flags = 0;
            if (first) flags |= 8 | (skip_boot ? 16 : 0);       // power on (and the BIOS intro unseen)
            if (reset_req.exchange(false)) flags |= 4;            // the RESET button
            m[0] = (unsigned char) (jx + 128); m[1] = (unsigned char) (jy + 128); m[2] = (unsigned char) (btn & 15);
            if (p2_seen) { flags |= 0x40; m[4] = (unsigned char) (in_jx[1] + 128); m[5] = (unsigned char) (in_jy[1] + 128); m[6] = (unsigned char) (in_btn[1] & 15); }
            m[3] = (unsigned char) flags;
            bool hide = first;                                    // (power on: the click now, the picture a frame later)
            if (first) { crt.power_on(); power_t = t_next; }
            vx_frame(m, send_gap, &fr);
            if (fr.frame_t0 < ft_last) ft_hi += 1ull << 32;       // (the core's 32-bit cycle count wraps)
            ft_last = fr.frame_t0;
            uint64_t t0 = ft_hi + fr.frame_t0;
            int ns = fr.nsamples;
            double len = std::max(ns, 1) / (double) RATE;
            if (t_next < clock() - 0.1) t_next = clock();         // (the intro skipped / a stall: start again from now)
            // the frame for the screen: pieces shorter than 15 ms join the next one (a game calling Wait_Recal
            // twice in one refresh - Mine Storm does - would split it in a short and a long piece)
            lines.clear();
            if (!hide)
                for (int i = 0; i < fr.nlines; i++) {
                    const vx_line_t &l = fr.lines[i];
                    lines.push_back({ (float) l.x0, (float) l.y0, (float) l.x1, (float) l.y1, l.bright, (float) l.energy, l.t0 });
                }
            if (!pend.on) { pend.lines = lines; pend.t_start = t_next; pend.t0 = t0; pend.len = len; pend.power = first; pend.on = true; }
            else {
                uint32_t sh = (uint32_t) (t0 - pend.t0);
                for (auto &l : lines) { l.t0 += sh; pend.lines.push_back(l); }
                pend.len += len;
            }
            if (pend.len >= 0.015) {
                std::lock_guard<std::mutex> g(qm);
                queue.push_back({ std::move(pend.lines), pend.t_start, pend.len, pend.t0, pend.power });
                while (queue.size() > 16) queue.pop_front();
                pend.lines.clear(); pend.on = false;
            }
            // the sound: the console's (and the speech module's), through the CRT model
            mono.resize(ns); out.resize(ns);
            const int16_t *sp = nullptr;
            if (fr.two_channels) {
                speech.resize(ns);
                for (int i = 0; i < ns; i++) { mono[i] = fr.pcm[2 * i]; speech[i] = fr.pcm[2 * i + 1]; }
                sp = speech.data();
            } else if (ns) memcpy(mono.data(), fr.pcm, ns * 2);
            crt.process(mono.data(), ns, fr.lines, fr.nlines, sp, out.data());
            ring.push(out.data(), ns);
            first = false; sounding = true;
            // pace by emulated time (this frame's samples = its length in CPU cycles); the sound queue caps
            // runaway buffering, and when it is about to run dry (the sound card's clock a little ahead of
            // this one) the next frame comes at once
            t_next += len;
            while (running && !paused) {
                double tn = clock(), q = ring.size() / (double) RATE;
                if (tn >= t_next - 0.002 && q < 0.15) break;
                if (q < low) break;
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
            }
        }
        vx_close();                                               // (the flash cartridges' saved state)
    }

    // ------------------------------------------------------------ the screen
    void render_loop(ANativeWindow *win)
    {
        std::unique_ptr<Backend> be;
        std::string want = get("renderer", "auto"), err;
        if (want != "gles") {
            be = make_vulkan();
            if (!be->init(win, err)) { LOGE("Vulkan unavailable: %s", err.c_str()); be.reset(); }
        }
        if (!be && want != "vulkan") {
            be = make_gles();
            if (!be->init(win, err)) { LOGE("OpenGL ES unavailable: %s", err.c_str()); be.reset(); }
        }
        if (!be) { LOGE("no renderer"); return; }
        renderer_name = be->name();
        LOGI("renderer: %s (%s images)", be->name(), be->float_images() ? "16-bit float" : "8-bit");
        Scene scene; scene.look = look;
        scene.sheet_ok = be->can_sheet();
        if (art_w) { be->set_art(art_w, art_h, art_rgba); scene.set_art(art_w, art_h, art_sheet, art_name.c_str()); }
        // the overlay's scratches: made at the start, and again when their settings change (at most every 0.3 s
        // while a slider moves, as the desktop)
        auto scr_key = [](const Look &L) {
            char k[160]; snprintf(k, sizeof k, "%d %d %.3f %.3f %.3f %.3f %.3f %d", L.art_on, L.scr_on, L.scr_wear, L.scr_fine, L.scr_medium, L.scr_large, L.scr_scrib, L.scr_seed);
            return std::string(k);
        };
        std::string made_key; double made_t = -1;
        auto scratches = [&]() {
            const Look &L = scene.look;
            made_key = scr_key(L); made_t = now();
            if (!art_w || !L.art_on || !L.scr_on || !scene.sheet_ok) return;
            double t0 = now();
            auto draws = scene.scratch_draws();
            scene.scr_ready = be->make_scratches(draws);
            LOGI("overlay scratches: %s, %zu draws, %.0f ms", scene.scr_ready ? "made" : "not possible here", draws.size(), 1000 * (now() - t0));
        };
        scratches();
        FramePlan plan;
        std::vector<QFrame> got;
        bool was_off = false;
        int seen = conf_version;
        double st_t0 = now(), st_cpu = 0, st_emu = 0; long st_shown = 0, st_games = 0;    // (the speed meter: the log, every 10 s)
        double os_t0 = clock(), os_emu = 0; long os_shown = 0, os_games = 0;              // (and the on-screen display, every second)
        while (running && window_ok) {
            double t_a = now();
            if (seen != conf_version) {                  // (a setting changed in the in-game menu)
                std::lock_guard<std::mutex> g(cm);
                seen = conf_version; scene.look = make_look();
            }
            if (scr_key(scene.look) != made_key && now() - made_t >= 0.3) scratches();
            { std::lock_guard<std::mutex> g(qm); got.assign(std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end())); queue.clear(); }
            for (auto &f : got) {
                st_games++; st_emu += f.len; os_games++; os_emu += f.len;
                scene.add_frame(std::move(f.lines), f.t_start, f.len, f.t0);
            }
            if (power_t >= 0) scene.power_on_t = power_t;
            if (off && !was_off) { was_off = true; scene.power_off(clock()); }
            int w = 0, h = 0; be->size(w, h);
            if (w <= 0 || h <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); continue; }
            scene.set_screen(w, h);
            scene.make_plan(clock(), plan);
            if (!be->render(plan)) { LOGI("window lost"); break; }
            st_cpu += now() - t_a; st_shown++; os_shown++;
            if (now() - st_t0 >= 10.0) {
                double dt = now() - st_t0;
                LOGI("%s %dx%d (images %dx%d): %.1f display fps, %.1f game fps, speed %.0f%%, render CPU %.2f ms/frame (with the vsync wait), %ld sound underruns",
                     be->name(), w, h, plan.tw, plan.th, st_shown / dt, st_games / dt, 100.0 * st_emu / dt, 1000.0 * st_cpu / std::max(1L, st_shown), (long) underruns);
                st_t0 = now(); st_cpu = st_emu = 0; st_shown = st_games = 0;
            }
            double od = clock() - os_t0;                 // (the on-screen display: the desktop's - the game's frame rate,
            if (od >= 1.0) {                             //  the screen's, the emulation speed)
                char b[96];
                snprintf(b, sizeof b, "game %.1f fps   screen %.0f Hz   speed %.0f %%", os_games / od, os_shown / od, 100.0 * os_emu / od);
                { std::lock_guard<std::mutex> g(sm); stats = b; }
                os_t0 = clock(); os_emu = 0; os_shown = os_games = 0;
            }
            if (paused) std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    void start()
    {
        configure();
        if (vx_load(bios.data(), bios.size(), cart.data(), cart.size(), sav.c_str())) { LOGE("bad BIOS"); return; }
        running = true;
        open_audio();
        emu_thr = std::thread([this]() { emu_loop(); });
    }
    void set_window(ANativeWindow *w)
    {
        stop_render();
        std::lock_guard<std::mutex> g(wm);
        window = w; window_ok = true;
        ren_thr = std::thread([this, w]() { render_loop(w); });
    }
    void stop_render()
    {
        window_ok = false;
        if (ren_thr.joinable()) ren_thr.join();
        std::lock_guard<std::mutex> g(wm);
        if (window) { ANativeWindow_release(window); window = nullptr; }
    }
    void stop()
    {
        running = false;
        stop_render();
        if (emu_thr.joinable()) emu_thr.join();
        close_audio();
    }
};

Engine *E = nullptr;

std::vector<uint8_t> bytes(JNIEnv *env, jbyteArray a)
{
    std::vector<uint8_t> v;
    if (!a) return v;
    jsize n = env->GetArrayLength(a); v.resize(n);
    if (n) env->GetByteArrayRegion(a, 0, n, (jbyte *) v.data());
    return v;
}
std::string str(JNIEnv *env, jstring s)
{
    if (!s) return "";
    const char *c = env->GetStringUTFChars(s, nullptr); std::string r = c; env->ReleaseStringUTFChars(s, c);
    return r;
}

}  // namespace

// shader table (shaders.cpp, made from shaders/ at build time)
extern "C" {

#define FN(name) Java_io_github_hugonl31_vectrexemu_EmulatorActivity_##name

JNIEXPORT void JNICALL FN(nativeStart)(JNIEnv *env, jclass, jobjectArray keys, jobjectArray vals, jbyteArray bios, jbyteArray cart,
                                       jstring sav, jbyteArray art, jint aw, jint ah, jfloat asheet, jstring aname,
                                       jbyteArray on_wav, jbyteArray off_wav)
{
    if (E) return;
    E = new Engine();
    for (jsize i = 0, n = env->GetArrayLength(keys); i < n; i++)
        E->conf[str(env, (jstring) env->GetObjectArrayElement(keys, i))] = str(env, (jstring) env->GetObjectArrayElement(vals, i));
    E->bios = bytes(env, bios); E->cart = bytes(env, cart); E->sav = str(env, sav);
    E->art_rgba = bytes(env, art); E->art_w = E->art_rgba.empty() ? 0 : aw; E->art_h = ah;
    E->art_sheet = asheet; E->art_name = str(env, aname);
    auto on = bytes(env, on_wav), off = bytes(env, off_wav);
    E->crt.snd_on = load_wav(on.data(), on.size()); E->crt.snd_off = load_wav(off.data(), off.size());
    E->start();
}

JNIEXPORT void JNICALL FN(nativeSurface)(JNIEnv *env, jclass, jobject surface)
{
    if (!E) return;
    if (!surface) { E->stop_render(); return; }
    E->set_window(ANativeWindow_fromSurface(env, surface));
}

JNIEXPORT void JNICALL FN(nativeInput)(JNIEnv *, jclass, jint player, jint jx, jint jy, jint buttons)
{
    if (!E || player < 0 || player > 1) return;
    E->in_jx[player] = std::max(-128, std::min(127, (int) jx)); E->in_jy[player] = std::max(-128, std::min(127, (int) jy));
    E->in_btn[player] = buttons & 15;
    if (player == 1) E->p2_seen = true;
}

JNIEXPORT void JNICALL FN(nativeReset)(JNIEnv *, jclass) { if (E) E->reset_req = true; }

JNIEXPORT void JNICALL FN(nativePause)(JNIEnv *, jclass, jboolean p) { if (E) { E->app_paused = p; E->set_paused(); } }

// the player's pause (Start, P): the picture stands still, the game waits
JNIEXPORT void JNICALL FN(nativeUserPause)(JNIEnv *, jclass, jboolean p) { if (E) { E->user_paused = p; E->set_paused(); } }

// a setting changed while playing (the in-game menu): the look, the sound and the line gaps follow at once
JNIEXPORT void JNICALL FN(nativeSet)(JNIEnv *env, jclass, jstring k, jstring v)
{
    if (!E) return;
    std::lock_guard<std::mutex> g(E->cm);
    std::string key = str(env, k), val = v ? str(env, v) : "";
    if (v) E->conf[key] = val; else E->conf.erase(key);
    E->conf_version++;
}

JNIEXPORT jstring JNICALL FN(nativeStats)(JNIEnv *env, jclass)
{
    if (!E) return env->NewStringUTF("");
    std::lock_guard<std::mutex> g(E->sm);
    return env->NewStringUTF(E->stats.c_str());
}

// switching off (Settings > Switch off when quitting): the picture's zap or collapse and the switch's click
JNIEXPORT void JNICALL FN(nativePowerOff)(JNIEnv *, jclass) { if (E) { E->off = true; E->user_paused = false; E->app_paused = false; E->set_paused(); } }

JNIEXPORT void JNICALL FN(nativeStop)(JNIEnv *, jclass)
{
    if (!E) return;
    E->stop();
    LOGI("stopped, %ld sound underruns", (long) E->underruns);
}

JNIEXPORT jstring JNICALL FN(nativeRenderer)(JNIEnv *env, jclass) { return env->NewStringUTF(E ? E->renderer_name.c_str() : ""); }

}  // extern "C"
