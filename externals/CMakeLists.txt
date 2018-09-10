# Definitions for all external bundled libraries

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)
include(DownloadExternals)

# Catch
add_library(catch-single-include INTERFACE)
target_include_directories(catch-single-include INTERFACE catch/single_include)

# Dynarmic
if (ARCHITECTURE_x86_64)
    add_library(xbyak INTERFACE)
    set(DYNARMIC_TESTS OFF)
    set(DYNARMIC_NO_BUNDLED_FMT ON)
    add_subdirectory(dynarmic)
endif()

# libfmt
add_subdirectory(fmt)
add_library(fmt::fmt ALIAS fmt)

# getopt
if (MSVC)
    add_subdirectory(getopt)
endif()

# Glad
add_subdirectory(glad)

# inih
add_subdirectory(inih)

# lz4
set(LZ4_BUNDLED_MODE ON)
add_subdirectory(lz4/contrib/cmake_unofficial EXCLUDE_FROM_ALL)
target_include_directories(lz4_static INTERFACE ./lz4/lib)

# mbedtls
add_subdirectory(mbedtls EXCLUDE_FROM_ALL)
target_include_directories(mbedtls PUBLIC ./mbedtls/include)

# MicroProfile
add_library(microprofile INTERFACE)
target_include_directories(microprofile INTERFACE ./microprofile)

# Open Source Archives
add_subdirectory(open_source_archives EXCLUDE_FROM_ALL)

# Unicorn
add_library(unicorn-headers INTERFACE)
target_include_directories(unicorn-headers INTERFACE ./unicorn/include)

# SoundTouch
add_subdirectory(soundtouch)

# Xbyak
if (ARCHITECTURE_x86_64)
    # Defined before "dynarmic" above
    # add_library(xbyak INTERFACE)
    target_include_directories(xbyak INTERFACE ./xbyak/xbyak)
    target_compile_definitions(xbyak INTERFACE XBYAK_NO_OP_NAMES)
endif()

# Opus
add_subdirectory(opus)
target_include_directories(opus INTERFACE ./opus/include)

# Cubeb
if(ENABLE_CUBEB)
    set(BUILD_TESTS OFF CACHE BOOL "")
    add_subdirectory(cubeb EXCLUDE_FROM_ALL)
endif()
