add_library(pikasnip_overlay STATIC
    src/SelectionController.cpp
    src/MagnifierHud.cpp
    src/FloatingToolbar.cpp
    src/OverlayWindow.cpp
)

target_include_directories(pikasnip_overlay
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(pikasnip_overlay
    PUBLIC
        pikasnip_platform
        pikasnip_capture
        pikasnip_annotations
)
