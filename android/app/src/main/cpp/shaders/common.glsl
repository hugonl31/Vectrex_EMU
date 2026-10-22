// common.glsl - put in front of every shader (after the #version line the build adds): one source for
// Vulkan (VK defined: glslc at build time) and OpenGL ES 3.1 (compiled by the driver at run time).
// Conventions, the same in both: pixel and texture row 0 = the top of the picture; every pass's
// parameters are one block of vec4s (Vulkan: push constants; GLES: a uniform buffer at binding 0).
#ifdef VK
#define PUSH layout(push_constant) uniform
#define SAMP(n) layout(set = 0, binding = n) uniform sampler2D
#define VID gl_VertexIndex
#else
#define PUSH layout(std140, binding = 0) uniform
#define SAMP(n) layout(binding = n) uniform sampler2D
#define VID gl_VertexID
#endif

// the afterglow curve (vecrender.py PHOS_FUNC; PHOS_A = 0.1404 at 45.4 ms, 0.0582 at 350 ms)
float bright(float f, float hold, float tscale, float toff) {   // a spot's brightness by its freshness f = e^-age
    if (f <= 0.0) return 0.0;
    float age = -log(f);
    if (age < hold) return 1.0;
    if (toff > 0.5) return 0.0;
    float t = age / tscale;
    return 0.1404 * exp(-t / 0.0454) + 0.0582 * exp(-t / 0.3500);
}
float lum(vec3 c) { return dot(c, vec3(0.3, 0.5, 0.2)); }
