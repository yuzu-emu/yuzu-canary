set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)

add_executable(yuzu-tester
    config.cpp
    config.h
    default_ini.h
    emu_window/emu_window_sdl2_hide.cpp
    emu_window/emu_window_sdl2_hide.h
    resource.h
    service/yuzutest.cpp
    service/yuzutest.h
    yuzu.cpp
    yuzu.rc
)

create_target_directory_groups(yuzu-tester)

target_link_libraries(yuzu-tester PRIVATE common core input_common)
target_link_libraries(yuzu-tester PRIVATE inih glad)
if (MSVC)
    target_link_libraries(yuzu-tester PRIVATE getopt)
endif()
target_link_libraries(yuzu-tester PRIVATE ${PLATFORM_LIBRARIES} SDL2 Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS yuzu-tester RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if (MSVC)
    include(CopyYuzuSDLDeps)
    include(CopyYuzuUnicornDeps)
    copy_yuzu_SDL_deps(yuzu-tester)
    copy_yuzu_unicorn_deps(yuzu-tester)
endif()
