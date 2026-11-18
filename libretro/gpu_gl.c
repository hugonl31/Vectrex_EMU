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
/* (txb: a filtered read of a half-float texture - the glow, the afterglow, the overlay's maps, the glass's light map. On an
 *  iPhone / iPad bilinear by hand: there the GPU read them as nearest - the glow in steps, stripes; elsewhere the GPU's) */
#if VX_IOS
#define GL_BL "vec4 txb(sampler2D t, vec2 uv) { vec2 g = uv * vec2(textureSize(t, 0)) - 0.5; ivec2 i = ivec2(floor(g)), L = textureSize(t, 0) - 1;\n" \
	"  vec2 f = g - floor(g);\n" \
	"  return mix(mix(texelFetch(t, clamp(i, ivec2(0), L), 0), texelFetch(t, clamp(i + ivec2(1, 0), ivec2(0), L), 0), f.x),\n" \
	"             mix(texelFetch(t, clamp(i + ivec2(0, 1), ivec2(0), L), 0), texelFetch(t, clamp(i + ivec2(1, 1), ivec2(0), L), 0), f.x), f.y); }\n" \
	/* (txc: the glow's cells spread smoothly - a cubic B-spline, 4 x 4 cells: bilinear left a crease at each cell, at an \
	 *  iPad's resolution 6 px apart - stripes along the lines, rings round a spot, seen in a dim glow) */ \
	"vec4 bsw(float t) { float s = 1.0 - t; return vec4(s * s * s, 4.0 - 6.0 * t * t + 3.0 * t * t * t, 1.0 + 3.0 * t + 3.0 * t * t - 3.0 * t * t * t, t * t * t) / 6.0; }\n" \
	"vec4 txc(sampler2D t, vec2 uv) { vec2 g = uv * vec2(textureSize(t, 0)) - 0.5; ivec2 i = ivec2(floor(g)) - 1, L = textureSize(t, 0) - 1;\n" \
	"  vec2 f = g - floor(g); vec4 wx = bsw(f.x), wy = bsw(f.y); vec4 a = vec4(0.0);\n" \
	"  for (int y = 0; y < 4; y++) { vec4 r = vec4(0.0);\n" \
	"    for (int x = 0; x < 4; x++) r += wx[x] * texelFetch(t, clamp(i + ivec2(x, y), ivec2(0), L), 0);\n" \
	"    a += wy[y] * r; }\n" \
	"  return a; }\n"
#else
#define GL_BL "#define txb texture\n#define txc texture\n"
#endif

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
GLFN(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)); GLFN(void, glUniform2i, (GLint, GLint, GLint)); GLFN(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat));
GLFN(void, glBindFramebuffer, (GLenum, GLuint)); GLFN(void, glClearColor_d, (GLfloat, GLfloat, GLfloat, GLfloat)); GLFN(void, glClear_d, (GLbitfield));
GLFN(void, glEnable_d, (GLenum)); GLFN(void, glBlendFunc_d, (GLenum, GLenum)); GLFN(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
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

static struct retro_hw_render_callback hwr; static int gpu_novideo; static void scr_detect (void);
static uint32_t *dk_img; static int dk_gen, dk_w, dk_h;   /* (deck.c) */
static GLuint dk_prog, dk_tex; static int dk_tgen = -1, dk_tw, dk_th;
static uint32_t *br_img; static int br_shown, br_gen; static GLuint br_tex; static int br_tgen = -1, br_tw, br_th;   /* (browser.c: the ROM browser's panel) */
GLFN(const unsigned char *, glGetString, (GLenum));
static void ph_reset (void); static void ph_lost (void); static int ph_ok; static GLuint ph_P;   /* (gpu_phos.c) */
static int gpu_asked;                         /* (the frontend took the request: the picture goes through the GPU) */
static int gpu_ok; static int gq_cur = -1, gq_ok = -1;   /* (the GPU meter's queries: below gpu_present) */                            /* (the context is there and the program built) */
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
	"            float mr = txb(lmap, mq).r * mfade(mq);\n"
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
	"                vec4 m = txb(lmap, mc) * mfade(mc);\n"
	"                float sharp = ceiling((reflp(tq, 0.2) - lpos.xy) / lpos.z, 0.012);\n"
	"                add += lk * (m.g * (1.0 + 0.018 * gr) * vec3(0.95, 0.96, 1.0) + 0.88 * (0.98 * sharp + m.r) * LIGHTC);\n"
	"            }\n"
	"        }\n"
	"        c = sqrt(c * c + add * add) * g;\n"
	"        if (cse > 0.5) c += sdG(gd) > -3.0 * pxs ? plastic(gd, tgt) : vec3((hash(gl_FragCoord.xy) - hash(gl_FragCoord.xy + 7.31)) / 255.0);\n"
	"    }\n"
	"    fragc = vec4(c, 1.0);\n"
	"}\n";

/* ---- the room's light at a lamp position (roomlight.py: place, ceiling, reflp, pattern, centre, blur, maps;
 * casemodel.light_pos) - the light map made here, for any position */
#define RL_HZ 0.80
#define RL_CH 2.4
#define RL_FIX_W 0.30
#define RL_FIX_L 1.2
#define RL_GLASS_SIZE 0.48
#define RL_GLASS_CURV 0.2
#define RL_INNER_CURV 1.1
#define RL_INNER_ENL 2.1
#define RL_INNER_PINCH 0.42
#define RL_INNER_OFF 0.21
#define RL_REF_AW 0.7114            /* (vecrender GLASS_REF_AW, GLASS_N: the outline the maps are made for) */
#define RL_GN 7.17
static void rl_place (double x, double y, double *gx, double *gy)
{ const double dx = 1.48 * x, dy = -0.21 + 0.77 * y; *gx = -0.74 + dx; *gy = -1.12 + dy; }
static double rl_ceiling (double px, double py)
{
	const double den = RL_HZ - py; if (den < 0.05) return 0.0;
	const double D = RL_CH / den, D0 = RL_CH / RL_HZ, X = px * D / 2.0, Zr = D0 - D, ca = cos (GPU_LANG), sa = sin (GPU_LANG);
	const double Xr = ca * X - sa * Zr, Z = sa * X + ca * Zr;
	if (fabs (Xr) >= RL_FIX_W || fabs (Z) >= RL_FIX_L) return 0.0;
	double fall = pow (D0 / D, 0.7); fall = (fall < 0.75 ? 0.75 : fall > 1.25 ? 1.25 : fall) / 1.25;
	if (fabs (Xr) > RL_FIX_W - 0.05 || fabs (Z) > RL_FIX_L - 0.05) return 0.12 * fall;
	const double cx = (Xr + RL_FIX_W) / (2 * RL_FIX_W) * 2, cz = (Z + RL_FIX_L) / (2 * RL_FIX_L) * 9, fx = cx - floor (cx), fz = cz - floor (cz);
	if (fabs (fx - 0.5) > 0.44 || fabs (fz - 0.5) > 0.42) return 0.18 * fall;
	double t = 1 - fabs (fx - 0.5) * 2; return (0.85 + 0.15 * pow (t > 0 ? t : 0, 0.6)) * fall;
}
/* (roomlight.flat_points: the light's reflection on a flat overlay as six points over its area - half picture heights) */
static void rl_flat_points (double x, double y, float out[12])
{
	const double dx = 1.48 * x, dy = -0.21 + 0.77 * y, fx = -0.44 + 0.6 * dx, fy = -0.72 + 0.6 * dy, cu0 = fx / 0.9, cv0 = fy / 0.9;
	double su = 0, sv = 0, suu = 0, svv = 0, suv = 0; int n = 0;
	for (int j = -30; j <= 30; j++) for (int i = -30; i <= 30; i++) { const double u = cu0 + i * 0.012, v = cv0 + j * 0.012;
		if (rl_ceiling ((u * 0.9 - fx) / 0.42, (v * 0.9 - fy) / 0.42) > 0.3) { su += u; sv += v; suu += u * u; svv += v * v; suv += u * v; n++; } }
	if (n < 3) { for (int k = 0; k < 6; k++) { out[2 * k] = (float) cu0; out[2 * k + 1] = (float) cv0; } return; }
	const double mu = su / n, mv = sv / n, cuu = suu / n - mu * mu, cvv = svv / n - mv * mv, cuv = suv / n - mu * mv;
	const double th = 0.5 * atan2 (2 * cuv, cuu - cvv), ca = cos (th), sa = sin (th);
	double a = ca * ca * cuu + 2 * ca * sa * cuv + sa * sa * cvv, b = sa * sa * cuu - 2 * ca * sa * cuv + ca * ca * cvv;
	const double sl = sqrt (a > 1e-8 ? a : 1e-8), ss = sqrt (b > 1e-8 ? b : 1e-8);
	static const double kl[4] = { -1.5, -0.5, 0.5, 1.5 }, ks[2] = { -1.0, 1.0 };
	for (int k = 0; k < 4; k++) { out[2 * k] = (float) (mu + ca * kl[k] * sl); out[2 * k + 1] = (float) (mv + sa * kl[k] * sl); }
	for (int k = 0; k < 2; k++) { out[8 + 2 * k] = (float) (mu - sa * ks[k] * ss); out[9 + 2 * k] = (float) (mv + ca * ks[k] * ss); }
}
static void rl_reflp (double u, double v, double curv, double *ox, double *oy)
{
	double nx = curv * u, ny = curv * v, nz = 1.0, L = sqrt (nx * nx + ny * ny + nz * nz); nx /= L; ny /= L; nz /= L;
	const double dn = -nz, rx = -2 * dn * nx, ry = -2 * dn * ny, rz = -1 - 2 * dn * nz;
	*ox = u * 0.9 + rx / rz; *oy = v * 0.9 + ry / rz;
}
static double rl_pattern (double u, double v, double lx, double ly, double size, double curv)
{ double px, py; rl_reflp (u, v, curv, &px, &py); return rl_ceiling ((px - lx) / size, (py - ly) / size); }
static void rl_centre (double lx, double ly, double curv, double *ou, double *ov)
{
	double u = lx / 0.9, v = ly / 0.9;
	for (int k = 0; k < 12; k++) {
		double px, py, ax, ay, bx, by; const double e = 1e-4;
		rl_reflp (u, v, curv, &px, &py); rl_reflp (u + e, v, curv, &ax, &ay); rl_reflp (u, v + e, curv, &bx, &by);
		const double j11 = (ax - px) / e, j21 = (ay - py) / e, j12 = (bx - px) / e, j22 = (by - py) / e, det = j11 * j22 - j12 * j21;
		const double rx = px - lx, ry = py - ly; u -= (j22 * rx - j12 * ry) / det; v -= (-j21 * rx + j11 * ry) / det; }
	*ou = u; *ov = v;
}
static void rl_box (const double *a, double *out, int w, int h, int r, int horiz)
{
	const int n = 2 * r + 1; double *acc = malloc (sizeof (double) * (size_t) ((horiz ? w : h) + 1));
	if (horiz) for (int y = 0; y < h; y++) { double s = 0; acc[0] = 0; for (int x = 0; x < w; x++) { s += a[y * w + x]; acc[x + 1] = s; }
		for (int x = 0; x < w; x++) { int hi = x + r + 1 < w ? x + r + 1 : w, lo = x - r > 0 ? x - r : 0; out[y * w + x] = (acc[hi] - acc[lo]) / n; } }
	else for (int x = 0; x < w; x++) { double s = 0; acc[0] = 0; for (int y = 0; y < h; y++) { s += a[y * w + x]; acc[y + 1] = s; }
		for (int y = 0; y < h; y++) { int hi = y + r + 1 < h ? y + r + 1 : h, lo = y - r > 0 ? y - r : 0; out[y * w + x] = (acc[hi] - acc[lo]) / n; } }
	free (acc);
}
static void rl_blur (const double *a, double *out, int w, int h, double sigma)
{
	memcpy (out, a, sizeof (double) * (size_t) w * h); if (sigma < 0.3) return;
	int r = (int) floor ((sqrt (12 * sigma * sigma / 3 + 1) - 1) / 2 + 0.5); if (r < 1) r = 1;
	double *t = malloc (sizeof (double) * (size_t) w * h);
	for (int k = 0; k < 3; k++) { rl_box (out, t, w, h, r, 1); rl_box (t, out, w, h, r, 0); }
	free (t);
}
/* the R / G map (R: the glass's own soft light round the reflection, G: the light on the phosphor layer behind it) for
 * the lamp at (x, y) - roomlight.maps(x, y, GLASS_REF_AW, GLASS_N); interleaved RG, top row first */
static void rl_maps (double x, double y, float *rg)
{
	const int w = GPU_LMAP_W, h = GPU_LMAP_H, n = w * h; double lx, ly; rl_place (x, y, &lx, &ly);
	const double U0 = GPU_LMR[0], U1 = GPU_LMR[1], V0 = GPU_LMR[2], V1 = GPU_LMR[3], TEXEL_PX = (V1 - V0) / h * 917.0;
	double *us = malloc (sizeof (double) * w), *vs = malloc (sizeof (double) * h), *Gp = malloc (sizeof (double) * n), *I = malloc (sizeof (double) * n);
	double *b1 = malloc (sizeof (double) * n), *b2 = malloc (sizeof (double) * n), *b3 = malloc (sizeof (double) * n);
	for (int i = 0; i < w; i++) us[i] = U0 + (i + 0.5) * (U1 - U0) / w;
	for (int j = 0; j < h; j++) vs[j] = V0 + (j + 0.5) * (V1 - V0) / h;
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) Gp[j * w + i] = rl_pattern (us[i], vs[j], lx, ly, RL_GLASS_SIZE, RL_GLASS_CURV);
	rl_blur (Gp, b1, w, h, 8 / TEXEL_PX); rl_blur (Gp, b2, w, h, 25 / TEXEL_PX); rl_blur (Gp, b3, w, h, 40 / TEXEL_PX);
	for (int k = 0; k < n; k++) rg[2 * k] = (float) (0.12 * b1[k] + 0.06 * b2[k] + 0.02 * b3[k]);
	/* the phosphor layer: the fixture in the more curved inner surface, enlarged, very blurry, moved inwards by the glass */
	double ug, vg; rl_centre (lx, ly, RL_GLASS_CURV, &ug, &vg);
	double tot = 0, cu = 0, cv = 0; for (int k = 0; k < n; k++) { tot += Gp[k]; cu += Gp[k] * us[k % w]; cv += Gp[k] * vs[k / w]; }
	if (tot <= 0) tot = 1.0; cu /= tot; cv /= tot;
	double cuu = 0, cvv = 0, cuv = 0; for (int k = 0; k < n; k++) { const double du = us[k % w] - cu, dv = vs[k / w] - cv; cuu += Gp[k] * du * du; cvv += Gp[k] * dv * dv; cuv += Gp[k] * du * dv; }
	const double th = 0.5 * atan2 (2 * cuv, cuu - cvv), ca = cos (th), sa = sin (th);
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
		double u = us[i], v = vs[j];
		const double r_ = pow (pow (fabs (u / RL_REF_AW), RL_GN) + pow (fabs (v), RL_GN), 1.0 / RL_GN), n_ = fmax (1e-6, hypot (u, v));
		const double o_ = RL_INNER_OFF * pow (fmin (1.0, r_), 2); u += o_ * u / n_; v += o_ * v / n_;
		const double du = u - cu, dv = v - cv, a_ = (ca * du + sa * dv) / (RL_INNER_ENL * RL_INNER_PINCH), b_ = (-sa * du + ca * dv) / RL_INNER_ENL;
		I[j * w + i] = rl_pattern (cu + ca * a_ - sa * b_, cv + sa * a_ + ca * b_, lx, ly, RL_GLASS_SIZE, RL_GLASS_CURV); }
	rl_blur (I, b1, w, h, 80 / TEXEL_PX); rl_blur (I, b2, w, h, 150 / TEXEL_PX); rl_blur (I, b3, w, h, 260 / TEXEL_PX);
	for (int k = 0; k < n; k++) rg[2 * k + 1] = (float) (0.286 * (1.1 * b1[k] + 0.6 * b2[k] + 0.2 * b3[k]));
	(void) ug; (void) vg;
	free (us); free (vs); free (Gp); free (I); free (b1); free (b2); free (b3);
}
static void rl_light_pos (double x, double y, float lpos[3], float lc[3])   /* (the shader's lpos and lc for the lamp at x, y) */
{
	double gx, gy; rl_place (x, y, &gx, &gy); lpos[0] = (float) gx; lpos[1] = (float) gy; lpos[2] = (float) RL_GLASS_SIZE;
	double a = 0.49 * gx, b = -0.49 * gy, c = 1.0, nn = sqrt (a * a + b * b + c * c);   /* (casemodel.light_pos: 236 cm away) */
	lc[0] = (float) (236.0 * a / nn); lc[1] = (float) (236.0 * b / nn); lc[2] = (float) (236.0 * c / nn);
}
static float g_lamp_x = 0.0f, g_lamp_y = 0.273f, g_lamp_done_x = -1, g_lamp_done_y = -1, g_lpos[3], g_lc[3];
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
	int ok = 1; gpu_ok = 0; fprintf (stderr, "[vectrex_emu] GPU: context reset\n"); gq_ok = -1; gq_cur = -1;   /* (a hang there: the log says so) */
	GLLOAD (glGenTextures); GLLOAD (glDeleteTextures); GLLOAD (glBindTexture); GLLOAD (glActiveTexture); GLLOAD (glTexImage2D);
	GLLOAD (glTexSubImage2D); GLLOAD (glTexParameteri); GLLOAD (glPixelStorei); GLLOAD (glCreateShader); GLLOAD (glShaderSource);
	GLLOAD (glCompileShader); GLLOAD (glGetShaderiv); GLLOAD (glGetShaderInfoLog); GLLOAD (glDeleteShader); GLLOAD (glCreateProgram);
	GLLOAD (glAttachShader); GLLOAD (glLinkProgram); GLLOAD (glGetProgramiv); GLLOAD (glGetProgramInfoLog); GLLOAD (glDeleteProgram);
	GLLOAD (glUseProgram); GLLOAD (glGetUniformLocation); GLLOAD (glUniform1i); GLLOAD (glUniform1f); GLLOAD (glUniform2f);
	GLLOAD (glUniform3f); GLLOAD (glUniform2i); GLLOAD (glUniform4f); GLLOAD (glBindFramebuffer); glClearColor_d_ = (pf_glClearColor_d) hwr.get_proc_address ("glClearColor"); glClear_d_ = (pf_glClear_d) hwr.get_proc_address ("glClear");
	glEnable_d_ = (pf_glEnable_d) hwr.get_proc_address ("glEnable"); glBlendFunc_d_ = (pf_glBlendFunc_d) hwr.get_proc_address ("glBlendFunc"); ok &= glClear_d_ && glClearColor_d_ && glEnable_d_ && glBlendFunc_d_; GLLOAD (glViewport); GLLOAD (glDisable);
	GLLOAD (glDrawArrays); GLLOAD (glGenVertexArrays); GLLOAD (glBindVertexArray); GLLOAD (glDeleteVertexArrays); GLLOAD (glGetError);
	if (!ok) { fprintf (stderr, "[vectrex_emu] GPU: the frontend lacks GL functions - the CPU's picture\n"); return; }
	const char *hdr = gpu_es ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" : "#version 330 core\n";   /* (sampler2D highp: see gpu_phos.c) */
	const char *vs[2] = { hdr, GPU_VS }, *fs[4] = { hdr, GL_BL, GPU_FS_A, GPU_FS_B };
	GLuint v = gpu_shader (GL_VERTEX_SHADER, vs, 2), f = gpu_shader (GL_FRAGMENT_SHADER, fs, 4);
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
	g_lamp_done_x = g_lamp_done_y = -1;               /* (the light map: made at the first picture, for the lamp's position) */
	{ static const float z[2] = { 0, 0 }; glBindTexture_ (GL_TEXTURE_2D, g_lmap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RG16F, 1, 1, 0, GL_RG, GL_FLOAT, z); }
	{ snprintf (p, sizeof p, "%s" SEP "vectrex_emu" SEP "gpu" SEP "case_scratch.png", g_sys); int w, h, n;
	  unsigned char *im = stbi_load (p, &w, &h, &n, 4);
	  if (im) { glBindTexture_ (GL_TEXTURE_2D, g_smap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im); stbi_image_free (im); g_smap_ok = 1; }
	  else { static const unsigned char z[4] = { 0, 0, 0, 255 }; glBindTexture_ (GL_TEXTURE_2D, g_smap); glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, z);
	         fprintf (stderr, "[vectrex_emu] GPU: no %s - no case scratches\n", p); } }
	{ static const char *DKF = "in vec2 vuv; out vec4 fragc; uniform sampler2D t; uniform int rot; uniform vec4 crop;\n"
		"void main() { vec2 a = vuv; vec2 q = rot == 1 ? vec2(a.y, 1.0 - a.x) : rot == 2 ? vec2(1.0 - a.x, 1.0 - a.y) : rot == 3 ? vec2(1.0 - a.y, a.x) : a;\n"
		"  vec2 p = crop.xy + q * (crop.zw - crop.xy);\n"
		"  fragc = texture(t, vec2(p.x, 1.0 - p.y)).bgra; }\n";   /* (crop: the image's part, y up - the browser's: the picture's) */   /* (the deck's image: premultiplied, B G R A bytes) */
	  const char *dv[2] = { hdr, GPU_VS }, *df[2] = { hdr, DKF };
	  GLuint v2 = gpu_shader (GL_VERTEX_SHADER, dv, 2), f2 = gpu_shader (GL_FRAGMENT_SHADER, df, 2); dk_prog = 0;
	  if (v2 && f2) { dk_prog = glCreateProgram_ (); glAttachShader_ (dk_prog, v2); glAttachShader_ (dk_prog, f2); glLinkProgram_ (dk_prog); }
	  if (v2) glDeleteShader_ (v2); if (f2) glDeleteShader_ (f2); dk_tex = 0; dk_tgen = -1;
	  br_tex = 0; br_tgen = -1; }   /* (2026-11-16: the browser's too - a new context (Android: back from the background) has its own
	                                  *  names; the old one could be another texture of ours by now - the Nova's crash on load from the browser) */
	gpu_ok = 1;
	scr_detect ();                                    /* (Picture size auto: the screen's size) */
	ph_reset ();                                      /* (the tube's light there too: gpu_phos.c) */
	fprintf (stderr, "[vectrex_emu] GPU: %s - the glass and the case drawn there%s\n", gpu_es ? "OpenGL ES 3" : "OpenGL 3.3", ph_ok ? ", the light too" : "");
}
static void gpu_destroy (void) { fprintf (stderr, "[vectrex_emu] GPU: context destroyed\n"); ph_lost (); gpu_ok = 0; gq_ok = -1; gq_cur = -1; g_prog = g_tex = g_lmap = g_smap = g_vao = 0; g_tw = g_th = 0; }

/* (the screen's size, for Picture size auto: RetroArch's window as its graphics context has it - EGL's surface
 *  (Android, Linux), Windows: the window's monitor - its short side kept in the saves folder for the next start) */
#if defined(_WIN32)
#if defined(_MSC_VER)
#pragma comment(lib, "user32.lib")
#endif
static int scr_query (int *w, int *h)
{
	HMODULE gl = GetModuleHandleA ("opengl32.dll"); if (!gl) return 0;
	HDC (WINAPI *cur) (void) = (HDC (WINAPI *) (void)) GetProcAddress (gl, "wglGetCurrentDC"); if (!cur) return 0;
	HDC dc = cur (); HWND wn = dc ? WindowFromDC (dc) : NULL; if (!wn) return 0;
	MONITORINFO mi; mi.cbSize = sizeof mi; if (!GetMonitorInfoA (MonitorFromWindow (wn, MONITOR_DEFAULTTONEAREST), &mi)) return 0;
	*w = mi.rcMonitor.right - mi.rcMonitor.left; *h = mi.rcMonitor.bottom - mi.rcMonitor.top; return 1;
}
#elif VX_IOS
/* (iPhone / iPad: UIScreen - its pixels (nativeBounds, always upright), turned as the screen stands now (bounds, in points)) */
#include <objc/runtime.h>
#include <objc/message.h>
typedef struct { double x, y, w, h; } vx_rect;
static int scr_query (int *w, int *h)
{
	Class c = objc_getClass ("UIScreen"); if (!c) return 0;
	id s = ((id (*) (id, SEL)) objc_msgSend) ((id) c, sel_registerName ("mainScreen")); if (!s) return 0;
	const vx_rect n = ((vx_rect (*) (id, SEL)) objc_msgSend) (s, sel_registerName ("nativeBounds"));
	const vx_rect b = ((vx_rect (*) (id, SEL)) objc_msgSend) (s, sel_registerName ("bounds"));
	if (n.w < 1 || n.h < 1) return 0;
	const int land = b.w > b.h; *w = (int) (land ? n.h : n.w); *h = (int) (land ? n.w : n.h);
	return 1;
}
#else
#include <dlfcn.h>
static int scr_query (int *w, int *h)
{
	void *e = dlopen ("libEGL.so", RTLD_NOW | RTLD_NOLOAD); if (!e) e = dlopen ("libEGL.so.1", RTLD_NOW | RTLD_NOLOAD); if (!e) return 0;
	void *(*gd) (void) = (void *(*) (void)) dlsym (e, "eglGetCurrentDisplay"); void *(*gs) (int) = (void *(*) (int)) dlsym (e, "eglGetCurrentSurface");
	unsigned (*qs) (void *, void *, int, int *) = (unsigned (*) (void *, void *, int, int *)) dlsym (e, "eglQuerySurface");
	if (!gd || !gs || !qs) return 0;
	void *d = gd (), *sf = gs (0x3059 /* EGL_DRAW */); if (!d || !sf) return 0;
	return qs (d, sf, 0x3057 /* EGL_WIDTH */, w) && qs (d, sf, 0x3056 /* EGL_HEIGHT */, h) && *w > 0 && *h > 0;
}
#endif
static void scr_detect (void)
{
	int w = 0, h = 0; if (!scr_query (&w, &h)) return;
	scr_w = w; scr_h = h;                       /* (as it stands: the deck's block takes an upright screen's shape) */
	/* (the picture's height on the screen as big as fits: the screen's height, or less where its width is the limit - an
	 *  upright phone; a wide screen: its short side, as before. Turned 90 / 270: the picture lies the other way) */
	double a = (opt_rot & 1) ? 207.0 / 162.0 : 162.0 / 207.0;   /* (its width / height as it stands on the screen) */
	/* (the deck below the picture - full or slim, 4:3, not turned: the size is the picture and the deck together
	 *  (geom_calc), so the block - narrower for the deck - fits; an upright iPad then gets its frame at the screen's
	 *  own 1640 x 2360, 1:1 - it was 1507 x 2169, stretched 9 %, the lines softer and stepped) */
	if ((opt_deck == 1 || opt_deck == 2) && !(opt_rot & 1) && !opt_shape) a /= 1.0 + (opt_deck == 2 ? 0.046 : 0.08134) / 0.924;
	const int sh = (int) (h < w / a ? h : w / a); if (sh < 240 || sh > 4320) return;
	{ static int lw, lh; if (w != lw || h != lh) { lw = w; lh = h; fprintf (stderr, "[vectrex_emu] screen %dx%d\n", w, h); } }   /* (a change only: it is looked at each second) */
	if (sh != scr_short) { scr_short = sh; scr_new = 1; char f[1200]; scr_file (f, sizeof f); FILE *t = f[0] ? fopen (f, "w") : NULL; if (t) { fprintf (t, "%d\n", sh); fclose (t); } }
}
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

static void gpu_vp (double x0, double y0, double x1, double y1, int rot, int dw, int dh)   /* (x, y down, of TW x TH) */
{
	const double qx0 = x0 / TW, qx1 = x1 / TW, qy0 = 1.0 - y1 / TH, qy1 = 1.0 - y0 / TH;   /* (q: y up) */
	double ax0, ax1, ay0, ay1;
	if (rot == 1) { ax0 = 1 - qy1; ax1 = 1 - qy0; ay0 = qx0; ay1 = qx1; }
	else if (rot == 2) { ax0 = 1 - qx1; ax1 = 1 - qx0; ay0 = 1 - qy1; ay1 = 1 - qy0; }
	else if (rot == 3) { ax0 = qy0; ax1 = qy1; ay0 = 1 - qx1; ay1 = 1 - qx0; }
	else { ax0 = qx0; ax1 = qx1; ay0 = qy0; ay1 = qy1; }
	const int vx = (int) floor (ax0 * dw + 0.5), vy = (int) floor (ay0 * dh + 0.5);
	glViewport_ (vx, vy, (int) floor (ax1 * dw + 0.5) - vx, (int) floor (ay1 * dh + 0.5) - vy);
}
/* the picture: the frame (the whole sheet, XRGB, pitch in pixels) up, drawn into the frontend's framebuffer turned and
 * cut to the area sent (crop: the sheet's share, y up), with the glass and the case on top */
/* (the GPU's own time for each picture, as the desktop's OSD meter (vecrender.py _gpu_begin): GL_TIME_ELAPSED queries -
 *  GL_EXT_disjoint_timer_query on Android / iOS, core in desktop GL 3.3 - read a few pictures later, so nothing waits
 *  for them; the deck's GPU bar is their sum over a second) */
typedef void (GLAPIENTRY *pf_gq_gen) (GLsizei, GLuint *); typedef void (GLAPIENTRY *pf_gq_beg) (GLenum, GLuint);
typedef void (GLAPIENTRY *pf_gq_end) (GLenum); typedef void (GLAPIENTRY *pf_gq_iv) (GLuint, GLenum, GLuint *);
typedef void (GLAPIENTRY *pf_gq_u64) (GLuint, GLenum, unsigned long long *);
static pf_gq_gen gq_gen; static pf_gq_beg gq_beg; static pf_gq_end gq_endf; static pf_gq_iv gq_iv; static pf_gq_u64 gq_u64;
#define GQ_N 8
static GLuint gq_id[GQ_N]; static int gq_busy[GQ_N], gq_next; static double gq_sum_ms; static long gq_n;
static void *gq_proc (const char *a, const char *b) { void *f = (void *) hwr.get_proc_address (a); return f ? f : b ? (void *) hwr.get_proc_address (b) : NULL; }
static void gq_setup (void)
{
	gq_ok = 0; gq_cur = -1; for (int i = 0; i < GQ_N; i++) gq_busy[i] = 0;
	if (!glGetString_) glGetString_ = (pf_glGetString) hwr.get_proc_address ("glGetString");
	const char *ex = glGetString_ ? (const char *) glGetString_ (0x1F03) : NULL;   /* (GL_EXTENSIONS) */
	if (gpu_es && !(ex && strstr (ex, "GL_EXT_disjoint_timer_query"))) { fprintf (stderr, "[vectrex_emu] GPU: no timer queries - no GPU meter\n"); return; }
	gq_gen = (pf_gq_gen) gq_proc ("glGenQueriesEXT", "glGenQueries"); gq_beg = (pf_gq_beg) gq_proc ("glBeginQueryEXT", "glBeginQuery");
	gq_endf = (pf_gq_end) gq_proc ("glEndQueryEXT", "glEndQuery"); gq_iv = (pf_gq_iv) gq_proc ("glGetQueryObjectuivEXT", "glGetQueryObjectuiv");
	gq_u64 = (pf_gq_u64) gq_proc ("glGetQueryObjectui64vEXT", "glGetQueryObjectui64v");
	if (!gq_gen || !gq_beg || !gq_endf || !gq_iv || !gq_u64) return;
	gq_gen (GQ_N, gq_id); gq_ok = 1;
}
static void gq_poll (void)                      /* (the finished queries, oldest first: their times added up) */
{
	for (int k = 0; k < GQ_N; k++) { const int i = (gq_next + k) % GQ_N; if (!gq_busy[i]) continue;
		GLuint av = 0; gq_iv (gq_id[i], 0x8867, &av);   /* (GL_QUERY_RESULT_AVAILABLE) */
		if (!av) break;
		unsigned long long ns = 0; gq_u64 (gq_id[i], 0x8866, &ns);   /* (GL_QUERY_RESULT) */
		gq_sum_ms += ns / 1e6; gq_n++; gq_busy[i] = 0; }
}
static void gq_begin (void)
{
	if (gq_ok < 0) gq_setup ();
	if (gq_ok <= 0 || gq_cur >= 0 || gpu_novideo) return;
	gq_poll ();
	if (gq_busy[gq_next]) return;                   /* (all in flight: this picture not measured) */
	gq_beg (0x88BF, gq_id[gq_next]); gq_cur = gq_next;   /* (GL_TIME_ELAPSED) */
}
static void gq_end (void)
{
	if (gq_cur < 0) return;
	gq_endf (0x88BF); gq_busy[gq_cur] = 1; gq_next = (gq_cur + 1) % GQ_N; gq_cur = -1;
}
static void gpu_present (const uint32_t *frame, int fw, int fh, int pitch, float cx0, float cy0, float cx1, float cy1, int ow, int oh, int rot)
{
	gq_begin ();                                 /* (a no-op while the picture's query runs) */
	if (g_lamp_x != g_lamp_done_x || g_lamp_y != g_lamp_done_y) {   /* (the lamp moved, or the first picture: its light map) */
		float *rg = malloc (sizeof (float) * 2 * GPU_LMAP_W * GPU_LMAP_H);
		if (rg) { rl_maps (g_lamp_x, g_lamp_y, rg); glBindTexture_ (GL_TEXTURE_2D, g_lmap);
			glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RG16F, GPU_LMAP_W, GPU_LMAP_H, 0, GL_RG, GL_FLOAT, rg); free (rg); g_lmap_ok = 1; }
		rl_light_pos (g_lamp_x, g_lamp_y, g_lpos, g_lc); g_lamp_done_x = g_lamp_x; g_lamp_done_y = g_lamp_y; }
	GLuint ptex = g_tex;
	if (!frame) ptex = ph_P;                                          /* (the light made on the GPU: gpu_phos.c) */
	else { glBindTexture_ (GL_TEXTURE_2D, g_tex);
	if (fw != g_tw || fh != g_th) { glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); g_tw = fw; g_th = fh; }
	glPixelStorei_ (GL_UNPACK_ROW_LENGTH, pitch);
	glTexSubImage2D_ (GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frame);
	glPixelStorei_ (GL_UNPACK_ROW_LENGTH, 0); }
	glBindFramebuffer_ (GL_FRAMEBUFFER, (GLuint) hwr.get_current_framebuffer ());
	if (dk_on || dk_pop) { ow = TW; oh = TH; }                           /* (the control deck: the block, deck.c) */
	const int dw = rot & 1 ? oh : ow, dh = rot & 1 ? ow : oh;
	glViewport_ (0, 0, dw, dh);
	if (dk_on) { glDisable_ (GL_SCISSOR_TEST); glClearColor_d_ (0, 0, 0, 1); glClear_d_ (0x4000); gpu_vp (BX, BY, BX + CW, BY + CH, rot, dw, dh); }
	glDisable_ (GL_BLEND); glDisable_ (GL_DEPTH_TEST); glDisable_ (GL_SCISSOR_TEST); glDisable_ (GL_CULL_FACE); glDisable_ (GL_STENCIL_TEST);
	glUseProgram_ (g_prog); glBindVertexArray_ (g_vao);
	glActiveTexture_ (GL_TEXTURE0); glBindTexture_ (GL_TEXTURE_2D, ptex); glUniform1i_ (g_u[U_TEX], 0);
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
	glUniform3f_ (g_u[U_LPOS], g_lpos[0], g_lpos[1], g_lpos[2]); glUniform4f_ (g_u[U_LMR], GPU_LMR[0], GPU_LMR[1], GPU_LMR[2], GPU_LMR[3]);
	glUniform2f_ (g_u[U_GSC], GPU_GSC[0], GPU_GSC[1]);
	glUniform1f_ (g_u[U_CSE], (float) gfx.cse); glUniform1f_ (g_u[U_CROOM], gfx.croom); glUniform1f_ (g_u[U_CREFL], gfx.crefl);
	glUniform1f_ (g_u[U_CWEAR], g_smap_ok ? gfx.cwear : 0.0f); glUniform3f_ (g_u[U_LC], g_lc[0], g_lc[1], g_lc[2]);   /* (the light turns with the picture in tate: the screen is turned) */
	glUniform4f_ (g_u[U_SMR], GPU_SCR_BOUNDS[0], GPU_SCR_BOUNDS[1], GPU_SCR_BOUNDS[2], GPU_SCR_BOUNDS[3]);
	glUniform1f_ (g_u[U_LCA], cosf (GPU_LANG)); glUniform1f_ (g_u[U_LSA], sinf (GPU_LANG));
	glDrawArrays_ (GL_TRIANGLES, 0, 3);
	if ((dk_on || dk_pop) && dk_img && dk_prog) {                       /* (the deck under the picture - the pop-up's over it) */
		if (!dk_tex || dk_tw != dk_w || dk_th != dk_h) { if (!dk_tex) glGenTextures_ (1, &dk_tex); glBindTexture_ (GL_TEXTURE_2D, dk_tex);
			glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, dk_w, dk_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); dk_tw = dk_w; dk_th = dk_h; dk_tgen = -1; }
		glActiveTexture_ (GL_TEXTURE0); glBindTexture_ (GL_TEXTURE_2D, dk_tex);
		if (dk_tgen != dk_gen) { glTexSubImage2D_ (GL_TEXTURE_2D, 0, 0, 0, dk_w, dk_h, GL_RGBA, GL_UNSIGNED_BYTE, dk_img); dk_tgen = dk_gen; }
		glUseProgram_ (dk_prog); glUniform1i_ (glGetUniformLocation_ (dk_prog, "t"), 0); glUniform1i_ (glGetUniformLocation_ (dk_prog, "rot"), rot);
		glUniform4f_ (glGetUniformLocation_ (dk_prog, "crop"), 0, 0, 1, 1);
		if (!dk_on) gpu_vp (0, dk_y, dk_w, dk_y + dk_h, rot, dw, dh); else gpu_vp (BX, BY + dk_y, BX + dk_w, BY + dk_y + dk_h, rot, dw, dh);
		glEnable_d_ (GL_BLEND); glBlendFunc_d_ (1, 0x0303); glDrawArrays_ (GL_TRIANGLES, 0, 3); glDisable_ (GL_BLEND); }
	if (br_shown && br_img && dk_prog) {                                /* (the ROM browser: in front of the glass and the case) */
		if (!br_tex || br_tw != fw || br_th != fh) { if (!br_tex) glGenTextures_ (1, &br_tex); glBindTexture_ (GL_TEXTURE_2D, br_tex);
			glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D_ (GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); br_tw = fw; br_th = fh; br_tgen = -1; }
		glActiveTexture_ (GL_TEXTURE0); glBindTexture_ (GL_TEXTURE_2D, br_tex);
		if (br_tgen != br_gen) { glTexSubImage2D_ (GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, br_img); br_tgen = br_gen; }
		glUseProgram_ (dk_prog); glUniform1i_ (glGetUniformLocation_ (dk_prog, "t"), 0); glUniform1i_ (glGetUniformLocation_ (dk_prog, "rot"), rot);
		glUniform4f_ (glGetUniformLocation_ (dk_prog, "crop"), cx0, cy0, cx1, cy1);
		if (dk_on || dk_pop) gpu_vp (BX, BY, BX + CW, BY + CH, rot, dw, dh); else glViewport_ (0, 0, dw, dh);
		glEnable_d_ (GL_BLEND); glBlendFunc_d_ (1, 0x0303); glDrawArrays_ (GL_TRIANGLES, 0, 3); glDisable_ (GL_BLEND); }
	glBindVertexArray_ (0); glUseProgram_ (0);
	gq_end ();
	if (!gpu_novideo) video_cb (RETRO_HW_FRAME_BUFFER_VALID, (unsigned) dw, (unsigned) dh, 0);   /* (Optimize: drawn, not shown) */
}
