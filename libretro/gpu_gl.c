/* gpu_gl.c - Vectrex_EMU's libretro core: the picture finished on the GPU (OpenGL 3.3 core / OpenGL ES 3.0), included
 * by libretro_vx.c (the RetroArch flavour). The CPU still draws the tube's light (the lines, the afterglow, the glow)
 * into the frame as before; this uploads it and draws it through the desktop Vectrex_EMU's CURVE_SHADER (vecrender.py)
 * - the glass's rounded edge, the ceiling light reflected in the glass, the glass in the room's light, the plastic case
 * round the glass with the vectors mirrored in its glossy walls, its scratches and its matte front (casemodel). With an
 * overlay painted in the case is hidden (as on the desktop: the sheet covers it) and the picture passes through.
 * The picture is also turned here (tate) - no CPU copy - the room's light with it (the screen is turned).
 *
 * No GL headers: the few functions it uses come from the frontend (get_proc_address), the same on every system.
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include "gpu_tab.h"

typedef unsigned int GLuint; typedef int GLint; typedef unsigned int GLenum; typedef int GLsizei; typedef float GLfloat;
typedef char GLchar; typedef unsigned char GLboolean; typedef unsigned int GLbitfield;
#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif
#define GLFN(ret, name, args) typedef ret (GLAPIENTRY *pf_##name) args; static pf_##name name##_
GLFN(void, glGenTextures, (GLsizei, GLuint *)); GLFN(void, glDeleteTextures, (GLsizei, const GLuint *));
GLFN(void, glBindTexture, (GLenum, GLuint)); GLFN(void, glActiveTexture, (GLenum));
GLFN(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
GLFN(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *));
GLFN(void, glTexParameteri, (GLenum, GLenum, GLint)); GLFN(void, glPixelStorei, (GLenum, GLint));
GLFN(GLuint, glCreateShader, (GLenum)); GLFN(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
GLFN(void, glCompileShader, (GLuint)); GLFN(void, glGetShaderiv, (GLuint, GLenum, GLint *));
GLFN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)); GLFN(void, glDeleteShader, (GLuint));
GLFN(GLuint, glCreateProgram, (void)); GLFN(void, glAttachShader, (GLuint, GLuint)); GLFN(void, glLinkProgram, (GLuint));
GLFN(void, glGetProgramiv, (GLuint, GLenum, GLint *)); GLFN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *));
GLFN(void, glDeleteProgram, (GLuint)); GLFN(void, glUseProgram, (GLuint));
GLFN(GLint, glGetUniformLocation, (GLuint, const GLchar *)); GLFN(void, glUniform1i, (GLint, GLint));
GLFN(void, glUniform1f, (GLint, GLfloat)); GLFN(void, glUniform2f, (GLint, GLfloat, GLfloat));
GLFN(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)); GLFN(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat));
GLFN(void, glBindFramebuffer, (GLenum, GLuint)); GLFN(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
GLFN(void, glDisable, (GLenum)); GLFN(void, glDrawArrays, (GLenum, GLint, GLsizei));
GLFN(void, glGenVertexArrays, (GLsizei, GLuint *)); GLFN(void, glBindVertexArray, (GLuint));
GLFN(void, glDeleteVertexArrays, (GLsizei, const GLuint *)); GLFN(GLenum, glGetError, (void));
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RG 0x8227
#define GL_RG8 0x822B
#define GL_RG16F 0x822F
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_TRIANGLES 0x0004
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_CULL_FACE 0x0B44
#define GL_STENCIL_TEST 0x0B90

static struct retro_hw_render_callback hwr;
static int gpu_asked;                         /* (the frontend took the request: the picture goes through the GPU) */
static int gpu_ok;                            /* (the context is there and the program built) */
static int gpu_es;                            /* (OpenGL ES) */
static GLuint g_prog, g_tex, g_lmap, g_smap, g_vao; static int g_tw, g_th, g_lmap_ok, g_smap_ok;
enum { U_TEX, U_LMAP, U_SMAP, U_ROT, U_CROP, U_NORM, U_PXS, U_BSCALE, U_FX, U_GLASS, U_EDGE, U_GREY, U_LK, U_LPOS, U_LMR,
       U_GSC, U_CSE, U_CROOM, U_CREFL, U_CWEAR, U_LC, U_SMR, U_LCA, U_LSA, U_N };
static GLint g_u[U_N];
static const char *const g_un[U_N] = { "tex", "lmap", "smap", "rot", "crop", "norm", "pxs", "bscale", "fx", "glass", "edge",
	"grey", "lk", "lpos", "lmr", "gsc", "cse", "croom", "crefl", "cwear", "lc", "smr", "lca", "lsa" };
/* what the picture shows this refresh (set by retro_run from the options and the profile) */
static struct { int fx, glass, ceiling, cse; float grey, lk, croom, crefl, cwear; } gfx;

static const char *GPU_VS =
	"out vec2 vuv;\n"
	"void main() { vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);\n"
	"  vuv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";

/* vecrender.py LIGHT_GLSL + CURVE_SHADER (2026-11-15), the tube's bending left out (the CPU bends the lines) and the
 * head tracking's shifts zero; the picture's texture holds the whole overlay sheet (y up, 0..1) */
static const char *GPU_FS_A =
	"in vec2 vuv; out vec4 fragc;\n"
	"uniform sampler2D tex, lmap, smap;\n"
	"uniform int rot, fx;\n"
	"uniform vec4 crop;\n"
	"uniform vec2 norm;\n"
	"uniform float pxs, bscale, glass, edge, grey, lk;\n"
	"uniform vec3 lpos; uniform vec4 lmr; uniform vec2 gsc;\n"
	"uniform float cse, croom, crefl, cwear; uniform vec3 lc; uniform vec4 smr;\n"
	"uniform float lca, lsa;\n"
	"const float glut[91] = float[91](" GPU_GLASS_LUT ");\n"
	"const float clut[91] = float[91](" GPU_CASE_LUT ");\n"
	"vec3 picAt(vec2 s) { return texture(tex, vec2(s.x, 1.0 - s.y)).bgr; }\n"   /* (the frame's XRGB bytes: B G R) */
	"vec3 picQ(vec2 q) { return picAt(vec2(0.5) + vec2(q.x, -q.y) / norm); }\n" /* (q: units round the centre, y down) */
	"const float HZ = 0.80, CH = 2.4, FIX_W = 0.30, FIX_L = 1.2;\n"
	"const vec3 LIGHTC = vec3(1.0, 0.94, 0.894);\n"
	"float ceiling(vec2 p, float soft) {\n"
	"    float den = max(HZ - p.y, 0.05), vis = step(0.05, HZ - p.y);\n"
	"    float D = CH / den, D0 = CH / HZ;\n"
	"    float X = p.x * D / 2.0, Zr = D0 - D;\n"
	"    float Xr = lca * X - lsa * Zr, Z = lsa * X + lca * Zr;\n"
	"    float wx = soft + 0.8 * fwidth(Xr), wz = soft + 0.8 * fwidth(Z);\n"
	"    float ins = (1.0 - smoothstep(FIX_W - wx, FIX_W + wx, abs(Xr))) * (1.0 - smoothstep(FIX_L - wz, FIX_L + wz, abs(Z)));\n"
	"    float fr = max(smoothstep(FIX_W - 0.05 - wx, FIX_W - 0.05 + wx, abs(Xr)), smoothstep(FIX_L - 0.05 - wz, FIX_L - 0.05 + wz, abs(Z)));\n"
	"    float cx = (Xr + FIX_W) / (2.0 * FIX_W) * 2.0, cz = (Z + FIX_L) / (2.0 * FIX_L) * 9.0;\n"
	"    float fx_ = fract(cx), fz = fract(cz);\n"
	"    float bx = min(wx / FIX_W, 0.45), bz = min(wz / 0.267, 0.45);\n"
	"    float blade = max(smoothstep(0.44 - bx, 0.44 + bx, abs(fx_ - 0.5)), smoothstep(0.42 - bz, 0.42 + bz, abs(fz - 0.5)));\n"
	"    float cell = 0.85 + 0.15 * pow(max(1.0 - abs(fx_ - 0.5) * 2.0, 0.0), 0.6);\n"
	"    float val = mix(mix(cell, 0.18, blade), 0.12, fr);\n"
	"    float fall = clamp(pow(D0 / D, 0.7), 0.75, 1.25) / 1.25;\n"
	"    return vis * ins * val * fall;\n"
	"}\n"
	"vec2 reflp(vec2 uv, float curv) {\n"
	"    vec3 n = normalize(vec3(curv * uv, 1.0));\n"
	"    float dn = -n.z;\n"
	"    vec3 r = vec3(-2.0 * dn * n.x, -2.0 * dn * n.y, -1.0 - 2.0 * dn * n.z);\n"
	"    return uv * 0.9 + r.xy / r.z;\n"
	"}\n"
	"float hash(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }\n"
	"float vnoise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
	"    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y); }\n"
	"float bumps(vec2 p) { return (vnoise(p - 0.35) - vnoise(p + 0.35)) * 4.0; }\n"
	"vec2 crom(float p0, float p1, float p2, float p3, float f) {\n"
	"    float a = -p0 + p2, b = 2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3, c = -p0 + 3.0 * p1 - 3.0 * p2 + p3;\n"
	"    return 0.5 * vec2(2.0 * p1 + f * (a + f * (b + f * c)), a + f * (2.0 * b + 3.0 * f * c)); }\n"
	"int mi(int i) { return i < 0 ? -i : (i > 90 ? 180 - i : i); }\n"
	"vec2 gR(float a) { float i = min(floor(a), 89.0); int k = int(i); return crom(glut[mi(k - 1)], glut[k], glut[k + 1], glut[mi(k + 2)], a - i); }\n"
	"vec2 cR(float a) { float i = min(floor(a), 89.0); int k = int(i); return crom(clut[mi(k - 1)], clut[k], clut[k + 1], clut[mi(k + 2)], a - i); }\n"
	"float sdist(vec2 d, vec2 R) { float dr = R.y * 57.29578; return (length(d) - R.x) * R.x / sqrt(R.x * R.x + dr * dr); }\n"
	"float ang(vec2 d) { return degrees(atan(abs(d.y), abs(d.x) + 1e-9)); }\n"
	"float mfade(vec2 mc) { vec2 o = max(-mc, mc - 1.0); float d = length(max(o, 0.0)); return exp(-d * d / 0.012); }\n"
	"float sdG(vec2 d) { return sdist(d, gR(ang(d))); }\n"
	"float sdC(vec2 d) { vec2 q = abs(d) - vec2(0.69188 - 0.045826, 0.89275 - 0.045826);\n"
	"    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.045826; }\n"
	"const float U = 919.67, FRONT_W = 0.03125;\n"
	"vec3 tex9(vec2 q, float rad) {\n"
	"    vec2 o = vec2(rad / U) / norm, s = vec2(0.5) + vec2(q.x, -q.y) / norm;\n"
	"    vec3 a = picAt(s) * 2.0;\n"
	"    a += picAt(s + vec2(o.x, 0.0)) + picAt(s - vec2(o.x, 0.0));\n"
	"    a += picAt(s + vec2(0.0, o.y)) + picAt(s - vec2(0.0, o.y));\n"
	"    a += 0.7 * (picAt(s + o) + picAt(s - o) + picAt(s + vec2(o.x, -o.y)) + picAt(s - vec2(o.x, -o.y)));\n"
	"    return a / 8.8; }\n"
	"vec3 disc(vec2 q, float rad) {\n"
	"    vec2 s = vec2(0.5) + vec2(q.x, -q.y) / norm;\n"
	"    float rot_ = 6.2832 * hash(gl_FragCoord.xy);\n"
	"    vec3 a = vec3(0.0); float ws = 0.0;\n"
	"    for (int i = 0; i < 40; i++) {\n"
	"        float f = (float(i) + 0.5) / 40.0, r = rad * sqrt(f), an = rot_ + 2.39996 * float(i);\n"
	"        float w = exp(-1.5 * f);\n"
	"        a += w * picAt(s + vec2(cos(an), sin(an)) * r / U / norm); ws += w;\n"
	"    }\n"
	"    return a / ws; }\n";
static const char *GPU_FS_B =
	"vec3 plastic(vec2 gd, vec2 tgt) {\n"
	"    float pu = pxs;\n"
	"    float dg = sdG(gd), dc = -sdC(gd);\n"
	"    vec2 rp = gd * U;\n"
	"    float de = dc + pu * 1.6 * (0.5 - 0.65 * vnoise(rp / 9.0 + 41.0) - 0.35 * vnoise(rp / 2.5 + 13.0));\n"
	"    float wa = clamp(dg / pu + 0.5, 0.0, 1.0) * clamp(de / pu + 0.5, 0.0, 1.0);\n"
	"    float fa = clamp(0.5 - de / pu, 0.0, 1.0);\n"
	"    float wave = 2.0 * vnoise(rp / 110.0) - 1.0 + 0.5 * (2.0 * vnoise(rp / 40.0 + 17.0) - 1.0);\n"
	"    float flow = 2.0 * vnoise(rp / 14.0 + 5.0) - 1.0;\n"
	"    vec3 col = vec3(0.0);\n"
	"    if (wa > 0.0) {\n"
	"        float th = atan(gd.y, gd.x), a = ang(gd), sg = sign(gd.x * gd.y);\n"
	"        vec2 G = gR(a), C = cR(a);\n"
	"        float Rg = G.x, D = max(C.x - G.x, 1e-4), rho = length(gd);\n"
	"        float dRg = G.y * 57.29578 * sg, dD = (C.y - G.y) * 57.29578 * sg;\n"
	"        float u = clamp((rho - Rg) / D, 0.0, 1.0);\n"
	"        float k = 1.9712 * D - 0.09864, q = sqrt(k * k + 0.0004);\n"
	"        float H = 0.10123 + 0.5 * (k + q), dH = 0.9856 * (1.0 + k / q) * dD;\n"
	"        float cs = cos(th), sn = sin(th);\n"
	"        vec3 Pu = vec3(D * cs, D * sn, H);\n"
	"        float dr = dRg + u * dD;\n"
	"        vec3 Pt = vec3(dr * cs - rho * sn, dr * sn + rho * cs, u * dH);\n"
	"        vec3 N = normalize(cross(Pt, Pu)); N *= sign(N.z);\n"
	"        vec2 n = normalize(gd);\n"
	"        float wave2 = 2.0 * vnoise(rp / 70.0 + 31.0) - 1.0 + 0.4 * (2.0 * vnoise(rp / 26.0 + 9.0) - 1.0);\n"
	"        N = normalize(N + vec3(0.02 * wave + 0.005 * flow, 0.02 * wave2 + 0.005 * flow, 0.0));\n"
	"        float lip = smoothstep(0.93, 1.0, u);\n"
	"        N = normalize(mix(N, vec3(0.0, 0.0, 1.0), 0.85 * lip));\n"
	"        float dpos = (rho - Rg) * U, dout = max(de, 0.0) * U;\n"
	"        vec3 P = vec3(tgt, H * u), E = vec3(0.0, 0.0, 50.0 / 11.0);\n"
	"        vec3 Rr = reflect(normalize(P - E), N);\n"
	"        vec3 refl = vec3(0.0); float gm = 0.0, back = 0.0;\n"
	"        if (crefl > 0.0 && Rr.z < -0.02) {\n"
	"            vec2 mq = P.xy + Rr.xy * (-P.z / Rr.z);\n"
	"            back = length(mq - P.xy) * U;\n"
	"            gm = clamp(0.5 - sdG(mq) / pu, 0.0, 1.0);\n"
	"            refl = max(mix(disc(mq, 9.0), disc(mq, 6.0), 0.5) * gm - 0.02, 0.0);\n"
	"        }\n"
	"        float fall = 1.0 / (1.0 + pow(back / 130.0, 2.0));\n"
	"        float side = abs(n.x), bot = max(n.y, 0.0), top = max(-n.y, 0.0);\n"
	"        float strength = crefl * 1.5 * (0.85 * pow(side, 1.5) + 0.40 * bot + 0.35 * top) * fall * (1.0 + 0.06 * flow);\n"
	"        vec3 Nu = vec3(N.x, -N.y, N.z);\n"
	"        vec3 Pc = vec3(tgt.x * 11.0, -tgt.y * 11.0, H * u * 11.0);\n"
	"        vec3 V = normalize(vec3(0.0, 0.0, 50.0) - Pc), L = normalize(lc - Pc);\n"
	"        float lit = croom * pow(clamp((dot(Nu, L) - 0.55) / 0.45, 0.0, 1.0), 1.4);\n"
	"        float ndh = clamp(dot(Nu, normalize(V + L)), 0.0, 1.0);\n"
	"        float spec = croom * 0.35 * pow(ndh, 60.0);\n"
	"        float base = 0.006 + 0.002 * flow + 0.010 * lit + spec;\n"
	"        float bl = 0.0;\n"
	"        if (lk > 0.0) {\n"
	"            vec2 eq = tgt - n * dpos / U;\n"
	"            vec2 eqn = eq / gsc;\n"
	"            vec2 mq = vec2((eqn.x - lmr.x) / (lmr.y - lmr.x), (eqn.y - lmr.z) / (lmr.w - lmr.z));\n"
	"            float mr = texture(lmap, mq).r * mfade(mq);\n"
	"            bl = lk * mr * (0.03 * exp(-dpos / 55.0) * (0.25 + 0.75 * pow(top, 0.7)) + 0.035 * exp(-dpos / 16.5) * pow(top, 0.7));\n"
	"        }\n"
	"        vec2 sq = vec2((gd.x - smr.x) / (smr.y - smr.x), (gd.y - smr.z) / (smr.w - smr.z));\n"   /* (the map: top row first) */
	"        vec2 sc = texture(smap, sq).rg;\n"
	"        float lv = clamp(3.0 * dot(tex9(tgt - n * (dpos + 25.0) / U, 30.0), vec3(0.333)), 0.0, 1.0);\n"
	"        float scr = cwear * 0.9 * (0.5 * sc.r * lv + sc.g * (0.6 * lit + 0.8 * bl * 30.0));\n"
	"        col = vec3(base) + refl * strength * vec3(0.85, 0.92, 1.0) + 0.35 * scr * vec3(0.95, 0.97, 1.0)\n"
	"            + bl * vec3(0.97, 0.98, 1.0) + exp(-pow(dout / 2.0, 2.0)) * 0.26 * lit;\n"
	"        col *= mix(0.2, 1.0, smoothstep(0.0, 0.06, u));\n"
	"        col *= wa;\n"
	"    }\n"
	"    float fr = fa * smoothstep(0.002, 0.007, FRONT_W + dc);\n"
	"    if (fr > 0.0) {\n"
	"        vec2 cell = floor(rp / 1.1);\n"
	"        float grain = (vnoise(rp / 2.0 + 5.3) - 0.5) * 0.13 + (hash(cell) - 0.5) * 0.05;\n"
	"        float acr = -de * U, mid = 0.5 * FRONT_W * U;\n"
	"        float band = exp(-pow((acr - mid - 6.0 * (vnoise(rp / 300.0 + 4.0) - 0.5)) / 3.0, 2.0));\n"
	"        float marks = 0.5 + 0.5 * vnoise(vec2(acr / 0.9, 0.0) + rp / 500.0);\n"
	"        float cornw = 1.0 - 0.85 * exp(-pow((ang(gd) - 55.0) / 9.0, 2.0));\n"
	"        float sidew = mix(1.0, 0.3, smoothstep(45.0, 65.0, ang(gd)));\n"
	"        float wear = min(1.0, cwear) * band * marks * cornw * sidew * smoothstep(0.58, 0.82, vnoise(rp / 340.0 + 17.0));\n"
	"        grain *= 1.0 - 0.65 * wear;\n"
	"        float mott = (vnoise(rp / 55.0 + 23.0) - 0.5) * 0.04;\n"
	"        float tone = 0.010 + 0.09 * croom;\n"
	"        float sp = hash(cell + 91.7), spark = step(0.982, sp) * (0.5 + hash(cell + 3.3));\n"
	"        vec3 tint = mix(vec3(1.0, 0.92, 0.75), vec3(0.75, 0.9, 1.0), hash(cell + 7.9));\n"
	"        float rim = exp(-pow(max(-de, 0.0) * U / 3.0, 2.0)) * 0.2;\n"
	"        float gloss = smoothstep(0.2, 0.85, 0.6 * vnoise(rp / 420.0 + 83.0) + 0.4 * vnoise(rp / 900.0 + 37.0));\n"
	"        vec3 Pc = vec3(tgt.x * 11.0, -tgt.y * 11.0, 1.4);\n"
	"        vec3 Hf = normalize(normalize(vec3(0.0, 0.0, 50.0) - Pc) + normalize(lc - Pc));\n"
	"        float sheen = croom * (0.014 + 0.07 * gloss) * pow(Hf.z, 6.0) * (1.0 + 3.0 * grain);\n"
	"        float drift = 0.85 + 0.3 * vnoise(rp / 700.0 + 29.0);\n"
	"        sheen *= 0.2 + 1.6 * vnoise(rp / 600.0 + 74.0);\n"
	"        sheen *= mix(1.2, 0.45, smoothstep(45.0, 65.0, ang(gd)) * clamp(-gd.y / 0.3, 0.0, 1.0));\n"
	"        sheen += croom * 0.024 * wear * pow(Hf.z, 6.0);\n"
	"        col += (vec3(drift * tone * (1.0 + grain + mott + rim)) * vec3(0.98, 0.99, 1.02) + sheen\n"
	"              + croom * (0.01 + 0.04 * gloss) * spark * tint) * fr;\n"
	"    }\n"
	"    return col + (hash(gl_FragCoord.xy) - hash(gl_FragCoord.xy + 7.31)) / 255.0;\n"
	"}\n"
	"void main() {\n"
	"    vec2 a = vuv;\n"                                                   /* (the picture turned: its own point, y up) */
	"    vec2 q = rot == 1 ? vec2(a.y, 1.0 - a.x) : rot == 2 ? vec2(1.0 - a.x, 1.0 - a.y) : rot == 3 ? vec2(1.0 - a.y, a.x) : a;\n"
	"    vec2 p = crop.xy + q * (crop.zw - crop.xy);\n"                     /* (in the whole sheet) */
	"    vec3 c = picAt(p);\n"
	"    if (fx != 0) {\n"
	"        vec2 tgt = vec2((p.x - 0.5) * norm.x, -(p.y - 0.5) * norm.y);\n"   /* (units round the centre, y down) */
	"        vec2 gd = tgt;\n"
	"        float g = 1.0;\n"
	"        if (glass > 0.5) {\n"
	"            float ga = degrees(atan(abs(gd.y), abs(gd.x) + 1e-9));\n"
	"            float r = length(gd) / gR(ga).x;\n"
	"            g = smoothstep(0.0, 1.0, clamp((1.0 - r) / edge, 0.0, 1.0));\n"
	"        } else g = sdG(gd) < 0.0 ? 1.0 : 0.0;\n"                       /* (the case needs the glass's outline: a hard edge) */
	"        vec3 add = vec3(0.0);\n"
	"        vec2 tq = tgt / gsc;\n"
	"        if (grey > 0.0 || lk > 0.0) {\n"
	"            float gr = bumps(gl_FragCoord.xy * bscale);\n"
	"            add += grey * (1.0 - 0.25 * tq.y) * (1.0 + 0.005 * gr) * vec3(0.92, 0.95, 1.0);\n"
	"            if (lk > 0.0) {\n"
	"                vec2 mc = vec2((tq.x - lmr.x) / (lmr.y - lmr.x), (tq.y - lmr.z) / (lmr.w - lmr.z));\n"
	"                vec4 m = texture(lmap, mc) * mfade(mc);\n"
	"                float sharp = ceiling((reflp(tq, 0.2) - lpos.xy) / lpos.z, 0.012);\n"
	"                add += lk * (m.g * (1.0 + 0.018 * gr) * vec3(0.95, 0.96, 1.0) + 0.88 * (0.98 * sharp + m.r) * LIGHTC);\n"
	"            }\n"
	"        }\n"
	"        c = sqrt(c * c + add * add) * g;\n"
	"        if (cse > 0.5) c += sdG(gd) > -3.0 * pxs ? plastic(gd, tgt) : vec3((hash(gl_FragCoord.xy) - hash(gl_FragCoord.xy + 7.31)) / 255.0);\n"
	"    }\n"
	"    fragc = vec4(c, 1.0);\n"
	"}\n";

static void (*gpu_log) (const char *);
static GLuint gpu_shader (GLenum type, const char *const *src, int n)
{
	GLuint s = glCreateShader_ (type); glShaderSource_ (s, n, src, NULL); glCompileShader_ (s);
	GLint ok = 0; glGetShaderiv_ (s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[2048] = ""; glGetShaderInfoLog_ (s, sizeof log, NULL, log); fprintf (stderr, "[vectrex_emu] GPU: shader: %s\n", log); glDeleteShader_ (s); return 0; }
	return s;
}
#define GLLOAD(n) (n##_ = (pf_##n) hwr.get_proc_address (#n), ok &= n##_ != NULL)
static void gpu_reset (void)
{
	int ok = 1; gpu_ok = 0;
	GLLOAD (glGenTextures); GLLOAD (glDeleteTextures); GLLOAD (glBindTexture); GLLOAD (glActiveTexture); GLLOAD (glTexImage2D);
	GLLOAD (glTexSubImage2D); GLLOAD (glTexParameteri); GLLOAD (glPixelStorei); GLLOAD (glCreateShader); GLLOAD (glShaderSource);
	GLLOAD (glCompileShader); GLLOAD (glGetShaderiv); GLLOAD (glGetShaderInfoLog); GLLOAD (glDeleteShader); GLLOAD (glCreateProgram);
	GLLOAD (glAttachShader); GLLOAD (glLinkProgram); GLLOAD (glGetProgramiv); GLLOAD (glGetProgramInfoLog); GLLOAD (glDeleteProgram);
	GLLOAD (glUseProgram); GLLOAD (glGetUniformLocation); GLLOAD (glUniform1i); GLLOAD (glUniform1f); GLLOAD (glUniform2f);
	GLLOAD (glUniform3f); GLLOAD (glUniform4f); GLLOAD (glBindFramebuffer); GLLOAD (glViewport); GLLOAD (glDisable);
	GLLOAD (glDrawArrays); GLLOAD (glGenVertexArrays); GLLOAD (glBindVertexArray); GLLOAD (glDeleteVertexArrays); GLLOAD (glGetError);
	if (!ok) { fprintf (stderr, "[vectrex_emu] GPU: the frontend lacks GL functions - the CPU's picture\n"); return; }
	const char *hdr = gpu_es ? "#version 300 es\nprecision highp float;\nprecision highp int;\n" : "#version 330 core\n";
	const char *vs[2] = { hdr, GPU_VS }, *fs[3] = { hdr, GPU_FS_A, GPU_FS_B };
	GLuint v = gpu_shader (GL_VERTEX_SHADER, vs, 2), f = gpu_shader (GL_FRAGMENT_SHADER, fs, 3);
	if (!v || !f) return;
	g_prog = glCreateProgram_ (); glAttachShader_ (g_prog, v); glAttachShader_ (g_prog, f); glLinkProgram_ (g_prog);
	glDeleteShader_ (v); glDeleteShader_ (f);
	{ GLint lk = 0; glGetProgramiv_ (g_prog, GL_LINK_STATUS, &lk);
	  if (!lk) { char log[2048] = ""; glGetProgramInfoLog_ (g_prog, sizeof log, NULL, log); fprintf (stderr, "[vectrex_emu] GPU: link: %s\n", log); return; } }
	for (int i = 0; i < U_N; i++) g_u[i] = glGetUniformLocation_ (g_prog, g_un[i]);
	glGenVertexArrays_ (1, &g_vao);
	GLuint t[3]; glGenTextures_ (3, t); g_tex = t[0]; g_lmap = t[1]; g_smap = t[2]; g_tw = g_th = 0;
	for (int i = 0; i < 3; i++) { glBindTexture_ (GL_TEXTURE_2D, t[i]);
		glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
	glPixelStorei_ (GL_UNPACK_ALIGNMENT, 1);
	/* the data from the desktop's models (make_gpu_assets.py): <system>/vectrex_emu/gpu - without them, no ceiling light /
	 * no scratches (the rest is drawn) */
	char p[1700]; g_lmap_ok = g_smap_ok = 0;
	{ snprintf (p, sizeof p, "%s" SEP "vectrex_emu" SEP "gpu" SEP "glass_light.bin", g_sys); FILE *fl = fopen (p, "rb");
	  const int n = GPU_LMAP_W * GPU_LMAP_H; float *rg = fl ? malloc ((size_t) n * 8) : NULL, *a = fl ? malloc ((size_t) n * 8) : NULL;
	  if (fl && rg && a && fread (a, 4, (size_t) n * 2, fl) == (size_t) n * 2) {
		for (int i = 0; i < n; i++) { rg[2 * i] = a[i]; rg[2 * i + 1] = a[n + i]; }
		glBindTexture_ (GL_TEXTURE_2D, g_lmap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RG16F, GPU_LMAP_W, GPU_LMAP_H, 0, GL_RG, GL_FLOAT, rg); g_lmap_ok = 1; }
	  else fprintf (stderr, "[vectrex_emu] GPU: no %s - no ceiling light\n", p);
	  if (fl) fclose (fl); free (rg); free (a); }
	{ snprintf (p, sizeof p, "%s" SEP "vectrex_emu" SEP "gpu" SEP "case_scratch.png", g_sys); int w, h, n;
	  unsigned char *im = stbi_load (p, &w, &h, &n, 4);
	  if (im) { glBindTexture_ (GL_TEXTURE_2D, g_smap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im); stbi_image_free (im); g_smap_ok = 1; }
	  else { static const unsigned char z[4] = { 0, 0, 0, 255 }; glBindTexture_ (GL_TEXTURE_2D, g_smap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, z);
	         fprintf (stderr, "[vectrex_emu] GPU: no %s - no case scratches\n", p); } }
	if (!g_lmap_ok) { static const float z[2] = { 0, 0 }; glBindTexture_ (GL_TEXTURE_2D, g_lmap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RG16F, 1, 1, 0, GL_RG, GL_FLOAT, z); }
	gpu_ok = 1;
	fprintf (stderr, "[vectrex_emu] GPU: %s - the glass and the case drawn there\n", gpu_es ? "OpenGL ES 3" : "OpenGL 3.3");
}
static void gpu_destroy (void) { gpu_ok = 0; g_prog = g_tex = g_lmap = g_smap = g_vao = 0; g_tw = g_th = 0; }

/* asked for at a game's start (RetroArch sets the context up after it): OpenGL ES 3 on Android, else OpenGL 3.3 core
 * (and ES 3 where that is all there is) */
static int gpu_request (void)
{
	memset (&hwr, 0, sizeof hwr);
	hwr.context_reset = gpu_reset; hwr.context_destroy = gpu_destroy; hwr.bottom_left_origin = true; hwr.depth = false; hwr.stencil = false;
	const int tries[2][3] = {
#if defined(__ANDROID__)
		{ RETRO_HW_CONTEXT_OPENGLES3, 3, 0 }, { RETRO_HW_CONTEXT_OPENGL_CORE, 3, 3 } };
#else
		{ RETRO_HW_CONTEXT_OPENGL_CORE, 3, 3 }, { RETRO_HW_CONTEXT_OPENGLES3, 3, 0 } };
#endif
	for (int k = 0; k < 2; k++) {
		hwr.context_type = (enum retro_hw_context_type) tries[k][0]; hwr.version_major = (unsigned) tries[k][1]; hwr.version_minor = (unsigned) tries[k][2];
		if (env_cb (RETRO_ENVIRONMENT_SET_HW_RENDER, &hwr)) { gpu_es = tries[k][0] == RETRO_HW_CONTEXT_OPENGLES3; return 1; } }
	fprintf (stderr, "[vectrex_emu] GPU: the frontend gave no OpenGL - the CPU's picture\n");
	return 0;
}

/* the picture: the frame (the whole sheet, XRGB, pitch in pixels) up, drawn into the frontend's framebuffer turned and
 * cut to the area sent (crop: the sheet's share, y up), with the glass and the case on top */
static void gpu_present (const uint32_t *frame, int fw, int fh, int pitch, float cx0, float cy0, float cx1, float cy1, int ow, int oh, int rot)
{
	glBindTexture_ (GL_TEXTURE_2D, g_tex);
	if (fw != g_tw || fh != g_th) { glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); g_tw = fw; g_th = fh; }
	glPixelStorei_ (GL_UNPACK_ROW_LENGTH, pitch);
	glTexSubImage2D_ (GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frame);
	glPixelStorei_ (GL_UNPACK_ROW_LENGTH, 0);
	glBindFramebuffer_ (GL_FRAMEBUFFER, (GLuint) hwr.get_current_framebuffer ());
	const int dw = rot & 1 ? oh : ow, dh = rot & 1 ? ow : oh;
	glViewport_ (0, 0, dw, dh);
	glDisable_ (GL_BLEND); glDisable_ (GL_DEPTH_TEST); glDisable_ (GL_SCISSOR_TEST); glDisable_ (GL_CULL_FACE); glDisable_ (GL_STENCIL_TEST);
	glUseProgram_ (g_prog); glBindVertexArray_ (g_vao);
	glActiveTexture_ (GL_TEXTURE0); glBindTexture_ (GL_TEXTURE_2D, g_tex); glUniform1i_ (g_u[U_TEX], 0);
	glActiveTexture_ (GL_TEXTURE0 + 1); glBindTexture_ (GL_TEXTURE_2D, g_lmap); glUniform1i_ (g_u[U_LMAP], 1);
	glActiveTexture_ (GL_TEXTURE0 + 2); glBindTexture_ (GL_TEXTURE_2D, g_smap); glUniform1i_ (g_u[U_SMAP], 2);
	glActiveTexture_ (GL_TEXTURE0);
	const float nx = 2.0f * GPU_ART_AW, ny = 2.0f * GPU_ART_AH;          /* (the sheet in units) */
	glUniform1i_ (g_u[U_ROT], rot); glUniform4f_ (g_u[U_CROP], cx0, cy0, cx1, cy1); glUniform2f_ (g_u[U_NORM], nx, ny);
	glUniform1i_ (g_u[U_FX], gfx.fx);
	const float pxs = ny / (float) fh;                                     /* (one picture pixel, units) */
	glUniform1f_ (g_u[U_PXS], pxs); glUniform1f_ (g_u[U_BSCALE], 1834.0f * pxs / 2.0f / 3.2f);
	glUniform1f_ (g_u[U_GLASS], (float) gfx.glass); glUniform1f_ (g_u[U_EDGE], GPU_EDGE); glUniform1f_ (g_u[U_GREY], gfx.grey);
	glUniform1f_ (g_u[U_LK], g_lmap_ok ? gfx.lk : 0.0f);
	glUniform3f_ (g_u[U_LPOS], GPU_LPOS[0], GPU_LPOS[1], GPU_LPOS[2]); glUniform4f_ (g_u[U_LMR], GPU_LMR[0], GPU_LMR[1], GPU_LMR[2], GPU_LMR[3]);
	glUniform2f_ (g_u[U_GSC], GPU_GSC[0], GPU_GSC[1]);
	glUniform1f_ (g_u[U_CSE], (float) gfx.cse); glUniform1f_ (g_u[U_CROOM], gfx.croom); glUniform1f_ (g_u[U_CREFL], gfx.crefl);
	glUniform1f_ (g_u[U_CWEAR], g_smap_ok ? gfx.cwear : 0.0f); glUniform3f_ (g_u[U_LC], GPU_LC[0], GPU_LC[1], GPU_LC[2]);   /* (the light turns with the picture in tate: the screen is turned) */
	glUniform4f_ (g_u[U_SMR], GPU_SCR_BOUNDS[0], GPU_SCR_BOUNDS[1], GPU_SCR_BOUNDS[2], GPU_SCR_BOUNDS[3]);
	glUniform1f_ (g_u[U_LCA], cosf (GPU_LANG)); glUniform1f_ (g_u[U_LSA], sinf (GPU_LANG));
	glDrawArrays_ (GL_TRIANGLES, 0, 3);
	glBindVertexArray_ (0); glUseProgram_ (0);
	video_cb (RETRO_HW_FRAME_BUFFER_VALID, (unsigned) dw, (unsigned) dh, 0);
}
