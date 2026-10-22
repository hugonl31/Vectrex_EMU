// the overlay (vecrender.py ART_SHADER): alpha 255 printed (lit by the room), 1-254 a colour filter,
// 0 clear; pass 0 multiplies (blend DST_COLOR, SRC_COLOR = dst x 2 src), pass 1 adds the sheet's own light
layout(location = 0) in vec2 uv;
SAMP(1) tex;
PUSH PC { vec4 vp; vec4 rect; vec4 a; } pc;      // a = pass, tint, room, print
layout(location = 0) out vec4 outc;
void main() {
    vec4 o = texture(tex, uv);
    float filt = step(0.004, o.a) * (1.0 - step(0.996, o.a)), opq = step(0.996, o.a);
    vec3 hue = o.rgb / max(max(max(o.r, o.g), o.b), 0.02);       // (the filter's colour at full strength)
    if (pc.a.x < 0.5) {
        vec3 m = mix(vec3(1.0), hue * 1.15, pc.a.y * filt) * (1.0 - opq);
        outc = vec4(0.5 * m, 1.0);
    } else
        outc = vec4(o.rgb * (filt * pc.a.z + opq * pc.a.w), 1.0);
}
