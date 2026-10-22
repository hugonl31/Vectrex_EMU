// what the phosphor image shows now (vecrender.py SHOW_SHADER; alpha 0.7: the bloom weight)
layout(location = 0) in vec2 uv;
SAMP(1) tex;
PUSH PC { vec4 vp; vec4 a; vec4 b; } pc;          // a = hold, tscale, toff, sm; b.xy = px
layout(location = 0) out vec4 outc;
float HOLD, TS, TOFF;
vec3 glow(vec2 q) {                       // a pixel's afterglow (the lit part - younger than the hold - not)
    vec4 t = texture(tex, q);
    if (t.a <= 0.0 || -log(t.a) < HOLD) return vec3(0.0);
    return t.rgb * bright(t.a, HOLD, TS, TOFF);
}
void main() {
    HOLD = pc.a.x; TS = pc.a.y; TOFF = pc.a.z;
    float sm = pc.a.w; vec2 px = pc.b.xy;
    vec4 p = texture(tex, uv);
    vec3 c = p.rgb * bright(p.a, HOLD, TS, TOFF);
    if (sm > 0.0) {                       // the afterglow's spots overlap (PHOS_SMOOTH); the lit lines stay sharp
        vec2 d = px * sm;
        vec3 g = 0.25 * glow(uv)
               + 0.125 * (glow(uv + vec2(d.x, 0.0)) + glow(uv - vec2(d.x, 0.0)) + glow(uv + vec2(0.0, d.y)) + glow(uv - vec2(0.0, d.y)))
               + 0.0625 * (glow(uv + d) + glow(uv - d) + glow(uv + vec2(d.x, -d.y)) + glow(uv + vec2(-d.x, d.y)));
        vec3 lit = (p.a > 0.0 && -log(p.a) < HOLD) ? p.rgb : vec3(0.0);
        c = max(lit, g);
    }
    outc = vec4(c, 0.7);
}
