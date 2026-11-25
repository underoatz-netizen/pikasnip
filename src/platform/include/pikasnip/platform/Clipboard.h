#pragma once

#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace pikasnip::platform {

/// Decoded clipboard image: straight (non-premultiplied) top-down BGRA8.
struct ClipboardImage {
    int32_t width = 0;
    int32_t height = 0;
    int32_t strideBytes = 0;
    std::vector<uint8_t> bgraPixels;

    [[nodiscard]] bool isValid() const noexcept;
};

/// Builds a canonical top-down CF_DIBV5 payload (BITMAPV5HEADER + straight BGRA8 pixels).
[[nodiscard]] std::vector<uint8_t> BuildDibV5PayloadFromBgra(
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes);

/// Encodes straight BGRA8 pixels to PNG bytes using WIC.
[[nodiscard]] std::vector<uint8_t> EncodePngBytesFromBgra(
    IWICImagingFactory* wic,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes);

/// Publishes both registered "PNG" and CF_DIBV5 formats in one atomic clipboard transaction.
[[nodiscard]] bool CopyImageToClipboard(
    HWND owner,
    IWICImagingFactory* wic,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes);

/// Publishes UTF-16 text as CF_UNICODETEXT to the clipboard.
[[nodiscard]] bool CopyUnicodeTextToClipboard(
    HWND owner,
    std::wstring_view text);

/// Encodes straight BGRA8 pixels to PNG and writes atomically to filePath.
[[nodiscard]] bool SavePngToFile(
    IWICImagingFactory* wic,
    const std::filesystem::path& filePath,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes);

/// Saves straight BGRA8 pixels as PikaSnip_YYYYMMDD_HHMMSS.png in GetDefaultSaveDirectory().
/// Appends _1, _2, ... if a same-second file already exists; returns saved path or empty on error.
[[nodiscard]] std::filesystem::path SaveTimestampedPngToDefaultDir(
    IWICImagingFactory* wic,
    const uint8_t* bgra,
    int32_t width,
    int32_t height,
    int32_t strideBytes);

/// Reads the clipboard image (registered "PNG" first, then CF_DIBV5 / CF_DIB)
/// and decodes it into straight top-down BGRA8. Used by the Alt+P paste flow.
[[nodiscard]] bool ReadImageFromClipboard(
    HWND owner,
    IWICImagingFactory* wic,
    ClipboardImage& outImage);

/// Decodes PNG bytes into straight top-down BGRA8 (WIC).
[[nodiscard]] bool DecodePngBytesToBgra(
    IWICImagingFactory* wic,
    const uint8_t* pngData,
    size_t pngSize,
    ClipboardImage& outImage);

/// Decodes a packed DIB payload (BITMAPINFOHEADER/BITMAPV5HEADER + pixels)
/// into straight top-down BGRA8, including 16/24/32-bit and bitfield DIBs.
[[nodiscard]] bool DecodeDibPayloadToBgra(
    const uint8_t* dibData,
    size_t dibSize,
    ClipboardImage& outImage);

} // namespace pikasnip::platform
