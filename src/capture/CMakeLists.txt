add_library(pikasnip_capture STATIC
    include/pikasnip/capture/CaptureEngine.h
    include/pikasnip/capture/CapturedFrame.h
    include/pikasnip/capture/DxgiDuplicator.h
    include/pikasnip/capture/GdiCaptureFallback.h
    include/pikasnip/capture/WindowDetector.h
    src/CaptureEngine.cpp
    src/CapturedFrame.cpp
    src/DxgiDuplicator.cpp
    src/GdiCaptureFallback.cpp
    src/WindowDetector.cpp
)

target_include_directories(pikasnip_capture
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# pikasnip_platform is PUBLIC: CapturedFrame.h exposes platform::RectI.
target_link_libraries(pikasnip_capture
    PUBLIC
        pikasnip_platform
    PRIVATE
        d3d11
        dxgi
        dwmapi
        shcore
)

set_target_properties(pikasnip_capture PROPERTIES
    FOLDER "src/capture"
    OUTPUT_NAME "pikasnip_capture")
