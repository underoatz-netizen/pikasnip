#include "pikasnip/ocr/OcrEngine.h"

#include "OpenThaiOcr.h"

#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pikasnip::ocr {

namespace {

struct ScopedMtaApartment {
    ScopedMtaApartment() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    ~ScopedMtaApartment() {
        winrt::uninit_apartment();
    }
    ScopedMtaApartment(const ScopedMtaApartment&) = delete;
    ScopedMtaApartment& operator=(const ScopedMtaApartment&) = delete;
};

template <typename Fn>
void RunOnWinRtMtaThread(Fn&& fn) noexcept {
    try {
        std::thread worker([fn = std::forward<Fn>(fn)]() mutable noexcept {
            try {
                const ScopedMtaApartment mta;
                fn();
            } catch (...) {
            }
        });
        worker.join();
    } catch (...) {
    }
}

[[nodiscard]] wchar_t ToLowerAscii(wchar_t ch) noexcept {
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return ch;
}

[[nodiscard]] bool EqualsInsensitive(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (ToLowerAscii(a[i]) != ToLowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool StartsWithInsensitive(std::wstring_view str, std::wstring_view prefix) noexcept {
    if (str.size() < prefix.size()) {
        return false;
    }
    return EqualsInsensitive(str.substr(0, prefix.size()), prefix);
}

[[nodiscard]] winrt::Windows::Media::Ocr::OcrEngine CreateBestWinRtOcrEngine() {
    using WinRtOcrEngine = winrt::Windows::Media::Ocr::OcrEngine;
    using winrt::Windows::Globalization::Language;

    if (auto userEngine = WinRtOcrEngine::TryCreateFromUserProfileLanguages()) {
        return userEngine;
    }

    const auto languages = WinRtOcrEngine::AvailableRecognizerLanguages();
    if (languages.Size() == 0) {
        return nullptr;
    }

    std::vector<std::wstring> tags;
    std::vector<Language> langObjects;
    tags.reserve(languages.Size());
    langObjects.reserve(languages.Size());

    for (const auto& lang : languages) {
        const winrt::hstring tagStr = lang.LanguageTag();
        tags.emplace_back(tagStr.c_str(), tagStr.size());
        langObjects.push_back(lang);
    }

    const std::vector<size_t> ranked = RankFallbackOcrLanguageTags(tags);
    for (const size_t idx : ranked) {
        if (auto engine = WinRtOcrEngine::TryCreateFromLanguage(langObjects[idx])) {
            return engine;
        }
    }

    return nullptr;
}

[[nodiscard]] bool IsWinRtOcrSupported() noexcept {
    bool supported = false;
    RunOnWinRtMtaThread([&supported]() {
        supported = (CreateBestWinRtOcrEngine() != nullptr);
    });
    return supported;
}

struct PreparedBgraImage {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> pixels;
};

[[nodiscard]] PreparedBgraImage PrepareFrameForOcr(
    const pikasnip::capture::CapturedFrame& frame,
    uint32_t maxDimension) {
    PreparedBgraImage out;
    const uint32_t effectiveMaxDim = (maxDimension > 0U) ? maxDimension : 2600U;

    int32_t scaledW = frame.width;
    int32_t scaledH = frame.height;
    std::vector<uint8_t> working;

    if (static_cast<uint32_t>(frame.width) > effectiveMaxDim ||
        static_cast<uint32_t>(frame.height) > effectiveMaxDim) {
        const double ratio = std::min(
            static_cast<double>(effectiveMaxDim) / static_cast<double>(frame.width),
            static_cast<double>(effectiveMaxDim) / static_cast<double>(frame.height));
        scaledW = std::max(1, static_cast<int32_t>(std::floor(static_cast<double>(frame.width) * ratio)));
        scaledH = std::max(1, static_cast<int32_t>(std::floor(static_cast<double>(frame.height) * ratio)));

        working.resize(static_cast<size_t>(scaledW) * static_cast<size_t>(scaledH) * 4U);
        const double scaleX = static_cast<double>(frame.width) / static_cast<double>(scaledW);
        const double scaleY = static_cast<double>(frame.height) / static_cast<double>(scaledH);

        for (int32_t dy = 0; dy < scaledH; ++dy) {
            const double sy = std::clamp((static_cast<double>(dy) + 0.5) * scaleY - 0.5, 0.0, static_cast<double>(frame.height - 1));
            const int32_t y0 = static_cast<int32_t>(std::floor(sy));
            const int32_t y1 = std::min(y0 + 1, frame.height - 1);
            const double fy = sy - static_cast<double>(y0);

            const uint8_t* row0 = frame.bgraPixels.data() + static_cast<size_t>(y0) * static_cast<size_t>(frame.strideBytes);
            const uint8_t* row1 = frame.bgraPixels.data() + static_cast<size_t>(y1) * static_cast<size_t>(frame.strideBytes);
            uint8_t* dstRow = working.data() + static_cast<size_t>(dy) * static_cast<size_t>(scaledW) * 4U;

            for (int32_t dx = 0; dx < scaledW; ++dx) {
                const double sx = std::clamp((static_cast<double>(dx) + 0.5) * scaleX - 0.5, 0.0, static_cast<double>(frame.width - 1));
                const int32_t x0 = static_cast<int32_t>(std::floor(sx));
                const int32_t x1 = std::min(x0 + 1, frame.width - 1);
                const double fx = sx - static_cast<double>(x0);

                const double w00 = (1.0 - fx) * (1.0 - fy);
                const double w10 = fx * (1.0 - fy);
                const double w01 = (1.0 - fx) * fy;
                const double w11 = fx * fy;

                const size_t i00 = static_cast<size_t>(x0) * 4U;
                const size_t i10 = static_cast<size_t>(x1) * 4U;
                const size_t d = static_cast<size_t>(dx) * 4U;

                for (size_t c = 0; c < 4; ++c) {
                    const double val =
                        w00 * row0[i00 + c] +
                        w10 * row0[i10 + c] +
                        w01 * row1[i00 + c] +
                        w11 * row1[i10 + c];
                    dstRow[d + c] = static_cast<uint8_t>(std::clamp(std::lround(val), 0L, 255L));
                }
            }
        }
    } else {
        const size_t packedRowBytes = static_cast<size_t>(scaledW) * 4U;
        working.resize(packedRowBytes * static_cast<size_t>(scaledH));
        for (int32_t y = 0; y < scaledH; ++y) {
            std::memcpy(
                working.data() + static_cast<size_t>(y) * packedRowBytes,
                frame.bgraPixels.data() + static_cast<size_t>(y) * static_cast<size_t>(frame.strideBytes),
                packedRowBytes);
        }
    }

    // Premultiply RGB by alpha for BitmapAlphaMode::Premultiplied.
    const size_t pixelCount = static_cast<size_t>(scaledW) * static_cast<size_t>(scaledH);
    for (size_t i = 0; i < pixelCount; ++i) {
        uint8_t* px = working.data() + i * 4U;
        const uint8_t a = px[3];
        if (a == 0U) {
            px[0] = 0U;
            px[1] = 0U;
            px[2] = 0U;
        } else if (a < 255U) {
            px[0] = static_cast<uint8_t>((static_cast<uint32_t>(px[0]) * a + 127U) / 255U);
            px[1] = static_cast<uint8_t>((static_cast<uint32_t>(px[1]) * a + 127U) / 255U);
            px[2] = static_cast<uint8_t>((static_cast<uint32_t>(px[2]) * a + 127U) / 255U);
        }
    }

    if (scaledW < 40 || scaledH < 40) {
        const int32_t minTarget = static_cast<int32_t>(std::min<uint32_t>(64U, effectiveMaxDim));
        const int32_t paddedW = std::max(scaledW, minTarget);
        const int32_t paddedH = std::max(scaledH, minTarget);
        std::vector<uint8_t> padded(static_cast<size_t>(paddedW) * static_cast<size_t>(paddedH) * 4U, 0xFFU);

        const int32_t offX = (paddedW - scaledW) / 2;
        const int32_t offY = (paddedH - scaledH) / 2;
        const size_t srcRowBytes = static_cast<size_t>(scaledW) * 4U;
        const size_t dstRowBytes = static_cast<size_t>(paddedW) * 4U;

        for (int32_t y = 0; y < scaledH; ++y) {
            std::memcpy(
                padded.data() + static_cast<size_t>(offY + y) * dstRowBytes + static_cast<size_t>(offX) * 4U,
                working.data() + static_cast<size_t>(y) * srcRowBytes,
                srcRowBytes);
        }

        out.width = paddedW;
        out.height = paddedH;
        out.pixels = std::move(padded);
        return out;
    }

    out.width = scaledW;
    out.height = scaledH;
    out.pixels = std::move(working);
    return out;
}

} // namespace

std::vector<size_t> RankFallbackOcrLanguageTags(const std::vector<std::wstring>& languageTags) {
    std::array<std::vector<size_t>, 4> tiers;
    for (size_t i = 0; i < languageTags.size(); ++i) {
        const std::wstring_view tag = languageTags[i];
        if (StartsWithInsensitive(tag, L"th")) {
            tiers[0].push_back(i);
        } else if (EqualsInsensitive(tag, L"en-US")) {
            tiers[1].push_back(i);
        } else if (StartsWithInsensitive(tag, L"en")) {
            tiers[2].push_back(i);
        } else {
            tiers[3].push_back(i);
        }
    }

    std::vector<size_t> ranked;
    ranked.reserve(languageTags.size());
    for (const auto& tier : tiers) {
        ranked.insert(ranked.end(), tier.begin(), tier.end());
    }
    return ranked;
}

bool OcrEngine::IsSupported() const noexcept {
    return OpenThaiOcrEngine::AreAssetsPresent() || IsWinRtOcrSupported();
}

OcrResult OcrEngine::RecognizeFrame(const pikasnip::capture::CapturedFrame& frame) const {
    OcrResult result;
    if (!frame.isValid()) {
        result.errorMessage = L"Invalid capture frame";
        return result;
    }

    if (OpenThaiOcrEngine::AreAssetsPresent()) {
        try {
            std::thread worker([&frame, &result]() {
                result = OpenThaiOcrEngine::Instance().Recognize(frame);
            });
            worker.join();
        } catch (...) {
            result.succeeded = false;
            result.text.clear();
            result.errorMessage = L"OpenThai OCR failed";
        }
        if (result.succeeded || !IsWinRtOcrSupported()) {
            return result;
        }
        result = OcrResult{};
    }

    RunOnWinRtMtaThread([&frame, &result]() {
        try {
            using WinRtOcrEngine = winrt::Windows::Media::Ocr::OcrEngine;
            using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
            using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
            using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
            using winrt::Windows::Storage::Streams::DataWriter;

            const auto engine = CreateBestWinRtOcrEngine();
            if (!engine) {
                result.succeeded = false;
                result.errorMessage = L"No Windows OCR language pack installed";
                return;
            }

            const uint32_t maxDim = WinRtOcrEngine::MaxImageDimension();
            const PreparedBgraImage prepared = PrepareFrameForOcr(frame, maxDim);
            if (prepared.width <= 0 || prepared.height <= 0 || prepared.pixels.empty()) {
                result.succeeded = false;
                result.errorMessage = L"Failed to prepare image for OCR";
                return;
            }

            DataWriter writer;
            writer.WriteBytes(winrt::array_view<const uint8_t>(
                prepared.pixels.data(),
                prepared.pixels.data() + prepared.pixels.size()));
            const auto buffer = writer.DetachBuffer();

            const SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
                buffer,
                BitmapPixelFormat::Bgra8,
                prepared.width,
                prepared.height,
                BitmapAlphaMode::Premultiplied);

            const auto ocrResult = engine.RecognizeAsync(bitmap).get();
            std::wstring text;
            for (const auto& line : ocrResult.Lines()) {
                const winrt::hstring lineText = line.Text();
                if (!lineText.empty()) {
                    if (!text.empty()) {
                        text += L"\r\n";
                    }
                    text.append(lineText.c_str(), lineText.size());
                }
            }

            result.succeeded = true;
            result.text = std::move(text);
            result.errorMessage.clear();
        } catch (const winrt::hresult_error& err) {
            result.succeeded = false;
            result.text.clear();
            result.errorMessage = std::wstring(err.message().c_str());
            if (result.errorMessage.empty()) {
                result.errorMessage = L"Windows OCR failed";
            }
        } catch (...) {
            result.succeeded = false;
            result.text.clear();
            result.errorMessage = L"Windows OCR failed";
        }
    });

    if (!result.succeeded && result.errorMessage.empty()) {
        result.errorMessage = L"Windows OCR failed";
    }
    return result;
}

} // namespace pikasnip::ocr
