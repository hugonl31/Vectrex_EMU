// gles_backend.cpp - the FramePlan on OpenGL ES 3.1 (see backend.h). Every pass's parameters go into
// one uniform buffer per display frame (one upload, then a range bound per draw).
#include "backend.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <android/log.h>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VectrexEMU", __VA_ARGS__)

namespace vx {
namespace {

struct Target { GLuint tex = 0, fb = 0; int w = 0, h = 0; };

class Gles : public Backend {
public:
    ~Gles() override { destroy(); }
    const char *name() const override { return "OpenGL ES 3.1"; }
    bool float_images() const override { return float_ok; }

    bool init(ANativeWindow *win, std::string &err) override
    {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) { err = "no EGL display"; return false; }
        const EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_DEPTH_SIZE, 0, EGL_NONE };
        EGLConfig cfg; EGLint n = 0;
        if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1) { err = "no EGL config"; return false; }
        const EGLint xattr[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 1, EGL_NONE };
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr);
        if (ctx == EGL_NO_CONTEXT) { err = "no OpenGL ES 3.1 context"; return false; }
        surf = eglCreateWindowSurface(dpy, cfg, win, nullptr);
        if (surf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, surf, surf, ctx)) { err = "no EGL window surface"; return false; }
        eglSwapInterval(dpy, 1);                         // (the display's refresh paces the render thread)
        LOGI("GLES: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
        const char *ext = (const char *) glGetString(GL_EXTENSIONS);
        float_ok = ext && (strstr(ext, "GL_EXT_color_buffer_half_float") || strstr(ext, "GL_EXT_color_buffer_float"));
        if (!(prog_cap = program("capsule.vert", "capsule.frag", err)) || !(prog_up = program("fs.vert", "update.frag", err))
            || !(prog_show = program("fs.vert", "show.frag", err)) || !(prog_blur = program("fs.vert", "blur.frag", err))
            || !(prog_fin = program("fs.vert", "final.frag", err)) || !(prog_art = program("art.vert", "art.frag", err)))
            return false;
        glGenVertexArrays(1, &vao_cap); glGenVertexArrays(1, &vao_empty);
        glGenBuffers(1, &inst_buf); glGenBuffers(1, &ubo);
        GLint al = 256; glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &al); ualign = al > 0 ? al : 256;
        if (ualign < 64) ualign = 64;
        if (glGetError() != GL_NO_ERROR) { err = "OpenGL ES set-up failed"; return false; }
        return true;
    }

    void size(int &w, int &h) override
    {
        EGLint a = 0, b = 0;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &a); eglQuerySurface(dpy, surf, EGL_HEIGHT, &b);
        w = a; h = b;
    }

    void set_art(int w, int h, const std::vector<uint8_t> &rgba) override
    {
        if (art_tex) { glDeleteTextures(1, &art_tex); art_tex = 0; }
        if (w <= 0) return;
        glGenTextures(1, &art_tex); glBindTexture(GL_TEXTURE_2D, art_tex);
        params();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }

    bool render(const FramePlan &p) override
    {
        if (p.tw != tw || p.th != th) targets(p.tw, p.th, p.qw, p.qh);
        // every draw's parameter block, one upload
        ublocks.clear();
        auto add = [&](const float *vp, const float *rest, int nrest) {
            size_t off = ublocks.size();
            ublocks.resize(off + ualign, 0);
            float *d = (float *) &ublocks[off];
            memcpy(d, vp, 16); if (nrest) memcpy(d + 4, rest, nrest * 4);
            return (GLintptr) off;
        };
        std::vector<GLintptr> ub_cap;
        for (auto &b : p.batches) { float t[4] = { b.timed, b.age0, b.dtw, b.gain }; ub_cap.push_back(add(p.cap_vp, t, 4)); }
        float zero[4] = { 0, 0, 0, 0 };
        GLintptr ub_up = add(zero, p.update, 12), ub_show = add(zero, p.show, 8), ub_blur[4];
        for (int i = 0; i < 4; i++) ub_blur[i] = add(zero, p.blur[i], 4);
        float fvp[4] = { 1.f, (float) p.screen_h, (float) p.screen_w, 0 };
        GLintptr ub_fin = add(fvp, p.fin + 4, 12), ub_art[2];
        float avp[4] = { -1.f, 0, (float) p.screen_w, (float) p.screen_h };
        for (int i = 0; i < 2; i++) { float r[8]; memcpy(r, p.art_rect, 16); memcpy(r + 4, p.art_a[i], 16); ub_art[i] = add(avp, r, 8); }
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferData(GL_UNIFORM_BUFFER, ublocks.size(), ublocks.data(), GL_STREAM_DRAW);
        auto bind_ub = [&](GLintptr off) { glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, off, ualign); };
        if (!p.inst.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, inst_buf);
            glBufferData(GL_ARRAY_BUFFER, p.inst.size() * sizeof(Instance), p.inst.data(), GL_STREAM_DRAW);
        }

        // 1. the fresh light (phosphor) / the crisp lines (into D)
        Target &cap_to = p.phosphor ? F : D;
        glBindFramebuffer(GL_FRAMEBUFFER, cap_to.fb); glViewport(0, 0, tw, th);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        if (p.phosphor) { glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE); glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX); }
        else { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glBlendEquation(GL_FUNC_ADD); }
        if (!p.batches.empty()) {
            glUseProgram(prog_cap); glBindVertexArray(vao_cap); glBindBuffer(GL_ARRAY_BUFFER, inst_buf);
            glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
            glVertexAttribDivisor(0, 1); glVertexAttribDivisor(1, 1);
            for (size_t i = 0; i < p.batches.size(); i++) {
                const Batch &b = p.batches[i];
                if (!b.count) continue;
                bind_ub(ub_cap[i]);
                size_t o = b.first * sizeof(Instance);
                glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), (const void *) o);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), (const void *) (o + 16));
                glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, b.count);
            }
        }
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_BLEND);
        glBindVertexArray(vao_empty);
        if (p.phosphor) {
            // 2. the phosphor image aged and lit again
            fs(P2, prog_up, ub_up, P.tex, F.tex, bind_ub);
            std::swap(P, P2);
            // 3. what it shows
            fs(D, prog_show, ub_show, P.tex, 0, bind_ub);
        }
        // 4. the bloom
        for (int i = 0; i < p.blur_passes; i++)
            fs((i & 1) ? B1 : B2, prog_blur, ub_blur[i], i == 0 ? D.tex : (i & 1) ? B2.tex : B1.tex, 0, bind_ub);
        // 5. the screen
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, p.screen_w, p.screen_h);
        glUseProgram(prog_fin); bind_ub(ub_fin);
        tex(1, D.tex); tex(2, B1.tex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // 6. the overlay: multiply, then add its own light
        if (p.art && art_tex) {
            glUseProgram(prog_art); tex(1, art_tex); glEnable(GL_BLEND);
            bind_ub(ub_art[0]); glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            bind_ub(ub_art[1]); glBlendFunc(GL_ONE, GL_ONE); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisable(GL_BLEND);
        }
        if (!eglSwapBuffers(dpy, surf)) {
            EGLint e = eglGetError();
            return !(e == EGL_BAD_SURFACE || e == EGL_BAD_NATIVE_WINDOW || e == EGL_CONTEXT_LOST);
        }
        return true;
    }

private:
    EGLDisplay dpy = EGL_NO_DISPLAY; EGLContext ctx = EGL_NO_CONTEXT; EGLSurface surf = EGL_NO_SURFACE;
    bool float_ok = false;
    GLuint prog_cap = 0, prog_up = 0, prog_show = 0, prog_blur = 0, prog_fin = 0, prog_art = 0;
    GLuint vao_cap = 0, vao_empty = 0, inst_buf = 0, ubo = 0, art_tex = 0;
    int ualign = 256;
    std::vector<uint8_t> ublocks;
    Target P, P2, F, D, B1, B2;
    int tw = 0, th = 0;

    static void params()
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    static void tex(int unit, GLuint t) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); glActiveTexture(GL_TEXTURE0); }

    template <class F_> void fs(Target &to, GLuint prog, GLintptr ub, GLuint t1, GLuint t2, F_ &bind_ub)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, to.fb); glViewport(0, 0, to.w, to.h);
        glUseProgram(prog); bind_ub(ub);
        tex(1, t1); if (t2) tex(2, t2);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    bool make(Target &t, int w, int h)
    {
        free(t);
        t.w = w; t.h = h;
        glGenTextures(1, &t.tex); glBindTexture(GL_TEXTURE_2D, t.tex); params();
        if (float_ok) glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
        else glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
        glGenFramebuffers(1, &t.fb); glBindFramebuffer(GL_FRAMEBUFFER, t.fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        return ok;
    }
    void free(Target &t)
    {
        if (t.tex) glDeleteTextures(1, &t.tex);
        if (t.fb) glDeleteFramebuffers(1, &t.fb);
        t = Target();
    }
    void targets(int w, int h, int qw, int qh)
    {
        tw = w; th = h;
        bool ok = make(P, w, h) && make(P2, w, h) && make(F, w, h) && make(D, w, h) && make(B1, qw, qh) && make(B2, qw, qh);
        if (!ok && float_ok) {                           // (16-bit float images not drawable here: 8-bit, a shorter afterglow)
            LOGI("GLES: float images incomplete, 8-bit images");
            float_ok = false; targets(w, h, qw, qh);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    GLuint compile(GLenum kind, const char *nm, std::string &err)
    {
        const ShaderSrc &s = shader(nm);
        GLuint sh = glCreateShader(kind);
        glShaderSource(sh, 1, &s.gles, nullptr); glCompileShader(sh);
        GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024] = ""; glGetShaderInfoLog(sh, sizeof log, nullptr, log); err = std::string(nm) + ": " + log; glDeleteShader(sh); return 0; }
        return sh;
    }
    GLuint program(const char *vs, const char *fs_, std::string &err)
    {
        GLuint v = compile(GL_VERTEX_SHADER, vs, err); if (!v) return 0;
        GLuint f = compile(GL_FRAGMENT_SHADER, fs_, err); if (!f) return 0;
        GLuint pr = glCreateProgram();
        glAttachShader(pr, v); glAttachShader(pr, f); glLinkProgram(pr);
        glDeleteShader(v); glDeleteShader(f);
        GLint ok = 0; glGetProgramiv(pr, GL_LINK_STATUS, &ok);
        if (!ok) { char log[1024] = ""; glGetProgramInfoLog(pr, sizeof log, nullptr, log); err = std::string(fs_) + " link: " + log; return 0; }
        return pr;
    }

    void destroy()
    {
        if (dpy == EGL_NO_DISPLAY) return;
        if (ctx != EGL_NO_CONTEXT && surf != EGL_NO_SURFACE) {
            for (Target *t : { &P, &P2, &F, &D, &B1, &B2 }) free(*t);
        }
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
        if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        dpy = EGL_NO_DISPLAY;
    }
};

}  // namespace

std::unique_ptr<Backend> make_gles() { return std::unique_ptr<Backend>(new Gles()); }

}  // namespace vx
