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
GLFN(void, glFinish, (void)); GLFN(void, glInvalidateFramebuffer, (GLenum, GLsizei, const GLenum *)); GLFN(void, glDeleteFramebuffers, (GLsizei, const GLuint *)); GLFN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_RGBA16F 0x881A
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_ONE 1
#define GL_NEAREST 0x2600

static uint32_t *br_img; static int br_shown, br_gen; static GLuint ph_ui, ph_uit; static int ph_uigen = -1, ph_uiw, ph_uih;   /* (browser.c) */
static GLuint ph_cap, ph_upd, ph_comp, ph_down, ph_blur, ph_out, ph_vbo, ph_vao, ph_fbo;
static GLuint ph_F[2], ph_G1[2], ph_G2[2];   /* (ph_P: gpu_gl.c) */
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
float vx_hdr_peak = 1.0f;                       /* (HDR: the brightest lights top, x the screens white - the own iOS app sets it
                                                from the screens headroom; 1: SDR, as everywhere else) */
static int ph_w, ph_h, ph_i, ph_ovgen = -1, ph_made;
static float *ph_buf; static size_t ph_bufn;

static const char *PH_VS_CAP =                  /* (a piece: its box, in picture px - y down, as the CPU's rows) */
	"layout(location = 0) in vec4 ab; layout(location = 1) in vec4 box; layout(location = 2) in vec4 c0;\n"
	"layout(location = 3) in vec4 c1; layout(location = 4) in vec4 rc; layout(location = 5) in vec4 kx;\n"
	"uniform vec2 sz;\n"
	"flat out vec4 vab, vc0, vc1, vrc; flat out float vk, vx;\n"
	"void main() { int k = gl_VertexID; vec2 q = vec2((k == 1 || k == 2 || k == 4) ? 1.0 : 0.0, (k == 2 || k == 4 || k == 5) ? 1.0 : 0.0);\n"
	"  vec2 lo = min(box.xy, box.zw) - vec2(rc.x + 1.0), hi = max(box.xy, box.zw) + vec2(rc.x + 1.0);\n"
	"  vec2 p = mix(lo, hi, q); vab = ab; vc0 = c0; vc1 = c1; vrc = rc; vk = kx.x; vx = kx.y;\n"
	"  gl_Position = vec4(p / sz * 2.0 - 1.0, 0.0, 1.0); }\n";
static const char *PH_FS_CAP =                  /* (splat_band: the spot's Gaussian round the segment, the joints' cuts) */
	"flat in vec4 vab, vc0, vc1, vrc; flat in float vk, vx; out vec4 fragc;\n"
	"void main() { vec2 p = gl_FragCoord.xy, a = vab.xy, d = vab.zw - vab.xy;\n"
	"  float L2 = dot(d, d), t = L2 > 0.0 ? clamp(dot(p - a, d) / L2, 0.0, 1.0) : 0.0;\n"
	"  vec2 e = p - (a + d * t); float d2 = dot(e, e);\n"
	"  if (d2 >= vrc.x * vrc.x) discard;\n"
	"  if (vc0.w > 0.5 && dot(p, vc0.xy) < vc0.z) discard;\n"
	"  if (vc1.w > 0.5 && dot(p, vc1.xy) > vc1.z) discard;\n"
	"  float sh = exp(-d2 * vk); fragc = vec4(vrc.yzw * sh, sh * vx); }\n";
static const char *PH_FS_UPD =                  /* (the CPU's band loop: the afterglow's two parts, one refresh on) */
	"vec3 lift(vec3 g) { return g * (1.0 + 3.0 / (1.0 + g / 0.00625)); }\n"   /* (the afterglow's faint end lifted, everywhere: on black
	                                              as it looked through an overlay - measured, the owner 2026-11-19) */
	"uniform sampler2D fc, fp, g1t, g2t; uniform float a1, a2, e1, e2, kd1, kd2; uniform int after; uniform vec2 gs;\n"
	"layout(location = 0) out vec4 o1; layout(location = 1) out vec4 o2; layout(location = 2) out vec4 os;\n"
	"void main() { ivec2 ip = ivec2(gl_FragCoord.xy); vec2 uv = gl_FragCoord.xy / gs; vec3 f = txb(fc, uv).rgb;\n"
	"  float fa = txb(fc, uv).a;\n"
	"  if (after == 0) { o1 = vec4(0.0); o2 = vec4(0.0); os = vec4(f, fa); return; }\n"
	"  vec3 p = txb(fp, uv).rgb, g1 = texelFetch(g1t, ip, 0).rgb * kd1, g2 = texelFetch(g2t, ip, 0).rgb * kd2;\n"
	"  bool pv = any(greaterThan(p, vec3(0.0))); float lp = dot(p, vec3(1.0 / 3.0));\n"
	"  float cap = pv && lp > 1.0 ? 1.0 / lp : 1.0; vec3 l = pv ? max(p * cap - f, 0.0) : vec3(0.0);\n"
	"  g1 += l * e1; g2 += l * e2;\n"
	"  vec3 lit = vec3(greaterThan(f, vec3(0.0)));\n"
	"  g1 = mix(g1, max(g1 - f * a1, 0.0), lit); g2 = mix(g2, max(g2 - f * a2, 0.0), lit);\n"
	"  g1 *= vec3(greaterThanEqual(g1, vec3(1e-4))); g2 *= vec3(greaterThanEqual(g2, vec3(1e-4)));\n"
	"  o1 = vec4(g1, 0.0); o2 = vec4(g2, 0.0); os = vec4(f + lift(g1 + g2), fa); }\n";   /* (HDR: the light above white is the spot's own,
	                                              no trail of it - that trail, up to 8x the ordinary one, gave Last Human's ship a long bright tail) */
static const char *PH_FS_COMP =                 /* (Medium, Low, Basic: the phosphor smaller - what it shows, at full size) */
	"vec3 lift(vec3 g) { return g * (1.0 + 3.0 / (1.0 + g / 0.00625)); }\n"   /* (the afterglow's faint end lifted, everywhere: on black
	                                              as it looked through an overlay - measured, the owner 2026-11-19) */
	"uniform sampler2D fc, g1t, g2t; uniform int after; uniform vec2 sz; out vec4 fragc;\n"
	"void main() { vec2 uv = gl_FragCoord.xy / sz; vec3 o = txb(fc, uv).rgb;\n"
	"  float a = txb(fc, uv).a;\n"
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
static const char *PH_FS_BLUR =                 /* (the Gaussian, one way: gtw) */
	"uniform sampler2D src; uniform float w[63]; uniform int n; uniform ivec2 dir, lim; out vec4 fragc;\n"
	"void main() { ivec2 c = ivec2(gl_FragCoord.xy); vec3 a = vec3(0.0);\n"
	"  for (int k = -n; k <= n; k++) a += w[k + n] * texelFetch(src, clamp(c + dir * k, ivec2(0), lim), 0).rgb;\n"
	"  fragc = vec4(a, 1.0); }\n";
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
	"  fragc = vec4((min(hdrx(hlift(knee(o)), hx, ht), vec3(max(hk, 1.0))) + dith * (dh(gl_FragCoord.xy) - dh(gl_FragCoord.xy + 17.31)) / 255.0).bgr, 1.0); }\n";

static GLuint ph_prog (const char *vsrc, const char *fsrc)
{
	const char *hdr = gpu_es ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" : "#version 330 core\n";   /* (sampler2D highp: lowp by
	                                              default in ES - Apple's GPUs then read the glow's faint half-floats in coarse steps, banded) */
	const char *vs[2] = { hdr, vsrc }, *fs[3] = { hdr, GL_BL, fsrc };
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
	int ok = 1; memset (ph_uc, 0, sizeof ph_uc); ph_ok = 0; ph_w = ph_h = 0; ph_ovgen = -1; ph_made = 0; memset (ph_F, 0, sizeof ph_F); memset (ph_G1, 0, sizeof ph_G1); memset (ph_G2, 0, sizeof ph_G2); memset (ph_B, 0, sizeof ph_B); ph_S = ph_P = 0;   /* (a new context: the old names mean nothing in it) */
	if (getenv ("VX_GPUPHOS") && !atoi (getenv ("VX_GPUPHOS"))) { fprintf (stderr, "[vectrex_emu] GPU: the light on the CPU (VX_GPUPHOS=0)\n"); return; }
	GLLOAD (glGenBuffers); GLLOAD (glBindBuffer); GLLOAD (glBufferData); GLLOAD (glVertexAttribPointer); GLLOAD (glEnableVertexAttribArray);
	GLLOAD (glVertexAttribDivisor); GLLOAD (glDrawArraysInstanced); GLLOAD (glGenFramebuffers); GLLOAD (glFramebufferTexture2D);
	GLLOAD (glCheckFramebufferStatus); GLLOAD (glDrawBuffers); GLLOAD (glEnable); GLLOAD (glBlendFunc); GLLOAD (glClearColor);
	GLLOAD (glClear); GLLOAD (glUniform1fv); GLLOAD (glUniform2fv); GLLOAD (glUniform4fv); GLLOAD (glReadPixels); GLLOAD (glDeleteFramebuffers); GLLOAD (glFinish);
	glInvalidateFramebuffer_ = (pf_glInvalidateFramebuffer) hwr.get_proc_address ("glInvalidateFramebuffer");   /* (optional: GL 4.3 / ES 3) */
	if (!ok) { fprintf (stderr, "[vectrex_emu] GPU: no instancing / framebuffers - the light on the CPU\n"); return; }
	ph_cap = ph_prog (PH_VS_CAP, PH_FS_CAP); ph_upd = ph_prog (GPU_VS, PH_FS_UPD); ph_down = ph_prog (GPU_VS, PH_FS_DOWN);
	ph_blur = ph_prog (GPU_VS, PH_FS_BLUR); ph_out = ph_prog (GPU_VS, PH_FS_OUT); ph_comp = ph_prog (GPU_VS, PH_FS_COMP); ph_ui = ph_prog (GPU_VS, PH_FS_UI); ph_uit = 0; ph_uigen = -1;
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
static void ph_lost (void) { memset (ph_uc, 0, sizeof ph_uc); ox_lost (); ph_nfb = 0; ph_ok = 0; ph_made = 0; ph_ovm = ph_ova = 0; ph_w = ph_h = 0; memset (ph_F, 0, sizeof ph_F); memset (ph_G1, 0, sizeof ph_G1); memset (ph_G2, 0, sizeof ph_G2); memset (ph_B, 0, sizeof ph_B); ph_S = ph_P = 0; }   /* (the context gone: its objects with it) */
static GLuint ph_tex (int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filt, const void *data)
{
	GLuint t; glGenTextures_ (1, &t); glBindTexture_ (GL_TEXTURE_2D, t);
	glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
	glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D_ (GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data);
	return t;
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
	if (ph_F[0]) {                                      /* (the old images out whenever there are any - not ph_made: a new game and a
	                                                       new profile set it 0 for a dark phosphor, the images stayed: +160 MB a game
	                                                       switch at 1640 x 2096, the iPad 2026-11-19) */
		GLuint t[13] = { ph_F[0], ph_F[1], ph_G1[0], ph_G1[1], ph_G2[0], ph_G2[1], ph_S, ph_B[0], ph_B[1], ph_P, 0, 0, 0 };
		glDeleteTextures_ (10, t); }
	const int gd = 2 * S; ph_gw = FW / gd; ph_gh = FH / gd; if (ph_gw < 2) ph_gw = 2; if (ph_gh < 2) ph_gh = 2;
	ph_pw = (int) (FW * fx_ps + 0.5); ph_ph = (int) (FH * fx_ps + 0.5); if (ph_pw < 2) ph_pw = 2; if (ph_ph < 2) ph_ph = 2;
	ph_fw = fx_lh ? ph_pw : FW; ph_fh = fx_lh ? ph_ph : FH; ph_ps_made = fx_ps; ph_lh_made = fx_lh;
	for (int k = 0; k < 2; k++) {
		ph_F[k] = ph_tex (ph_fw, ph_fh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ph_G1[k] = ph_tex (ph_pw, ph_ph, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ph_G2[k] = ph_tex (ph_pw, ph_ph, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ph_B[k] = ph_tex (ph_gw, ph_gh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL); }
	ph_S = ph_tex (FW, FH, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, NULL);
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
	if (!ph_ok) return 0;
	if (vx_gprof_now) ph_t0 = vt_wall ();
	if (FW != ph_w || FH != ph_h || !ph_made || fx_ps != ph_ps_made || fx_lh != ph_lh_made || (vx_hdr_peak > 1.0f || vx_hdr_lift > 1.5f) != ph_hdr_made) { ph_size (); if (!ph_ok) return 0; }
	const int scaled = ph_pw != FW || ph_ph != FH || ph_fw != FW; const float ls = (float) ph_fw / FW;   /* (the lines' scale: Basic) */
	const int cur = ph_i, prv = ph_i ^ 1; ph_i ^= 1;
	/* the pieces: 24 floats each */
	if ((size_t) npl * 24 > ph_bufn) { free (ph_buf); ph_bufn = (size_t) npl * 24 + 4096; ph_buf = malloc (ph_bufn * sizeof (float)); }
	for (int k = 0; k < npl; k++) { const pline *q = &pl[k]; float *o = ph_buf + (size_t) k * 24; const float ox = (float) (q->to * EW);
		o[0] = q->ax + ox; o[1] = q->ay; o[2] = q->bx + ox; o[3] = q->by;
		o[4] = q->x0 + ox; o[5] = q->y0; o[6] = q->x1 + ox; o[7] = q->y1;
		o[8] = q->n0x; o[9] = q->n0y; o[10] = q->c0 + q->n0x * ox; o[11] = (q->cut & 1) ? 1.0f : 0.0f;
		o[12] = q->n1x; o[13] = q->n1y; o[14] = q->c1 + q->n1x * ox; o[15] = (q->cut & 2) ? 1.0f : 0.0f;
		o[16] = q->R; o[17] = q->r; o[18] = q->g; o[19] = q->b; o[20] = q->k2; o[21] = q->hx; o[22] = o[23] = 0;
		if (ls != 1.0f) { for (int i = 0; i < 8; i++) o[i] *= ls; o[10] *= ls; o[14] *= ls;
			float sw = sqrtf (0.5f / q->k2) * ls; if (sw < 0.5f) { o[17] *= sw / 0.5f; o[18] *= sw / 0.5f; o[19] *= sw / 0.5f; o[21] *= sw / 0.5f; sw = 0.5f; }
			o[16] = sw * 3.0f; o[20] = 0.5f / (sw * sw); } }
	glDisable_ (GL_DEPTH_TEST); glDisable_ (GL_SCISSOR_TEST); glDisable_ (GL_CULL_FACE); glDisable_ (GL_STENCIL_TEST);
	/* the fresh light */
	ph_target (ph_F[cur], 0, 0, ph_fw, ph_fh); glClearColor_ (0, 0, 0, 0); glClear_ (GL_COLOR_BUFFER_BIT);
	if (npl) {
		glEnable_ (GL_BLEND); glBlendFunc_ (GL_ONE, GL_ONE);
		glUseProgram_ (ph_cap); glUniform2f_ (ph_ul (ph_cap, "sz"), (float) ph_fw, (float) ph_fh);
		glBindVertexArray_ (ph_vao); glBindBuffer_ (GL_ARRAY_BUFFER, ph_vbo);
		glBufferData_ (GL_ARRAY_BUFFER, (ptrdiff_t) ((size_t) npl * 24 * sizeof (float)), ph_buf, GL_STREAM_DRAW);
		glDrawArraysInstanced_ (GL_TRIANGLES, 0, 6, npl);
		glBindBuffer_ (GL_ARRAY_BUFFER, 0); glDisable_ (GL_BLEND);
	}
	PHT (0);
	glBindVertexArray_ (g_vao);
	/* the phosphor: the afterglow's parts (into the other pair), what it shows */
	{ extern float vx_trail_mul; const float tk = (ov && ov_on && !sheet_file ? opt_trail_k : opt_trail_k0) * vx_trail_mul;
	  const float a1 = 0.1404f * tk, a2 = 0.0582f * tk, ta1 = 0.0454f * opt_trail, ta2 = 0.350f * opt_trail;
	  const float kd1 = expf (-(float) dt / ta1), kd2 = expf (-(float) dt / ta2);
	  if (scaled) ph_target (ph_G1[cur], ph_G2[cur], 0, ph_pw, ph_ph); else ph_target (ph_G1[cur], ph_G2[cur], ph_S, FW, FH);
	  glUseProgram_ (ph_upd); glUniform2f_ (ph_ul (ph_upd, "gs"), (float) ph_pw, (float) ph_ph);
	  ph_bind (ph_upd, "fc", 0, ph_F[cur]); ph_bind (ph_upd, "fp", 1, ph_F[prv]); ph_bind (ph_upd, "g1t", 2, ph_G1[prv]); ph_bind (ph_upd, "g2t", 3, ph_G2[prv]);
	  PU1F (ph_upd, "a1", a1); PU1F (ph_upd, "a2", a2); PU1F (ph_upd, "e1", a1 * kd1); PU1F (ph_upd, "e2", a2 * kd2);
	  PU1F (ph_upd, "kd1", kd1); PU1F (ph_upd, "kd2", kd2); PU1I (ph_upd, "after", after);
	  glDrawArrays_ (GL_TRIANGLES, 0, 3);
	  if (scaled) { ph_target (ph_S, 0, 0, FW, FH); glUseProgram_ (ph_comp);
		ph_bind (ph_comp, "fc", 0, ph_F[cur]); ph_bind (ph_comp, "g1t", 1, ph_G1[cur]); ph_bind (ph_comp, "g2t", 2, ph_G2[cur]);
		PU1I (ph_comp, "after", after); glUniform2f_ (ph_ul (ph_comp, "sz"), (float) FW, (float) FH); glDrawArrays_ (GL_TRIANGLES, 0, 3); } }
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
		ph_target (ph_B[0], 0, 0, ph_gw, ph_gh); glUseProgram_ (ph_down); ph_bind (ph_down, "src", 0, ph_S);
		PU1I (ph_down, "s", S); PU1I (ph_down, "gd", gd); glUniform2i_ (ph_ul (ph_down, "lim"), FW - 1, FH - 1);
		glDrawArrays_ (GL_TRIANGLES, 0, 3);
		glUseProgram_ (ph_blur); glUniform1fv_ (ph_ul (ph_blur, "w"), 2 * n + 1, w); PU1I (ph_blur, "n", n);
		glUniform2i_ (ph_ul (ph_blur, "lim"), ph_gw - 1, ph_gh - 1);
		for (int pass = 0; pass < 2; pass++) {
			ph_target (ph_B[pass ^ 1], 0, 0, ph_gw, ph_gh); ph_bind (ph_blur, "src", 0, ph_B[pass]);
			glUniform2i_ (ph_ul (ph_blur, "dir"), pass ? 0 : 1, pass ? 1 : 0); glDrawArrays_ (GL_TRIANGLES, 0, 3); }
	}
	PHT (2);
	/* the screen's values: through the sheet with its effects (gpu_ovfx.c), or as PH_FS_OUT */
	{ const int ovon = ov && ov_on && !sheet_file && ov_lm; if (ovon) ph_overlay ();
	  if (ovon && ph_ovm && ox_draw (glow, dt)) goto done;
	  ph_target (ph_P, 0, 0, FW, FH); glUseProgram_ (ph_out);
	  ph_bind (ph_out, "s", 0, ph_S); ph_bind (ph_out, "gt", 1, ph_B[0]); ph_bind (ph_out, "ovm", 2, ph_ovm); ph_bind (ph_out, "ova", 3, ph_ova);
	  const float gk = GLOW_K * (glow == 2 ? 2.0f : 1.0f);
	  PU1I (ph_out, "glow", glow); PU1I (ph_out, "ovon", ovon && ph_ovm); PU1I (ph_out, "wbon", wb_on);
	  glUniform3f_ (ph_ul (ph_out, "tint"), gk * GLOW_TINT[0], gk * GLOW_TINT[1], gk * GLOW_TINT[2]);
	  glUniform3f_ (ph_ul (ph_out, "wbg"), wb_g[0], wb_g[1], wb_g[2]); glUniform3f_ (ph_ul (ph_out, "wbc"), wb_c[0], wb_c[1], wb_c[2]);
	  PU1F (ph_out, "bright", opt_bright); PU1F (ph_out, "grey", glass_grey); PU1F (ph_out, "dith", fx_dith ? 1.0 : 0.0); PU1F (ph_out, "hk", vx_hdr_peak > 1.0f ? vx_hdr_peak : 1.0f); PU1F (ph_out, "hl", vx_hdr_lift); PU1F (ph_out, "vs", vx_hdr_peak > 1.0f ? vx_hdr_vs : vx_hdr_vss); PU1F (ph_out, "lt", vx_hdr_lt); PU1F (ph_out, "sn", vx_hdr_sn); PU1F (ph_out, "gb", vx_hdr_gb);
	  glUniform2f_ (ph_ul (ph_out, "gsz"), (float) (ph_gw * 2 * S), (float) (ph_gh * 2 * S));
	  glDrawArrays_ (GL_TRIANGLES, 0, 3); }
done:
	PHT (3);
	glActiveTexture_ (GL_TEXTURE0);
	if (shot) glReadPixels_ (0, 0, FW, FH, GL_RGBA, GL_UNSIGNED_BYTE, fb);   /* (test: the picture back, B G R A = the CPU's XRGB) */
	glBindVertexArray_ (0); glUseProgram_ (0); glBindFramebuffer_ (GL_FRAMEBUFFER, 0);
	return 1;
}
