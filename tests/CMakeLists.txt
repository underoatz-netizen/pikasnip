add_executable(pikasnip_tests
    milestone1_tests.cpp
    milestone3_tests.cpp
)

target_link_libraries(pikasnip_tests
    PRIVATE
        pikasnip_platform
        pikasnip_capture
        pikasnip_overlay
        pikasnip_annotations
        pikasnip_pin
        pikasnip_ocr
)

add_custom_command(TARGET pikasnip_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pikasnip_tests>/models/openthai-ocr"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/msvcp140.dll"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/msvcp140_1.dll"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/vcruntime140.dll"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/bin/vcruntime140_1.dll"
        "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt"
        "$<TARGET_FILE_DIR:pikasnip_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/det.onnx"
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/rec.onnx"
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/rec2.onnx"
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/rec_dict.txt"
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/NOTICE"
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/config.json"
        "${CMAKE_SOURCE_DIR}/assets/models/openthai-ocr/SHA256SUMS"
        "$<TARGET_FILE_DIR:pikasnip_tests>/models/openthai-ocr"
    COMMENT "Copying ONNX Runtime, notices, and OpenThai 2.1 OCR models beside pikasnip_tests.exe"
)

add_test(NAME milestone1_tests COMMAND pikasnip_tests)
