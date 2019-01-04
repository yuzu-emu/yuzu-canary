function(copy_yuzu_Qt5_deps target_dir)
    include(WindowsCopyFiles)
    set(DLL_DEST "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/")
    set(Qt5_DLL_DIR "${Qt5_DIR}/../../../bin")
    set(Qt5_PLATFORMS_DIR "${Qt5_DIR}/../../../plugins/platforms/")
    set(Qt5_STYLES_DIR "${Qt5_DIR}/../../../plugins/styles/")
    set(Qt5_IMAGEFORMATS_DIR "${Qt5_DIR}/../../../plugins/imageformats/")
    set(Qt5_RESOURCES_DIR "${Qt5_DIR}/../../../resources/")
    set(PLATFORMS ${DLL_DEST}platforms/)
    set(STYLES ${DLL_DEST}styles/)
    set(IMAGEFORMATS ${DLL_DEST}imageformats/)
    set(RESOURCES ${DLL_DEST}resources/)
    windows_copy_files(${target_dir} ${Qt5_DLL_DIR} ${DLL_DEST}
        icudt*.dll
        icuin*.dll
        icuuc*.dll
        Qt5Core$<$<CONFIG:Debug>:d>.*
        Qt5Gui$<$<CONFIG:Debug>:d>.*
        Qt5OpenGL$<$<CONFIG:Debug>:d>.*
        Qt5Widgets$<$<CONFIG:Debug>:d>.*
    )

    if (YUZU_USE_QT_WEB_ENGINE)
        windows_copy_files(${target_dir} ${Qt5_DLL_DIR} ${DLL_DEST} 
            Qt5Network$<$<CONFIG:Debug>:d>.*
            Qt5Positioning$<$<CONFIG:Debug>:d>.*
            Qt5PrintSupport$<$<CONFIG:Debug>:d>.*
            Qt5Qml$<$<CONFIG:Debug>:d>.*
            Qt5Quick$<$<CONFIG:Debug>:d>.*
            Qt5QuickWidgets$<$<CONFIG:Debug>:d>.*
            Qt5WebChannel$<$<CONFIG:Debug>:d>.*
            Qt5WebEngine$<$<CONFIG:Debug>:d>.*
            Qt5WebEngineCore$<$<CONFIG:Debug>:d>.*
            Qt5WebEngineWidgets$<$<CONFIG:Debug>:d>.*
            QtWebEngineProcess$<$<CONFIG:Debug>:d>.*
        )

        windows_copy_files(${target_dir} ${Qt5_RESOURCES_DIR} ${RESOURCES}
            qtwebengine_resources.pak
            qtwebengine_devtools_resources.pak
            qtwebengine_resources_100p.pak
            qtwebengine_resources_200p.pak
            icudtl.dat
        )
    endif ()

    windows_copy_files(yuzu ${Qt5_PLATFORMS_DIR} ${PLATFORMS} qwindows$<$<CONFIG:Debug>:d>.*)
    windows_copy_files(yuzu ${Qt5_STYLES_DIR} ${STYLES} qwindowsvistastyle$<$<CONFIG:Debug>:d>.*)
    windows_copy_files(yuzu ${Qt5_IMAGEFORMATS_DIR} ${IMAGEFORMATS} qjpeg$<$<CONFIG:Debug>:d>.*)
endfunction(copy_yuzu_Qt5_deps)
