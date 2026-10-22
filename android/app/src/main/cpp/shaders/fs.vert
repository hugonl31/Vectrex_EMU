// a full-screen triangle (no vertex buffer); uv (0, 0) = texture row 0
layout(location = 0) out vec2 uv;
void main() {
    vec2 p = vec2(float((VID << 1) & 2), float(VID & 2));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
