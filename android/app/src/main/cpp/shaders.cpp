// shaders.cpp - the shader table: each of shaders/ compiled to SPIR-V and kept as GLSL ES source
// (gen_shader.cmake, at build time)
#include "backend.h"
#include <cstring>
#include <stdexcept>


namespace vx {
namespace {
const uint32_t spv_fs_vert[] =
#include "fs_vert.spv.inc"
;
const char gles_fs_vert[] =
#include "fs_vert.gles.inc"
;
const uint32_t spv_capsule_vert[] =
#include "capsule_vert.spv.inc"
;
const char gles_capsule_vert[] =
#include "capsule_vert.gles.inc"
;
const uint32_t spv_capsule_frag[] =
#include "capsule_frag.spv.inc"
;
const char gles_capsule_frag[] =
#include "capsule_frag.gles.inc"
;
const uint32_t spv_update_frag[] =
#include "update_frag.spv.inc"
;
const char gles_update_frag[] =
#include "update_frag.gles.inc"
;
const uint32_t spv_show_frag[] =
#include "show_frag.spv.inc"
;
const char gles_show_frag[] =
#include "show_frag.gles.inc"
;
const uint32_t spv_blur_frag[] =
#include "blur_frag.spv.inc"
;
const char gles_blur_frag[] =
#include "blur_frag.gles.inc"
;
const uint32_t spv_final_frag[] =
#include "final_frag.spv.inc"
;
const char gles_final_frag[] =
#include "final_frag.gles.inc"
;
const uint32_t spv_art_vert[] =
#include "art_vert.spv.inc"
;
const char gles_art_vert[] =
#include "art_vert.gles.inc"
;
const uint32_t spv_art_frag[] =
#include "art_frag.spv.inc"
;
const char gles_art_frag[] =
#include "art_frag.gles.inc"
;
#define E(n, id) { n, spv_##id, sizeof spv_##id, gles_##id }
const ShaderSrc table[] = {
    E("fs.vert", fs_vert), E("capsule.vert", capsule_vert), E("capsule.frag", capsule_frag), E("update.frag", update_frag),
    E("show.frag", show_frag), E("blur.frag", blur_frag), E("final.frag", final_frag), E("art.vert", art_vert), E("art.frag", art_frag),
};
}  // namespace

const ShaderSrc &shader(const char *name)
{
    for (auto &s : table) if (!strcmp(s.name, name)) return s;
    return table[0];
}

}  // namespace vx
