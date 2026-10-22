// the screen in one pass: the curved tube (vecrender.py CURVE_SHADER) maps each screen pixel into the
// picture, which is toned (TONE_SHADER) with the bloom added (x 0.85, 0.92, 1 and the strength: the
// desktop's near-white halo). The desktop tones and adds the bloom into a full-window image and bends
// that; here the bend only moves the lookup: one full-size pass and one full-size image less.
layout(location = 0) in vec2 uv;
SAMP(1) pic;
SAMP(2) bloomt;
// vp = flip (1: a GLES window, row 0 at the bottom), screen height, screen width, the Vulkan pre-rotation
// (0-3: the image turned 0 / 90 / 180 / 270 degrees clockwise for the display); a = curve centre (screen px), half
// the picture's height (px), curve k (0 = flat); b = the picture images on the screen (x, y, w, h px);
// c = bloom strength, tone on
PUSH PC { vec4 vp; vec4 a; vec4 b; vec4 c; } pc;
layout(location = 0) out vec4 outc;
void main() {
    vec2 q = gl_FragCoord.xy, p = q;                 // (the screen pixel, upright, row 0 at the top)
    float W = pc.vp.z, H = pc.vp.y; int rot = int(pc.vp.w + 0.5);
    if (pc.vp.x > 0.5) p.y = H - q.y;
    else if (rot == 1) p = vec2(q.y, H - q.x);
    else if (rot == 2) p = vec2(W - q.x, H - q.y);
    else if (rot == 3) p = vec2(W - q.y, q.x);
    vec2 ctr = pc.a.xy;
    vec2 d = (p - ctr) / pc.a.z;
    vec2 s = ctr + (p - ctr) * (1.0 + pc.a.w * dot(d, d));
    vec2 t = (s - pc.b.xy) / pc.b.zw;
    if (t.x < 0.0 || t.x > 1.0 || t.y < 0.0 || t.y > 1.0) { outc = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec3 c = texture(pic, t).rgb;
    if (pc.c.y > 0.5) {                   // a soft shoulder: linear up to 0.55, then rolling off towards 1
        vec3 k = 0.55 + 0.45 * (1.0 - exp(-(c - 0.55) / 0.6));
        c = mix(c, k, step(0.55, c));
    }
    vec4 b = texture(bloomt, t);
    c += b.rgb * vec3(0.85, 0.92, 1.0) * (b.a * pc.c.x);
    outc = vec4(c, 1.0);
}
