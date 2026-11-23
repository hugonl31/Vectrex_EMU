/* gpu_phos.c - Vectrex_EMU's libretro core: the tube's light on the GPU (included by libretro_vx.c after gpu_gl.c).
 * The CPU still builds the pieces (build_lines: where each lies, its spot, its light); here they are drawn as capsules
 * into a float image (the fresh light), the phosphor's two afterglow parts updated per pixel, the glow (the CPU's
 * glow_blur: the 1 3 3 1 step down, a Gaussian across and down) and the screen's values (pixel_out: the brightness, the
 * white balance, the overlay, the knee) made - the CPU's model, pixel for pixel. The picture then goes through
 * gpu_present (the glass, the case) as before. Not for the 3D Imager's two pictures side by side (stereo).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */
#include <stddef.h>

GLFN(void, glGenBuffers, (GLsizei, GLuint *)); GLFN(void, glBindBuffer, (GLenum, GLuint));
GLFN(void, glBufferData, (GLenum, ptrdiff_t, const void *, GLenum));
GLFN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *));
GLFN(void, glEnableVertexAttribArray, (GLuint)); GLFN(void, glVertexAttribDivisor, (GLuint, GLuint));
GLFN(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei));
GLFN(void, glGenFramebuffers, (GLsizei, GLuint *)); GLFN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint));
GLFN(GLenum, glCheckFramebufferStatus, (GLenum)); GLFN(void, glDrawBuffers, (GLsizei, const GLenum *));
GLFN(void, glEnable, (GLenum)); GLFN(void, glBlendFunc, (GLenum, GLenum)); GLFN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
GLFN(void, glClear, (GLbitfield)); GLFN(void, glUniform1fv, (GLint, GLsizei, const GLfloat *)); GLFN(void, glUniform2fv, (GLint, GLsizei, const GLfloat *)); GLFN(void, glUniform4fv, (GLint, GLsizei, const GLfloat *));
GLFN(void, glFinish, (void)); GLFN(void, glDeleteFramebuffers, (GLsizei, const GLuint *)); GLFN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_RGBA16F 0x881A
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_ONE 1
#define GL_NEAREST 0x2600

static uint32_t *br_img; static int br_shown, br_gen; static GLuint ph_ui, ph_uit; static int ph_uigen = -1, ph_uiw, ph_uih;   /* (browser.c) */
static GLuint ph_cap, ph_upd, ph_comp, ph_down, ph_down2, ph_blurl, ph_blur, ph_out, ph_vbo, ph_vao, ph_fbo;
static GLuint ph_F[2], ph_G1[2], ph_G2[2];   /* (ph_P: gpu_gl.c) */
/* Smooth motion's afterglow (2026-11-22): the in-between pictures move a line 2.4 places a game frame at 120 Hz, and every place
 * it left started an afterglow - 2.4 trail lines where a Vectrex has one (the owner's iPhone, the Test Cart). So the afterglow
 * starts only where the game itself drew: once a game frame its real lines are drawn into ph_E (pieces npl .. npl + ph_ex_n,
 * built after the picture's), and that - not the last picture - is what the afterglow starts from; the other refreshes none
 * (ph_ex_mode: Smooth motion is drawing in-betweens; set by libretro_vx.c each refresh, taken here) */
static int ph_ex_mode, ph_ex_n; static GLuint ph_E;
static GLuint ph_beam, ph_sat, ph_L, ph_H; static float *ph_hbuf; static size_t ph_hbufn;   /* (the beam on the phosphor: the light image, the glass's glow) */
static int ph_pw, ph_ph, ph_fw, ph_fh; static float ph_ps_made = -1; static int ph_lh_made = -1, ph_hdr_made = -1;
float vx_hdr_ee = 1.4f, vx_hdr_eq = 0.31f, vx_hdr_ec = 6.0f, vx_hdr_vs = 0.25f, vx_hdr_vss = 0.15f, vx_hdr_dk = 0.1f, vx_hdr_dx = 2.5f, vx_hdr_lt = 0.3f, vx_hdr_sn = 6.0f, vx_hdr_gb = 1.3f, vx_hdr_hb = 2.75f, vx_hdr_hw = 1.0f, vx_hdr_dw = 1.6f;   /* (the apps: the dots' spot 1.6x wide, their halo x2.75 - the owner: x14 too much, as build 134 + 10 %) */   /* (SDR Vectrex: the dots' glow x2.5, their spot x1.6 wide (the photo: 1.2-1.6x the line's width at white) - at white as the lines, their glare tells them) */   /* (SDR: the lines 0.64 of white, the fat ones clipping at white - the owner, 2026-11-21: the grid too dim at 0.08) */   /* (dx: the dots above the lines, HDR; lt: the lines up to this share of the peak - the rest
                                                  the dots' room: 3.6x a line at every intensity, the owner: the dots not bright enough next to the lines) */   /* (vss: the Vectrex look on an SDR screen -
                                                  the lines lower, the slow ones and the dots up to white) */       /* (HDR, the Vectrex look: a line this slow (cycles a game px: Test Cartridge Rev. 4's grid) as
                                                  before, slower ones brighter by the time the beam spends - its bars 5.8x, the photo 6.8x with
                                                  the edge under them; the lines' scale) */
int vx_gprof = 0;                              /* (the apps' GPU diagnostic: each stage's time, the log) */
int vx_app_dots = 0;                           /* (the own apps: on SDR the dots' glare in Bright too - their spot wider, its glow up) */
float vx_trail_mul = 1.0f;                     /* (the own apps: the trail for the look and the screen's brightness - the owner's
                                                  iPhone and Nova next to a real Vectrex, 2026-11-21) */
float vx_hdr_lift = 0.0f;                      /* (HDR, the own iOS app: 0 the rests above white only (before 2026-11-20), 1 the whole picture
                                                  lifted towards the peak, 2 the measured Vectrex - dots with the slow beam round them, the
                                                  brightest lines a little above white; below the knee as SDR) */
int vx_phone_trail = 0;                        /* (the own phone / tablet apps set it: the afterglow there. The owner matched his iPhone and iPad to the
                                                   PC's Vectrex look by eye, 2026-11-22 (it held in both looks, HDR on and off): the measured afterglow with
                                                   its bright start 1.5 x as long and its faint end twice as long at 0.75 of its level - a phone shows 120
                                                   pictures a second, each ghost along its whole fade, where the PC shows it fresh. The PC and RetroArch:
                                                   the measured one, vecrender.py PHOS_A) */
#define PHOS_A1 0.1404f
#define PHOS_T1 (vx_phone_trail ? 0.0454f * 1.5f : 0.0454f)
#define PHOS_A2 (vx_phone_trail ? 0.0582f * 0.75f : 0.0582f)
#define PHOS_T2 (vx_phone_trail ? 0.350f * 2.0f : 0.350f)
float vx_bright = 0.0f;                        /* (the look: 0 Vectrex - the measured picture; 1 Bright, for a small screen whose thin lines
                                                   give little light (the owner, 2026-11-22): the tube's light lifted and the beam 1.3 x wider
                                                   (libretro_vx.c). An ordinary screen: its values x BL_GAIN before the roll-off - the faint lines
                                                   come up, the top is pressed into white. An HDR screen: the ordinary screen's whole picture,
                                                   roll-off and all, x the screen's top - the same range, higher up (tone: bgh). The room's light
                                                   stays as it is) */
#define BL_GAIN 1.5f
#define BT_GLOW_R 0.62f                            /* (the glow's tint against the core's - the owner's choice from the previews, 2026-11-22; vecrender.py BT_GLOW) */
#define BT_GLOW_G 0.80f
#define BT_HALO_A 3.0f                             /* (the glass's glow beside a line: 0 the Gaussian; else its level at the line and its power - PH_FS_BEAM hsh) */
#define BT_HALO_N 3.0f
float vx_hdr_peak = 1.0f;                       /* (HDR: the brightest lights top, x the screens white - the own iOS app sets it
                                                from the screens headroom; 1: SDR, as everywhere else) */
static int ph_w, ph_h, ph_i, ph_ovgen = -1, ph_made;
static float *ph_buf; static size_t ph_bufn;

static const char *PH_VS_CAP =                  /* (a piece: its box, in picture px - y down, as the CPU's rows) */
	"layout(location = 0) in vec4 ab; layout(location = 1) in vec4 box; layout(location = 2) in vec4 c0;\n"
	"layout(location = 3) in vec4 c1; layout(location = 4) in vec4 rc; layout(location = 5) in vec4 kx;\n"
	"uniform vec2 sz;\n"
	"flat out vec4 vab, vc0, vc1, vrc; flat out float vk, vx, va;\n"
	"void main() { int k = gl_VertexID; vec2 q = vec2((k == 1 || k == 2 || k == 4) ? 1.0 : 0.0, (k == 2 || k == 4 || k == 5) ? 1.0 : 0.0);\n"
	"  vec2 lo = min(box.xy, box.zw) - vec2(rc.x + 1.0), hi = max(box.xy, box.zw) + vec2(rc.x + 1.0);\n"
	"  vec2 p = mix(lo, hi, q); vab = ab; vc0 = c0; vc1 = c1; vrc = rc; vk = kx.x; vx = kx.y; va = kx.z;\n"
	"  gl_Position = vec4(p / sz * 2.0 - 1.0, 0.0, 1.0); }\n";
static const char *PH_FS_CAP =                  /* (splat_band: the spot's Gaussian round the segment, the joints' cuts) */
	"flat in vec4 vab, vc0, vc1, vrc; flat in float vk, vx, va; out vec4 fragc;\n"
	"void main() { vec2 p = gl_FragCoord.xy, a = vab.xy, d = vab.zw - vab.xy;\n"
	"  float L2 = dot(d, d), t = L2 > 0.0 ? clamp(dot(p - a, d) / L2, 0.0, 1.0) : 0.0;\n"
	"  vec2 e = p - (a + d * t); float d2 = dot(e, e);\n"
	"  if (d2 >= vrc.x * vrc.x) discard;\n"
	"  if (vc0.w > 0.5 && dot(p, vc0.xy) < vc0.z) discard;\n"
	"  if (vc1.w > 0.5 && dot(p, vc1.xy) > vc1.z) discard;\n"
	"  float sh = exp(-d2 * vk); fragc = vec4(vrc.yzw * sh, sh * vx); }\n";
/* The beam on the phosphor (vecrender.py CAPSULE_SHADER, beam): a piece's light spread by the spot - flatter on top and steeper at
 * the sides than a Gaussian, exp(-(d / a)^2.6) (the owner's photo of Vectorblade's logo), along the piece the Gaussian's
 * integral: pieces of one line add up to an even line, ends are soft. The glass's glow (halo 1): a line's a plain Gaussian,
 * a spot's fading out from the spot. vrc.yzw: the peak of the piece's light as a spot's (light / (2 pi sigma^2)), vk: sigma, px */
static const char *PH_FS_BEAM =
	"flat in vec4 vab, vc0, vc1, vrc; flat in float vk, vx, va; out vec4 fragc; uniform int halo; uniform vec2 hsh;\n"   /* (hsh: the glass's glow beside a line - x > 0: x (1 - v / 3 sigma)^y, brighter
	                                              at the line and down to black at the same reach as the Gaussian's; 0: the Gaussian) */
	"float Phi(float x) { float z = abs(x) * 0.70710678, t = 1.0 / (1.0 + 0.3275911 * z);\n"
	"  float e = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-z * z);\n"
	"  return 0.5 + 0.5 * (x < 0.0 ? -e : e); }\n"
	"void main() { vec2 p = gl_FragCoord.xy, a = vab.xy, ba = vab.zw - vab.xy, q = p - a; float L = length(ba), sg = vk, g;\n"
	"  if (L < 1e-3) { float r = length(q); if (r >= vrc.x) discard;\n"
	"    g = halo != 0 ? 4.0 * exp(-r / (0.5 * sg)) : 1.177 * exp(-pow(r / (1.356 * sg), 2.6)); }\n"
	"  else { vec2 t = ba / L; float u = dot(q, t), v = abs(q.x * t.y - q.y * t.x);\n"
	"    if (v >= vrc.x || u <= -vrc.x || u >= L + vrc.x) discard;\n"
	"    g = 2.5066283 * sg / L * (Phi(u / sg) - Phi((u - L) / sg)) * (halo != 0 ? (hsh.x > 0.0 ? hsh.x * pow(max(1.0 - v / (3.0 * sg), 0.0), hsh.y) : exp(-0.5 * v * v / (sg * sg))) : 1.041 * exp(-pow(v / (1.356 * sg), 2.6))); }\n"
	"  vec3 c = vrc.yzw * g; fragc = vec4(c, dot(c, vec3(1.0 / 3.0)) * va); }\n";   /* (alpha: the light x its age, s - PH_FS_SAT: the age) */
/* (the light as the screen's values - vecrender.py SAT_SHADER: (the exposure x the light)^(1 / 2.2) in the tube's own white; the
 *  glass's glow read from its smaller image) */
static const char *PH_FS_SAT =
	"uniform sampler2D lt, ht; uniform float k, gs; uniform vec3 tint, tintg; uniform vec2 sz; out vec4 fragc;\n"
	"void main() { vec3 l = max(texelFetch(lt, ivec2(gl_FragCoord.xy), 0).rgb, 0.0) + max(txb(ht, gl_FragCoord.xy / sz).rgb, 0.0);\n"
	"  vec3 v = pow(k * l, vec3(1.0 / 2.2)); float m = max(v.r, max(v.g, v.b)) * gs;\n"   /* (the faint light - the lines' skirts, the glass's glow -
	                                              in the glow's tint, the bright core in the tube's white: vecrender.py SAT_SHADER cold()) */
	"  vec4 lr = texelFetch(lt, ivec2(gl_FragCoord.xy), 0); float ll = dot(max(lr.rgb, 0.0), vec3(1.0 / 3.0));\n"
	"  fragc = vec4(v * mix(tintg, tint, smoothstep(0.35, 0.9, m)), ll > 0.0 ? max(lr.a, 0.0) / ll : 0.0); }\n";   /* (alpha: the light's age, s - PH_FS_UPD) */
static const char *PH_FS_UPD =                  /* (the CPU's band loop: the afterglow's two parts, one refresh on) */
	"vec3 lift(vec3 g) { return g * (1.0 + 3.0 / (1.0 + g * 1.6 / 0.00625)); }\n"   /* (the afterglow's faint end lifted, everywhere: on black
	                                              as it looked through an overlay - measured, the owner 2026-11-19) */
	"uniform sampler2D fc, fp, g1t, g2t; uniform float a1, a2, t1, t2, dtv, usea, fak, kd1, kd2, acap, pex; uniform int after; uniform vec2 gs;\n"
	"layout(location = 0) out vec4 o1; layout(location = 1) out vec4 o2; layout(location = 2) out vec4 os;\n"
	"void main() { ivec2 ip = ivec2(gl_FragCoord.xy); vec2 uv = gl_FragCoord.xy / gs; vec3 f = txb(fc, uv).rgb;\n"
	"  float fa = txb(fc, uv).a * fak;\n"   /* (fak 0: the beam on the phosphor - alpha is the light's age there, not the HDR rests' light) */
	"  if (after == 0) { o1 = vec4(0.0); o2 = vec4(0.0); os = vec4(f, fa); return; }\n"
	"  vec4 pp = txb(fp, uv); vec3 p = pp.rgb * pex, g1 = texelFetch(g1t, ip, 0).rgb * kd1, g2 = texelFetch(g2t, ip, 0).rgb * kd2;\n"
	"  float age = usea * max(pp.a, 0.0) + dtv, e1 = a1 * exp(-age / t1), e2 = a2 * exp(-age / t2);\n"
	"  bool pv = any(greaterThan(p, vec3(0.0))); float lp = dot(p, vec3(1.0 / 3.0));\n"
	"  float cap = pv && lp > acap ? acap / lp : 1.0; vec3 l = pv ? max(p * cap - f, 0.0) : vec3(0.0);\n"
	"  g1 += l * e1; g2 += l * e2;\n"
	"  vec3 lit = vec3(greaterThan(f, vec3(0.0)));\n"
	"  g1 = mix(g1, max(g1 - f * a1, 0.0), lit); g2 = mix(g2, max(g2 - f * a2, 0.0), lit);\n"
	"  g1 *= vec3(greaterThanEqual(g1, vec3(1e-4))); g2 *= vec3(greaterThanEqual(g2, vec3(1e-4)));\n"
	"  o1 = vec4(g1, 0.0); o2 = vec4(g2, 0.0); os = vec4(f + lift(g1 + g2), fa); }\n";   /* (HDR: the light above white is the spot's own,
	                                              no trail of it - that trail, up to 8x the ordinary one, gave Last Human's ship a long bright tail) */
static const char *PH_FS_COMP =                 /* (Medium, Low, Basic: the phosphor smaller - what it shows, at full size) */
	"vec3 lift(vec3 g) { return g * (1.0 + 3.0 / (1.0 + g * 1.6 / 0.00625)); }\n"   /* (the afterglow's faint end lifted, everywhere: on black
	                                              as it looked through an overlay - measured, the owner 2026-11-19) */
	"uniform sampler2D fc, g1t, g2t; uniform int after; uniform vec2 sz; uniform float fak; out vec4 fragc;\n"
	"void main() { vec2 uv = gl_FragCoord.xy / sz; vec3 o = txb(fc, uv).rgb;\n"
	"  float a = txb(fc, uv).a * fak;\n"
	"  if (after != 0) { o += lift(txb(g1t, uv).rgb + txb(g2t, uv).rgb); a += txb(g1t, uv).a + txb(g2t, uv).a; }\n"
	"  fragc = vec4(o, a); }\n";
static const char *PH_FS_UI =                   /* (the ROM browser's panel: premultiplied, over the picture) */
	"uniform sampler2D t; out vec4 fragc; void main() { fragc = texelFetch(t, ivec2(gl_FragCoord.xy), 0); }\n";
static const char *PH_FS_DOWN =                 /* (glow_blur: 2 S x 2 S pixels to a cell, 1 3 3 1 by groups of S) */
	"uniform sampler2D src; uniform int s, gd; uniform ivec2 lim; out vec4 fragc;\n"
	"void main() { ivec2 c = ivec2(gl_FragCoord.xy); vec3 a = vec3(0.0); int nf = 4 * s;\n"
	"  for (int j = 0; j < nf; j++) { int yy = clamp(c.y * gd + j - s, 0, lim.y); float wy = (j / s == 0 || j / s == 3) ? 1.0 : 3.0;\n"
	"    for (int i = 0; i < nf; i++) { int xx = clamp(c.x * gd + i - s, 0, lim.x); float wx = (i / s == 0 || i / s == 3) ? 1.0 : 3.0;\n"
	"      a += wx * wy * texelFetch(src, ivec2(xx, yy), 0).rgb; } }\n"
	"  fragc = vec4(a / (64.0 * float(s * s)), 1.0); }\n";
/* (the same with the GPU's filter, the scale 2: each pair of pixels one read between them - 16 reads a cell for 64;
 *  the glow's first step was the costliest of its three on the iPhone) */
static const char *PH_FS_DOWN2 =
	"uniform sampler2D src; out vec4 fragc;\n"
	"void main() { vec2 c = floor(gl_FragCoord.xy) * 4.0, r = 1.0 / vec2(textureSize(src, 0)); vec3 a = vec3(0.0);\n"
	"  for (int j = 0; j < 4; j++) { float wy = (j == 0 || j == 3) ? 1.0 : 3.0;\n"
	"    for (int i = 0; i < 4; i++) { float wx = (i == 0 || i == 3) ? 1.0 : 3.0;\n"
	"      a += wx * wy * texture(src, (c + vec2(float(2 * i) - 1.0, float(2 * j) - 1.0)) * r).rgb; } }\n"
	"  fragc = vec4(a / 64.0, 1.0); }\n";
/* (the Gaussian with the GPU's filter: two neighbours in one read between them, by their weights - 8 reads for 15) */
static const char *PH_FS_BLURL =
	"uniform sampler2D src; uniform float w[32], o[32]; uniform int m; uniform vec2 dir; out vec4 fragc;\n"
	"void main() { vec2 c = floor(gl_FragCoord.xy) + 0.5, r = 1.0 / vec2(textureSize(src, 0)); vec3 a = vec3(0.0);\n"
	"  for (int k = 0; k < m; k++) a += w[k] * texture(src, (c + dir * o[k]) * r).rgb;\n"
	"  fragc = vec4(a, 1.0); }\n";
static const char *PH_FS_BLUR =                 /* (the Gaussian, one way: gtw) */
	"uniform sampler2D src; uniform float w[63]; uniform int n; uniform ivec2 dir, lim; out vec4 fragc;\n"
	"void main() { ivec2 c = ivec2(gl_FragCoord.xy); vec3 a = vec3(0.0);\n"
	"  for (int k = -n; k <= n; k++) a += w[k + n] * texelFetch(src, clamp(c + dir * k, ivec2(0), lim), 0).rgb;\n"
	"  fragc = vec4(a, 1.0); }\n";
/* (vecrender.py TONE_SHADER: the picture as it is up to 0.55, then rolling off towards white - bright slow shapes keep their detail;
 *  an HDR screen: what the roll-off took given back above it, softly up to the screen's peak hk - the same picture from black to
 *  the middle, the top open) */
#define PH_TONE "uniform int beam; uniform float bgh;\n" \
	"vec3 tone(vec3 c) { if (bgh > 0.0) { vec3 d = c / bgh, e = 0.55 + 0.45 * (1.0 - exp(-(d - 0.55) / 0.6)); return mix(d, e, step(0.55, d)) * bgh; }\n" \
	"  vec3 k =0.55 + 0.45 * (1.0 - exp(-(c - 0.55) / 0.6)); vec3 o = mix(c, k, step(0.55, c));\n" \
	"  if (hk > 1.0) { vec3 x = max(c - o, 0.0); float m = hk - 1.0; o += x / pow(1.0 + pow(x / m, vec3(8.0)), vec3(0.125)); }\n" \
	"  return o; }\n"
static const char *PH_FS_OUT =                  /* (pixel_out; written B G R: gpu_present reads the CPU's XRGB bytes) */
	"uniform sampler2D s, gt, ovm, ova; uniform int glow, ovon, wbon; uniform vec3 tint, wbg, wbc; uniform float bright, grey;\n"
	"uniform vec2 gsz; uniform float dith; out vec4 fragc;\n"
	"float dh(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }\n"
	"uniform float hk;\n"   /* (HDR: the top, x the screen's white - 1.0 no HDR. The knee as always; above white only the long rests' own light (hx), up to it) */
	"uniform float hl;\n"   /* (HDR, the whole picture (the own iOS app's default): the brighter a part, the closer to the screen's peak -
	                           black stays black, the faint trail nearly as it was; 0: only the rests above white) */
	"vec3 hlift(vec3 k) { if (hl < 0.5 || hl > 1.5 || hk <= 1.0) return k; float m = clamp(max(k.r, max(k.g, k.b)), 0.0, 1.0); return k * (1.0 + (hk - 1.0) * m * m); }\n"
	"uniform float vs, lt, sn, gb;\n"   /* (HDR, the Vectrex look: the bright lines' scale above mid grey - the faint ones and the trail as SDR; a line at white, the slow ones and the dots above) */
	"vec3 vxl(vec3 c) {\n"   /* (the Vectrex look: the tube's light as a real tube gives it - no knee, every part x vs in light (the
	                                                   ratios kept: slow lines, overlaps, dots), softly to the peak only near it; the room's light added after it) */
	"    vec3 l = pow(max(c, 0.0), vec3(2.2)) * vs; float p = pow(max(hk, 1.0), 2.2) * (hk > 1.0 ? lt : 1.0); float kn = hk > 1.0 ? 6.0 : sn; l = l / pow(1.0 + pow(l / p, vec3(kn)), vec3(1.0 / kn));\n"
	"    return pow(l, vec3(1.0 / 2.2)); }\n"
	"vec3 knee(vec3 c) { if (hl > 1.5) return c;\n"   /* (the Vectrex look: mapped before the room's light - vxl) */
	"  return mix(c, 1.0 - 0.25 * exp(-(c - 0.75) / 0.25), step(0.75, c)); }\n"
	PH_TONE
	"vec3 hdrx(vec3 k, float x, vec3 t) { if (hk <= 1.0 || x <= 0.0) return k; float m = hk - 1.0;\n"   /* (the rests' light above white,
	                                                   through the overlay's colour; softly up to the screen's peak) */
	"  return k + t * (x / pow(1.0 + pow(x / m, 8.0), 0.125)); }\n"
	"void main() { ivec2 ip = ivec2(gl_FragCoord.xy); vec3 o = texelFetch(s, ip, 0).rgb; float hx = texelFetch(s, ip, 0).a;\n"
	"  if (glow != 0) o += tint * txc(gt, gl_FragCoord.xy / gsz).rgb * (hl > 1.5 && hk <= 1.0 ? gb : 1.0);\n"   /* (the Vectrex look on SDR: the bloom up - the fat lines' glare) */
	"  o = max(o, 0.0) * bright;\n"
	"  if (wbon != 0) o *= mix(wbg, wbc, smoothstep(0.35, 0.9, max(o.r, max(o.g, o.b))));\n"
	"  if (ovon != 0) { vec2 u = gl_FragCoord.xy / vec2(textureSize(s, 0)); vec3 m = txb(ovm, u).rgb, la = txb(ova, u).rgb;\n"
	"    vec3 l = dot(o, vec3(1.0 / 3.0)) * m; if (hl > 1.5) l = vxl(l); o = sqrt(l * l + la * la); }\n"
	"  else { o *= 1.6; if (hl > 1.5) o = vxl(o); if (grey > 0.0) { vec3 g = grey * vec3(0.92, 0.95, 1.0); o = sqrt(o * o + g * g); } }\n"
	"  vec3 ht = ovon != 0 ? txb(ovm, gl_FragCoord.xy / vec2(textureSize(s, 0))).rgb : vec3(1.0);\n"
	"  fragc = vec4((min(beam != 0 ? tone(o) : hdrx(hlift(knee(o)), hx, ht), vec3(max(hk, 1.0))) + dith * (dh(gl_FragCoord.xy) - dh(gl_FragCoord.xy + 17.31)) / 255.0).bgr, 1.0); }\n";

static GLuint ph_prog (const char *vsrc, const char *fsrc)
{
	const char *hdr = gpu_es ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" : "#version 330 core\n";   /* (sampler2D highp: lowp by
	                                              default in ES - Apple's GPUs then read the glow's faint half-floats in coarse steps, banded) */
	const char *vs[2] = { hdr, vsrc }, *fs[3] = { hdr, g_bl, fsrc };
	GLuint v = gpu_shader (GL_VERTEX_SHADER, vs, 2), f = gpu_shader (GL_FRAGMENT_SHADER, fs, 3);
	if (!v || !f) return 0;
	GLuint p = glCreateProgram_ (); glAttachShader_ (p, v); glAttachShader_ (p, f); glLinkProgram_ (p);
	glDeleteShader_ (v); glDeleteShader_ (f);
	GLint lk = 0; glGetProgramiv_ (p, GL_LINK_STATUS, &lk);
	if (!lk) { char log[2048] = ""; glGetProgramInfoLog_ (p, sizeof log, NULL, log); fprintf (stderr, "[vectrex_emu] GPU: link: %s\n", log); return 0; }
	return p;
}
/* (a uniform's location, kept: the name looked up once per program - by the name's address, the names are literals;
 *  each lookup cost the Nova's driver a good part of the overlay's 15 ms, 2026-11-15) */
static struct { GLuint p; const char *n; GLint l; } ph_uc[512]; static int ph_nuc;
static GLint ph_ul (GLuint p, const char *n)
{
	unsigned h = ((unsigned) p * 2654435761u ^ (unsigned) (size_t) n * 40503u) & 511;
	for (int k = 0; k < 512; k++, h = (h + 1) & 511) {
		if (ph_uc[h].n == n && ph_uc[h].p == p) return ph_uc[h].l;
		if (!ph_uc[h].n) { ph_uc[h].p = p; ph_uc[h].n = n; ph_uc[h].l = glGetUniformLocation_ (p, n); ph_nuc++; return ph_uc[h].l; } }
	return glGetUniformLocation_ (p, n);
}
static GLuint ph_tex (int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filt, const void *data);
static void ph_target (GLuint t0, GLuint t1, GLuint t2, int w, int h); static void ph_fb_flush (void);
static void ph_bind (GLuint prog, const char *name, int unit, GLuint tex);
static int ph_gw, ph_gh; static GLuint ph_S, ph_B[2], ph_ovm, ph_ova;
#include "gpu_ovfx.c"
/* (gpu_reset's end: the functions, the programs) */
static void ph_reset (void)
{
	int ok = 1; memset (ph_uc, 0, sizeof ph_uc); ph_ok = 0; ph_w = ph_h = 0; ph_ovgen = -1; ph_made = 0; memset (ph_F, 0, sizeof ph_F); memset (ph_G1, 0, sizeof ph_G1); memset (ph_G2, 0, sizeof ph_G2); memset (ph_B, 0, sizeof ph_B); ph_S = ph_P = 0; ph_L = ph_H = ph_E = 0;   /* (a new context: the old names mean nothing in it) */
	if (getenv ("VX_GPUPHOS") && !atoi (getenv ("VX_GPUPHOS"))) { fprintf (stderr, "[vectrex_emu] GPU: the light on the CPU (VX_GPUPHOS=0)\n"); return; }
	GLLOAD (glGenBuffers); GLLOAD (glBindBuffer); GLLOAD (glBufferData); GLLOAD (glVertexAttribPointer); GLLOAD (glEnableVertexAttribArray);
	GLLOAD (glVertexAttribDivisor); GLLOAD (glDrawArraysInstanced); GLLOAD (glGenFramebuffers); GLLOAD (glFramebufferTexture2D);
	GLLOAD (glCheckFramebufferStatus); GLLOAD (glDrawBuffers); GLLOAD (glEnable); GLLOAD (glBlendFunc); GLLOAD (glClearColor);
	GLLOAD (glClear); GLLOAD (glUniform1fv); GLLOAD (glUniform2fv); GLLOAD (glUniform4fv); GLLOAD (glReadPixels); GLLOAD (glDeleteFramebuffers); GLLOAD (glFinish);
	glInvalidateFramebuffer_ = (pf_glInvalidateFramebuffer) hwr.get_proc_address ("glInvalidateFramebuffer");   /* (optional: GL 4.3 / ES 3) */
	if (!ok) { fprintf (stderr, "[vectrex_emu] GPU: no instancing / framebuffers - the light on the CPU\n"); return; }
	ph_cap = ph_prog (PH_VS_CAP, PH_FS_CAP); ph_beam = ph_prog (PH_VS_CAP, PH_FS_BEAM); ph_sat = ph_prog (GPU_VS, PH_FS_SAT); ph_upd = ph_prog (GPU_VS, PH_FS_UPD); ph_down = ph_prog (GPU_VS, PH_FS_DOWN);
	ph_blur = ph_prog (GPU_VS, PH_FS_BLUR); ph_down2 = ph_prog (GPU_VS, PH_FS_DOWN2); ph_blurl = ph_prog (GPU_VS, PH_FS_BLURL); ph_out = ph_prog (GPU_VS, PH_FS_OUT); ph_comp = ph_prog (GPU_VS, PH_FS_COMP); ph_ui = ph_prog (GPU_VS, PH_FS_UI); ph_uit = 0; ph_uigen = -1;
	if (!ph_cap || !ph_upd || !ph_down || !ph_blur || !ph_out || !ph_comp) return;
	glGenBuffers_ (1, &ph_vbo); glGenVertexArrays_ (1, &ph_vao); glGenFramebuffers_ (1, &ph_fbo);
	glBindVertexArray_ (ph_vao); glBindBuffer_ (GL_ARRAY_BUFFER, ph_vbo);
	for (int i = 0; i < 6; i++) { glEnableVertexAttribArray_ ((GLuint) i);
		glVertexAttribPointer_ ((GLuint) i, 4, GL_FLOAT, 0, 24 * sizeof (float), (const void *) (size_t) (i * 4 * sizeof (float)));
		glVertexAttribDivisor_ ((GLuint) i, 1); }
	glBindVertexArray_ (0); glBindBuffer_ (GL_ARRAY_BUFFER, 0);
	ph_ok = 1;
	ox_reset ();                                       /* (the overlay's effects: gpu_ovfx.c) */
}
static int ph_nfb;
static void ph_lost (void) { memset (ph_uc, 0, sizeof ph_uc); ox_lost (); ph_nfb = 0; ph_ok = 0; ph_made = 0; ph_ovm = ph_ova = 0; ph_w = ph_h = 0; memset (ph_F, 0, sizeof ph_F); memset (ph_G1, 0, sizeof ph_G1); memset (ph_G2, 0, sizeof ph_G2); memset (ph_B, 0, sizeof ph_B); ph_S = ph_P = 0; ph_L = ph_H = ph_E = 0; }   /* (the context gone: its objects with it) */
static GLuint ph_tex (int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filt, const void *data)
{
	GLuint t; glGenTextures_ (1, &t); glBindTexture_ (GL_TEXTURE_2D, t);
	glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
	glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D_ (GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data);
	return t;
}
/* (an iPhone / iPad: does its GPU filter a half-float texture itself? Read between two texels, 0 and 1 - if so the
 *  shaders use its reads, not txb's by hand; the answer in the log. VX_BLHAND=1: by hand whatever it says) */
static void ph_bltest (void)
{
#if VX_IOS || defined(VX_BLTEST)
	int ok = 1; g_bl = GL_BL_HAND; g_bl_lin = 0;
	GLLOAD (glGenFramebuffers); GLLOAD (glFramebufferTexture2D); GLLOAD (glCheckFramebufferStatus); GLLOAD (glReadPixels); GLLOAD (glDeleteFramebuffers);
	if (!ok) return;
	static const float two[8] = { 0, 0, 0, 0, 1, 1, 1, 1 }; unsigned char px[4] = { 0, 0, 0, 0 };
	const char *hdr = gpu_es ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" : "#version 330 core\n";
	const char *vs[2] = { hdr, GPU_VS }, *fs[2] = { hdr, "uniform sampler2D t; out vec4 fragc; void main() { fragc = texture(t, vec2(0.5)); }\n" };
	GLuint v = gpu_shader (GL_VERTEX_SHADER, vs, 2), f = gpu_shader (GL_FRAGMENT_SHADER, fs, 2), tp = 0, va = 0, fb = 0;
	if (v && f) { tp = glCreateProgram_ (); glAttachShader_ (tp, v); glAttachShader_ (tp, f); glLinkProgram_ (tp); }
	if (v) glDeleteShader_ (v); if (f) glDeleteShader_ (f);
	if (!tp) return;
	GLuint a = ph_tex (2, 1, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, two), b = ph_tex (1, 1, 0x8058, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, NULL), t[2];
	glGenVertexArrays_ (1, &va); glGenFramebuffers_ (1, &fb); glBindFramebuffer_ (GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D_ (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b, 0);
	if (glCheckFramebufferStatus_ (GL_FRAMEBUFFER) == 0x8CD5) {
		glViewport_ (0, 0, 1, 1); glDisable_ (GL_BLEND); glDisable_ (GL_SCISSOR_TEST); glUseProgram_ (tp); glBindVertexArray_ (va);
		glActiveTexture_ (GL_TEXTURE0); glBindTexture_ (GL_TEXTURE_2D, a); glUniform1i_ (glGetUniformLocation_ (tp, "t"), 0);
		glDrawArrays_ (GL_TRIANGLES, 0, 3); glReadPixels_ (0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		const int filt = px[0] > 118 && px[0] < 138, hand = getenv ("VX_BLHAND") && atoi (getenv ("VX_BLHAND"));
		g_bl_lin = filt && !hand; if (g_bl_lin) g_bl = GL_BL_CUBIC;
		fprintf (stderr, "[vectrex_emu] GPU: a half-float texture read between two texels (0 and 1): %.3f - %s\n", px[0] / 255.0,
			!filt ? "not filtered (nearest): read by hand" : hand ? "filtered by the GPU, read by hand all the same (VX_BLHAND)" : "filtered by the GPU: its own reads"); }
	glBindVertexArray_ (0); glBindFramebuffer_ (GL_FRAMEBUFFER, 0); glDeleteFramebuffers_ (1, &fb); glDeleteVertexArrays_ (1, &va);
	t[0] = a; t[1] = b; glDeleteTextures_ (2, t); glDeleteProgram_ (tp);
#endif
}
/* (each target its own framebuffer, made once: one framebuffer's images changed pass after pass stalled the Nova's
 *  driver - the overlay's passes 9 ms, 2026-11-15) */
static struct { GLuint t0, t1, t2, fbo; } ph_fbs[64]; static int ph_keep;   /* (ph_keep: a pass drawn over what is there) */
static void ph_fb_flush (void) { for (int i = 0; i < ph_nfb; i++) glDeleteFramebuffers_ (1, &ph_fbs[i].fbo); ph_nfb = 0; }
static void ph_target (GLuint t0, GLuint t1, GLuint t2, int w, int h)
{
	static const GLenum db[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT0 + 1, GL_COLOR_ATTACHMENT0 + 2 };
	int i = 0; while (i < ph_nfb && !(ph_fbs[i].t0 == t0 && ph_fbs[i].t1 == t1 && ph_fbs[i].t2 == t2)) i++;
	if (i == ph_nfb) {
		if (ph_nfb == 64) ph_fb_flush (), i = 0;
		GLuint f; glGenFramebuffers_ (1, &f); glBindFramebuffer_ (GL_FRAMEBUFFER, f);
		glFramebufferTexture2D_ (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t0, 0);
		if (t1) glFramebufferTexture2D_ (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1, GL_TEXTURE_2D, t1, 0);
		if (t2) glFramebufferTexture2D_ (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 2, GL_TEXTURE_2D, t2, 0);
		glDrawBuffers_ (t2 ? 3 : t1 ? 2 : 1, db);
		ph_fbs[i].t0 = t0; ph_fbs[i].t1 = t1; ph_fbs[i].t2 = t2; ph_fbs[i].fbo = f; ph_nfb = i + 1;
	} else glBindFramebuffer_ (GL_FRAMEBUFFER, ph_fbs[i].fbo);
	glViewport_ (0, 0, w, h);
	/* (every pass writes all of its target: its old contents not needed - a tiled GPU (Adreno, Mali, the Pi's V3D) then
	 *  does not load them into its tile memory first) */
	if (glInvalidateFramebuffer_ && !ph_keep) glInvalidateFramebuffer_ (GL_FRAMEBUFFER, t2 ? 3 : t1 ? 2 : 1, db);
}
static void ph_bind (GLuint prog, const char *name, int unit, GLuint tex)
{
	glActiveTexture_ (GL_TEXTURE0 + unit); glBindTexture_ (GL_TEXTURE_2D, tex); glUniform1i_ (ph_ul (prog, name), unit);
}
#define PU1F(p, n, v) glUniform1f_ (ph_ul (p, n), (v))
#define PU1I(p, n, v) glUniform1i_ (ph_ul (p, n), (v))
/* (the images at the picture's size: new, dark) */
static void ph_size (void)
{
	ph_fb_flush ();
	if (ph_E) { glDeleteTextures_ (1, &ph_E); ph_E = 0; }
	if (ph_F[0]) {                                      /* (the old images out whenever there are any - not ph_made: a new game and a
	                                                       new profile set it 0 for a dark phosphor, the images stayed: +160 MB a game
	                                                       switch at 1640 x 2096, the iPad 2026-11-19) */
		GLuint t[13] = { ph_F[0], ph_F[1], ph_G1[0], ph_G1[1], ph_G2[0], ph_G2[1], ph_S, ph_B[0], ph_B[1], ph_P, ph_L, ph_H, 0 };
		glDeleteTextures_ (ph_L ? 12 : 10, t); ph_L = ph_H = ph_E = 0; }
	const int gd = 2 * S; ph_gw = FW / gd; ph_gh = FH / gd; if (ph_gw < 2) ph_gw = 2; if (ph_gh < 2) ph_gh = 2;
	ph_pw = (int) (FW * fx_ps + 0.5); ph_ph = (int) (FH * fx_ps + 0.5); if (ph_pw < 2) ph_pw = 2; if (ph_ph < 2) ph_ph = 2;
	ph_fw = fx_lh ? ph_pw : FW; ph_fh = fx_lh ? ph_ph : FH; ph_ps_made = fx_ps; ph_lh_made = fx_lh;
	for (int k = 0; k < 2; k++) {
		ph_F[k] = ph_tex (ph_fw, ph_fh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ph_G1[k] = ph_tex (ph_pw, ph_ph, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ph_G2[k] = ph_tex (ph_pw, ph_ph, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ph_B[k] = ph_tex (ph_gw, ph_gh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL); }
	ph_S = ph_tex (FW, FH, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, NULL);
	if (ph_beam && ph_sat) { ph_L = ph_tex (ph_fw, ph_fh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, NULL); ph_H = ph_tex (ph_gw, ph_gh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL); }
	ph_P = (vx_hdr_peak > 1.0f || vx_hdr_lift > 1.5f) ? ph_tex (FW, FH, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL)   /* (HDR: room above white) */
	                          : ph_tex (FW, FH, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, NULL); ph_hdr_made = vx_hdr_peak > 1.0f || vx_hdr_lift > 1.5f;
	ph_made = 1; ph_w = FW; ph_h = FH;
	glClearColor_ (0, 0, 0, 0);
	GLuint all[7] = { ph_F[0], ph_F[1], ph_G1[0], ph_G1[1], ph_G2[0], ph_G2[1], ph_S };
	for (int k = 0; k < 7; k++) { ph_target (all[k], 0, 0, k < 2 ? ph_fw : k < 6 ? ph_pw : FW, k < 2 ? ph_fh : k < 6 ? ph_ph : FH);
		if (glCheckFramebufferStatus_ (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			fprintf (stderr, "[vectrex_emu] GPU: no float images to draw into - the light on the CPU\n"); ph_ok = 0; return; }
		glClear_ (GL_COLOR_BUFFER_BIT); }
}
/* (the overlay's tables, ov_make's: the lines' gain per filter pixel, the room's light on it) */
static void ph_overlay (void)
{
	if (ph_ovgen == ov_gen) return;
	const size_t n = (size_t) ovw * ovh; float *a = malloc (n * 8 * sizeof (float)); if (!a) return;
	for (size_t i = 0; i < n; i++) { const float *m = ov_lm + i * 6;
		a[4 * i] = m[0]; a[4 * i + 1] = m[1]; a[4 * i + 2] = m[2]; a[4 * i + 3] = 1;
		a[4 * n + 4 * i] = m[3]; a[4 * n + 4 * i + 1] = m[4]; a[4 * n + 4 * i + 2] = m[5]; a[4 * n + 4 * i + 3] = 1; }
	if (ph_ovm) { GLuint t[2] = { ph_ovm, ph_ova }; glDeleteTextures_ (2, t); }
	ph_ovm = ph_tex (ovw, ovh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, a);
	ph_ova = ph_tex (ovw, ovh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, a + 4 * n);
	free (a); ph_ovgen = ov_gen;
}
/* this refresh's light: the pieces pl[0..npl), the phosphor one refresh on (dt), into ph_P (the screen's values) */
/* (the apps' GPU diagnostic: a timed picture's steps - the lines, the phosphor, the glow, the screen's values) */
int vx_gprof_now; double vx_gpst[4]; static double ph_t0;
extern double vt_wall (void);
#define PHT(k) do { if (vx_gprof_now) { glFinish_ (); const double t_ = vt_wall (); vx_gpst[k] += t_ - ph_t0; ph_t0 = t_; } } while (0)
static int ph_frame (int npl, double dt, int glow, int after, int shot)
{
	const int beam = bt_gpu && ph_beam && ph_sat; if (beam) glow = 0;   /* (the beam on the phosphor: the glass's glow is in the light itself) */
	if (!ph_ok) return 0;
	if (vx_gprof_now) ph_t0 = vt_wall ();
	if (FW != ph_w || FH != ph_h || !ph_made || fx_ps != ph_ps_made || fx_lh != ph_lh_made || (vx_hdr_peak > 1.0f || vx_hdr_lift > 1.5f) != ph_hdr_made) { ph_size (); if (!ph_ok) return 0; }
	const int scaled = ph_pw != FW || ph_ph != FH || ph_fw != FW; const float ls = (float) ph_fw / FW;   /* (the lines' scale: Basic) */
	const int cur = ph_i, prv = ph_i ^ 1; ph_i ^= 1;
	const int ex_mode = ph_ex_mode, ex_n = ph_ex_mode ? ph_ex_n : 0, ntot = npl + ex_n; ph_ex_mode = ph_ex_n = 0;
	if (ex_n && !ph_E) ph_E = ph_tex (ph_fw, ph_fh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	/* the pieces: 24 floats each */
	if ((size_t) ntot * 24 > ph_bufn) { free (ph_buf); ph_bufn = (size_t) ntot * 24 + 4096; ph_buf = malloc (ph_bufn * sizeof (float)); }
	for (int k = 0; k < ntot; k++) { const pline *q = &pl[k]; float *o = ph_buf + (size_t) k * 24; const float ox = (float) (q->to * EW);
		o[0] = q->ax + ox; o[1] = q->ay; o[2] = q->bx + ox; o[3] = q->by;
		o[4] = q->x0 + ox; o[5] = q->y0; o[6] = q->x1 + ox; o[7] = q->y1;
		o[8] = q->n0x; o[9] = q->n0y; o[10] = q->c0 + q->n0x * ox; o[11] = (q->cut & 1) ? 1.0f : 0.0f;
		o[12] = q->n1x; o[13] = q->n1y; o[14] = q->c1 + q->n1x * ox; o[15] = (q->cut & 2) ? 1.0f : 0.0f;
		o[16] = q->R; o[17] = q->r; o[18] = q->g; o[19] = q->b; o[20] = q->k2; o[21] = q->hx; o[22] = beam ? q->ag : 0; o[23] = 0;
		if (beam) { if (ls != 1.0f) { for (int i = 0; i < 8; i++) o[i] *= ls; float sw = q->k2 * ls; if (sw < 0.5f) { const float f = (sw / 0.5f) * (sw / 0.5f); o[17] *= f; o[18] *= f; o[19] *= f; sw = 0.5f; }
			o[20] = sw; o[16] = 3.5f * sw + 1.0f; } o[21] = 0; continue; }
		if (ls != 1.0f) { for (int i = 0; i < 8; i++) o[i] *= ls; o[10] *= ls; o[14] *= ls;
			float sw = sqrtf (0.5f / q->k2) * ls; if (sw < 0.5f) { o[17] *= sw / 0.5f; o[18] *= sw / 0.5f; o[19] *= sw / 0.5f; o[21] *= sw / 0.5f; sw = 0.5f; }
			o[16] = sw * 3.0f; o[20] = 0.5f / (sw * sw); } }
	glDisable_ (GL_DEPTH_TEST); glDisable_ (GL_SCISSOR_TEST); glDisable_ (GL_CULL_FACE); glDisable_ (GL_STENCIL_TEST);
	/* the fresh light */
	if (beam && !ph_L) { ph_made = 0; ph_size (); if (!ph_ok) return 0; }
	const GLuint capp = beam ? ph_beam : ph_cap;
	for (int pass = 0; pass < (ex_n ? 2 : 1); pass++) {   /* (the picture; then, Smooth motion, the game frame's own lines for the afterglow) */
	const int p0 = pass ? npl : 0, pc = pass ? ex_n : npl; const GLuint dstF = pass ? ph_E : ph_F[cur];
	ph_target (beam ? ph_L : dstF, 0, 0, ph_fw, ph_fh); glClearColor_ (0, 0, 0, 0); glClear_ (GL_COLOR_BUFFER_BIT);
	if (pc) {
		glEnable_ (GL_BLEND); glBlendFunc_ (GL_ONE, GL_ONE);
		glUseProgram_ (capp); glUniform2f_ (ph_ul (capp, "sz"), (float) ph_fw, (float) ph_fh); if (beam) PU1I (capp, "halo", 0);
		glBindVertexArray_ (ph_vao); glBindBuffer_ (GL_ARRAY_BUFFER, ph_vbo);
		glBufferData_ (GL_ARRAY_BUFFER, (ptrdiff_t) ((size_t) pc * 24 * sizeof (float)), ph_buf + (size_t) p0 * 24, GL_STREAM_DRAW);
		glDrawArraysInstanced_ (GL_TRIANGLES, 0, 6, pc);
		glBindBuffer_ (GL_ARRAY_BUFFER, 0); glDisable_ (GL_BLEND);
	}
	if (beam) {
		/* the glass's glow: every piece once more, wide and faint (BT_HALO_F of its light, sigma BT_HALO_S units) - into an image the
		 * glow's size (so smooth a light needs no more; at the picture's size it was most of the lines' cost), read into the light below */
		const float hx = (float) ph_gw / FW, hy = (float) ph_gh / FH, hm = 0.5f * (hx + hy), hf = (float) (BT_HALO_F / (1.0 - BT_HALO_F));
		/* (Bright widens the spot, its peak kept (build_lines): the piece's light x BL_WIDTH^2 - the glass's glow is the light of the spot as
		 *  measured, as the PC's (vecrender.py _build_np: the glow's piece from the light before the widening). It came 1.7 x the PC's) */
		const float bwk = vx_bright > 0.5f ? (float) (1.0 / (BL_WIDTH * BL_WIDTH)) : 1.0f;
		if ((size_t) ntot * 24 > ph_hbufn) { free (ph_hbuf); ph_hbufn = (size_t) ntot * 24 + 4096; ph_hbuf = malloc (ph_hbufn * sizeof (float)); }
		/* (pieces that follow each other along one straight line - a line's ends and middle, its caps, the tube's segments - as one
		 *  piece with their light together: over the glow's width, 2.5 units, where along half a unit the light sat shows nothing - a
		 *  third of the glow's pieces to draw, its caps and ends together, a short dash one piece - within one unit) */
		int nh_ = 0, mh = 0; float ma[2] = { 0, 0 }, mb[2] = { 0, 0 }, mc[3] = { 0, 0, 0 }, ms = 0, ml = 0, md[2] = { 0, 0 }; int mt = 0;
		for (int k = p0; k <= p0 + pc; k++) { const pline *q = k < p0 + pc ? &pl[k] : NULL; if (q && !(q->hx > 0)) continue;
			float len = 0, dx = 0, dy = 0, f = 0; int join = 0;
			if (q) { dx = q->bx - q->ax; dy = q->by - q->ay; len = sqrtf (dx * dx + dy * dy); if (len > 1e-3f) { dx /= len; dy /= len; }
				f = (q->k2 / q->hx) * (q->k2 / q->hx) * hf * bwk;
				join = mh && q->to == mt && q->hx == ms && fabsf (q->ax - mb[0]) < 0.75f && fabsf (q->ay - mb[1]) < 0.75f && ml + len < 0.4f * ms
				       && ((len <= 1e-3f && ml <= 1e-3f) || (len > 1e-3f && ml > 1e-3f && dx * md[0] + dy * md[1] > 0.995f)); }   /* (a spot's glow fades out from it, a line's is a Gaussian: spots with spots, moving pieces with moving ones) */
			if (join) { if (len > 1e-3f) { if (ml <= 1e-3f) { md[0] = dx; md[1] = dy; } mb[0] = q->bx; mb[1] = q->by; ml += len; }
				mc[0] += q->r * f; mc[1] += q->g * f; mc[2] += q->b * f; continue; }
			if (mh) { float *o = ph_hbuf + (size_t) nh_++ * 24; const float ox = (float) (mt * EW);
				float sh = ms * hm; const float c = sh < 0.5f ? (sh / 0.5f) * (sh / 0.5f) : 1.0f; if (sh < 0.5f) sh = 0.5f;
				o[0] = o[4] = (ma[0] + ox) * hx; o[1] = o[5] = ma[1] * hy; o[2] = o[6] = (mb[0] + ox) * hx; o[3] = o[7] = mb[1] * hy;
				for (int i = 8; i < 16; i++) o[i] = 0;
				o[16] = 3.5f * sh + 1.0f; o[17] = mc[0] * c; o[18] = mc[1] * c; o[19] = mc[2] * c; o[20] = sh; o[21] = o[22] = o[23] = 0; mh = 0; }
			if (q) { mh = 1; mt = q->to; ms = q->hx; ma[0] = q->ax; ma[1] = q->ay; mb[0] = q->bx; mb[1] = q->by; ml = len; md[0] = dx; md[1] = dy;
				mc[0] = q->r * f; mc[1] = q->g * f; mc[2] = q->b * f; } }
		ph_target (ph_H, 0, 0, ph_gw, ph_gh); glClearColor_ (0, 0, 0, 0); glClear_ (GL_COLOR_BUFFER_BIT);
		if (nh_) {
			glEnable_ (GL_BLEND); glBlendFunc_ (GL_ONE, GL_ONE);
			glUseProgram_ (ph_beam); glUniform2f_ (ph_ul (ph_beam, "sz"), (float) ph_gw, (float) ph_gh); PU1I (ph_beam, "halo", 1); { static float hs[2] = { BT_HALO_A, BT_HALO_N }; static int rd; if (!rd) { rd = 1; const char *z = getenv ("VX_HALOSHAPE"); if (z) sscanf (z, "%f,%f", &hs[0], &hs[1]); } glUniform2f_ (ph_ul (ph_beam, "hsh"), hs[0], hs[1]); }
			glBindVertexArray_ (ph_vao); glBindBuffer_ (GL_ARRAY_BUFFER, ph_vbo);
			glBufferData_ (GL_ARRAY_BUFFER, (ptrdiff_t) ((size_t) nh_ * 24 * sizeof (float)), ph_hbuf, GL_STREAM_DRAW);
			glDrawArraysInstanced_ (GL_TRIANGLES, 0, 6, nh_);
			glBindBuffer_ (GL_ARRAY_BUFFER, 0); glDisable_ (GL_BLEND); }
		/* the light as the screen's values: the exposure, the tube's white (BT_TINT, moved with the White balance) */
		{ float tg[3], t0[3], tn[3], mx = 0; static const float BT_TINT[3] = { 0.65f, 0.86f, 1.0f };
		  white_gain (opt_white, tg); white_gain (12000.0, t0);
		  for (int k = 0; k < 3; k++) { tn[k] = BT_TINT[k] * tg[k] / t0[k]; if (tn[k] > mx) mx = tn[k]; }
		  glBindVertexArray_ (g_vao); ph_target (dstF, 0, 0, ph_fw, ph_fh); glUseProgram_ (ph_sat);
		  ph_bind (ph_sat, "lt", 0, ph_L); ph_bind (ph_sat, "ht", 1, ph_H);
		  PU1F (ph_sat, "k", (float) (BT_EXPO * BT_SPOT / pow (OV_LINE_GAIN, 2.2) * pow (vx_bright > 0.5f ? (vx_hdr_peak > 1.0f ? vx_hdr_peak : BL_GAIN) : 1.0, 2.2)));   /* (Bright: the tube's light lifted here - all that comes of it with it) */   /* (the PC's screen value / OV_LINE_GAIN: the scale the phosphor and the sheet work in) */
		  glUniform3f_ (ph_ul (ph_sat, "tint"), tn[0] / mx, tn[1] / mx, tn[2] / mx);
		  { /* (the glow bluer than the core - the owner, 2026-11-22: next to the tube the phone's vectors looked greenish; BT_GLOW x the tube's white) */
		    static float gt[3] = { BT_GLOW_R, BT_GLOW_G, 1.0f }; static int rd; if (!rd) { rd = 1; const char *z = getenv ("VX_GLOWTINT"); if (z) sscanf (z, "%f,%f,%f", &gt[0], &gt[1], &gt[2]); }
		    glUniform3f_ (ph_ul (ph_sat, "tintg"), tn[0] / mx * gt[0], tn[1] / mx * gt[1], tn[2] / mx * gt[2]);
		    PU1F (ph_sat, "gs", (float) (OV_LINE_GAIN / (vx_bright > 0.5f ? (vx_hdr_peak > 1.0f ? vx_hdr_peak : BL_GAIN) : 1.0))); }   /* (as a screen value of the Vectrex look) */ glUniform2f_ (ph_ul (ph_sat, "sz"), (float) ph_fw, (float) ph_fh);
		  glDrawArrays_ (GL_TRIANGLES, 0, 3); }
	}
	}
	PHT (0);
	glBindVertexArray_ (g_vao);
	/* the phosphor: the afterglow's parts (into the other pair), what it shows */
	{ extern float vx_trail_mul; const float tk = (ov && ov_on && !sheet_file ? opt_trail_k : opt_trail_k0) * vx_trail_mul;
	  /* (the afterglow's two parts - vecrender.py PHOS_A; the phone apps' own shape: vx_phone_trail) */
	  const float a1 = PHOS_A1 * tk, a2 = PHOS_A2 * tk, ta1 = PHOS_T1 * opt_trail, ta2 = PHOS_T2 * opt_trail;
	  const float kd1 = expf (-(float) dt / ta1), kd2 = expf (-(float) dt / ta2);
	  if (scaled) ph_target (ph_G1[cur], ph_G2[cur], 0, ph_pw, ph_ph); else ph_target (ph_G1[cur], ph_G2[cur], ph_S, FW, FH);
	  glUseProgram_ (ph_upd); glUniform2f_ (ph_ul (ph_upd, "gs"), (float) ph_pw, (float) ph_ph);
	  PU1F (ph_upd, "pex", ex_mode && !ex_n ? 0.0f : 1.0f);
	  ph_bind (ph_upd, "fc", 0, ph_F[cur]); ph_bind (ph_upd, "fp", 1, ex_n ? ph_E : ph_F[prv]); ph_bind (ph_upd, "g1t", 2, ph_G1[prv]); ph_bind (ph_upd, "g2t", 3, ph_G2[prv]);
	  PU1F (ph_upd, "a1", a1); PU1F (ph_upd, "a2", a2); PU1F (ph_upd, "t1", ta1); PU1F (ph_upd, "t2", ta2); PU1F (ph_upd, "dtv", (float) dt);
	  /* (the afterglow starts at the leaving light's age (its alpha: libretro_vx.c wl_from) + this refresh - vecrender.py GLOW_COMMON;
	   *  Smooth motion's game-frame lines (ph_E): one refresh, as before. VX_PHAGE=0: one refresh always - the core before 2026-11-23) */
	  { static int ua = -1; if (ua < 0) ua = !(getenv ("VX_PHAGE") && !atoi (getenv ("VX_PHAGE")));
	    PU1F (ph_upd, "usea", beam && ua && !ex_n ? 1.0f : 0.0f); }
	  PU1F (ph_upd, "fak", beam ? 0.0f : 1.0f);
	  PU1F (ph_upd, "acap", (beam ? 1.0f / 1.6f : 1.0f) * (beam && vx_bright > 0.5f ? (vx_hdr_peak > 1.0f ? vx_hdr_peak : BL_GAIN) : 1.0f));   /* (the afterglow's brightest: a line's - the PC's PHOS_ACAP, 1.0 as a screen value; the values here are 1.6 x smaller. Bright: lifted with the light) */
	  PU1F (ph_upd, "kd1", kd1); PU1F (ph_upd, "kd2", kd2); PU1I (ph_upd, "after", after);
	  glDrawArrays_ (GL_TRIANGLES, 0, 3);
	  if (scaled) { ph_target (ph_S, 0, 0, FW, FH); glUseProgram_ (ph_comp);
		ph_bind (ph_comp, "fc", 0, ph_F[cur]); ph_bind (ph_comp, "g1t", 1, ph_G1[cur]); ph_bind (ph_comp, "g2t", 2, ph_G2[cur]);
		PU1I (ph_comp, "after", after); PU1F (ph_comp, "fak", beam ? 0.0f : 1.0f); glUniform2f_ (ph_ul (ph_comp, "sz"), (float) FW, (float) FH); glDrawArrays_ (GL_TRIANGLES, 0, 3); } }
	PHT (1);
	/* the glow: down, across, up and down (glow_blur's Gaussian) */
	if (glow) {
		const int gd = 2 * S, nf = 2 * gd; double vd = 0;
		for (int i = 0; i < nf; i++) { static const float k4[4] = { 1, 3, 3, 1 }; double w = k4[i / S] / (8.0 * S), p = i - 2 * S + 0.5; vd += w * p * p; }
		const double sg = (fx_passes >= 2 ? GLOW_WSIG : 7.182) * FH / (0.924 * GLOW_WPH), vu = gd * gd / 6.0, v = sg * sg - vd - vu;
		const double sc = sqrt (v > 0.0625 * gd * gd ? v : 0.0625 * gd * gd) / gd;
		int n = (int) ceil (3.0 * sc); if (n > 31) n = 31; float w[63]; double t = 0;
		for (int k = -n; k <= n; k++) { w[k + n] = (float) exp (-0.5 * k * k / (sc * sc)); t += w[k + n]; }
		for (int k = 0; k <= 2 * n; k++) w[k] = (float) (w[k] / t);
		const int lin = g_bl_lin && ph_down2 && ph_blurl && !(getenv ("VX_GLOWOLD") && atoi (getenv ("VX_GLOWOLD")));   /* (the GPU's filter: fewer reads, the same glow) */
		if (lin && S == 2) { ph_target (ph_B[0], 0, 0, ph_gw, ph_gh); glUseProgram_ (ph_down2); ph_bind (ph_down2, "src", 0, ph_S);
			glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glDrawArrays_ (GL_TRIANGLES, 0, 3);
			glBindTexture_ (GL_TEXTURE_2D, ph_S); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); }
		else { ph_target (ph_B[0], 0, 0, ph_gw, ph_gh); glUseProgram_ (ph_down); ph_bind (ph_down, "src", 0, ph_S);
			PU1I (ph_down, "s", S); PU1I (ph_down, "gd", gd); glUniform2i_ (ph_ul (ph_down, "lim"), FW - 1, FH - 1);
			glDrawArrays_ (GL_TRIANGLES, 0, 3); }
		if (lin) { float pw[32], po[32]; int m = 0;
			for (int k = -n; k <= n; k += 2, m++) { const float w1 = w[k + n], w2 = k + 1 <= n ? w[k + 1 + n] : 0.0f; pw[m] = w1 + w2; po[m] = (float) k + w2 / (w1 + w2); }
			glUseProgram_ (ph_blurl); glUniform1fv_ (ph_ul (ph_blurl, "w"), m, pw); glUniform1fv_ (ph_ul (ph_blurl, "o"), m, po); PU1I (ph_blurl, "m", m);
			for (int pass = 0; pass < 2; pass++) {
				ph_target (ph_B[pass ^ 1], 0, 0, ph_gw, ph_gh); ph_bind (ph_blurl, "src", 0, ph_B[pass]);
				glUniform2f_ (ph_ul (ph_blurl, "dir"), pass ? 0.0f : 1.0f, pass ? 1.0f : 0.0f); glDrawArrays_ (GL_TRIANGLES, 0, 3); } }
		else {
		glUseProgram_ (ph_blur); glUniform1fv_ (ph_ul (ph_blur, "w"), 2 * n + 1, w); PU1I (ph_blur, "n", n);
		glUniform2i_ (ph_ul (ph_blur, "lim"), ph_gw - 1, ph_gh - 1);
		for (int pass = 0; pass < 2; pass++) {
			ph_target (ph_B[pass ^ 1], 0, 0, ph_gw, ph_gh); ph_bind (ph_blur, "src", 0, ph_B[pass]);
			glUniform2i_ (ph_ul (ph_blur, "dir"), pass ? 0 : 1, pass ? 1 : 0); glDrawArrays_ (GL_TRIANGLES, 0, 3); } }
	}
	PHT (2);
	/* the screen's values: through the sheet with its effects (gpu_ovfx.c), or as PH_FS_OUT */
	{ const int ovon = ov && ov_on && !sheet_file && ov_lm; if (ovon) ph_overlay ();
	  if (ovon && ph_ovm && ox_draw (glow, dt)) goto done;
	  ph_target (ph_P, 0, 0, FW, FH); glUseProgram_ (ph_out);
	  ph_bind (ph_out, "s", 0, ph_S); ph_bind (ph_out, "gt", 1, ph_B[0]); ph_bind (ph_out, "ovm", 2, ph_ovm); ph_bind (ph_out, "ova", 3, ph_ova);
	  const float gk = GLOW_K * (glow == 2 ? 2.0f : 1.0f);
	  PU1I (ph_out, "glow", glow); PU1I (ph_out, "ovon", ovon && ph_ovm); PU1I (ph_out, "wbon", wb_on && !beam); PU1I (ph_out, "beam", beam);
	  glUniform3f_ (ph_ul (ph_out, "tint"), gk * GLOW_TINT[0], gk * GLOW_TINT[1], gk * GLOW_TINT[2]);
	  glUniform3f_ (ph_ul (ph_out, "wbg"), wb_g[0], wb_g[1], wb_g[2]); glUniform3f_ (ph_ul (ph_out, "wbc"), wb_c[0], wb_c[1], wb_c[2]);
	  PU1F (ph_out, "bright", beam ? 1.0f : opt_bright); PU1F (ph_out, "grey", glass_grey); PU1F (ph_out, "dith", fx_dith ? 1.0 : 0.0); PU1F (ph_out, "hk", vx_hdr_peak > 1.0f ? vx_hdr_peak : 1.0f); PU1F (ph_out, "bgh", beam && vx_bright > 0.5f && vx_hdr_peak > 1.0f ? vx_hdr_peak : 0.0f); PU1F (ph_out, "hl", beam ? 0.0f : vx_hdr_lift); PU1F (ph_out, "vs", vx_hdr_peak > 1.0f ? vx_hdr_vs : vx_hdr_vss); PU1F (ph_out, "lt", vx_hdr_lt); PU1F (ph_out, "sn", vx_hdr_sn); PU1F (ph_out, "gb", vx_hdr_gb);
	  glUniform2f_ (ph_ul (ph_out, "gsz"), (float) (ph_gw * 2 * S), (float) (ph_gh * 2 * S));
	  glDrawArrays_ (GL_TRIANGLES, 0, 3); }
done:
	PHT (3);
	glActiveTexture_ (GL_TEXTURE0);
	if (shot) glReadPixels_ (0, 0, FW, FH, GL_RGBA, GL_UNSIGNED_BYTE, fb);   /* (test: the picture back, B G R A = the CPU's XRGB) */
	glBindVertexArray_ (0); glUseProgram_ (0); glBindFramebuffer_ (GL_FRAMEBUFFER, 0);
	return 1;
}
