// the overlay sheet: a quad (a 4-vertex strip) at rect = x0, y0, x1, y1 (screen px, row 0 at the top)
// vp.x = 1 Vulkan, -1 a GLES window; vp.y = the Vulkan pre-rotation (see final.frag); vp.zw = screen size (upright)
PUSH VPC { vec4 vp; vec4 rect; } vpc;
layout(location = 0) out vec2 uv;
void main() {
    vec2 q = vec2(float(VID & 1), float((VID >> 1) & 1));
    vec2 p = mix(vpc.rect.xy, vpc.rect.zw, q);
    uv = q;
    float W = vpc.vp.z, H = vpc.vp.w; int rot = int(vpc.vp.y + 0.5);
    vec2 sz = vpc.vp.zw;
    if (rot == 1) { p = vec2(H - p.y, p.x); sz = vec2(H, W); }
    else if (rot == 2) p = vec2(W - p.x, H - p.y);
    else if (rot == 3) { p = vec2(p.y, W - p.x); sz = vec2(H, W); }
    vec2 ndc = p / sz * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y * vpc.vp.x, 0.0, 1.0);
}
