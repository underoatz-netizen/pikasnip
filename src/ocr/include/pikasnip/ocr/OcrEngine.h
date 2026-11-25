#pragma once

#include "pikasnip/capture/CapturedFrame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pikasnip::ocr {

struct OcrResult {
    bool succeeded = false;
    std::wstring text;
    std::wstring errorMessage;
};

/// Offline OCR: OpenThai 2.1 (ONNX Runtime, lazy loaded) with a
/// Windows.Media.Ocr fallback when the Thai models are not installed.
class OcrEngine {
public:
    [[nodiscard]] bool IsSupported() const noexcept;
    [[nodiscard]] OcrResult RecognizeFrame(const pikasnip::capture::CapturedFrame& frame) const;
};

/// Orders OCR recognizer language tags by preference: Thai first, then en-US,
/// then other English variants, then everything else. Used by the WinRT
/// fallback so Thai text wins when a Thai recognizer is available.
[[nodiscard]] std::vector<size_t> RankFallbackOcrLanguageTags(
    const std::vector<std::wstring>& languageTags);

} // namespace pikasnip::ocr
