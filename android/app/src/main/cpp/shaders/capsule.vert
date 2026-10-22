// a beam segment as a capsule with a Gaussian cross-section (vecrender.py CAPSULE_SHADER and _build):
// one instance per line, its quad made here. seg = a.xy, b.xy (target px, row 0 at the top);
// prm = brightness, sigma (px), the line's time in its frame (s)
layout(location = 0) in vec4 seg;
layout(location = 1) in vec4 prm;
PUSH PC { vec4 vp; vec4 t; } pc;          // vp.xy = target size, vp.z = Even beam; t = timed, age0, dtw, gain
layout(location = 0) flat out vec4 vseg;
layout(location = 1) flat out vec4 vprm;
void main() {
    vec2 a = seg.xy, b = seg.zw, d = b - a;
    float lp = length(d), m = 2.6 * prm.y + 1.0;            // (the quad's margin)
    vec2 u = lp > 1e-3 ? d / lp * m : vec2(m, 0.0);
    vec2 n = vec2(-u.y, u.x);
    int i = VID;                                            // (a strip: a-u+n, b+u+n, a-u-n, b+u-n)
    vec2 p = ((i & 1) == 0 ? a - u : b + u) + ((i & 2) == 0 ? n : -n);
    vseg = seg; vprm = prm;
    gl_Position = vec4(p / pc.vp.xy * 2.0 - 1.0, 0.0, 1.0);
}
