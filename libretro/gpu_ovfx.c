/* gpu_ovfx.c - Vectrex_EMU's libretro core: the overlay's effects on the GPU (included by gpu_phos.c). Ported from the
 * desktop's vecrender.py (2026-11-15): the sheet's scratches (SCR_GEN_VS: made once per overlay, seed and wear, into
 * three float images - their depth and direction harmonics), their glint and the plastic's glow from the light around
 * them (SCR_HARM_H / SCR_HARM_V at quarter size - the wide sigma at eighth since 2026-11-26; not smoothed in time since
 * 2026-11-22), the light the matte scatters
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


static GLuint ox_gen, ox_kb, ox_down, ox_d8, ox_hh, ox_hv, ox_blur, ox_sheet, ox_tvbo, ox_tvao;
static GLuint ox_Q0, ox_B[3], ox_H[2], ox_V[2], ox_E, ox_EH[2], ox_EV[2], ox_S[3], ox_K[4], ox_Cq, ox_art; static int ox_qw, ox_qh, ox_ew, ox_eh, ox_w, ox_h, ox_scr_ok;
static char ox_key[2048]; static const unsigned char *ox_artsrc; static int ox_artgen = -1;
static float ox_kkey[16]; static char ox_kscr[2048]; static int ox_kok; static long ox_kn;   /* (the lasting parts: made from what, made at all, how often) */
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
/* (2026-11-26: the sheet's parts that stay the same while a game runs - the ceiling light on the sheet, the plastic brighter
 *  round it, the scratches' own light, the room lamps' glints, the scratches at the picture's size - made once into four
 *  images (ox_K) and read back each picture; the light before the sheet no longer drawn at full size: the sheet and the
 *  quarter-size light work it out from the picture's light themselves. The same picture, less work - the iPhone 17 Pro
 *  Max's overlay climbed from 5.5 to 8.3 ms a picture as it warmed, the full-size passes the most) */
static const char *OX_FS_KB =                   /* (the sheet's lasting parts, part 0: K0 = the ceiling light and the scratches' own light
	                                               added (rgb) and the light's gain through the sheet (a; negative: no scratch here);
	                                               part 1: K1, K2 = the scratches' A and B at this size, K3 = C's two, the cut's gain,
	                                               the ceiling light's soft glow - mode 2 - that the light before the sheet had) */
	"uniform sampler2D art, A, B, C; uniform int part; uniform float glint, room, scrk, scron, glow, ref, ovl, wear, k2;\n"
	"uniform vec4 pen, rlp; uniform vec2 rpt[6]; uniform vec3 lpos, pic;\n"
	"layout(location = 0) out vec4 o0; layout(location = 1) out vec4 o1; layout(location = 2) out vec4 o2;\n"
	"void main() { vec2 size = vec2(pic.xy * 2.0), uv = gl_FragCoord.xy / size; vec2 u = (gl_FragCoord.xy - pic.xy) / pic.z;\n"
	"  vec4 a = texture(A, uv) * scron, b = texture(B, uv) * scron, cc = texture(C, uv) * scron;\n"
	"  vec4 ov = texture(art, uv); float sheet = step(0.004, ov.a);\n"
	"  if (part == 1) { float cg = k2 > 0.0 ? k2 * ceiling((u * 0.9 - lpos.xy) / lpos.z, 0.15) * sheet : 0.0;\n"
	"    o0 = a; o1 = b; o2 = vec4(cc.x, cc.y, smoothstep(pen.x, pen.y, cc.z) * pen.z, cg); return; }\n"
	"  float G = 1.0; vec3 R = vec3(0.0); bool here = false;\n"
	"  if (ovl > 0.0) { vec2 d = (u - lpos.xy / 0.9) * vec2(1.0 / 1.25, 1.0); float s = (70.0 + 110.0 * wear) / 917.0, d2 = dot(d, d);\n"
	"    float g = 0.30 * exp(-d2 / (2.0 * s * s)) + 0.05 * exp(-d2 / (18.0 * s * s));\n"
	"    G = 1.0 + 6.0 * g * ovl * sheet; R = LIGHTC * 0.45 * ovl * ceiling((u * 0.9 - lpos.xy) / lpos.z, 0.02) * sheet; }\n"
	"  vec3 e = vec3(0.0);\n"
	"  if (scron > 0.0 || glow > 0.0) {\n"
	"    float opq = step(0.996, ov.a), filt = step(0.004, ov.a) * (1.0 - opq);\n"
	"    vec3 hue = ov.rgb / max(max(max(ov.r, ov.g), ov.b), 0.02);\n"
	"    vec3 trans = mix(mix(vec3(1.0), hue, filt), vec3(0.0), opq), own = mix(vec3(0.35), ov.rgb, max(filt, opq));\n"
	"    float D = max(a.x, 0.0), mt = D > 0.0 ? (1.0 - exp(-1.5 * D / ref)) / (1.0 - exp(-1.5)) : 0.0, k = scrk * mt;\n"
	"    float G2 = 1.0 - 0.25 * clamp(k, 0.0, 1.0) * (1.0 - opq); G *= G2; R *= G2;\n"
	"    e = room * 0.6 * mt * own;\n"
	"    float dd = clamp(b.y * 0.6, 0.0, 1.4); e += dd * room * 0.9 * own;\n"
	"    if (rlp.w > 0.0 && (D > 0.0 || b.y > 0.0)) { vec3 lcol = LIGHTC * mix(vec3(1.0), trans, 0.6); float bc = b.z / max(b.y, 1e-6), bs = b.w / max(b.y, 1e-6);\n"
	"      for (int i = 0; i < 6; i++) { vec2 dv = (rpt[i] - gl_FragCoord.xy) / rlp.z;\n"
	"        float dl = length(dv * vec2(1.0 / 1.25, 1.0)); if (dl > 1.6) continue;\n"
	"        float r2 = dot(dv, dv) + 1e-14, c2l = (dv.x * dv.x - dv.y * dv.y) / r2, s2l = 2.0 * dv.x * dv.y / r2;\n"
	"        if (D > 0.0) { float acf = pow(clamp(0.5 - 0.5 * (a.y * c2l + a.z * s2l) / D, 0.0, 1.0), 2.0);\n"
	"          e += rlp.w / 4.0 * (0.9 * exp(-dl / 0.05) + 0.3 * exp(-dl / 0.11)) * mt * acf * 2.5 * lcol; }\n"
	"        if (b.y > 0.0) { float acr = pow(clamp(0.5 - 0.5 * (bc * c2l + bs * s2l), 0.0, 1.0), 3.0);\n"
	"          float farl = 0.5 * exp(-dl / 0.06) + 0.25 * exp(-dl / 0.10) + 0.18 * exp(-dl / 0.24) + 0.07 * exp(-dl / 0.55);\n"
	"          e += rlp.w / 4.0 * farl * dd * acr * 6.0 * lcol; } } }\n"
	"    e += smoothstep(pen.x, pen.y, cc.z) * room * 0.5;\n"
	"    here = D > 0.0 || b.y > 0.0; }\n"
	"  o0 = vec4(R + e, here ? G : -G); }\n";
static const char *OX_FS_DOWN =                 /* (DOWN_SHADER: 8 x 8 pixels, 1 3 3 1 per pair, by linear taps; the light less its
	                                               afterglow - what the plastic scatters: the fresh light (+ the tube's glow), the
	                                               ceiling light's soft glow (Cq: mode 2 made so once); with a white balance: each
	                                               read's light worked out as the sheet's, less its afterglow. mode 2: Cq itself) */
	"uniform sampler2D pS, pF, gt, K3, Cq; uniform vec2 sp, qs, gsc; uniform vec3 tint, wbg, wbc; uniform float br, bright; uniform int mode, glow, wbon; out vec4 fragc;\n"
	"void main() { vec2 uv = gl_FragCoord.xy / qs; vec3 c = vec3(0.0);\n"
	"  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float w = ((i == 0 || i == 3) ? 1.0 : 3.0) * ((j == 0 || j == 3) ? 1.0 : 3.0);\n"
	"    vec2 u = uv + vec2(float(2 * i - 3), float(2 * j - 3)) * sp;\n"
	"    if (mode == 2) c.x += w * texture(K3, u).w;\n"
	"    else if (mode == 0) { vec3 t = texture(pF, u).rgb; if (glow != 0) t += tint * texture(gt, u * gsc).rgb; c += w * t; }\n"
	"    else { vec3 s = texture(pS, u).rgb, o = s; if (glow != 0) o += tint * texture(gt, u * gsc).rgb; o = max(o, 0.0) * bright;\n"
	"      if (wbon != 0) o *= mix(wbg, wbc, smoothstep(0.35, 0.9, max(o.r, max(o.g, o.b))));\n"
	"      c += w * max(o + LIGHTC * texture(K3, u).w - br * (s - texture(pF, u).rgb), 0.0); } }\n"
	"  c /= 64.0; if (mode == 0) c = br * c + LIGHTC * texelFetch(Cq, ivec2(gl_FragCoord.xy), 0).x;\n"
	"  fragc = vec4(c, 0.0); }\n";
static const char *OX_FS_BLUR =                 /* (BLUR_SHADER) */
	"uniform sampler2D tex; uniform vec2 dir, qs; out vec4 fragc;\n"
	"void main() { vec2 uv = gl_FragCoord.xy / qs; vec4 c = texture(tex, uv) * 0.2270270270;\n"
	"  c += texture(tex, uv + dir * 1.3846153846) * 0.3162162162; c += texture(tex, uv - dir * 1.3846153846) * 0.3162162162;\n"
	"  c += texture(tex, uv + dir * 3.2307692308) * 0.0702702703; c += texture(tex, uv - dir * 3.2307692308) * 0.0702702703;\n"
	"  fragc = c; }\n";
static const char *OX_FS_DOWN8 =                /* (the quarter-size light to eighth size for the wide parts: 8 x 8 cells, 1 3 3 1 per pair,
	                                               as DOWN_SHADER - a thin line counts the same wherever it lies) */
	"uniform sampler2D tex; out vec4 fragc;\n"
	"void main() { vec2 ts = vec2(textureSize(tex, 0)), p = gl_FragCoord.xy * 2.0; vec3 c = vec3(0.0);\n"
	"  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float w = ((i == 0 || i == 3) ? 1.0 : 3.0) * ((j == 0 || j == 3) ? 1.0 : 3.0);\n"
	"    c += w * texture(tex, (p + vec2(float(2 * i - 3), float(2 * j - 3))) / ts).rgb; }\n"
	"  fragc = vec4(c / 64.0, 0.0); }\n";
static const char *OX_FS_HH =                   /* (SCR_HARM_H, one sigma's parts at once - the narrow one at quarter size, the wide one
	                                               at eighth: the taps' weights made on the CPU - w: g, u g, u^2 g, u^3 g, w4: u^4 g;
	                                               every st-th cell - a large picture - each the mean of its st cells: one cell alone left
	                                               a grid of st cells over the plastic glow, the iPad 2026-11-18) */
	"uniform sampler2D src; uniform int n, st; uniform vec4 w[65]; uniform float w4[65];\n"
	"layout(location = 0) out vec4 o0; layout(location = 1) out vec4 o1;\n"
	"void main() { vec2 sz = vec2(textureSize(src, 0)); float y = gl_FragCoord.y / sz.y, hl = float(st / 2); vec4 a = vec4(0.0); float c = 0.0;\n"
	"  for (int i = -n; i <= n; i++) { float l0 = gl_FragCoord.x + float(i * st) - hl; vec3 t = vec3(0.0);\n"
	"    for (int k = 0; k + 1 < st; k += 2) t += 2.0 * texture(src, vec2((l0 + float(k) + 0.5) / sz.x, y)).rgb;\n"
	"    if (st - 2 * (st / 2) != 0) t += texture(src, vec2((l0 + float(st - 1)) / sz.x, y)).rgb;\n"
	"    t /= float(st); float l = max(max(t.r, t.g), t.b);\n"
	"    a += l * w[i + n]; c += l * w4[i + n]; }\n"
	"  o0 = a; o1 = vec4(c, 0.0, 0.0, 0.0); }\n";
static const char *OX_FS_HV =                   /* (SCR_HARM_V, one sigma's parts; the core's rows run down: v = +j / sigma, y down as there;
	                                               q: g, g v, g v^2, g v^3 x its norm, q4: g v^4; gn: the glow's share (G alone, the wide
	                                               sigma). add: the wide sigma's sums (eighth size) added - read at this cell's place,
	                                               sh: where its taps' middle lies) */
	"uniform sampler2D Ha, Hb, Va, Vb; uniform int n, st, add; uniform vec4 q[65]; uniform float q4[65]; uniform float gn; uniform vec2 sh;\n"
	"layout(location = 0) out vec4 o0; layout(location = 1) out vec4 o1;\n"
	"void main() { vec2 sz = vec2(textureSize(Ha, 0)); float x = gl_FragCoord.x / sz.x, hl = float(st / 2); vec4 r0 = vec4(0.0); vec2 r1 = vec2(0.0);\n"
	"  for (int j = -n; j <= n; j++) { float l0 = gl_FragCoord.y + float(j * st) - hl; vec4 a = vec4(0.0); float c = 0.0;\n"
	"    for (int k = 0; k + 1 < st; k += 2) { vec2 p = vec2(x, (l0 + float(k) + 0.5) / sz.y); a += 2.0 * texture(Ha, p); c += 2.0 * texture(Hb, p).x; }\n"
	"    if (st - 2 * (st / 2) != 0) { vec2 p = vec2(x, (l0 + float(st - 1)) / sz.y); a += texture(Ha, p); c += texture(Hb, p).x; }\n"
	"    a /= float(st); c /= float(st);\n"
	"    vec4 A = q[j + n]; float F = q4[j + n];\n"
	"    r0 += vec4(c * A.x + 2.0 * a.z * A.z + a.x * F, c * A.x - a.x * F, 2.0 * (a.w * A.y + a.y * A.w), c * A.x - 6.0 * a.z * A.z + a.x * F);\n"
	"    r1.x += 4.0 * (a.w * A.y - a.y * A.w); r1.y += a.x * A.x * gn; }\n"
	"  if (add != 0) { vec2 e = (gl_FragCoord.xy + sh) * 0.5 / vec2(textureSize(Va, 0)); r0 += texture(Va, e); r1 += texture(Vb, e).xy; }\n"
	"  o0 = r0; o1 = vec4(r1, 0.0, 0.0); }\n";
static const char *OX_FS_SHEET =                /* (the sheet: the core's filter tables (ART_SHADER), the ceiling light on it
	                                               (OV_LIGHT_SHADER modes 0 and 1), the scratches and the glow (SCR_SHADE's two
	                                               passes), the knee - written B G R as PH_FS_OUT) */
	"uniform sampler2D pS, pF, gt, K0, K1, K2, K3, ovm, ova, art, M, F, V1, V2; uniform float br, bright; uniform int tglow, wbon;\n"
	"uniform vec3 tint, wbg, wbc; uniform vec2 gsz;\n"
	"vec3 light(vec3 s, vec2 p, float k3) { vec3 o = s; if (tglow != 0) o += tint * texture(gt, p / gsz).rgb; o = max(o, 0.0) * bright;\n"   /* (the light
	                                              before the sheet - PH_FS_OUT's first half; p: where, in pixels) */
	"  if (wbon != 0) o *= mix(wbg, wbc, smoothstep(0.35, 0.9, max(o.r, max(o.g, o.b)))); return o + LIGHTC * k3; }\n"
	"uniform float glint, scrk, scron, a2, a4, gk, glow, ref, pxo;\n"
	"uniform float dith; out vec4 fragc;\n"
	"float dh(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }\n"
	"float lum(vec3 c) { return max(max(c.r, c.g), c.b); }\n"
	"uniform float hk;\n"   /* (HDR: as PH_FS_OUT) */
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
	"void main() { ivec2 ip = ivec2(gl_FragCoord.xy); vec2 size = vec2(textureSize(pS, 0)), uv = gl_FragCoord.xy / size;\n"
	"  vec4 s4 = texelFetch(pS, ip, 0), k3 = texelFetch(K3, ip, 0), k0 = texelFetch(K0, ip, 0); vec3 o = light(s4.rgb, gl_FragCoord.xy, k3.w); float hx = s4.a;\n"
	"  vec3 ag = br * (s4.rgb - texture(pF, uv).rgb), o0 = max(o - ag, 0.0);\n"   /* (the afterglow here, read once: the picture's and the scatter's) */
	"  vec3 m = texture(ovm, uv).rgb, la = texture(ova, uv).rgb, l = dot(o, vec3(1.0 / 3.0)) * m, l0 = dot(o0, vec3(1.0 / 3.0)) * m;\n"
	"  if (hl > 1.5) { l = vxl(l); l0 = vxl(l0); }\n"   /* (the Vectrex look: the tube's light only - the room's on the sheet as it is) */
	"  float tm = abs(k0.w); vec3 c = sqrt(l0 * l0 + la * la) * tm + k0.rgb;\n"   /* (the picture from the light without the afterglow; tm: what
	                                              the sheet does to the light passing it, the afterglow's too; k0.rgb: the sheet's own light) */
	"  if (scron > 0.0 || glow > 0.0) {\n"
	"    vec4 ov = texture(art, uv); float opq = step(0.996, ov.a), filt = step(0.004, ov.a) * (1.0 - opq);\n"
	"    vec3 hue = ov.rgb / max(max(max(ov.r, ov.g), ov.b), 0.02), trans = mix(mix(vec3(1.0), hue, filt), vec3(0.0), opq);\n"
	"    vec3 mid = texture(M, uv).rgb, far = texture(F, uv).rgb; vec4 h2 = texture(V2, uv); vec3 lc = (far + 0.002) / (lum(far) + 0.002);\n"
	"    vec3 e = glow * max(h2.y, 0.0) * lc * trans;\n"
	"    vec4 a = vec4(0.0), b = vec4(0.0); float D = 0.0, mt = 0.0, k = 0.0;\n"
	"    if (k0.w > 0.0) { a = texelFetch(K1, ip, 0); b = texelFetch(K2, ip, 0);\n"   /* (a scratch here) */
	"      D = max(a.x, 0.0); mt = D > 0.0 ? (1.0 - exp(-1.5 * D / ref)) / (1.0 - exp(-1.5)) : 0.0; k = scrk * mt; }\n"
	"    vec3 near = vec3(0.0);\n"
	"    if (k > 0.0 || k3.z > 0.0) { vec2 px = vec2(pxo) / size, p2 = vec2(px.x, -px.y);\n"   /* (the scatter from the fresh light only: the fading trail lights the plastic hardly at all) */
	"      vec2 q1 = uv + px, q2 = uv - px, q3 = uv + p2, q4 = uv - p2;\n"
	"      near = max((o * 2.0 + light(texture(pS, q1).rgb, q1 * size, texture(K3, q1).w) + light(texture(pS, q2).rgb, q2 * size, texture(K3, q2).w)\n"
	"        + light(texture(pS, q3).rgb, q3 * size, texture(K3, q3).w) + light(texture(pS, q4).rgb, q4 * size, texture(K3, q4).w)) / 6.0 - ag, 0.0); }\n"
	"    if (k0.w > 0.0) {\n"
	"      e += (0.6 * near + 0.8 * mid + 0.5 * far) * trans * k * 0.8 * glint;\n"
	"      vec4 h = texture(V1, uv); float Rl = max(h.x, 0.0);\n"
	"      float Gf = D * Rl - a2 * (a.y * h.y + a.z * h.z) + a4 * (a.w * h.w + b.x * h2.x);\n"
	"      float Gs = b.y * Rl - a2 * (b.z * h.y + b.w * h.z) + a4 * (k3.x * h.w + k3.y * h2.x);\n"
	"      float df = D > 0.0 ? clamp(Gf / max(D * Rl, 1e-7), 0.0, 4.0) : 0.0, ds = b.y > 0.0 ? clamp(Gs / max(b.y * Rl, 1e-7), 0.0, 4.0) : 0.0;\n"
	"      float dd = clamp(b.y * 0.6, 0.0, 1.4);\n"
	"      e += (mt * df * df + 2.0 * dd * ds * ds) * Rl * gk * glint * lc * trans; }\n"
	"    e += k3.z * (0.5 * near + 0.8 * mid + 0.6 * far) * (vec3(1.0) - trans * 0.5);\n"
	"    c += e; }\n"
	"  c = sqrt(c * c + max(l * l - l0 * l0, 0.0) * tm * tm);\n"   /* (the afterglow added as light: the sheet's own light lifts it no more -
	                                              added on it as a screen value it did, 2026-11-19; with no afterglow the picture as before) */
	"  fragc = vec4((min(beam != 0 ? tone(c) : hdrx(hlift(knee(c)), hx, m), vec3(max(hk, 1.0))) + dith * (dh(gl_FragCoord.xy) - dh(gl_FragCoord.xy + 17.31)) / 255.0).bgr, 1.0); }\n";

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
	ox_w = ox_h = 0; ox_key[0] = 0; ox_scr_ok = 0; ox_artsrc = NULL; ox_art = 0; ox_kok = 0; for (int k = 0; k < 3; k++) ox_S[k] = 0;
	ox_gen = ox_prog (OX_VS_GEN, OX_FS_GEN, NULL); ox_kb = ox_prog (GPU_VS, OX_CEIL, OX_FS_KB);
	ox_down = ox_prog (GPU_VS, OX_CEIL, OX_FS_DOWN); ox_d8 = ox_prog (GPU_VS, OX_FS_DOWN8, NULL); ox_hh = ox_prog (GPU_VS, OX_FS_HH, NULL);
	ox_hv = ox_prog (GPU_VS, OX_FS_HV, NULL); ox_blur = ox_prog (GPU_VS, OX_FS_BLUR, NULL); ox_sheet = ox_prog (GPU_VS, OX_CEIL, OX_FS_SHEET);
	if (!ox_gen || !ox_kb || !ox_down || !ox_d8 || !ox_hh || !ox_hv || !ox_blur || !ox_sheet) { ox_sheet = 0; fprintf (stderr, "[vectrex_emu] GPU: no overlay effects\n"); return; }
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
static void ox_lost (void) { ox_sheet = 0; ox_w = ox_h = 0; ox_scr_ok = 0; ox_art = 0; ox_artsrc = NULL; for (int k = 0; k < 3; k++) ox_S[k] = 0; ox_key[0] = 0; ox_kok = 0; }
#define OU1F(p, n, v) glUniform1f_ (ph_ul (p, n), (float) (v))
#define OU1I(p, n, v) glUniform1i_ (ph_ul (p, n), (v))
static int ox_rep = -1;   /* (test, VX_OXREP=k with VX_OXPROF=2: each pass drawn k times - the GPU's work, not the gaps between) */
static void ox_quad (void) { if (ox_rep < 0) ox_rep = getenv ("VX_OXREP") ? atoi (getenv ("VX_OXREP")) : 1; for (int r = 0; r < (ox_rep > 1 ? ox_rep : 1); r++) glDrawArrays_ (GL_TRIANGLES, 0, 3); }
/* (the images at this picture's size: the sheet's lasting parts, the quarter-size ones) */
static void ox_size (void)
{
	ph_fb_flush ();
	if (ox_w) { GLuint t[18] = { ox_Q0, ox_B[0], ox_B[1], ox_B[2], ox_H[0], ox_H[1], ox_V[0], ox_V[1], ox_E, ox_EH[0], ox_EH[1], ox_EV[0], ox_EV[1], ox_K[0], ox_K[1], ox_K[2], ox_K[3], ox_Cq }; glDeleteTextures_ (18, t); }
	ox_qw = FW / 4; ox_qh = FH / 4; if (ox_qw < 4) ox_qw = 4; if (ox_qh < 4) ox_qh = 4;
	ox_ew = (ox_qw + 1) / 2; ox_eh = (ox_qh + 1) / 2;   /* (eighth size: cell k over the quarter's 2k, 2k + 1) */
	ox_Q0 = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	for (int k = 0; k < 3; k++) ox_B[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	for (int k = 0; k < 2; k++) { ox_H[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ox_V[k] = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
		ox_EH[k] = ph_tex (ox_ew, ox_eh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL); ox_EV[k] = ph_tex (ox_ew, ox_eh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL); }
	ox_E = ph_tex (ox_ew, ox_eh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, NULL);
	for (int k = 0; k < 4; k++) ox_K[k] = ph_tex (FW, FH, GL_RGBA16F, GL_RGBA, GL_FLOAT, k == 3 ? GL_LINEAR : GL_NEAREST, NULL);
	ox_Cq = ph_tex (ox_qw, ox_qh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, NULL);
	ox_w = FW; ox_h = FH; ox_kok = 0;
}
static const char *ox_base (void) { const char *b = ov_path; for (const char *q = ov_path; *q; q++) if (*q == '/' || *q == '\\') b = q + 1; return b; }
static void ox_scrkey (char *key, size_t n) { snprintf (key, n, "%s|%d|%.3f|%.3f|%.3f|%.3f|%.3f", ox_base (), ovfx.seed, ovfx.wear, ovfx.fine, ovfx.medium, ovfx.large, ovfx.scrib); }
/* (the scratches for this overlay, seed and wear: vecrender.py _scr_make) */
static void ox_scratches (void)
{
	const char *b = ox_base ();
	char key[2048]; ox_scrkey (key, sizeof key);
	if (ox_scr_ok && ox_S[0] && !strcmp (key, ox_key)) return;
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
	ph_keep = 1;                                /* (drawn over the fine scratches / the cleared base: kept - on a tiled GPU
	                                             *  (phones) an invalidate threw them away; the PC never dropped them) */
	for (int c = 2; c <= 3; c++) { ph_target (ox_S[c - 1], 0, 0, SCR_TW, SCR_TH);
		for (int i = 0; i < 9; i++) { const double kn = kn_[i + 2], kd = kd_[i + 2]; if (kn <= 0) continue;
			const double depth = (0.02 + 1.58 * pow (i / 8.0, 3)) * (i >= 6 ? 1.1 : 1.0);
			OX_DRAW (SCR_COUNTS[i + 1] * kn, i + 1, 1, 1, 5.0, 80.0, 0.75, 1.25, depth * kd * SCR_STEP_W * SCR_TPM, c, 0.0); } }
	ph_keep = 0;
	#undef OX_DRAW
	glDisable_ (GL_BLEND); glBindVertexArray_ (0);
	ox_scr_ok = 1;
}
/* (the sheet's lasting parts at this picture's size, from the overlay's art, its scratches, the room's lamp: K0 - K3 and Cq, the
 *  soft glow at quarter size. The scratches' own images then let go - made again for the next change: ~80 MB less) */
static void ox_kbuild (int scr, float hh, float fxp, float fyp, float room, float glint, float scrk, float pgl)
{
	glBindVertexArray_ (g_vao); glDisable_ (GL_BLEND); glUseProgram_ (ox_kb);
	ph_bind (ox_kb, "art", 0, ox_art); ph_bind (ox_kb, "A", 1, scr ? ox_S[0] : ox_art); ph_bind (ox_kb, "B", 2, scr ? ox_S[1] : ox_art); ph_bind (ox_kb, "C", 3, scr ? ox_S[2] : ox_art);
	OU1F (ox_kb, "glint", glint); OU1F (ox_kb, "room", room); OU1F (ox_kb, "scrk", scrk); OU1F (ox_kb, "scron", scr ? 1.0 : 0.0);
	OU1F (ox_kb, "glow", pgl); OU1F (ox_kb, "ref", SCR_REF); OU1F (ox_kb, "ovl", ovfx.ovl); OU1F (ox_kb, "wear", ovfx.wear < 0 ? 0 : ovfx.wear > 2 ? 2 : ovfx.wear);
	OU1F (ox_kb, "k2", ovfx.ovl > 0 && scr ? 0.6 * ovfx.ovl : 0.0);
	glUniform4f_ (ph_ul (ox_kb, "pen"), 0.45f, 0.85f, 1.5f, 0.0f);
	glUniform4f_ (ph_ul (ox_kb, "rlp"), 0.0f, 0.0f, hh, scr ? ovfx.ovl : 0.0f);
	glUniform3f_ (ph_ul (ox_kb, "lpos"), fxp, fyp, 0.42f); glUniform3f_ (ph_ul (ox_kb, "pic"), FW / 2.0f, FH / 2.0f, hh);
	OU1F (ox_kb, "lca", cos (20 * M_PI / 180)); OU1F (ox_kb, "lsa", sin (20 * M_PI / 180));
	{ float pts[12], w[12]; rl_flat_points (g_lamp_x, g_lamp_y, pts);   /* (roomlight.flat_points: the light's area as six points) */
	  for (int i = 0; i < 6; i++) { w[2 * i] = FW / 2.0f + pts[2 * i] * hh; w[2 * i + 1] = FH / 2.0f + pts[2 * i + 1] * hh; }
	  glUniform2fv_ (ph_ul (ox_kb, "rpt"), 6, w); }
	OU1I (ox_kb, "part", 1); ph_target (ox_K[1], ox_K[2], ox_K[3], FW, FH); ox_quad ();
	OU1I (ox_kb, "part", 0); ph_target (ox_K[0], 0, 0, FW, FH); ox_quad ();
	ph_target (ox_Cq, 0, 0, ox_qw, ox_qh); glUseProgram_ (ox_down); ph_bind (ox_down, "K3", 0, ox_K[3]); OU1I (ox_down, "mode", 2);
	glUniform2f_ (ph_ul (ox_down, "sp"), 1.0f / FW, 1.0f / FH); glUniform2f_ (ph_ul (ox_down, "qs"), (float) ox_qw, (float) ox_qh); ox_quad ();
	if (ox_S[0]) { ph_fb_flush (); glDeleteTextures_ (3, ox_S); for (int k = 0; k < 3; k++) ox_S[k] = 0; ox_scr_ok = 0; }
}
/* (one sigma's taps, n each side of the cell, every st-th cell: w / w4 the across pass's (g, u g, u^2 g, u^3 g / u^4 g), q / q4 the
 *  down pass's, x the norm nk) */
static void ox_taps (double s, int n, int st, double nk, float *w, float *w4, float *q, float *q4)
{
	for (int i = -n; i <= n; i++) { const int k = i + n; const double u = (double) i * st / s, g = exp (-0.5 * u * u) * st;
		w[4 * k] = (float) g; w[4 * k + 1] = (float) (u * g); w[4 * k + 2] = (float) (u * u * g); w[4 * k + 3] = (float) (u * u * u * g); w4[k] = (float) (u * u * u * u * g);
		q[4 * k] = (float) (nk * g); q[4 * k + 1] = (float) (nk * g * u); q[4 * k + 2] = (float) (nk * g * u * u); q[4 * k + 3] = (float) (nk * g * u * u * u); q4[k] = (float) (nk * g * u * u * u * u); }
}
/* the sheet over this refresh's light (ph_S and the glow ph_B[0]) into ph_P; glow: the tube's glow (PH_FS_OUT's) */
extern double vt_wall (void);
static double ox_tt[9], ox_t0; static int ox_prof = -1; static long ox_pn;   /* (test: VX_OXPROF=1 - each pass's time, the GPU waited for: the log) */
extern int vx_gprof_now; double vx_oxst[8];   /* (the apps' GPU diagnostic: the sheet's passes on its timed pictures - since 2026-11-26:
	                                              0 the scratches and the lasting parts (when made again), 1 none (the full-size light gone),
	                                              2 the quarter-size light, 3 the wide sigma at eighth size, 4 / 5 the narrow one across / down
	                                              (the wide added), 6 the blur rounds, 7 the sheet) */
typedef void (GLAPIENTRY *pf_ox_qc) (GLuint, GLenum); static pf_ox_qc ox_qc; static GLuint ox_tq[9]; static int ox_tqp;   /* (VX_OXPROF=2: GPU timestamps
	                                              between the passes instead - the GPU's own time for each, the picture not held up) */
#define OXT(k) do { if (ox_prof == 2) ox_qc (ox_tq[(k) + 1], 0x8E28); else if (ox_prof > 0 || vx_gprof_now) { glFinish_ (); double t_ = vt_wall (); if (vx_gprof_now) vx_oxst[k] += t_ - ox_t0; else ox_tt[k] += t_ - ox_t0; ox_t0 = t_; } } while (0)
static int ox_draw (int glow, double dt)
{
	if (ox_prof < 0) { const char *z = getenv ("VX_OXPROF"); ox_prof = z ? (atoi (z) == 2 ? 2 : 1) : 0;
		if (ox_prof == 2) { ox_qc = (pf_ox_qc) gq_proc ("glQueryCounterEXT", "glQueryCounter"); if (ox_qc && gq_gen && gq_u64) gq_gen (9, ox_tq); else ox_prof = 1; } }
	if (ox_prof == 2 && ox_tqp) { unsigned long long t[9]; for (int k = 0; k < 9; k++) gq_u64 (ox_tq[k], 0x8866, &t[k]);   /* (GL_QUERY_RESULT: the last picture's) */
		for (int k = 0; k < 8; k++) ox_tt[k] += (double) (t[k + 1] - t[k]) * 1e-9; ox_tqp = 0; }
	if (vx_gprof_now) { glFinish_ (); ox_t0 = vt_wall (); }
	if (ox_prof > 0) { if (ox_prof == 1) { glFinish_ (); ox_t0 = vt_wall (); } if (++ox_pn % 300 == 0) { fprintf (stderr, "oxprof ms:"); for (int i = 0; i < 8; i++) fprintf (stderr, " %.3f", ox_tt[i] / 300 * 1e3); fprintf (stderr, " (the lasting parts made %ld times)\n", ox_kn); memset (ox_tt, 0, sizeof ox_tt); } }
	(void) dt;
	if (!ox_sheet || !ov || !ov_lm) return 0;
	const int scr = ovfx.scr, pglow = ovfx.glow > 0, ovl = ovfx.ovl > 0;
	if (!scr && !pglow && !ovl) return 0;
	if (ox_prof == 2) ox_qc (ox_tq[0], 0x8E28);   /* (GL_TIMESTAMP) */
	if (FW != ox_w || FH != ox_h) ox_size ();
	if (ox_artsrc != ov || ox_artgen != ov_gen) { if (ox_art) glDeleteTextures_ (1, &ox_art);   /* (ov_gen too: another game's overlay can
		                                       *  come at the same address - the last one's art stayed, Armor Attack's walls under
		                                       *  Scramble, the Nova 2026-11-16) */
		ox_art = ph_tex (ovw, ovh, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, ov); ox_artsrc = ov; ox_artgen = ov_gen; ox_key[0] = 0; ox_scr_ok = 0; ox_kok = 0; }
	const double kpx = FH / (0.924 * 1970.0);               /* (core px per px of the owner's window) */
	const float hh = (float) (FH / (2 * 0.924));             /* (half the picture's height, px: the room light's unit) */
	const float fxp = (float) (-0.44 + 0.6 * 1.48 * g_lamp_x), fyp = (float) (-0.72 + 0.6 * (-0.21 + 0.77 * g_lamp_y));   /* (roomlight.place: flat) */
	const double room = 0.0648 * 0.0648 / 0.075 * opt_room * opt_room;   /* (vecrender.py room_light: the default x the setting squared) */
	const float glint = scr ? (float) (ovfx.glint * SCR_BASE[2]) : 0.0f, scrk = scr ? (float) (2.5 * 0.85 * 0.5) : 0.0f, pgl = (float) (ART_GLOW_K * ovfx.glow);
	glBindVertexArray_ (g_vao);
	/* the sheet's lasting parts (K0 - K3, Cq): made again only when what they come from changes - the overlay, its scratches, the
	 * picture's size, the room's light and lamp, the settings below */
	{ char sk[2048]; if (scr) ox_scrkey (sk, sizeof sk); else sk[0] = 0;
	  const float key[16] = { (float) FW, (float) FH, (float) scr, ovfx.ovl, pgl, glint, (float) room, scrk, ovfx.wear, hh, fxp, fyp, g_lamp_x, g_lamp_y, (float) ovfx.glow, 0 };
	  if (!ox_kok || memcmp (key, ox_kkey, sizeof key) || strcmp (sk, ox_kscr)) {
		if (scr) ox_scratches ();
		ox_kbuild (scr, hh, fxp, fyp, (float) room, glint, scrk, pgl);
		memcpy (ox_kkey, key, sizeof key); snprintf (ox_kscr, sizeof ox_kscr, "%s", sk); ox_kok = 1; ox_kn++; } }
	OXT (0); OXT (1);
	const float qs[2] = { (float) ox_qw, (float) ox_qh }, gk = GLOW_K * (glow == 2 ? 2.0f : 1.0f), br = bt_gpu ? 1.0f : opt_bright;
	const float gsz[2] = { (float) (ph_gw * 2 * S), (float) (ph_gh * 2 * S) }; const int wbon = wb_on && !bt_gpu;
	glBindTexture_ (GL_TEXTURE_2D, ph_S); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);   /* (the light
	                                              between its pixels: the light before the sheet's reads, as its image's were) */
	/* the light at quarter size (the glints, the glow), blurred (M: once, F: three times) */
	ph_target (ox_Q0, 0, 0, ox_qw, ox_qh); glUseProgram_ (ox_down);
	ph_bind (ox_down, "pS", 0, ph_S); ph_bind (ox_down, "pF", 1, ph_F[ph_i ^ 1]); ph_bind (ox_down, "gt", 2, ph_B[0]); ph_bind (ox_down, "K3", 3, ox_K[3]); ph_bind (ox_down, "Cq", 4, ox_Cq);
	OU1I (ox_down, "mode", wbon ? 1 : 0); OU1I (ox_down, "glow", glow); OU1I (ox_down, "wbon", wbon); OU1F (ox_down, "br", br); OU1F (ox_down, "bright", br);
	glUniform3f_ (ph_ul (ox_down, "tint"), gk * GLOW_TINT[0], gk * GLOW_TINT[1], gk * GLOW_TINT[2]);
	glUniform3f_ (ph_ul (ox_down, "wbg"), wb_g[0], wb_g[1], wb_g[2]); glUniform3f_ (ph_ul (ox_down, "wbc"), wb_c[0], wb_c[1], wb_c[2]);
	glUniform2f_ (ph_ul (ox_down, "gsc"), FW / gsz[0], FH / gsz[1]);
	glUniform2f_ (ph_ul (ox_down, "sp"), 1.0f / FW, 1.0f / FH); glUniform2f_ (ph_ul (ox_down, "qs"), qs[0], qs[1]); ox_quad (); OXT (2);
	/* (2026-11-22: not smoothed in time any more - the plastic's glow trailed 40-150 px behind a moving line, and without it neither the
	 *  glow nor the glints flicker while the vectors are steady: measured with the Glow Test cart, the owner's choice. Its pass and its
	 *  two images gone 2026-11-26) */
	/* the glints' and the glow's sums: the narrow sigma (s1) at quarter size, its taps as before (every st-th cell) but only its own
	 * 5 sigma; the wide one (s2: the plastic glow, the glints' reach) at eighth size, from the quarter-size light shrunk again softly
	 * (1 3 3 1) - its sigma there less what that filter and the read back between its cells add, so the same spread; read back at the
	 * middle of the old taps (half a cell before the cell with an even st). 2026-11-26: a third of the work, the same sums */
	{ const double s1 = 4.0 * kpx, s2 = 12.0 * kpx, r = ovfx.reach < 0 ? 0 : ovfx.reach > 2 ? 2 : ovfx.reach;
	  const int nn = (int) ceil (5.0 * s2), st = (nn + 31) / 32, n1 = ((int) ceil (5.0 * s1) + st - 1) / st;
	  const int ev = st % 2 == 0; const double se2 = (s2 * s2 + (st * st - 1) / 12.0 - 3.25 - (ev ? 0.5 : 0.75)) / 4.0, se = sqrt (se2 > 0.25 ? se2 : 0.25);
	  const int ne = (int) ceil (5.0 * se), ste = (ne + 31) / 32, nE = (ne + ste - 1) / ste;
	  const double k1 = (1.5 - 0.5 * r) / (50.265 * s1 * s1), k2 = fmax (0.7 * r, 1e-6) / (50.265 * se * se);
	  float w[65 * 4], w4[65], q[65 * 4], q4[65];
	  ph_target (ox_E, 0, 0, ox_ew, ox_eh); glUseProgram_ (ox_d8); ph_bind (ox_d8, "tex", 0, ox_Q0); ox_quad ();
	  ox_taps (se, nE, ste, k2, w, w4, q, q4);
	  ph_target (ox_EH[0], ox_EH[1], 0, ox_ew, ox_eh); glUseProgram_ (ox_hh); ph_bind (ox_hh, "src", 0, ox_E); OU1I (ox_hh, "n", nE); OU1I (ox_hh, "st", ste);
	  glUniform4fv_ (ph_ul (ox_hh, "w"), 2 * nE + 1, w); glUniform1fv_ (ph_ul (ox_hh, "w4"), 2 * nE + 1, w4); ox_quad ();
	  ph_target (ox_EV[0], ox_EV[1], 0, ox_ew, ox_eh); glUseProgram_ (ox_hv); ph_bind (ox_hv, "Ha", 0, ox_EH[0]); ph_bind (ox_hv, "Hb", 1, ox_EH[1]);
	  ph_bind (ox_hv, "Va", 2, ox_E); ph_bind (ox_hv, "Vb", 3, ox_E);   /* (not read: add 0 - not the images drawn into) */
	  OU1I (ox_hv, "n", nE); OU1I (ox_hv, "st", ste); OU1I (ox_hv, "add", 0); OU1F (ox_hv, "gn", 1.0 / (6.2832 * se * se) / k2);   /* (the glow: G of the wide sigma alone) */
	  glUniform4fv_ (ph_ul (ox_hv, "q"), 2 * nE + 1, q); glUniform1fv_ (ph_ul (ox_hv, "q4"), 2 * nE + 1, q4); ox_quad (); OXT (3);
	  ox_taps (s1, n1, st, k1, w, w4, q, q4);
	  ph_target (ox_H[0], ox_H[1], 0, ox_qw, ox_qh); glUseProgram_ (ox_hh); ph_bind (ox_hh, "src", 0, ox_Q0); OU1I (ox_hh, "n", n1); OU1I (ox_hh, "st", st);
	  glUniform4fv_ (ph_ul (ox_hh, "w"), 2 * n1 + 1, w); glUniform1fv_ (ph_ul (ox_hh, "w4"), 2 * n1 + 1, w4); ox_quad (); OXT (4);
	  ph_target (ox_V[0], ox_V[1], 0, ox_qw, ox_qh); glUseProgram_ (ox_hv); ph_bind (ox_hv, "Ha", 0, ox_H[0]); ph_bind (ox_hv, "Hb", 1, ox_H[1]);
	  ph_bind (ox_hv, "Va", 2, ox_EV[0]); ph_bind (ox_hv, "Vb", 3, ox_EV[1]);
	  OU1I (ox_hv, "n", n1); OU1I (ox_hv, "st", st); OU1I (ox_hv, "add", 1); OU1F (ox_hv, "gn", 0.0); glUniform2f_ (ph_ul (ox_hv, "sh"), ev ? -0.5f : 0.0f, ev ? -0.5f : 0.0f);
	  glUniform4fv_ (ph_ul (ox_hv, "q"), 2 * n1 + 1, q); glUniform1fv_ (ph_ul (ox_hv, "q4"), 2 * n1 + 1, q4); ox_quad (); OXT (5); }
	glUseProgram_ (ox_blur); glUniform2f_ (ph_ul (ox_blur, "qs"), qs[0], qs[1]);
	/* (the light blurred, across into B[1] and back: B[2] (M) after the first round, B[0] (F) after the third - the first round's
	 *  written into B[2] itself and read from there: its copy, a pass a picture, gone) */
	{ for (int it = 0; it < 3; it++) {
		ph_target (ox_B[1], 0, 0, ox_qw, ox_qh); ph_bind (ox_blur, "tex", 0, it == 0 ? ox_Q0 : it == 1 ? ox_B[2] : ox_B[0]);
		glUniform2f_ (ph_ul (ox_blur, "dir"), (float) (kpx / ox_qw), 0.0f); ox_quad ();
		ph_target (it == 0 ? ox_B[2] : ox_B[0], 0, 0, ox_qw, ox_qh); ph_bind (ox_blur, "tex", 0, ox_B[1]);
		glUniform2f_ (ph_ul (ox_blur, "dir"), 0.0f, (float) (kpx / ox_qh)); ox_quad (); } } OXT (6);
	/* the sheet: the light before it worked out here (ph_S, the tube's glow, K3's soft glow), the lasting parts read from K0 - K3 */
	ph_target (ph_P, 0, 0, FW, FH); glUseProgram_ (ox_sheet);
	ph_bind (ox_sheet, "pS", 0, ph_S); ph_bind (ox_sheet, "pF", 1, ph_F[ph_i ^ 1]); ph_bind (ox_sheet, "gt", 2, ph_B[0]);
	ph_bind (ox_sheet, "K0", 3, ox_K[0]); ph_bind (ox_sheet, "K1", 4, ox_K[1]); ph_bind (ox_sheet, "K2", 5, ox_K[2]); ph_bind (ox_sheet, "K3", 6, ox_K[3]);
	ph_bind (ox_sheet, "ovm", 7, ph_ovm); ph_bind (ox_sheet, "ova", 8, ph_ova); ph_bind (ox_sheet, "art", 9, ox_art);
	ph_bind (ox_sheet, "M", 10, ox_B[2]); ph_bind (ox_sheet, "F", 11, ox_B[0]); ph_bind (ox_sheet, "V1", 12, ox_V[0]); ph_bind (ox_sheet, "V2", 13, ox_V[1]);
	OU1F (ox_sheet, "br", br); OU1F (ox_sheet, "bright", br); OU1I (ox_sheet, "tglow", glow); OU1I (ox_sheet, "wbon", wbon);
	glUniform3f_ (ph_ul (ox_sheet, "tint"), gk * GLOW_TINT[0], gk * GLOW_TINT[1], gk * GLOW_TINT[2]);
	glUniform3f_ (ph_ul (ox_sheet, "wbg"), wb_g[0], wb_g[1], wb_g[2]); glUniform3f_ (ph_ul (ox_sheet, "wbc"), wb_c[0], wb_c[1], wb_c[2]);
	glUniform2f_ (ph_ul (ox_sheet, "gsz"), gsz[0], gsz[1]);
	OU1F (ox_sheet, "glint", glint); OU1F (ox_sheet, "scrk", scrk); OU1F (ox_sheet, "scron", scr ? 1.0 : 0.0);
	OU1F (ox_sheet, "a2", scr_a2); OU1F (ox_sheet, "a4", scr_a4); OU1F (ox_sheet, "gk", scr ? 2.0 : 0.0);
	OU1F (ox_sheet, "glow", pgl); OU1F (ox_sheet, "ref", SCR_REF); OU1F (ox_sheet, "pxo", 1.5 * kpx);
	OU1F (ox_sheet, "dith", fx_dith ? 1.0 : 0.0); OU1F (ox_sheet, "hk", vx_hdr_peak > 1.0f ? vx_hdr_peak : 1.0f); { extern float vx_bright; OU1F (ox_sheet, "bgh", bt_gpu && vx_bright > 0.5f && vx_hdr_peak > 1.0f ? vx_hdr_peak : 0.0f); } { extern float vx_hdr_lift, vx_hdr_vs, vx_hdr_vss; OU1F (ox_sheet, "hl", bt_gpu ? 0.0f : vx_hdr_lift); OU1I (ox_sheet, "beam", bt_gpu); OU1F (ox_sheet, "vs", vx_hdr_peak > 1.0f ? vx_hdr_vs : vx_hdr_vss); { extern float vx_hdr_lt, vx_hdr_sn; OU1F (ox_sheet, "lt", vx_hdr_lt); OU1F (ox_sheet, "sn", vx_hdr_sn); } }
	ox_quad (); OXT (7); if (ox_prof == 2) ox_tqp = 1;
	glBindTexture_ (GL_TEXTURE_2D, ph_S); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri_ (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return 1;
}
