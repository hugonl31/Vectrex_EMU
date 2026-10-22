// the light's angle harmonics, horizontal (vecrender.py SCR_HARM_H; u = x / sigma, g = exp(-u^2/2)):
// part 0 / 1: sigma 1 / 2 (g, u g, u^2 g, u^3 g); part 2: u^4 g of both
layout(location = 0) in vec2 uv;
SAMP(1) src;
PUSH PC { vec4 vp; vec4 a; } pc;                  // a = part, s1, s2, px.x
layout(location = 0) out vec4 outc;
float lmax(vec3 c) { return max(max(c.r, c.g), c.b); }
void main() {
    float part = pc.a.x, s1 = pc.a.y, s2 = pc.a.z, px = pc.a.w;
    vec4 o = vec4(0.0);
    float sg = part < 0.5 ? s1 : s2;
    int n = int(ceil(5.0 * max(s1, s2)));
    for (int i = -64; i <= 64; i++) {
        if (i < -n || i > n) continue;
        float l = lmax(texture(src, uv + vec2(float(i) * px, 0.0)).rgb);
        if (part < 1.5) {
            float u = float(i) / sg, g = exp(-0.5 * u * u) * l;
            o += g * vec4(1.0, u, u * u, u * u * u);
        } else {
            float u1 = float(i) / s1, u2 = float(i) / s2;
            o.x += exp(-0.5 * u1 * u1) * u1 * u1 * u1 * u1 * l;
            o.y += exp(-0.5 * u2 * u2) * u2 * u2 * u2 * u2 * l;
        }
    }
    outc = o;
}
