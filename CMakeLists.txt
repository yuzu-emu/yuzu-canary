cmake_minimum_required(VERSION 3.7)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/externals/cmake-modules")
include(DownloadExternals)
include(CMakeDependentOption)

project(yuzu)

# Set bundled sdl2/qt as dependent options.
# OFF by default, but if ENABLE_SDL2 and MSVC are true then ON
option(ENABLE_SDL2 "Enable the SDL2 frontend" ON)
CMAKE_DEPENDENT_OPTION(YUZU_USE_BUNDLED_SDL2 "Download bundled SDL2 binaries" ON "ENABLE_SDL2;MSVC" OFF)

option(ENABLE_QT "Enable the Qt frontend" ON)
CMAKE_DEPENDENT_OPTION(YUZU_USE_BUNDLED_QT "Download bundled Qt binaries" ON "ENABLE_QT;MSVC" OFF)

option(ENABLE_WEB_SERVICE "Enable web services (telemetry, etc.)" ON)

option(YUZU_USE_BUNDLED_UNICORN "Build/Download bundled Unicorn" ON)

option(YUZU_USE_QT_WEB_ENGINE "Use QtWebEngine for web applet implementation" OFF)

option(ENABLE_CUBEB "Enables the cubeb audio backend" ON)

option(USE_DISCORD_PRESENCE "Enables Discord Rich Presence" OFF)

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/.git/hooks/pre-commit)
    message(STATUS "Copying pre-commit hook")
    file(COPY hooks/pre-commit
        DESTINATION ${PROJECT_SOURCE_DIR}/.git/hooks)
endif()

# Sanity check : Check that all submodules are present
# =======================================================================

function(check_submodules_present)
    file(READ "${PROJECT_SOURCE_DIR}/.gitmodules" gitmodules)
    string(REGEX MATCHALL "path *= *[^ \t\r\n]*" gitmodules ${gitmodules})
    foreach(module ${gitmodules})
        string(REGEX REPLACE "path *= *" "" module ${module})
        if (NOT EXISTS "${PROJECT_SOURCE_DIR}/${module}/.git")
            message(FATAL_ERROR "Git submodule ${module} not found. "
                    "Please run: git submodule update --init --recursive")
        endif()
    endforeach()
endfunction()
check_submodules_present()

configure_file(${PROJECT_SOURCE_DIR}/dist/compatibility_list/compatibility_list.qrc
               ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.qrc
               COPYONLY)
if (ENABLE_COMPATIBILITY_LIST_DOWNLOAD AND NOT EXISTS ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json)
    message(STATUS "Downloading compatibility list for yuzu...")
    file(DOWNLOAD
        https://api.yuzu-emu.org/gamedb/
        "${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json" SHOW_PROGRESS)
endif()
if (NOT EXISTS ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json)
    file(WRITE ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json "")
endif()

# Detect current compilation architecture and create standard definitions
# =======================================================================

include(CheckSymbolExists)
function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

if (NOT ENABLE_GENERIC)
    if (MSVC)
        detect_architecture("_M_AMD64" x86_64)
        detect_architecture("_M_IX86" x86)
        detect_architecture("_M_ARM" ARM)
        detect_architecture("_M_ARM64" ARM64)
    else()
        detect_architecture("__x86_64__" x86_64)
        detect_architecture("__i386__" x86)
        detect_architecture("__arm__" ARM)
        detect_architecture("__aarch64__" ARM64)
    endif()
endif()

if (NOT DEFINED ARCHITECTURE)
    set(ARCHITECTURE "GENERIC")
    set(ARCHITECTURE_GENERIC 1)
    add_definitions(-DARCHITECTURE_GENERIC=1)
endif()
message(STATUS "Target architecture: ${ARCHITECTURE}")


# Configure compilation flags
# ===========================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

    if (MINGW)
        add_definitions(-DMINGW_HAS_SECURE_API)

        if (MINGW_STATIC_BUILD)
            add_definitions(-DQT_STATICPLUGIN)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        endif()
    endif()
else()
    # Silence "deprecation" warnings
    add_definitions(/D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /D_SCL_SECURE_NO_WARNINGS)
    # Avoid windows.h junk
    add_definitions(/DNOMINMAX)
    # Avoid windows.h from including some usually unused libs like winsocks.h, since this might cause some redefinition errors.
    add_definitions(/DWIN32_LEAN_AND_MEAN)

    set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

    # Tweak optimization settings
    # As far as I can tell, there's no way to override the CMake defaults while leaving user
    # changes intact, so we'll just clobber everything and say sorry.
    message(STATUS "Cache compiler flags ignored, please edit CMakeLists.txt to change the flags.")

    # /W3 - Level 3 warnings
    # /MP - Multi-threaded compilation
    # /Zi - Output debugging information
    # /Zo - enhanced debug info for optimized builds
    # /permissive- - enables stricter C++ standards conformance checks
    set(CMAKE_C_FLAGS   "/W3 /MP /Zi /Zo /permissive-" CACHE STRING "" FORCE)
    # /EHsc - C++-only exception handling semantics
    # /Zc:throwingNew - let codegen assume `operator new` will never return null
    # /Zc:inline - let codegen omit inline functions in object files
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} /EHsc /std:c++latest /Zc:throwingNew,inline" CACHE STRING "" FORCE)

    # /MDd - Multi-threaded Debug Runtime DLL
    set(CMAKE_C_FLAGS_DEBUG   "/Od /MDd" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}" CACHE STRING "" FORCE)

    # /O2 - Optimization level 2
    # /GS- - No stack buffer overflow checks
    # /MD - Multi-threaded runtime DLL
    set(CMAKE_C_FLAGS_RELEASE   "/O2 /GS- /MD" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)

    set(CMAKE_EXE_LINKER_FLAGS_DEBUG   "/DEBUG /MANIFEST:NO" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG /MANIFEST:NO /INCREMENTAL:NO /OPT:REF,ICF" CACHE STRING "" FORCE)
endif()

# Fix GCC C++17 and Boost.ICL incompatibility (needed to build dynarmic)
# See https://bugzilla.redhat.com/show_bug.cgi?id=1485641#c1
if (CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-new-ttp-matching")
endif()

# Set file offset size to 64 bits.
#
# On modern Unixes, this is typically already the case. The lone exception is
# glibc, which may default to 32 bits. glibc allows this to be configured
# by setting _FILE_OFFSET_BITS.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR MINGW)
    add_definitions(-D_FILE_OFFSET_BITS=64)
endif()

# CMake seems to only define _DEBUG on Windows
set_property(DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

# System imported libraries
# ======================

find_package(Boost 1.63.0 QUIET)
if (NOT Boost_FOUND)
    message(STATUS "Boost 1.63.0 or newer not found, falling back to externals")

    set(BOOST_ROOT "${PROJECT_SOURCE_DIR}/externals/boost")
    set(Boost_NO_SYSTEM_PATHS OFF)
    find_package(Boost QUIET REQUIRED)
endif()

# Output binaries to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Prefer the -pthread flag on Linux.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (ENABLE_SDL2)
    if (YUZU_USE_BUNDLED_SDL2)
        # Detect toolchain and platform
        if ((MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1920) AND ARCHITECTURE_x86_64)
            set(SDL2_VER "SDL2-2.0.8")
        else()
            message(FATAL_ERROR "No bundled SDL2 binaries for your toolchain. Disable YUZU_USE_BUNDLED_SDL2 and provide your own.")
        endif()

        if (DEFINED SDL2_VER)
            download_bundled_external("sdl2/" ${SDL2_VER} SDL2_PREFIX)
        endif()

        set(SDL2_FOUND YES)
        set(SDL2_INCLUDE_DIR "${SDL2_PREFIX}/include" CACHE PATH "Path to SDL2 headers")
        set(SDL2_LIBRARY "${SDL2_PREFIX}/lib/x64/SDL2.lib" CACHE PATH "Path to SDL2 library")
        set(SDL2_DLL_DIR "${SDL2_PREFIX}/lib/x64/" CACHE PATH "Path to SDL2.dll")
    else()
        find_package(SDL2 REQUIRED)
    endif()

    if (SDL2_FOUND)
        # TODO(yuriks): Make FindSDL2.cmake export an IMPORTED library instead
        add_library(SDL2 INTERFACE)
        target_link_libraries(SDL2 INTERFACE "${SDL2_LIBRARY}")
        target_include_directories(SDL2 INTERFACE "${SDL2_INCLUDE_DIR}")
    endif()
else()
    set(SDL2_FOUND NO)
endif()

# If unicorn isn't found, msvc -> download bundled unicorn; everyone else -> build external
if (YUZU_USE_BUNDLED_UNICORN)
    if (MSVC)
        message(STATUS "unicorn not found, falling back to bundled")
        # Detect toolchain and platform
        if ((MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1920) AND ARCHITECTURE_x86_64)
            set(UNICORN_VER "unicorn-yuzu")
        else()
            message(FATAL_ERROR "No bundled Unicorn binaries for your toolchain. Disable YUZU_USE_BUNDLED_UNICORN and provide your own.")
        endif()

        if (DEFINED UNICORN_VER)
            download_bundled_external("unicorn/" ${UNICORN_VER} UNICORN_PREFIX)
        endif()

        if (DEFINED UNICORN_VER)
            download_bundled_external("unicorn/" ${UNICORN_VER} UNICORN_PREFIX)
        endif()

        set(UNICORN_FOUND YES)
        set(LIBUNICORN_INCLUDE_DIR "${UNICORN_PREFIX}/include" CACHE PATH "Path to Unicorn headers" FORCE)
        set(LIBUNICORN_LIBRARY "${UNICORN_PREFIX}/lib/x64/unicorn_dynload.lib" CACHE PATH "Path to Unicorn library" FORCE)
        set(UNICORN_DLL_DIR "${UNICORN_PREFIX}/lib/x64/" CACHE PATH "Path to unicorn.dll" FORCE)
    else()
        message(STATUS "unicorn not found, falling back to externals")
        if (MINGW)
            set(UNICORN_LIB_NAME "unicorn.a")
        else()
            set(UNICORN_LIB_NAME "libunicorn.a")
        endif()

        set(UNICORN_FOUND YES)
        set(UNICORN_PREFIX ${PROJECT_SOURCE_DIR}/externals/unicorn)
        set(LIBUNICORN_LIBRARY "${UNICORN_PREFIX}/${UNICORN_LIB_NAME}" CACHE PATH "Path to Unicorn library" FORCE)
        set(LIBUNICORN_INCLUDE_DIR "${UNICORN_PREFIX}/include" CACHE PATH "Path to Unicorn headers" FORCE)
        set(UNICORN_DLL_DIR "${UNICORN_PREFIX}/" CACHE PATH "Path to unicorn dynamic library" FORCE)

        find_package(PythonInterp 2.7 REQUIRED)

        if (MINGW)
            add_custom_command(OUTPUT ${LIBUNICORN_LIBRARY}
                COMMAND ${CMAKE_COMMAND} -E env UNICORN_ARCHS="aarch64" PYTHON="${PYTHON_EXECUTABLE}" /bin/sh make.sh cross-win64
                WORKING_DIRECTORY ${UNICORN_PREFIX}
            )
        else()
            add_custom_command(OUTPUT ${LIBUNICORN_LIBRARY}
                COMMAND ${CMAKE_COMMAND} -E env UNICORN_ARCHS="aarch64" PYTHON="${PYTHON_EXECUTABLE}" /bin/sh make.sh macos-universal-no
                WORKING_DIRECTORY ${UNICORN_PREFIX}
            )
        endif()

        # ALL makes this custom target build every time
        # but it won't actually build if LIBUNICORN_LIBRARY is up to date
        add_custom_target(unicorn-build ALL
            DEPENDS ${LIBUNICORN_LIBRARY}
        )
        unset(UNICORN_LIB_NAME)
    endif()
else()
    find_package(Unicorn REQUIRED)
endif()

if (UNICORN_FOUND)
    add_library(unicorn INTERFACE)
    add_dependencies(unicorn unicorn-build)
    target_link_libraries(unicorn INTERFACE "${LIBUNICORN_LIBRARY}")
    target_include_directories(unicorn INTERFACE "${LIBUNICORN_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "Could not find or build unicorn which is required.")
endif()

if (ENABLE_QT)
    if (YUZU_USE_BUNDLED_QT)
        if ((MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1920) AND ARCHITECTURE_x86_64)
            set(QT_VER qt-5.12.0-msvc2017_64)
        else()
            message(FATAL_ERROR "No bundled Qt binaries for your toolchain. Disable YUZU_USE_BUNDLED_QT and provide your own.")
        endif()

        if (DEFINED QT_VER)
            download_bundled_external("qt/" ${QT_VER} QT_PREFIX)
        endif()

        set(QT_PREFIX_HINT HINTS "${QT_PREFIX}")
    else()
        # Passing an empty HINTS seems to cause default system paths to get ignored in CMake 2.8 so
        # make sure to not pass anything if we don't have one.
        set(QT_PREFIX_HINT)
    endif()

    find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL ${QT_PREFIX_HINT})

    if (YUZU_USE_QT_WEB_ENGINE)
        find_package(Qt5 REQUIRED COMPONENTS WebEngineCore WebEngineWidgets ${QT_PREFIX_HINT})
    endif ()
endif()

# Platform-specific library requirements
# ======================================

IF (APPLE)
    find_library(COCOA_LIBRARY Cocoa)           # Umbrella framework for everything GUI-related
    set(PLATFORM_LIBRARIES ${COCOA_LIBRARY} ${IOKIT_LIBRARY} ${COREVIDEO_LIBRARY})

    if (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
    endif()
ELSEIF (WIN32)
    # WSAPoll and SHGetKnownFolderPath (AppData/Roaming) didn't exist before WinNT 6.x (Vista)
    add_definitions(-D_WIN32_WINNT=0x0600 -DWINVER=0x0600)
    set(PLATFORM_LIBRARIES winmm ws2_32)
    IF (MINGW)
        # PSAPI is the Process Status API
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} psapi imm32 version)
    ENDIF (MINGW)
ELSEIF (CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU|SunOS)$")
    set(PLATFORM_LIBRARIES rt)
ENDIF (APPLE)

# Setup a custom clang-format target (if clang-format can be found) that will run
# against all the src files. This should be used before making a pull request.
# =======================================================================

set(CLANG_FORMAT_POSTFIX "-6.0")
find_program(CLANG_FORMAT
    NAMES clang-format${CLANG_FORMAT_POSTFIX}
          clang-format
    PATHS ${PROJECT_BINARY_DIR}/externals)
# if find_program doesn't find it, try to download from externals
if (NOT CLANG_FORMAT)
    if (WIN32)
        message(STATUS "Clang format not found! Downloading...")
        set(CLANG_FORMAT "${PROJECT_BINARY_DIR}/externals/clang-format${CLANG_FORMAT_POSTFIX}.exe")
        file(DOWNLOAD
            https://github.com/yuzu-emu/ext-windows-bin/raw/master/clang-format${CLANG_FORMAT_POSTFIX}.exe
            "${CLANG_FORMAT}" SHOW_PROGRESS
            STATUS DOWNLOAD_SUCCESS)
        if (NOT DOWNLOAD_SUCCESS EQUAL 0)
            message(WARNING "Could not download clang format! Disabling the clang format target")
            file(REMOVE ${CLANG_FORMAT})
            unset(CLANG_FORMAT)
        endif()
    else()
        message(WARNING "Clang format not found! Disabling the clang format target")
    endif()
endif()

if (CLANG_FORMAT)
    set(SRCS ${PROJECT_SOURCE_DIR}/src)
    set(CCOMMENT "Running clang format against all the .h and .cpp files in src/")
    if (WIN32)
        add_custom_target(clang-format
            COMMAND powershell.exe -Command "Get-ChildItem ${SRCS}/* -Include *.cpp,*.h -Recurse | Foreach {${CLANG_FORMAT} -i $_.fullname}"
            COMMENT ${CCOMMENT})
    elseif(MINGW)
        add_custom_target(clang-format
            COMMAND find `cygpath -u ${SRCS}` -iname *.h -o -iname *.cpp | xargs `cygpath -u ${CLANG_FORMAT}` -i
            COMMENT ${CCOMMENT})
    else()
        add_custom_target(clang-format
            COMMAND find ${SRCS} -iname *.h -o -iname *.cpp | xargs ${CLANG_FORMAT} -i
            COMMENT ${CCOMMENT})
    endif()
    unset(SRCS)
    unset(CCOMMENT)
endif()

# Include source code
# ===================

# This function should be passed a list of all files in a target. It will automatically generate
# file groups following the directory hierarchy, so that the layout of the files in IDEs matches the
# one in the filesystem.
function(create_target_directory_groups target_name)
    # Place any files that aren't in the source list in a separate group so that they don't get in
    # the way.
    source_group("Other Files" REGULAR_EXPRESSION ".")

    get_target_property(target_sources "${target_name}" SOURCES)

    foreach(file_name IN LISTS target_sources)
        get_filename_component(dir_name "${file_name}" PATH)
        # Group names use '\' as a separator even though the entire rest of CMake uses '/'...
        string(REPLACE "/" "\\" group_name "${dir_name}")
        source_group("${group_name}" FILES "${file_name}")
    endforeach()
endfunction()

enable_testing()
add_subdirectory(externals)
add_subdirectory(src)

# Set yuzu project or yuzu-cmd project as default StartUp Project in Visual Studio depending on whether QT is enabled or not
if(ENABLE_QT)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT yuzu)
else()
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT yuzu-cmd)
endif()


# Installation instructions
# =========================

# Install freedesktop.org metadata files, following those specifications:
# http://standards.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html
# http://standards.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html
# http://standards.freedesktop.org/shared-mime-info-spec/shared-mime-info-spec-latest.html
if(ENABLE_QT AND UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/dist/yuzu.desktop"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
    install(FILES "${PROJECT_SOURCE_DIR}/dist/yuzu.svg"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps")
    install(FILES "${PROJECT_SOURCE_DIR}/dist/yuzu.xml"
            DESTINATION "${CMAKE_INSTALL_PREFIX}/share/mime/packages")
endif()
