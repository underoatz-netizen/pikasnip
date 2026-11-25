cmake_minimum_required(VERSION 3.24)

option(PIKASNIP_STATIC_CRT "Link MSVC static runtime library (/MT, /MTd)" ON)
option(PIKASNIP_BUILD_TESTS "Build PikaSnip unit tests" ON)

if(PIKASNIP_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(PikaSnip
    VERSION 0.1.0
    DESCRIPTION "PikaSnip - Zero-bloat native Windows screenshot utility"
    LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "PikaSnip requires a 64-bit (x64) build. "
        "Configure with an x64 preset (e.g. cmake --preset release).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        /EHsc
        /await:strict
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/Gw>
    )
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

foreach(kind RUNTIME LIBRARY ARCHIVE)
    set(CMAKE_${kind}_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo")
    set(CMAKE_${kind}_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel")
endforeach()

if(PIKASNIP_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/platform)
add_subdirectory(src/capture)
add_subdirectory(src/annotations)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/overlay/CMakeLists.txt")
    add_subdirectory(src/overlay)
endif()
add_subdirectory(src/pin)
add_subdirectory(src/ocr)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/CMakeLists.txt")
    add_subdirectory(src/app)
endif()

if(PIKASNIP_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
