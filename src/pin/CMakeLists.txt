add_library(pikasnip_pin STATIC
    include/pikasnip/pin/PinManager.h
    include/pikasnip/pin/PinWindow.h
    src/PinManager.cpp
    src/PinToolbarWindow.cpp
    src/PinToolbarWindow.h
    src/PinWindow.cpp
)

target_include_directories(pikasnip_pin
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# PinToolbarWindow reuses overlay/FloatingToolbar.h, hence the overlay link.
target_link_libraries(pikasnip_pin
    PUBLIC
        pikasnip_platform
        pikasnip_annotations
        pikasnip_overlay
    PRIVATE
        d2d1
        dwrite
        windowscodecs
        shcore
)

set_target_properties(pikasnip_pin PROPERTIES
    FOLDER "src/pin"
    OUTPUT_NAME "pikasnip_pin")
