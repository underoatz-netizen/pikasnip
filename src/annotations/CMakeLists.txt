add_library(pikasnip_annotations STATIC
    include/pikasnip/annotations/AnnotationRenderer.h
    include/pikasnip/annotations/AnnotationSession.h
    include/pikasnip/annotations/AnnotationTypes.h
    src/AnnotationRenderer.cpp
    src/AnnotationSession.cpp
)

target_include_directories(pikasnip_annotations
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Public headers include platform (D2dContext/Dpi) and capture (CapturedFrame).
target_link_libraries(pikasnip_annotations
    PUBLIC
        pikasnip_platform
        pikasnip_capture
    PRIVATE
        d2d1
        dwrite
        windowscodecs
)

set_target_properties(pikasnip_annotations PROPERTIES
    FOLDER "src/annotations"
    OUTPUT_NAME "pikasnip_annotations")
