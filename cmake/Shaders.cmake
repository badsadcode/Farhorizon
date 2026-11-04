# Compiles GLSL to SPIR-V at build time. Shaders stay separate, versionable files.
find_program(FH_GLSLANG NAMES glslangValidator HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)
if(NOT FH_GLSLANG)
  message(FATAL_ERROR "glslangValidator not found (install the Vulkan SDK or glslang-tools)")
endif()
set(FH_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

function(fh_compile_shaders target)
  set(outputs)
  foreach(src ${ARGN})
    get_filename_component(name ${src} NAME)
    set(out ${FH_SHADER_OUTPUT_DIR}/${name}.spv)
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${FH_SHADER_OUTPUT_DIR}
      COMMAND ${FH_GLSLANG} -V --target-env vulkan1.3 -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.glsl
      COMMENT "GLSL -> SPIR-V ${name}"
      VERBATIM)
    list(APPEND outputs ${out})
  endforeach()
  add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()
