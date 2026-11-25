add_library(pikasnip_ocr STATIC
    include/pikasnip/ocr/OcrEngine.h
    include/pikasnip/ocr/ThaiNorm.h
    src/OcrEngine.cpp
    src/OpenThaiOcr.cpp
    src/OpenThaiOcr.h
    src/ThaiNorm.cpp
)

target_include_directories(pikasnip_ocr
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include"
)

# OpenThaiOcr.h exposes capture::CapturedFrame and the OCR contract.
target_link_libraries(pikasnip_ocr
    PUBLIC
        pikasnip_platform
        pikasnip_capture
    PRIVATE
        d2d1
        dwrite
        windowscodecs
        windowsapp
)

set_target_properties(pikasnip_ocr PROPERTIES
    FOLDER "src/ocr"
    OUTPUT_NAME "pikasnip_ocr")
