// the overlay sheet with its forty years of handling, in one pass onto the screen (vecrender.py ART_SHADER
// and SCR_SHADE): S is the screen before the sheet; on it the overlay (its filters multiply, its print and
// filters catch the room's light), then the scratches (the matte takes light from the line it scatters, the
// scratches glint with the light they lie across from, the plastic glows around bright lines, the deepest
// damage lets the tube's light through untinted). The desktop does this in four blended passes onto its
// 8-bit window, which clamps after each: here one pass, clamped after each step the same way.
// S, the overlay, the scratch textures A B C (fine: sum d, d cos 2t, d sin 2t, d cos 4t / d sin 4t, the
// steps' d, d cos 2t, d sin 2t / the steps' d cos 4t, d sin 4t, the damage cut), M / F the light blurred
// (quarter size, ~6 / ~11 px), V1 / V2 the light's angle harmonics (harm_v.frag)
layout(location = 0) in vec2 uv0;
SAMP(1) S;
SAMP(2) art;
SAMP(3) A;
SAMP(4) B;
SAMP(5) C;
SAMP(6) M;
SAMP(7) F;
SAMP(8) V1;
SAMP(9) V2;
// vp = flip, screen height, screen width, rotation (final.frag); rect = the sheet on the screen (x0, y0, x1,
// y1 px); a = tint, room, print, sf (the sheet's share of the overlay plane); b = ref, glint, scrk, scron;
// c = a2, a4, gk, glow; d = pen (from, full, light through), scratches on
PUSH PC { vec4 vp; vec4 rect; vec4 a; vec4 b; vec4 c; vec4 d; } pc;
layout(location = 0) out vec4 outc;
float lmax(vec3 c) { return max(max(c.r, c.g), c.b); }
void main() {
    vec2 q = gl_FragCoord.xy, p = q;
    float W = pc.vp.z, H = pc.vp.y; int rot = int(pc.vp.w + 0.5);
    if (pc.vp.x > 0.5) p.y = H - q.y;
    else if (rot == 1) p = vec2(q.y, H - q.x);
    else if (rot == 2) p = vec2(W - q.x, H - q.y);
    else if (rot == 3) p = vec2(W - q.y, q.x);
    vec2 size = vec2(W, H), uv = p / size;
    vec3 x = texture(S, uv).rgb;
    vec2 tc = (p - pc.rect.xy) / (pc.rect.zw - pc.rect.xy);
    if (tc.x >= 0.0 && tc.x <= 1.0 && tc.y >= 0.0 && tc.y <= 1.0) {
        vec4 o = texture(art, tc);
        float opq = step(0.996, o.a), filt = step(0.004, o.a) * (1.0 - opq);
        vec3 hue = o.rgb / max(max(max(o.r, o.g), o.b), 0.02);
        float tint = pc.a.x, room = pc.a.y, prt = pc.a.z, sf = pc.a.w;
        // the overlay (art.frag): the filters multiply, then the sheet's own light
        x = clamp(x * mix(vec3(1.0), hue * 1.15, tint * filt) * (1.0 - opq), 0.0, 1.0);
        x = clamp(x + o.rgb * (filt * room + opq * prt), 0.0, 1.0);
        float su = (tc.x - 0.5 * (1.0 - sf)) / sf;
        if (pc.d.w > 0.5 && su >= 0.0 && su <= 1.0) {
            float ref = pc.b.x, glint = pc.b.y, scrk = pc.b.z, scron = pc.b.w;
            float a2 = pc.c.x, a4 = pc.c.y, gk = pc.c.z, glow = pc.c.w;
            vec3 trans = mix(mix(vec3(1.0), hue, filt), vec3(0.0), opq);
            vec3 own = mix(vec3(0.35), o.rgb, max(filt, opq));
            vec2 sv = vec2(su, tc.y);
            vec4 a = texture(A, sv) * scron, b = texture(B, sv) * scron, cc = texture(C, sv) * scron;
            float D = max(a.x, 0.0);
            float m = D > 0.0 ? (1.0 - exp(-1.5 * D / ref)) / (1.0 - exp(-1.5)) : 0.0;
            vec2 px = 1.5 / size;
            vec3 near = (texture(S, uv).rgb * 2.0 + texture(S, uv + px).rgb + texture(S, uv - px).rgb
                         + texture(S, uv + vec2(px.x, -px.y)).rgb + texture(S, uv - vec2(px.x, -px.y)).rgb) / 6.0;
            vec3 mid = texture(M, uv).rgb, far = texture(F, uv).rgb;
            float k = scrk * m;
            x = clamp(x * (1.0 - 0.25 * clamp(k, 0.0, 1.0) * (1.0 - opq)), 0.0, 1.0);   // (the matte takes the line's light)
            vec3 halo = (0.6 * near + 0.8 * mid + 0.5 * far) * trans;
            vec3 c = halo * k * 0.8 * glint + room * 0.6 * m * own;
            vec4 h = texture(V1, uv), h2 = texture(V2, uv);
            vec3 lc = (far + 0.002) / (lmax(far) + 0.002);
            float Rl = max(h.x, 0.0);
            float Gf = D * Rl - a2 * (a.y * h.y + a.z * h.z) + a4 * (a.w * h.w + b.x * h2.x);
            float Gs = b.y * Rl - a2 * (b.z * h.y + b.w * h.z) + a4 * (cc.x * h.w + cc.y * h2.x);
            float df = D > 0.0 ? clamp(Gf / max(D * Rl, 1e-7), 0.0, 4.0) : 0.0;
            float ds = b.y > 0.0 ? clamp(Gs / max(b.y * Rl, 1e-7), 0.0, 4.0) : 0.0;
            float dd = clamp(b.y * 0.6, 0.0, 1.4);
            c += (m * df * df + 2.0 * dd * ds * ds) * Rl * gk * glint * lc * trans + dd * room * 0.9 * own;
            c += glow * max(h2.y, 0.0) * lc * trans;
            float cut = smoothstep(pc.d.x, pc.d.y, cc.z);
            c += cut * pc.d.z * (0.5 * near + 0.8 * mid + 0.6 * far) * (vec3(1.0) - trans * 0.5) + cut * room * 0.5;
            x = clamp(x + c, 0.0, 1.0);
        }
    }
    outc = vec4(x, 1.0);
}
