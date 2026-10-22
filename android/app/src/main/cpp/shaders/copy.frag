// a smaller copy (one bilinear tap: at half size the mean of 2 x 2)
layout(location = 0) in vec2 uv;
SAMP(1) tex;
layout(location = 0) out vec4 outc;
void main() { outc = texture(tex, uv); }
