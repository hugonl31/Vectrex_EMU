// the phosphor image, one display frame on (vecrender.py UPDATE_SHADER): the old light ages (kf = e^-dt),
// the fading glow joins up (grow); where the fresh light is brighter, the spot is lit again
layout(location = 0) in vec2 uv;
SAMP(1) oldt;
SAMP(2) fresh;
// a = kf, mul, spread, grow; b = gcap, gn, gage, hold; c = tscale, toff, px.xy
PUSH PC { vec4 vp; vec4 a; vec4 b; vec4 c; } pc;
layout(location = 0) out vec4 outc;
float HOLD, TS, TOFF;
float br(float f) { return bright(f, HOLD, TS, TOFF); }
vec4 grown(vec4 o, vec4 n, float g, float hf, float gage, float gcap, inout float bb) {
    if (n.a > 0.0 && n.a < hf && n.a > hf * gage) {   // (only young glow spreads: it stays the vector's size)
        float l = min(lum(n.rgb), gcap);                // (capped: bright corners spread like lines)
        float v = l * br(n.a) * g;
        if (v > bb) { bb = v; return vec4(n.rgb * (l / max(lum(n.rgb), 1e-4)) * g, n.a); }
    }
    return o;
}
void main() {
    HOLD = pc.b.w; TS = pc.c.x; TOFF = pc.c.y;
    vec2 px = pc.c.zw;
    float kf = pc.a.x, mul = pc.a.y, spread = pc.a.z, grow = pc.a.w;
    float gcap = pc.b.x, gn = pc.b.y, gage = pc.b.z;
    vec4 o = texture(oldt, uv);
    if (spread > 0.0) {
        vec4 n1 = texture(oldt, uv + vec2(px.x, 0.0)), n2 = texture(oldt, uv - vec2(px.x, 0.0));
        vec4 n3 = texture(oldt, uv + vec2(0.0, px.y)), n4 = texture(oldt, uv - vec2(0.0, px.y));
        o = mix(o, 0.25 * (n1 + n2 + n3 + n4), spread);
    }
    if (grow > 0.0) {
        float hf = exp(-HOLD), bb = lum(o.rgb) * br(o.a);
        for (int i = 1; i <= 4; i++) {
            if (float(i) > gn) break;
            float g = pow(grow, float(i)); vec2 d = px * float(i);
            o = grown(o, texture(oldt, uv + vec2(d.x, 0.0)), g, hf, gage, gcap, bb);
            o = grown(o, texture(oldt, uv - vec2(d.x, 0.0)), g, hf, gage, gcap, bb);
            o = grown(o, texture(oldt, uv + vec2(0.0, d.y)), g, hf, gage, gcap, bb);
            o = grown(o, texture(oldt, uv - vec2(0.0, d.y)), g, hf, gage, gcap, bb);
        }
    }
    o.a *= kf; o.rgb *= mul;
    vec4 f = texture(fresh, uv);
    if (o.a < 0.002) o = vec4(0.0);                     // (6 s old: gone)
    outc = lum(f.rgb) * br(f.a) > lum(o.rgb) * br(o.a) ? f : o;
}
