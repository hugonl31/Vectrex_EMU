layout(location = 0) flat in vec4 vseg;
layout(location = 1) flat in vec4 vprm;
PUSH PC { vec4 vp; vec4 t; } pc;
layout(location = 0) out vec4 o;
void main() {
    vec2 p = gl_FragCoord.xy, a = vseg.xy, ba = vseg.zw - vseg.xy;
    float h = clamp(dot(p - a, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    float d = length(p - a - ba * h);
    float sg = min(vprm.y, 16.0);                           // (the desktop's colour alpha: sigma / 16, at most 1)
    float g = exp(-0.5 * d * d / (sg * sg));
    float timed = pc.t.x;
    if (timed > 0.5) {                      // fresh phosphor light: brightness, and its life left
        float age = timed > 1.5 ? 0.0 : pc.t.y - vprm.z;
        if (age < 0.0 || age >= pc.t.z) discard;           // (not drawn yet / already lit earlier)
        o = vec4(vec3(vprm.x * g * pc.t.w), exp(-age));     // (alpha: freshness, e^-age)
        return;
    }
    o = vec4(vec3(vprm.x), g);                              // (blend SRC_ALPHA, ONE: crisp, no phosphor)
}
