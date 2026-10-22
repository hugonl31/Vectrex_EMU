// backend.h - a graphics backend executes a FramePlan (scene.h) on a window. Two: Vulkan
// (vk_backend.cpp, preferred) and OpenGL ES 3.1 (gles_backend.cpp, where Vulkan is missing or fails).
// The passes, the same in both:
//   F     the fresh light: the young frames' capsules (colour added, freshness = max)    full size
//   P2    the phosphor image aged and lit again (update.frag; P / P2 ping-pong)          full size
//   D     what it shows (show.frag)                                                      full size
//   B     the bloom: four separable blur passes (blur.frag)                              quarter size
//   swap  the screen: tube curve, tone, bloom (final.frag), then the overlay (art.frag)
// "Full size" is the picture's rectangle (plus a margin) times the render scale, not the whole screen.
#pragma once
#include "scene.h"
#include <android/native_window.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vx {

class Backend {
public:
    virtual ~Backend() {}
    virtual bool init(ANativeWindow *win, std::string &err) = 0;
    virtual void size(int &w, int &h) = 0;              // the window's size in pixels
    virtual void set_art(int w, int h, const std::vector<uint8_t> &rgba) = 0;   // (rows top first; w = 0: none)
    virtual bool render(const FramePlan &p) = 0;        // false: the window was lost (recreate)
    virtual const char *name() const = 0;
    virtual bool float_images() const = 0;
};

std::unique_ptr<Backend> make_vulkan();
std::unique_ptr<Backend> make_gles();

// the shaders: SPIR-V for Vulkan, GLSL ES source for GLES (both made from shaders/ at build time)
struct ShaderSrc { const char *name; const uint32_t *spv; size_t spv_size; const char *gles; };
const ShaderSrc &shader(const char *name);

}  // namespace vx
