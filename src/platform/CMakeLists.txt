add_library(pikasnip_platform STATIC
    include/pikasnip/platform/AppEnvironment.h
    include/pikasnip/platform/Clipboard.h
    include/pikasnip/platform/ComInitializer.h
    include/pikasnip/platform/D2dContext.h
    include/pikasnip/platform/Dpi.h
    include/pikasnip/platform/SettingsStore.h
    include/pikasnip/platform/StartupManager.h
    include/pikasnip/platform/ToastWindow.h
    src/AppEnvironment.cpp
    src/Clipboard.cpp
    src/ComInitializer.cpp
    src/D2dContext.cpp
    src/Dpi.cpp
    src/SettingsStore.cpp
    src/StartupManager.cpp
    src/ToastWindow.cpp
)

target_include_directories(pikasnip_platform
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(pikasnip_platform
    PUBLIC
        d2d1
        dwrite
        windowscodecs
    PRIVATE
        ole32
        oleaut32
        shell32
        shlwapi
        shcore
        uuid
        windowsapp
)

set_target_properties(pikasnip_platform PROPERTIES
    FOLDER "src/platform"
    OUTPUT_NAME "pikasnip_platform")
