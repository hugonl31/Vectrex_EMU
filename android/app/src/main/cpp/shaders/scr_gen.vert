// the overlay's scratches, made once per overlay (vecrender.py SCR_GEN_VS): every scratch drawn from its
// index - 4096 a batch, 24 pieces each (a line list: the vertex index gives scratch j and piece k) - in the
// sheet's own space (162 x 207 mm), into the scratch textures (added up)
// a = batch, limit, seed, stepid; b = mode, place, L0, L1; c = d0, d1, wv, off; d = chan, dmax, scrib.xy;
// e = llen (the length tiers: 15 mm, 35 mm, the share 15-35, the share 35-80)
PUSH PC { vec4 vp; vec4 a; vec4 b; vec4 c; vec4 d; vec4 e; } pc;
layout(location = 0) out vec4 val;
const float SW = 162.0, SH = 207.0, PI = 3.14159265;
const int SEG = 24;
float BATCH, SEED, STEPID;
float h(float j, float k) {
    vec3 p = fract(vec3(j, BATCH, SEED + 13.0 * k + 97.0 * STEPID) * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float dens(vec2 q) {                    // where hands hold the sheet: the middle, the lower half, the edges
    float x = q.x, y = q.y, ex = min(x, 1.0 - x), ey = min(y, 1.0 - y);
    float mid = exp(-((x - 0.5) * (x - 0.5) / 0.08 + (y - 0.62) * (y - 0.62) / 0.12));
    float edge = exp(-ex / 0.06) * 0.6;
    float corner = 1.0 - 0.7 * exp(-(ex * ex + ey * ey) / 0.02);
    return (0.25 + mid + edge) * corner;
}
void main() {
    BATCH = pc.a.x; SEED = pc.a.z; STEPID = pc.a.w;
    float limit = pc.a.y, mode = pc.b.x, place = pc.b.y, L0 = pc.b.z, L1 = pc.b.w;
    float d0 = pc.c.x, d1 = pc.c.y, wv = pc.c.z, off = pc.c.w, chan = pc.d.x, dmax = pc.d.y;
    vec2 scrib = pc.d.zw; vec4 llen = pc.e;
    int vid = VID, jj = vid / (2 * SEG), r = vid - jj * 2 * SEG;
    float j = float(jj), s = float((r >> 1) + (r & 1)) / float(SEG);
    float u = h(j, 0.0), v = h(j, 1.0);
    bool keep = j < limit;
    if (place > 0.5) keep = keep && h(j, 2.0) < dens(vec2(u, v)) / dmax;
    float hl = h(j, 3.0), pl = llen.w, pm = llen.z;    // (three tiers: long, medium long, the rest)
    float L = hl < pl ? exp(mix(log(llen.y), log(L1), hl / pl))
            : hl < pl + pm ? exp(mix(log(llen.x), log(llen.y), (hl - pl) / pm))
            : exp(mix(log(L0), log(llen.x), (hl - pl - pm) / (1.0 - pl - pm)));
    L = min(L, 100.0);
    float th0 = h(j, 4.0) * 2.0 * PI;
    float bend = mode > 0.5 ? sqrt(-2.0 * log(max(h(j, 5.0), 1e-6))) * cos(2.0 * PI * h(j, 6.0)) * 0.35 : 0.0;
    float d = mix(d0, d1, h(j, 7.0));
    float taper = sqrt(max(sin(PI * s), 0.0));
    if (mode > 0.5) taper *= 0.75 + 0.25 * sin(7.0 * s + h(j, 8.0) * 6.2832);
    float th = th0 + bend * (s - 0.5), a0 = th0 - 0.5 * bend;
    vec2 p = abs(bend) > 1e-4 ? vec2(sin(th) - sin(a0), cos(a0) - cos(th)) * (L / bend) : vec2(cos(th0), sin(th0)) * s * L;
    p -= 0.5 * L * vec2(cos(th0), sin(th0));
    // a scribble (a few): a sand grain dragged under the sheet - short, one smooth sweep of up to half a
    // circle, ending in a small curl; the path summed in 32 pieces, centred
    float sc = h(j, 9.0) < scrib.x ? scrib.y : 0.0;
    if (sc > 0.0) {
        L = 6.0 + 18.0 * h(j, 10.0);
        float K = sc * (1.2 + 1.9 * h(j, 11.0)) * (h(j, 12.0) < 0.5 ? -1.0 : 1.0);
        float C = sc * (0.8 + 2.2 * h(j, 13.0)) * (h(j, 14.0) < 0.5 ? -1.0 : 1.0);
        vec2 acc = vec2(0.0), tot = vec2(0.0);
        for (int i = 0; i < 32; i++) {
            float t = (float(i) + 0.5) / 32.0, ts = t * s;
            float ea = clamp((t - 0.7) / 0.3, 0.0, 1.0), eb = clamp((ts - 0.7) / 0.3, 0.0, 1.0);
            float ha = th0 + K * (t - 0.5) + C * ea * ea, hb = th0 + K * (ts - 0.5) + C * eb * eb;
            tot += vec2(cos(ha), sin(ha)); acc += vec2(cos(hb), sin(hb)) * s;
        }
        p = (acc - 0.5 * tot) * (L / 32.0);
        float es = clamp((s - 0.7) / 0.3, 0.0, 1.0);
        th = th0 + K * (s - 0.5) + C * es * es;
    }
    p += vec2(u * SW, v * SH) + off * vec2(-sin(th), cos(th));
    gl_Position = keep ? vec4(p / vec2(SW, SH) * 2.0 - 1.0, 0.0, 1.0) : vec4(-9.0, -9.0, 0.0, 1.0);
    float w = d * taper * wv / max(abs(cos(th)), abs(sin(th)));     // (an aliased line: per length, not per texel)
    float c2 = cos(2.0 * th), s2 = sin(2.0 * th);
    if (chan < 0.5) val = w * vec4(1.0, c2, s2, cos(4.0 * th));
    else if (chan < 1.5) val = vec4(w * sin(4.0 * th), 0.0, 0.0, 0.0);
    else if (chan < 2.5) val = vec4(0.0, w, w * c2, w * s2);
    else {                                  // (C: the 4t terms; z: the damage's cut through the colour, widest part)
        float cut = STEPID > 6.5 ? pow(max(sin(PI * s), 0.0), 3.0) * d / 1.25 / max(abs(cos(th)), abs(sin(th))) : 0.0;
        val = vec4(w * cos(4.0 * th), w * sin(4.0 * th), cut, 0.0);
    }
}
