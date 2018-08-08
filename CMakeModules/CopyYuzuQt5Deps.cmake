function(copy_yuzu_Qt5_deps target_dir)
    include(WindowsCopyFiles)
    set(DLL_DEST "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/")
    set(Qt5_DLL_DIR "${Qt5_DIR}/../../../bin")
    set(Qt5_PLATFORMS_DIR "${Qt5_DIR}/../../../plugins/platforms/")
    set(Qt5_STYLES_DIR "${Qt5_DIR}/../../../plugins/styles/")
    set(Qt5_IMAGEFORMATS_DIR "${Qt5_DIR}/../../../plugins/imageformats/")
    set(PLATFORMS ${DLL_DEST}platforms/)
    set(STYLES ${DLL_DEST}styles/)
    set(IMAGEFORMATS ${DLL_DEST}imageformats/)
    windows_copy_files(${target_dir} ${Qt5_DLL_DIR} ${DLL_DEST}
        icudt*.dll
        icuin*.dll
        icuuc*.dll
        Qt5Core$<$<CONFIG:Debug>:d>.*
        Qt5Gui$<$<CONFIG:Debug>:d>.*
        Qt5OpenGL$<$<CONFIG:Debug>:d>.*
        Qt5Widgets$<$<CONFIG:Debug>:d>.*
    )
    windows_copy_files(yuzu ${Qt5_PLATFORMS_DIR} ${PLATFORMS} qwindows$<$<CONFIG:Debug>:d>.*)
    windows_copy_files(yuzu ${Qt5_STYLES_DIR} ${STYLES} qwindowsvistastyle$<$<CONFIG:Debug>:d>.*)
    windows_copy_files(yuzu ${Qt5_IMAGEFORMATS_DIR} ${IMAGEFORMATS} qjpeg$<$<CONFIG:Debug>:d>.*)
endfunction(copy_yuzu_Qt5_deps)
