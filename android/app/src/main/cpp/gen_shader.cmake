# gen_shader.cmake - one shader source (shaders/NAME.STAGE, no #version line) -> two includes:
#   NAME_STAGE.spv.inc   SPIR-V for Vulkan (glslc, with VK defined), a C initializer list
#   NAME_STAGE.gles.inc  the GLSL ES 3.10 source as a C++ raw string (compiled by the driver)
# called at build time: cmake -DSRC=... -DCOMMON=... -DGLSLC=... -DOUT=... -DSTAGE=vert|frag -P gen_shader.cmake
file(READ "${COMMON}" common)
file(READ "${SRC}" body)
get_filename_component(base "${SRC}" NAME)
string(REPLACE "." "_" cname "${base}")
file(WRITE "${OUT}/${cname}.vk.glsl" "#version 450\n#define VK 1\n${common}\n${body}")
execute_process(COMMAND "${GLSLC}" -fshader-stage=${STAGE} --target-env=vulkan1.0 -O -mfmt=c
                        -o "${OUT}/${cname}.spv.inc" "${OUT}/${cname}.vk.glsl"
                RESULT_VARIABLE res ERROR_VARIABLE msg)
if(res)
  message(FATAL_ERROR "glslc ${base}: ${msg}")
endif()
file(WRITE "${OUT}/${cname}.gles.inc"
     "R\"VXGLSL(#version 310 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n${common}\n${body})VXGLSL\"\n")
