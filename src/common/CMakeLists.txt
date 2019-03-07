# Add a custom command to generate a new shader_cache_version hash when any of the following files change
# NOTE: This is an approximation of what files affect shader generation, its possible something else
# could affect the result, but much more unlikely than the following files. Keeping a list of files
# like this allows for much better caching since it doesn't force the user to recompile binary shaders every update
set(VIDEO_CORE "${CMAKE_SOURCE_DIR}/src/video_core")
if (DEFINED ENV{CI})
  if (DEFINED ENV{TRAVIS})
    set(BUILD_REPOSITORY $ENV{TRAVIS_REPO_SLUG})
    set(BUILD_TAG $ENV{TRAVIS_TAG})
  elseif(DEFINED ENV{APPVEYOR})
    set(BUILD_REPOSITORY $ENV{APPVEYOR_REPO_NAME})
    set(BUILD_TAG $ENV{APPVEYOR_REPO_TAG_NAME})
  endif()
endif()
add_custom_command(OUTPUT scm_rev.cpp
    COMMAND ${CMAKE_COMMAND}
      -DSRC_DIR="${CMAKE_SOURCE_DIR}"
      -DBUILD_REPOSITORY="${BUILD_REPOSITORY}"
      -DBUILD_TAG="${BUILD_TAG}"
      -P "${CMAKE_SOURCE_DIR}/CMakeModules/GenerateSCMRev.cmake"
    DEPENDS
      # WARNING! It was too much work to try and make a common location for this list,
      # so if you need to change it, please update CMakeModules/GenerateSCMRev.cmake as well
      "${VIDEO_CORE}/renderer_opengl/gl_shader_cache.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_cache.h"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_decompiler.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_decompiler.h"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_disk_cache.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_disk_cache.h"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_gen.cpp"
      "${VIDEO_CORE}/renderer_opengl/gl_shader_gen.h"
      "${VIDEO_CORE}/shader/decode/arithmetic.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_half.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_half_immediate.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_immediate.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_integer.cpp"
      "${VIDEO_CORE}/shader/decode/arithmetic_integer_immediate.cpp"
      "${VIDEO_CORE}/shader/decode/bfe.cpp"
      "${VIDEO_CORE}/shader/decode/bfi.cpp"
      "${VIDEO_CORE}/shader/decode/conversion.cpp"
      "${VIDEO_CORE}/shader/decode/ffma.cpp"
      "${VIDEO_CORE}/shader/decode/float_set.cpp"
      "${VIDEO_CORE}/shader/decode/float_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/half_set.cpp"
      "${VIDEO_CORE}/shader/decode/half_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/hfma2.cpp"
      "${VIDEO_CORE}/shader/decode/integer_set.cpp"
      "${VIDEO_CORE}/shader/decode/integer_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/memory.cpp"
      "${VIDEO_CORE}/shader/decode/texture.cpp"
      "${VIDEO_CORE}/shader/decode/other.cpp"
      "${VIDEO_CORE}/shader/decode/predicate_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/predicate_set_register.cpp"
      "${VIDEO_CORE}/shader/decode/register_set_predicate.cpp"
      "${VIDEO_CORE}/shader/decode/shift.cpp"
      "${VIDEO_CORE}/shader/decode/video.cpp"
      "${VIDEO_CORE}/shader/decode/xmad.cpp"
      "${VIDEO_CORE}/shader/decode.cpp"
      "${VIDEO_CORE}/shader/shader_ir.cpp"
      "${VIDEO_CORE}/shader/shader_ir.h"
      "${VIDEO_CORE}/shader/track.cpp"
      # and also check that the scm_rev files haven't changed
      "${CMAKE_CURRENT_SOURCE_DIR}/scm_rev.cpp.in"
      "${CMAKE_CURRENT_SOURCE_DIR}/scm_rev.h"
      # technically we should regenerate if the git version changed, but its not worth the effort imo
      "${CMAKE_SOURCE_DIR}/CMakeModules/GenerateSCMRev.cmake"
)

add_library(common STATIC
    alignment.h
    assert.h
    detached_tasks.cpp
    detached_tasks.h
    bit_field.h
    bit_util.h
    cityhash.cpp
    cityhash.h
    color.h
    common_funcs.h
    common_paths.h
    common_types.h
    file_util.cpp
    file_util.h
    hash.h
    hex_util.cpp
    hex_util.h
    logging/backend.cpp
    logging/backend.h
    logging/filter.cpp
    logging/filter.h
    logging/log.h
    logging/text_formatter.cpp
    logging/text_formatter.h
    math_util.h
    microprofile.cpp
    microprofile.h
    microprofileui.h
    misc.cpp
    param_package.cpp
    param_package.h
    quaternion.h
    ring_buffer.h
    scm_rev.cpp
    scm_rev.h
    scope_exit.h
    string_util.cpp
    string_util.h
    swap.h
    telemetry.cpp
    telemetry.h
    thread.cpp
    thread.h
    thread_queue_list.h
    threadsafe_queue.h
    timer.cpp
    timer.h
    vector_math.h
    web_result.h
)

if(ARCHITECTURE_x86_64)
    target_sources(common
        PRIVATE
            x64/cpu_detect.cpp
            x64/cpu_detect.h
    )
endif()

create_target_directory_groups(common)

target_link_libraries(common PUBLIC Boost::boost fmt microprofile)
