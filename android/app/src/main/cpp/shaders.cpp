// shaders.cpp - the shader table: each of shaders/ compiled to SPIR-V and kept as GLSL ES source
// (gen_shader.cmake at build time; the list itself: SHADERS in CMakeLists.txt -> shader_table.inc)
#include "backend.h"
#include <cstring>

namespace vx {
namespace {
#include "shader_table.inc"
}  // namespace

const ShaderSrc &shader(const char *name)
{
    for (auto &s : table) if (!strcmp(s.name, name)) return s;
    return table[0];
}

}  // namespace vx
