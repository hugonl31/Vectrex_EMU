// separable Gaussian (vecrender.py BLUR_SHADER) at quarter size; the first pass reads the full-size
// picture directly (the desktop's quarter-size copy and its first blur in one pass)
layout(location = 0) in vec2 uv;
SAMP(1) tex;
PUSH PC { vec4 vp; vec4 a; } pc;                  // a.xy = dir (uv per quarter-size texel)
layout(location = 0) out vec4 outc;
void main() {
    vec2 dir = pc.a.xy;
    vec4 c = texture(tex, uv) * 0.2270270270;
    c += texture(tex, uv + dir * 1.3846153846) * 0.3162162162;
    c += texture(tex, uv - dir * 1.3846153846) * 0.3162162162;
    c += texture(tex, uv + dir * 3.2307692308) * 0.0702702703;
    c += texture(tex, uv - dir * 3.2307692308) * 0.0702702703;
    outc = c;
}
