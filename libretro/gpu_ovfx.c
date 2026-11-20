/* gpu_ovfx.c - Vectrex_EMU's libretro core: the overlay's effects on the GPU (included by gpu_phos.c). Ported from the
 * desktop's vecrender.py (2026-11-15): the sheet's scratches (SCR_GEN_VS: made once per overlay, seed and wear, into
 * three float images - their depth and direction harmonics), their glint and the plastic's glow from the light around
 * them (SCR_HARM_H / SCR_HARM_V at quarter size, the light smoothed over 150 ms), the light the matte scatters
 * (SCR_SHADE) and the ceiling light on the sheet (OV_LIGHT_SHADER: its reflection, the plastic brighter around it, a
 * soft glow the scratches catch). The core works in the sheet's own pixels (y down), the desktop in window pixels:
 * its sizes are scaled by kpx, the core's pixels per pixel of the owner's window (a 1970 px high picture).
 * Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md. */

#define GL_LINES 0x0001
/* (vecrender.py SCR_*: the recipe) */
static const double SCR_COUNTS[10] = { 9843, 24276, 8177, 3480, 2900, 2418, 684, 112, 8, 2 };
#define SCR_TPM (2048 / 207.0)
#define SCR_STEP_W 0.0538
#define SCR_FINE_W 0.0376
static const double SCR_BASE[3] = { 1.8145, 1.202, 0.5043 };
#define SCR_REF (4 * 18.53 * 15.42 * 0.65 * 0.7628 * SCR_FINE_W)
#define ART_GLOW_K 0.718
#define SCR_NB 4096
#define SCR_SEG 24
#define SCR_TW 1603                             /* (round (2048 x 162 / 207)) */
#define SCR_TH 2048


static GLuint ox_gen, ox_light, ox_down, ox_q1, ox_hh, ox_hv, ox_blur, ox_sheet, ox_tvbo, ox_tvao;
static GLuint ox_L, ox_Q0, ox_Q1[2], ox_B[3], ox_H[3], ox_V[2], ox_S[3], ox_art; static int ox_qw, ox_qh, ox_w, ox_h, ox_q1i, ox_scr_ok;
static char ox_key[2048]; static const unsigned char *ox_artsrc; static int ox_artgen = -1; static double ox_t_prev = -1;
static double scr_hmean, scr_hmax, scr_a2, scr_a4;

static const char *OX_CEIL =                    /* (LIGHT_GLSL: the fixture seen in perspective) */
	"const vec3 LIGHTC = vec3(1.0, 0.94, 0.894);\n"
	"uniform float lca, lsa;\n"
	"float ceiling(vec2 p, float soft) {\n"
	"    const float HZ = 0.80, CH = 2.4, FIX_W = 0.30, FIX_L = 1.2;\n"
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
	"}\n";
static const char *OX_VS_GEN =                  /* (SCR_GEN_VS: a scratch, its piece; gl_Vertex = vtx) */
	"layout(location = 0) in vec2 vtx;\n"
	"uniform float batch, limit, seed, stepid, mode, place, L0, L1, d0, d1, wv, off, chan, dmax;\n"
	"uniform vec2 scrib; uniform vec4 llen;\n"
	"out vec4 val;\n"
	"const float SW = 162.0, SH = 207.0, PI = 3.14159265;\n"
	"float h(float j, float k) { vec3 p = fract(vec3(j, batch, seed + 13.0 * k + 97.0 * stepid) * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }\n"
	"float dens(vec2 q) { float x = q.x, y = q.y, ex = min(x, 1.0 - x), ey = min(y, 1.0 - y);\n"
	"  float mid = exp(-((x - 0.5) * (x - 0.5) / 0.08 + (y - 0.62) * (y - 0.62) / 0.12)); float edge = exp(-ex / 0.06) * 0.6;\n"
	"  float corner = 1.0 - 0.7 * exp(-(ex * ex + ey * ey) / 0.02); return (0.25 + mid + edge) * corner; }\n"
	"void main() {\n"
	"  float j = vtx.x, s = vtx.y; float u = h(j, 0.0), v = h(j, 1.0); bool keep = j < limit;\n"
	"  if (place > 0.5) keep = keep && h(j, 2.0) < dens(vec2(u, v)) / dmax;\n"
	"  float hl = h(j, 3.0), pl = llen.w, pm = llen.z;\n"
	"  float L = hl < pl ? exp(mix(log(llen.y), log(L1), hl / pl)) : hl < pl + pm ? exp(mix(log(llen.x), log(llen.y), (hl - pl) / pm))\n"
	"          : exp(mix(log(L0), log(llen.x), (hl - pl - pm) / (1.0 - pl - pm)));\n"
	"  L = min(L, 100.0);\n"
	"  float th0 = h(j, 4.0) * 2.0 * PI;\n"
	"  float bend = mode > 0.5 ? sqrt(-2.0 * log(max(h(j, 5.0), 1e-6))) * cos(2.0 * PI * h(j, 6.0)) * 0.35 : 0.0;\n"
	"  float d = mix(d0, d1, h(j, 7.0)); float taper = sqrt(max(sin(PI * s), 0.0));\n"
	"  if (mode > 0.5) taper *= 0.75 + 0.25 * sin(7.0 * s + h(j, 8.0) * 6.2832);\n"
	"  float th = th0 + bend * (s - 0.5), a0 = th0 - 0.5 * bend;\n"
	"  vec2 p = abs(bend) > 1e-4 ? vec2(sin(th) - sin(a0), cos(a0) - cos(th)) * (L / bend) : vec2(cos(th0), sin(th0)) * s * L;\n"
	"  p -= 0.5 * L * vec2(cos(th0), sin(th0));\n"
	"  float sc = h(j, 9.0) < scrib.x ? scrib.y : 0.0;\n"
	"  if (sc > 0.0) { L = 6.0 + 18.0 * h(j, 10.0);\n"
	"    float K = sc * (1.2 + 1.9 * h(j, 11.0)) * (h(j, 12.0) < 0.5 ? -1.0 : 1.0), C = sc * (0.8 + 2.2 * h(j, 13.0)) * (h(j, 14.0) < 0.5 ? -1.0 : 1.0);\n"
	"    vec2 acc = vec2(0.0), tot = vec2(0.0);\n"
	"    for (int i = 0; i < 32; i++) { float t = (float(i) + 0.5) / 32.0, ts = t * s;\n"
	"      float ea = clamp((t - 0.7) / 0.3, 0.0, 1.0), eb = clamp((ts - 0.7) / 0.3, 0.0, 1.0);\n"
	"      float ha = th0 + K * (t - 0.5) + C * ea * ea, hb = th0 + K * (ts - 0.5) + C * eb * eb;\n"
	"      tot += vec2(cos(ha), sin(ha)); acc += vec2(cos(hb), sin(hb)) * s; }\n"
	"    p = (acc - 0.5 * tot) * (L / 32.0); float es = clamp((s - 0.7) / 0.3, 0.0, 1.0); th = th0 + K * (s - 0.5) + C * es * es; }\n"
	"  p += vec2(u * SW, v * SH) + off * vec2(-sin(th), cos(th));\n"
	"  gl_Position = keep ? vec4(p / vec2(SW, SH) * 2.0 - 1.0, 0.0, 1.0) : vec4(-9.0, -9.0, 0.0, 1.0);\n"
	"  float w = d * taper * wv / max(abs(cos(th)), abs(sin(th)));\n"
	"  float c2 = cos(2.0 * th), s2 = sin(2.0 * th);\n"
	"  if (chan < 0.5) val = w * vec4(1.0, c2, s2, cos(4.0 * th));\n"
	"  else if (chan < 1.5) val = vec4(w * sin(4.0 * th), 0.0, 0.0, 0.0);\n"
	"  else if (chan < 2.5) val = vec4(0.0, w, w * c2, w * s2);\n"
	"  else { float cut = stepid > 6.5 ? pow(max(sin(PI * s), 0.0), 3.0) * d / 1.25 / max(abs(cos(th)), abs(sin(th))) : 0.0;\n"
	"    val = vec4(w * cos(4.0 * th), w * sin(4.0 * th), cut, 0.0); }\n"
	"}\n";
static const char *OX_FS_GEN = "in vec4 val; out vec4 fragc; void main() { fragc = val; }\n";
static const char *OX_FS_LIGHT =                /* (the light before the sheet: PH_FS_OUT's first half; the ceiling light's soft
	                                               glow among it - mode 2 - where the scratches will catch it) */
	"uniform sampler2D s, gt, art; uniform int glow, wbon; uniform vec3 tint, wbg, wbc; uniform float bright, k2;\n"
	"uniform vec2 gsz; uniform vec3 lpos, pic; out vec4 fragc;\n"
	"void main() { ivec2 ip = ivec2(gl_FragCoord.xy); vec3 o = texelFetch(s, ip, 0).rgb;\n"
	"  if (glow != 0) o += tint * texture(gt, gl_FragCoord.xy / gsz).rgb;\n"
	"  o = max(o, 0.0) * bright;\n"
	"  if (wbon != 0) o *= mix(wbg, wbc, smoothstep(0.35, 0.9, max(o.r, max(o.g, o.b))));\n"
	"  if (k2 > 0.0) { float sheet = step(0.004, texture(art, gl_FragCoord.xy / vec2(textureSize(s, 0))).a);\n"
	"    vec2 uv = (gl_FragCoord.xy - pic.xy) / pic.z; o += LIGHTC * k2 * ceiling((uv * 0.9 - lpos.xy) / lpos.z, 0.15) * sheet; }\n"
	"  fragc = vec4(o, texelFetch(s, ip, 0).a); }\n";
static const char *OX_FS_DOWN =                 /* (DOWN_SHADER: 8 x 8 pixels, 1 3 3 1 per pair, by linear taps;
	                                               the light less its afterglow - what the plastic scatters) */
	"uniform sampler2D tex, pS, pF; uniform vec2 sp, qs; uniform float br; out vec4 fragc;\n"
	"vec3 sg(vec3 c, vec2 u) { return max(c - br * (texture(pS, u).rgb - texture(pF, u).rgb), 0.0); }\n"
	"void main() { vec2 uv = gl_FragCoord.xy / qs; vec4 c = vec4(0.0);\n"
	"  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float wi = (i == 0 || i == 3) ? 1.0 : 3.0, wj = (j == 0 || j == 3) ? 1.0 : 3.0;\n"
	"    vec2 u = uv + vec2(float(2 * i - 3), float(2 * j - 3)) * sp; vec4 t = texture(tex, u); c += wi * wj * vec4(sg(t.rgb, u), t.a); }\n"
	"  fragc = c / 64.0; }\n";
static const char *OX_FS_Q1 =                   /* (the light the plastic scatters, smoothed: GLOW_SMOOTH_S) */
	"uniform sampler2D q0, q1; uniform float a; out vec4 fragc;\n"
	"void main() { ivec2 ip = ivec2(gl_FragCoord.xy); fragc = mix(texelFetch(q1, ip, 0), texelFetch(q0, ip, 0), a); }\n";
static const char *OX_FS_BLUR =                 /* (BLUR_SHADER) */
	"uniform sampler2D tex; uniform vec2 dir, qs; out vec4 fragc;\n"
	"void main() { vec2 uv = gl_FragCoord.xy / qs; vec4 c = texture(tex, uv) * 0.2270270270;\n"
	"  c += texture(tex, uv + dir * 1.3846153846) * 0.3162162162; c += texture(tex, uv - dir * 1.3846153846) * 0.3162162162;\n"
	"  c += texture(tex, uv + dir * 3.2307692308) * 0.0702702703; c += texture(tex, uv - dir * 3.2307692308) * 0.0702702703;\n"
	"  fragc = c; }\n";
static const char *OX_FS_HH =                   /* (SCR_HARM_H, its three parts at once: the taps' weights made on the CPU -
	                                               w1 / w2: g, u g, u^2 g, u^3 g of sigma 1 / 2, w4: u^4 g of both; every st-th
	                                               cell - a large picture - each the mean of its st cells: one cell alone left a
	                                               grid of st cells over the plastic glow, the iPad 2026-11-18) */
	"uniform sampler2D src; uniform int n, st; uniform vec4 w1[65], w2[65]; uniform vec2 w4[65];\n"
	"layout(location = 0) out vec4 o0; layout(location = 1) out vec4 o1; layout(location = 2) out vec4 o2;\n"
	"void main() { vec2 sz = vec2(textureSize(src, 0)); float y = gl_FragCoord.y / sz.y, hl = float(st / 2); vec4 a = vec4(0.0), b = vec4(0.0); vec2 c = vec2(0.0);\n"
	"  for (int i = -n; i <= n; i++) { float l0 = gl_FragCoord.x + float(i * st) - hl; vec3 t = vec3(0.0);\n"
	"    for (int k = 0; k + 1 < st; k += 2) t += 2.0 * texture(src, vec2((l0 + float(k) + 0.5) / sz.x, y)).rgb;\n"
	"    if (st - 2 * (st / 2) != 0) t += texture(src, vec2((l0 + float(st - 1)) / sz.x, y)).rgb;\n"
	"    t /= float(st); float l = max(max(t.r, t.g), t.b);\n"
	"    a += l * w1[i + n]; b += l * w2[i + n]; c += l * w4[i + n]; }\n"
	"  o0 = a; o1 = b; o2 = vec4(c, 0.0, 0.0); }\n";
static const char *OX_FS_HV =                   /* (SCR_HARM_V, both parts at once; the core's rows run down: v = +j / sigma, y
	                                               down as there; qa / qb: g, g v, g v^2, g v^3 of sigma 1 / 2 x its norm, q4: g v^4) */
	"uniform sampler2D H1, H2, H3; uniform int n, st; uniform vec4 qa[65], qb[65]; uniform vec2 q4[65]; uniform float gn2;\n"
	"layout(location = 0) out vec4 o0; layout(location = 1) out vec4 o1;\n"
	"void main() { vec2 sz = vec2(textureSize(H1, 0)); float x = gl_FragCoord.x / sz.x, hl = float(st / 2); vec4 r0 = vec4(0.0); vec2 r1 = vec2(0.0);\n"
	"  for (int j = -n; j <= n; j++) { float l0 = gl_FragCoord.y + float(j * st) - hl; vec4 a = vec4(0.0), b = vec4(0.0); vec2 c = vec2(0.0);\n"
	"    for (int k = 0; k + 1 < st; k += 2) { vec2 q = vec2(x, (l0 + float(k) + 0.5) / sz.y);\n"
	"      a += 2.0 * texture(H1, q); b += 2.0 * texture(H2, q); c += 2.0 * texture(H3, q).xy; }\n"
	"    if (st - 2 * (st / 2) != 0) { vec2 q = vec2(x, (l0 + float(st - 1)) / sz.y); a += texture(H1, q); b += texture(H2, q); c += texture(H3, q).xy; }\n"
	"    a /= float(st); b /= float(st); c /= float(st);\n"
	"    vec4 A = qa[j + n], B = qb[j + n]; vec2 F = q4[j + n];\n"
	"    r0 += vec4(c.x * A.x + 2.0 * a.z * A.z + a.x * F.x, c.x * A.x - a.x * F.x, 2.0 * (a.w * A.y + a.y * A.w), c.x * A.x - 6.0 * a.z * A.z + a.x * F.x);\n"
	"    r0 += vec4(c.y * B.x + 2.0 * b.z * B.z + b.x * F.y, c.y * B.x - b.x * F.y, 2.0 * (b.w * B.y + b.y * B.w), c.y * B.x - 6.0 * b.z * B.z + b.x * F.y);\n"
	"    r1.x += 4.0 * (a.w * A.y - a.y * A.w) + 4.0 * (b.w * B.y - b.y * B.w); r1.y += b.x * B.x * gn2; }\n"
	"  o0 = r0; o1 = vec4(r1, 0.0, 0.0); }\n";
static const char *OX_FS_SHEET =                /* (the sheet: the core's filter tables (ART_SHADER), the ceiling light on it
	                                               (OV_LIGHT_SHADER modes 0 and 1), the scratches and the glow (SCR_SHADE's two
	                                               passes), the knee - written B G R as PH_FS_OUT) */
	"uniform sampler2D L, ovm, ova, art, A, B, C, M, F, V1, V2, pS, pF; uniform float br;\n"
	"vec3 sg(vec3 c, vec2 u) { return max(c - br * (texture(pS, u).rgb - texture(pF, u).rgb), 0.0); }\n"   /* (the light less its afterglow) */
	"uniform float glint, room, scrk, scron, a2, a4, gk, glow, ref, ovl, wear, pxo;\n"
	"uniform vec4 pen, rlp; uniform vec2 rpt[6]; uniform vec3 lpos, pic; uniform float dith; out vec4 fragc;\n"
	"float dh(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }\n"
	"float lum(vec3 c) { return max(max(c.r, c.g), c.b); }\n"
	"uniform float hk;\n"   /* (HDR: as PH_FS_OUT) */
	"uniform float hl;\n"   /* (HDR, the whole picture (the own iOS app's default): the brighter a part, the closer to the screen's peak -
	                           black stays black, the faint trail nearly as it was; 0: only the rests above white) */
	"vec3 hlift(vec3 k) { if (hl < 0.5 || hl > 1.5 || hk <= 1.0) return k; float m = clamp(max(k.r, max(k.g, k.b)), 0.0, 1.0); return k * (1.0 + (hk - 1.0) * m * m); }\n"
	"uniform float vs;\n"   /* (HDR, the Vectrex look: the bright lines' scale above mid grey - the faint ones and the trail as SDR; a line at white, the slow ones and the dots above) */
	"vec3 knee(vec3 c) { if (hk > 1.0 && hl > 1.5) {\n"   /* (HDR, the Vectrex look: light as a real tube gives it - no knee, every part
	                                                   x vs in light (the ratios kept: slow lines, overlaps, dots), softly to the peak only near it) */
	"    vec3 l = pow(max(c, 0.0), vec3(2.2)) * vs; float p = pow(hk, 2.2); l = l / pow(1.0 + pow(l / p, vec3(6.0)), vec3(1.0 / 6.0));\n"
	"    return pow(l, vec3(1.0 / 2.2)); }\n"
	"  return mix(c, 1.0 - 0.25 * exp(-(c - 0.75) / 0.25), step(0.75, c)); }\n"
	"vec3 hdrx(vec3 k, float x, vec3 t) { if (hk <= 1.0 || x <= 0.0) return k; float m = hk - 1.0;\n"   /* (the rests' light above white,
	                                                   through the overlay's colour; softly up to the screen's peak) */
	"  return k + t * (x / pow(1.0 + pow(x / m, 8.0), 0.125)); }\n"
	"void main() { vec2 size = vec2(textureSize(L, 0)), uv = gl_FragCoord.xy / size; vec3 o = texelFetch(L, ivec2(gl_FragCoord.xy), 0).rgb; float hx = texelFetch(L, ivec2(gl_FragCoord.xy), 0).a;\n"
	"  vec3 ag = br * (texture(pS, uv).rgb - texture(pF, uv).rgb), o0 = max(o - ag, 0.0);\n"   /* (the afterglow here, read once: the picture's and the scatter's) */
	"  vec3 m = texture(ovm, uv).rgb, la = texture(ova, uv).rgb, l = dot(o, vec3(1.0 / 3.0)) * m, l0 = dot(o0, vec3(1.0 / 3.0)) * m;\n"
	"  vec3 c = sqrt(l0 * l0 + la * la), tm = vec3(1.0);\n"   /* (the picture from the light without the afterglow; tm: what the sheet
	                                              does to the light passing it, the afterglow's too) */
	"  vec4 ov = texture(art, uv); float sheet = step(0.004, ov.a);\n"
	"  if (ovl > 0.0) { vec2 u = (gl_FragCoord.xy - pic.xy) / pic.z;\n"
	"    vec2 d = (u - lpos.xy / 0.9) * vec2(1.0 / 1.25, 1.0); float s = (70.0 + 110.0 * wear) / 917.0, d2 = dot(d, d);\n"
	"    float g = 0.30 * exp(-d2 / (2.0 * s * s)) + 0.05 * exp(-d2 / (18.0 * s * s));\n"
	"    c *= 1.0 + 6.0 * g * ovl * sheet; tm *= 1.0 + 6.0 * g * ovl * sheet;\n"
	"    c += LIGHTC * 0.45 * ovl * ceiling((u * 0.9 - lpos.xy) / lpos.z, 0.02) * sheet; }\n"
	"  if (scron > 0.0 || glow > 0.0) {\n"
	"    float opq = step(0.996, ov.a), filt = step(0.004, ov.a) * (1.0 - opq);\n"
	"    vec3 hue = ov.rgb / max(max(max(ov.r, ov.g), ov.b), 0.02);\n"
	"    vec3 trans = mix(mix(vec3(1.0), hue, filt), vec3(0.0), opq), own = mix(vec3(0.35), ov.rgb, max(filt, opq));\n"
	"    vec4 a = texture(A, uv) * scron, b = texture(B, uv) * scron, cc = texture(C, uv) * scron;\n"
	"    float D = max(a.x, 0.0), mt = D > 0.0 ? (1.0 - exp(-1.5 * D / ref)) / (1.0 - exp(-1.5)) : 0.0;\n"
	"    vec2 px = vec2(pxo) / size;\n"
	"    vec2 p2 = vec2(px.x, -px.y);\n"   /* (the scatter from the fresh light only: the fading trail lights the plastic hardly at all) */
	"    vec3 near = max((o * 2.0 + texture(L, uv + px).rgb + texture(L, uv - px).rgb + texture(L, uv + p2).rgb + texture(L, uv - p2).rgb) / 6.0 - ag, 0.0);\n"
	"    vec3 mid = texture(M, uv).rgb, far = texture(F, uv).rgb; float k = scrk * mt;\n"
	"    c *= vec3(1.0 - 0.25 * clamp(k, 0.0, 1.0) * (1.0 - opq)); tm *= vec3(1.0 - 0.25 * clamp(k, 0.0, 1.0) * (1.0 - opq));\n"
	"    vec3 halo = (0.6 * near + 0.8 * mid + 0.5 * far) * trans;\n"
	"    vec3 e = halo * k * 0.8 * glint + room * 0.6 * mt * own;\n"
	"    vec4 h = texture(V1, uv), h2 = texture(V2, uv); vec3 lc = (far + 0.002) / (lum(far) + 0.002); float Rl = max(h.x, 0.0);\n"
	"    float Gf = D * Rl - a2 * (a.y * h.y + a.z * h.z) + a4 * (a.w * h.w + b.x * h2.x);\n"
	"    float Gs = b.y * Rl - a2 * (b.z * h.y + b.w * h.z) + a4 * (cc.x * h.w + cc.y * h2.x);\n"
	"    float df = D > 0.0 ? clamp(Gf / max(D * Rl, 1e-7), 0.0, 4.0) : 0.0, ds = b.y > 0.0 ? clamp(Gs / max(b.y * Rl, 1e-7), 0.0, 4.0) : 0.0;\n"
	"    float dd = clamp(b.y * 0.6, 0.0, 1.4);\n"
	"    e += (mt * df * df + 2.0 * dd * ds * ds) * Rl * gk * glint * lc * trans + dd * room * 0.9 * own;\n"
	"    e += glow * max(h2.y, 0.0) * lc * trans;\n"
	"    if (rlp.w > 0.0) { vec3 lcol = LIGHTC * mix(vec3(1.0), trans, 0.6); float bc = b.z / max(b.y, 1e-6), bs = b.w / max(b.y, 1e-6);\n"
	"      for (int i = 0; i < 6; i++) { vec2 dv = (rpt[i] - gl_FragCoord.xy) / rlp.z;\n"   /* (both y down here) */
	"        float dl = length(dv * vec2(1.0 / 1.25, 1.0)); float ph2 = atan(dv.y, dv.x + 1e-7), c2l = cos(2.0 * ph2), s2l = sin(2.0 * ph2);\n"
	"        if (D > 0.0) { float acf = pow(clamp(0.5 - 0.5 * (a.y * c2l + a.z * s2l) / D, 0.0, 1.0), 2.0);\n"
	"          e += rlp.w / 4.0 * (0.9 * exp(-dl / 0.05) + 0.3 * exp(-dl / 0.11)) * mt * acf * 2.5 * lcol; }\n"
	"        if (b.y > 0.0) { float acr = pow(clamp(0.5 - 0.5 * (bc * c2l + bs * s2l), 0.0, 1.0), 3.0);\n"
	"          float farl = 0.5 * exp(-dl / 0.06) + 0.25 * exp(-dl / 0.10) + 0.18 * exp(-dl / 0.24) + 0.07 * exp(-dl / 0.55);\n"
	"          e += rlp.w / 4.0 * farl * dd * acr * 6.0 * lcol; } } }\n"
	"    float cut = smoothstep(pen.x, pen.y, cc.z);\n"
	"    e += cut * pen.z * (0.5 * near + 0.8 * mid + 0.6 * far) * (vec3(1.0) - trans * 0.5) + cut * room * 0.5;\n"
	"    c += e; }\n"
	"  c = sqrt(c * c + max(l * l - l0 * l0, 0.0) * tm * tm);\n"   /* (the afterglow added as light: the sheet's own light lifts it no more -
	                                              added on it as a screen value it did, 2026-11-19; with no afterglow the picture as before) */
	"  fragc = vec4((min(hdrx(hlift(knee(c)), hx, m), vec3(max(hk, 1.0))) + dith * (dh(gl_FragCoord.xy) - dh(gl_FragCoord.xy + 17.31)) / 255.0).bgr, 1.0); }\n";

static GLuint ox_prog (const char *vsrc, const char *a, const char *b)
{
	const char *hdr = gpu_es ? "#version 300 es\nprecision highp float;\nprecision highp int;\n" : "#version 330 core\n";
	const char *vs[2] = { hdr, vsrc }, *fs[3] = { hdr, a, b ? b : "" };
	GLuint v = gpu_shader (GL_VERTEX_SHADER, vs, 2), f = gpu_shader (GL_FRAGMENT_SHADER, fs, 3);
	if (!v || !f) return 0;
	GLuint p = glCreateProgram_ (); glAttachShader_ (p, v); glAttachShader_ (p, f); glLinkProgram_ (p);
	glDeleteShader_ (v); glDeleteShader_ (f);
	GLint lk = 0; glGetProgramiv_ (p, GL_LINK_STATUS, &lk);
	if (!lk) { char log[2048] = ""; glGetProgramInfoLog_ (p, sizeof log, NULL, log); fprintf (stderr, "[vectrex_emu] GPU: link: %s\n", log); return 0; }
	return p;
}
static void ox_consts (void)                    /* (vecrender.py _scr_handled_mean, scr_lobe (width 1)) */
{
	double tot = 0, mx = 0;
	for (int yi = 0; yi < 64; yi++) for (int xi = 0; xi < 48; xi++) { double x = xi / 47.0, y = yi / 63.0;
		double mid = exp (-((x - 0.5) * (x - 0.5) / 0.08 + (y - 0.62) * (y - 0.62) / 0.12)), ex = x < 1 - x ? x : 1 - x, ey = y < 1 - y ? y : 1 - y;
		double edge = exp (-ex / 0.06) * 0.6, corner = 1 - 0.7 * exp (-(ex * ex + ey * ey) / 0.02), d = (0.25 + mid + edge) * corner;
		tot += d; if (d > mx) mx = d; }
	scr_hmean = tot / (64 * 48) / mx; scr_hmax = mx;
	const double p = 4.0; double m0 = 0, s2 = 0, s4 = 0;
	for (int i = 0; i < 720; i++) { double x = (i + 0.5) * M_PI / 720, f = pow (fabs (cos (x)), p); m0 += f; s2 += f * cos (2 * x); s4 += f * cos (4 * x); }
	scr_a2 = 2 * s2 / m0; scr_a4 = 2 * s4 / m0;
}
static void ox_reset (void)                     /* (ph_reset's end) */
{
	ox_w = ox_h = 0; ox_key[0] = 0; ox_scr_ok = 0; ox_artsrc = NULL; ox_art = 0; ox_t_prev = -1;
	ox_gen = ox_prog (OX_VS_GEN, OX_FS_GEN, NULL); ox_light = ox_prog (GPU_VS, OX_CEIL, OX_FS_LIGHT);
	ox_down = ox_prog (GPU_VS, OX_FS_DOWN, NULL); ox_q1 = ox_prog (GPU_VS, OX_FS_Q1, NULL); ox_hh = ox_prog (GPU_VS, OX_FS_HH, NULL);
	ox_hv = ox_prog (GPU_VS, OX_FS_HV, NULL); ox_blur = ox_prog (GPU_VS, OX_FS_BLUR, NULL); ox_sheet = ox_prog (GPU_VS, OX_CEIL, OX_FS_SHEET);
	if (!ox_gen || !ox_light || !ox_down || !ox_q1 || !ox_hh || !ox_hv || !ox_blur || !ox_sheet) { ox_sheet = 0; fprintf (stderr, "[vectrex_emu] GPU: no overlay effects\n"); return; }
	ox_consts ();
	/* the scratches' template: SCR_NB scratches of SCR_SEG pieces (j, s0), (j, s1) */
	float *t = malloc (sizeof (float) * SCR_NB * SCR_SEG * 4); if (!t) { ox_sheet = 0; return; }
	for (int j = 0; j < SCR_NB; j++) for (int k = 0; k < SCR_SEG; k++) { float *o = t + ((size_t) j * SCR_SEG + k) * 4;
		o[0] = (float) j; o[1] = k / (float) SCR_SEG; o[2] = (float) j; o[3] = (k + 1) / (float) SCR_SEG; }
	glGenBuffers_ (1, &ox_tvbo); glGenVertexArrays_ (1, &ox_tvao); glBindVertexArray_ (ox_tvao); glBindBuffer_ (GL_ARRAY_BUFFER, ox_tvbo);
	glBufferData_ (GL_ARRAY_BUFFER, (ptrdiff_t) (sizeof (float) * SCR_NB * SCR_SEG * 4), t, 0x88E4 /* STATIC_DRAW */); free (t);
	glEnableVertexAttribArray_ (0); glVertexAttribPointer_ (0, 2, GL_FLOAT, 0, 2 * sizeof (float), NULL);
	glBindVertexArray_ (0); glBindBuffer_ (GL_ARRAY_BUFFER, 0);
}
static void ox_lost (void) { ox_sheet = 0; ox_w = ox_h = 0; ox_scr_ok = 0; ox_art = 0; ox_artsrc = NULL; for (int k = 0; k < 3; k++) ox_S[k] = 0; ox_key[0] = 0; }
#define OU1F(p, n, v) glUniform1f_ (ph_ul (p, n), (float) (v))
#define OU1I(p, n, v) glUniform1i_ (ph_ul (p, n), (v))
static void ox_quad (void) { glDrawArrays_ (GL_TRIANGLES, 0, 3); }
/* (the images at this picture's size: the light before the sheet, the quarter-size ones) */
static void ox_size (void)
{
	ph_fb_flush ();
	if (ox_w) { GLuint t[13] = { ox_L, ox_Q0, ox_Q1[0], ox_Q1[1], ox_B[0], ox_B[1], ox_B[2], ox_H[0], ox_H[1], ox_H[2], ox_V[0], ox_V[1], 0 }; glDeleteTextures_ (12, t); }
	ox_qw = FW / 4; ox_qh = FH / 4; if (ox_qw < 4) ox_qw = 4; if (ox_qh < 4) ox_qh = 4;
	ox_L = ph_tex (FW, FH, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	ox_Q0 = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	for (int k = 0; k < 2; k++) ox_Q1[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	for (int k = 0; k < 3; k++) { ox_B[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ox_H[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL); }
	for (int k = 0; k < 2; k++) ox_V[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	for (int k = 0; k < 2; k++) { ph_target (ox_Q1[k], 0, 0, ox_qw, ox_qh); glClear_ (GL_COLOR_BUFFER_BIT); }
	ox_w = FW; ox_h = FH; ox_t_prev = -1;
}
/* (the scratches for this overlay, seed and wear: vecrender.py _scr_make) */
static void ox_scratches (void)
{
	const char *b = ov_path; for (const char *q = ov_path; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
	char key[2048]; snprintf (key, sizeof key, "%s|%d|%.3f|%.3f|%.3f|%.3f|%.3f", b, ovfx.seed, ovfx.wear, ovfx.fine, ovfx.medium, ovfx.large, ovfx.scrib);
	if (ox_scr_ok && !strcmp (key, ox_key)) return;
	snprintf (ox_key, sizeof ox_key, "%s", key);
	if (!ox_S[0]) for (int k = 0; k < 3; k++) ox_S[k] = ph_tex (SCR_TW, SCR_TH, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	long nsum = 0; for (int i = 0; b[i]; i++) nsum += (long) (i + 1) * (unsigned char) b[i];
	const double sd = (double) ((ovfx.seed * 7919L + nsum) % 9973) + 1.0;
	const double dmg[3] = { ovfx.fine, ovfx.medium, ovfx.large };
	double kn_[11], kd_[11];
	for (int k = 1; k <= 10; k++) { const double base = SCR_BASE[k <= 5 ? 0 : 1]; const int g = k <= 3 ? 0 : k <= 7 ? 1 : 2;
		const double s = base * ovfx.wear * dmg[g]; kn_[k] = s; kd_[k] = s > 0 ? sqrt (s) : 0; }
	glDisable_ (GL_DEPTH_TEST); glDisable_ (GL_SCISSOR_TEST); glDisable_ (GL_CULL_FACE);
	glUseProgram_ (ox_gen); glBindVertexArray_ (ox_tvao);
	glEnable_ (GL_BLEND); glBlendFunc_ (GL_ONE, GL_ONE);
	glUniform2f_ (ph_ul (ox_gen, "scrib"), (float) (0.04 * ovfx.scrib), 1.0f);
	glUniform4f_ (ph_ul (ox_gen, "llen"), 15.0f, 35.0f, 0.04f, 0.005f);
	OU1F (ox_gen, "seed", sd); OU1F (ox_gen, "dmax", scr_hmax);
	#define OX_DRAW(n, stepid, mode, place, L0, L1, d0, d1, wv, chan, off) do { \
		double cand_ = (n) / ((place) ? scr_hmean : 1.0); long whole_ = (long) cand_; double fr_ = fmod (sd * 0.6180339 + (stepid) * 0.37, 1.0); \
		long cand = whole_ + (fr_ < cand_ - whole_ ? 1 : 0); \
		OU1F (ox_gen, "stepid", stepid); OU1F (ox_gen, "mode", mode); OU1F (ox_gen, "place", place); OU1F (ox_gen, "L0", L0); \
		OU1F (ox_gen, "L1", L1); OU1F (ox_gen, "d0", d0); OU1F (ox_gen, "d1", d1); OU1F (ox_gen, "wv", wv); OU1F (ox_gen, "chan", chan); \
		OU1F (ox_gen, "off", off); \
		for (long b_ = 0; b_ < cand; b_ += SCR_NB) { OU1F (ox_gen, "batch", b_ / SCR_NB); OU1F (ox_gen, "limit", cand - b_ < SCR_NB ? cand - b_ : SCR_NB); \
			glDrawArrays_ (GL_LINES, 0, SCR_NB * SCR_SEG * 2); } } while (0)
	const double fine_n = SCR_COUNTS[0] * kn_[1], fw = SCR_FINE_W * SCR_TPM * kd_[1];
	glClearColor_ (0, 0, 0, 0);
	for (int c = 0; c < 3; c++) { ph_target (ox_S[c], 0, 0, SCR_TW, SCR_TH); glClear_ (GL_COLOR_BUFFER_BIT);
		if (c < 2) OX_DRAW (fine_n, 0, 0, 0, 5.0, 80.0, 0.3, 1.0, fw, c, 0.0); }
	for (int c = 2; c <= 3; c++) { ph_target (ox_S[c - 1], 0, 0, SCR_TW, SCR_TH);
		for (int i = 0; i < 9; i++) { const double kn = kn_[i + 2], kd = kd_[i + 2]; if (kn <= 0) continue;
			const double depth = (0.02 + 1.58 * pow (i / 8.0, 3)) * (i >= 6 ? 1.1 : 1.0);
			OX_DRAW (SCR_COUNTS[i + 1] * kn, i + 1, 1, 1, 5.0, 80.0, 0.75, 1.25, depth * kd * SCR_STEP_W * SCR_TPM, c, 0.0); } }
	#undef OX_DRAW
	glDisable_ (GL_BLEND); glBindVertexArray_ (0);
	ox_scr_ok = 1;
}
/* the sheet over this refresh's light (ph_S and the glow ph_B[0]) into ph_P; glow: the tube's glow (PH_FS_OUT's) */
extern double vt_wall (void);
static double ox_tt[9], ox_t0; static int ox_prof = -1; static long ox_pn;   /* (test: VX_OXPROF=1 - each pass's time, the GPU waited for: the log) */
#define OXT(k) do { if (ox_prof > 0) { glFinish_ (); double t_ = vt_wall (); ox_tt[k] += t_ - ox_t0; ox_t0 = t_; } } while (0)
static int ox_draw (int glow, double dt)
{
	if (ox_prof < 0) ox_prof = getenv ("VX_OXPROF") != NULL;
	if (ox_prof > 0) { glFinish_ (); ox_t0 = vt_wall (); if (++ox_pn % 300 == 0) { fprintf (stderr, "oxprof ms:"); for (int i = 0; i < 8; i++) fprintf (stderr, " %.2f", ox_tt[i] / 300 * 1e3); fprintf (stderr, "\n"); memset (ox_tt, 0, sizeof ox_tt); } }
	if (!ox_sheet || !ov || !ov_lm) return 0;
	const int scr = ovfx.scr, pglow = ovfx.glow > 0, ovl = ovfx.ovl > 0;
	if (!scr && !pglow && !ovl) return 0;
	if (FW != ox_w || FH != ox_h) ox_size ();
	if (ox_artsrc != ov || ox_artgen != ov_gen) { if (ox_art) glDeleteTextures_ (1, &ox_art);   /* (ov_gen too: another game's overlay can
		                                       *  come at the same address - the last one's art stayed, Armor Attack's walls under
		                                       *  Scramble, the Nova 2026-11-16) */
		ox_art = ph_tex (ovw, ovh, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, ov); ox_artsrc = ov; ox_artgen = ov_gen; ox_key[0] = 0; ox_scr_ok = 0; }
	if (scr) ox_scratches (); OXT (0);
	const double kpx = FH / (0.924 * 1970.0);               /* (core px per px of the owner's window) */
	const float hh = (float) (FH / (2 * 0.924));             /* (half the picture's height, px: the room light's unit) */
	const float fxp = (float) (-0.44 + 0.6 * 1.48 * g_lamp_x), fyp = (float) (-0.72 + 0.6 * (-0.21 + 0.77 * g_lamp_y));   /* (roomlight.place: flat) */
	glBindVertexArray_ (g_vao);
	/* the light before the sheet */
	ph_target (ox_L, 0, 0, FW, FH); glUseProgram_ (ox_light);
	ph_bind (ox_light, "s", 0, ph_S); ph_bind (ox_light, "gt", 1, ph_B[0]); ph_bind (ox_light, "art", 2, ox_art);
	{ const float gk = GLOW_K * (glow == 2 ? 2.0f : 1.0f);
	  OU1I (ox_light, "glow", glow); OU1I (ox_light, "wbon", wb_on);
	  glUniform3f_ (ph_ul (ox_light, "tint"), gk * GLOW_TINT[0], gk * GLOW_TINT[1], gk * GLOW_TINT[2]);
	  glUniform3f_ (ph_ul (ox_light, "wbg"), wb_g[0], wb_g[1], wb_g[2]); glUniform3f_ (ph_ul (ox_light, "wbc"), wb_c[0], wb_c[1], wb_c[2]);
	  OU1F (ox_light, "bright", opt_bright); OU1F (ox_light, "k2", ovl && scr ? 0.6 * ovfx.ovl : 0.0);
	  glUniform2f_ (ph_ul (ox_light, "gsz"), (float) (ph_gw * 2 * S), (float) (ph_gh * 2 * S));
	  glUniform3f_ (ph_ul (ox_light, "lpos"), fxp, fyp, 0.42f); glUniform3f_ (ph_ul (ox_light, "pic"), FW / 2.0f, FH / 2.0f, hh);
	  OU1F (ox_light, "lca", cos (20 * M_PI / 180)); OU1F (ox_light, "lsa", sin (20 * M_PI / 180)); ox_quad (); } OXT (1);
	const float qs[2] = { (float) ox_qw, (float) ox_qh };
	/* the light at quarter size: smoothed (the glints, the glow), blurred (M: once, F: three times) */
	ph_target (ox_Q0, 0, 0, ox_qw, ox_qh); glUseProgram_ (ox_down); ph_bind (ox_down, "tex", 0, ox_L); ph_bind (ox_down, "pS", 1, ph_S); ph_bind (ox_down, "pF", 2, ph_F[ph_i ^ 1]); OU1F (ox_down, "br", opt_bright);
	glUniform2f_ (ph_ul (ox_down, "sp"), 1.0f / FW, 1.0f / FH); glUniform2f_ (ph_ul (ox_down, "qs"), qs[0], qs[1]); ox_quad (); OXT (2);
	{ double a = 1.0 - exp (-(dt < 0 ? 0 : dt > 1 ? 1 : dt) / 0.15); if (ox_t_prev < 0) a = 1; ox_t_prev = 1;
	  ph_target (ox_Q1[ox_q1i ^ 1], 0, 0, ox_qw, ox_qh); glUseProgram_ (ox_q1); ph_bind (ox_q1, "q0", 0, ox_Q0); ph_bind (ox_q1, "q1", 1, ox_Q1[ox_q1i]);
	  OU1F (ox_q1, "a", a); ox_quad (); ox_q1i ^= 1; } OXT (3);
	{ const double s1 = 4.0 * kpx, s2 = 12.0 * kpx, r = ovfx.reach < 0 ? 0 : ovfx.reach > 2 ? 2 : ovfx.reach;
	  const int nn = (int) ceil (5.0 * s2), st = (nn + 31) / 32, n = (nn + st - 1) / st;   /* (65 taps at most: every st-th cell) */
	  float w1[65 * 4], w2[65 * 4], w4[65 * 2], qa[65 * 4], qb[65 * 4], q4[65 * 2];
	  const double n1 = 1.0 * (1.5 - 0.5 * r) / (50.265 * s1 * s1), n2 = fmax (0.7 * r, 1e-6) / (50.265 * s2 * s2);
	  for (int i = -n; i <= n; i++) { const int k = i + n; const double x = (double) i * st, u1 = x / s1, u2 = x / s2;
		const double g1 = exp (-0.5 * u1 * u1) * st, g2 = exp (-0.5 * u2 * u2) * st;
		w1[4 * k] = (float) g1; w1[4 * k + 1] = (float) (u1 * g1); w1[4 * k + 2] = (float) (u1 * u1 * g1); w1[4 * k + 3] = (float) (u1 * u1 * u1 * g1);
		w2[4 * k] = (float) g2; w2[4 * k + 1] = (float) (u2 * g2); w2[4 * k + 2] = (float) (u2 * u2 * g2); w2[4 * k + 3] = (float) (u2 * u2 * u2 * g2);
		w4[2 * k] = (float) (u1 * u1 * u1 * u1 * g1); w4[2 * k + 1] = (float) (u2 * u2 * u2 * u2 * g2);
		/* (vertical: v = x / sigma; the norms n1 / n2 folded in) */
		qa[4 * k] = (float) (n1 * g1); qa[4 * k + 1] = (float) (n1 * g1 * u1); qa[4 * k + 2] = (float) (n1 * g1 * u1 * u1); qa[4 * k + 3] = (float) (n1 * g1 * u1 * u1 * u1);
		qb[4 * k] = (float) (n2 * g2); qb[4 * k + 1] = (float) (n2 * g2 * u2); qb[4 * k + 2] = (float) (n2 * g2 * u2 * u2); qb[4 * k + 3] = (float) (n2 * g2 * u2 * u2 * u2);
		q4[2 * k] = (float) (n1 * g1 * u1 * u1 * u1 * u1); q4[2 * k + 1] = (float) (n2 * g2 * u2 * u2 * u2 * u2); }
	  ph_target (ox_H[0], ox_H[1], ox_H[2], ox_qw, ox_qh); glUseProgram_ (ox_hh); ph_bind (ox_hh, "src", 0, ox_Q1[ox_q1i]);
	  OU1I (ox_hh, "n", n); OU1I (ox_hh, "st", st);
	  glUniform4fv_ (ph_ul (ox_hh, "w1"), 2 * n + 1, w1); glUniform4fv_ (ph_ul (ox_hh, "w2"), 2 * n + 1, w2);
	  glUniform2fv_ (ph_ul (ox_hh, "w4"), 2 * n + 1, w4); ox_quad (); OXT (4);
	  ph_target (ox_V[0], ox_V[1], 0, ox_qw, ox_qh); glUseProgram_ (ox_hv); ph_bind (ox_hv, "H1", 0, ox_H[0]); ph_bind (ox_hv, "H2", 1, ox_H[1]); ph_bind (ox_hv, "H3", 2, ox_H[2]);
	  OU1I (ox_hv, "n", n); OU1I (ox_hv, "st", st); OU1F (ox_hv, "gn2", 1.0 / (6.2832 * s2 * s2) / n2);   /* (the glow: G of sigma 2 alone) */
	  glUniform4fv_ (ph_ul (ox_hv, "qa"), 2 * n + 1, qa); glUniform4fv_ (ph_ul (ox_hv, "qb"), 2 * n + 1, qb);
	  glUniform2fv_ (ph_ul (ox_hv, "q4"), 2 * n + 1, q4); ox_quad (); OXT (5); }
	glUseProgram_ (ox_blur); glUniform2f_ (ph_ul (ox_blur, "qs"), qs[0], qs[1]);
	/* (B[0]: the light blurred, across into B[1] and back; B[2] (M) a copy after the first round, B[0] (F) after the third) */
	{ for (int it = 0; it < 3; it++) {
		ph_target (ox_B[1], 0, 0, ox_qw, ox_qh); ph_bind (ox_blur, "tex", 0, it ? ox_B[0] : ox_Q0);
		glUniform2f_ (ph_ul (ox_blur, "dir"), (float) (kpx / ox_qw), 0.0f); ox_quad ();
		ph_target (ox_B[0], 0, 0, ox_qw, ox_qh); ph_bind (ox_blur, "tex", 0, ox_B[1]);
		glUniform2f_ (ph_ul (ox_blur, "dir"), 0.0f, (float) (kpx / ox_qh)); ox_quad ();
		if (it == 0) { ph_target (ox_B[2], 0, 0, ox_qw, ox_qh); glUseProgram_ (ox_q1); ph_bind (ox_q1, "q0", 0, ox_B[0]); ph_bind (ox_q1, "q1", 1, ox_B[0]);
			OU1F (ox_q1, "a", 1.0); ox_quad (); glUseProgram_ (ox_blur); glUniform2f_ (ph_ul (ox_blur, "qs"), qs[0], qs[1]); } } } OXT (6);
	/* the sheet */
	ph_target (ph_P, 0, 0, FW, FH); glUseProgram_ (ox_sheet);
	ph_bind (ox_sheet, "L", 0, ox_L); ph_bind (ox_sheet, "ovm", 1, ph_ovm); ph_bind (ox_sheet, "ova", 2, ph_ova); ph_bind (ox_sheet, "art", 3, ox_art);
	ph_bind (ox_sheet, "A", 4, scr ? ox_S[0] : ox_art); ph_bind (ox_sheet, "B", 5, scr ? ox_S[1] : ox_art); ph_bind (ox_sheet, "C", 6, scr ? ox_S[2] : ox_art);
	ph_bind (ox_sheet, "M", 7, ox_B[2]); ph_bind (ox_sheet, "F", 8, ox_B[0]); ph_bind (ox_sheet, "V1", 9, ox_V[0]); ph_bind (ox_sheet, "V2", 10, ox_V[1]); ph_bind (ox_sheet, "pS", 11, ph_S); ph_bind (ox_sheet, "pF", 12, ph_F[ph_i ^ 1]); OU1F (ox_sheet, "br", opt_bright);
	const double room = 0.0648 * 0.0648 / 0.075 * opt_room * opt_room;   /* (vecrender.py room_light: the default x the setting squared) */
	OU1F (ox_sheet, "glint", scr ? ovfx.glint * SCR_BASE[2] : 0.0); OU1F (ox_sheet, "room", room);
	OU1F (ox_sheet, "scrk", scr ? 2.5 * 0.85 * 0.5 : 0.0); OU1F (ox_sheet, "scron", scr ? 1.0 : 0.0);
	OU1F (ox_sheet, "a2", scr_a2); OU1F (ox_sheet, "a4", scr_a4); OU1F (ox_sheet, "gk", scr ? 2.0 : 0.0);
	OU1F (ox_sheet, "glow", ART_GLOW_K * ovfx.glow); OU1F (ox_sheet, "ref", SCR_REF);
	OU1F (ox_sheet, "ovl", ovfx.ovl); OU1F (ox_sheet, "wear", ovfx.wear < 0 ? 0 : ovfx.wear > 2 ? 2 : ovfx.wear); OU1F (ox_sheet, "pxo", 1.5 * kpx);
	glUniform4f_ (ph_ul (ox_sheet, "pen"), 0.45f, 0.85f, 1.5f, 0.0f);
	glUniform4f_ (ph_ul (ox_sheet, "rlp"), 0.0f, 0.0f, hh, scr ? ovfx.ovl : 0.0f);
	glUniform3f_ (ph_ul (ox_sheet, "lpos"), fxp, fyp, 0.42f); glUniform3f_ (ph_ul (ox_sheet, "pic"), FW / 2.0f, FH / 2.0f, hh);
	OU1F (ox_sheet, "lca", cos (20 * M_PI / 180)); OU1F (ox_sheet, "lsa", sin (20 * M_PI / 180)); OU1F (ox_sheet, "dith", fx_dith ? 1.0 : 0.0); OU1F (ox_sheet, "hk", vx_hdr_peak > 1.0f ? vx_hdr_peak : 1.0f); { extern float vx_hdr_lift, vx_hdr_vs; OU1F (ox_sheet, "hl", vx_hdr_lift); OU1F (ox_sheet, "vs", vx_hdr_vs); }
	if (scr && ovfx.ovl > 0) {                                     /* (roomlight.flat_points: the light's area as six points) */
		static float key_x = -1, key_y = -1, pts[12];
		if (key_x != g_lamp_x || key_y != g_lamp_y) { key_x = g_lamp_x; key_y = g_lamp_y; rl_flat_points (g_lamp_x, g_lamp_y, pts); }
		float w[12]; for (int i = 0; i < 6; i++) { w[2 * i] = FW / 2.0f + pts[2 * i] * hh; w[2 * i + 1] = FH / 2.0f + pts[2 * i + 1] * hh; }
		glUniform2fv_ (ph_ul (ox_sheet, "rpt"), 6, w); }
	ox_quad (); OXT (7);
	return 1;
}
