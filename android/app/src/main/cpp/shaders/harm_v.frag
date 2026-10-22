// the light's angle harmonics, vertical (vecrender.py SCR_HARM_V; v = y / sigma, y down): part 0: (R, C2, S2,
// C4), part 1: (S4, glow) - R = sum w (r/s)^4 G * light, normalized (16 pi s^2); C / S n = the same with
// cos / sin n phi; glow = G of sigma 2 (2 pi s^2)
layout(location = 0) in vec2 uv;
SAMP(1) H1;
SAMP(2) H2;
SAMP(3) H3;
PUSH PC { vec4 vp; vec4 a; vec4 b; } pc;          // a = part, s1, s2, px.y; b = w1, w2
layout(location = 0) out vec4 outc;
void main() {
    float part = pc.a.x, s1 = pc.a.y, s2 = pc.a.z, py = pc.a.w, w1 = pc.b.x, w2 = pc.b.y;
    vec4 o = vec4(0.0);
    int n = int(ceil(5.0 * max(s1, s2)));
    float n1 = w1 / (50.265 * s1 * s1), n2 = w2 / (50.265 * s2 * s2);
    for (int j = -64; j <= 64; j++) {
        if (j < -n || j > n) continue;
        vec2 q = uv + vec2(0.0, float(j) * py);
        vec4 a = texture(H1, q), b = texture(H2, q), c = texture(H3, q);
        for (int k = 0; k < 2; k++) {
            float sg = k == 0 ? s1 : s2, nn = k == 0 ? n1 : n2;
            vec4 h = k == 0 ? a : b; float h4 = k == 0 ? c.x : c.y;
            float v = -float(j) / sg, g = exp(-0.5 * v * v);
            float v1 = g * v, v2 = v1 * v, v3 = v2 * v, v4 = v3 * v;
            if (part < 0.5)
                o += nn * vec4(h4 * g + 2.0 * h.z * v2 + h.x * v4, h4 * g - h.x * v4,
                               2.0 * (h.w * v1 + h.y * v3), h4 * g - 6.0 * h.z * v2 + h.x * v4);
            else {
                o.x += nn * 4.0 * (h.w * v1 - h.y * v3);
                if (k == 1) o.y += h.x * g / (6.2832 * s2 * s2);
            }
        }
    }
    outc = o;
}
