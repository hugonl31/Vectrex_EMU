# The native library: the Vectrex core (../../../../../src/core, the desktop program's, unchanged), the
# phosphor renderer (Vulkan and OpenGL ES 3.1), the CRT sound model and the JNI side.
cmake_minimum_required(VERSION 3.22)
project(vectrexemu C CXX)

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/core)
set(SH ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${GEN})

# glslc comes with the NDK
find_program(GLSLC glslc HINTS ${ANDROID_NDK}/shader-tools/windows-x86_64 ${ANDROID_NDK}/shader-tools/linux-x86_64
             ${ANDROID_NDK}/shader-tools/darwin-x86_64 REQUIRED)

set(SHADERS fs.vert capsule.vert capsule.frag update.frag show.frag blur.frag final.frag art.vert art.frag
            scr_gen.vert scr_gen.frag copy.frag harm_h.frag harm_v.frag sheet.frag)
set(SHADER_OUT)
foreach(s ${SHADERS})
  string(REPLACE "." "_" c ${s})
  get_filename_component(stage ${s} LAST_EXT)
  string(SUBSTRING ${stage} 1 -1 stage)
  add_custom_command(
    OUTPUT ${GEN}/${c}.spv.inc ${GEN}/${c}.gles.inc
    COMMAND ${CMAKE_COMMAND} -DSRC=${SH}/${s} -DCOMMON=${SH}/common.glsl -DGLSLC=${GLSLC} -DOUT=${GEN} -DSTAGE=${stage}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_shader.cmake
    DEPENDS ${SH}/${s} ${SH}/common.glsl ${CMAKE_CURRENT_SOURCE_DIR}/gen_shader.cmake
    COMMENT "shader ${s}")
  list(APPEND SHADER_OUT ${GEN}/${c}.spv.inc ${GEN}/${c}.gles.inc)
endforeach()
add_custom_target(vx_shaders DEPENDS ${SHADER_OUT})
# the table of them (shaders.cpp includes it)
set(TABLE "")
set(ENTRIES "")
foreach(s ${SHADERS})
  string(REPLACE "." "_" c ${s})
  string(APPEND TABLE "const uint32_t spv_${c}[] =\n#include \"${c}.spv.inc\"\n;\n"
                      "const char gles_${c}[] =\n#include \"${c}.gles.inc\"\n;\n")
  string(APPEND ENTRIES "    { \"${s}\", spv_${c}, sizeof spv_${c}, gles_${c} },\n")
endforeach()
file(WRITE ${GEN}/shader_table.inc.new "${TABLE}const ShaderSrc table[] = {\n${ENTRIES}};\n")
configure_file(${GEN}/shader_table.inc.new ${GEN}/shader_table.inc COPYONLY)

# the core, as the desktop builds it (build_linux.sh: -O2 -w -fgnu89-inline)
add_library(vxcore STATIC core_api.c ${CORE}/vecx.c ${CORE}/e6809.c ${CORE}/e8910.c)
target_include_directories(vxcore PRIVATE ${CORE} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(vxcore PRIVATE -O2 -w -fgnu89-inline)

add_library(vectrexemu SHARED engine.cpp scene.cpp crt_sound.cpp shaders.cpp gles_backend.cpp vk_backend.cpp)
add_dependencies(vectrexemu vx_shaders)
target_include_directories(vectrexemu PRIVATE ${GEN} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(vectrexemu PRIVATE -O2 -Wall -Wno-missing-braces -Wno-unused-function)
target_compile_features(vectrexemu PRIVATE cxx_std_17)
target_link_libraries(vectrexemu vxcore android log aaudio EGL GLESv3 vulkan m)
