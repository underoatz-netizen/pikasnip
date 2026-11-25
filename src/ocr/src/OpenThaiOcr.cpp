#include "OpenThaiOcr.h"

#include "onnxruntime_c_api.h"
#include "pikasnip/ocr/ThaiNorm.h"
#include "pikasnip/platform/AppEnvironment.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace pikasnip::ocr {

namespace {

struct TextBox {
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
};

[[nodiscard]] std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    const int written = ::MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        wide.data(),
        needed);
    if (written <= 0) {
        return {};
    }
    wide.resize(static_cast<size_t>(written));
    return wide;
}

[[nodiscard]] bool CheckStatus(const OrtApi* api, OrtStatus* status, std::wstring& outError) {
    if (status == nullptr) {
        return true;
    }
    const char* msg = api->GetErrorMessage(status);
    outError = (msg != nullptr) ? Utf8ToWide(msg) : L"ONNX Runtime error";
    api->ReleaseStatus(status);
    return false;
}

[[nodiscard]] bool LoadDictionary(
    const std::filesystem::path& dictPath,
    std::vector<std::wstring>& outCharset) {
    std::ifstream ifs(dictPath, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }

    outCharset.clear();
    outCharset.emplace_back(); // Index 0 = CTC blank.

    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        outCharset.push_back(Utf8ToWide(line));
    }

    outCharset.emplace_back(L" "); // Trailing space appended by PaddleOCR convention.
    return outCharset.size() > 2U;
}

[[nodiscard]] int CountMixedLatinInsideThai(std::wstring_view text) noexcept {
    int count = 0;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == L' ' || text[i] == L'\t' || text[i] == L'\r' || text[i] == L'\n')) {
            ++i;
        }
        const size_t start = i;
        bool hasThai = false;
        int latinCount = 0;
        while (i < text.size() && text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n') {
            const wchar_t ch = text[i];
            if (ch >= 0x0E01 && ch <= 0x0E4E) {
                hasThai = true;
            } else if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) {
                ++latinCount;
            }
            ++i;
        }
        if (i > start && hasThai) {
            count += latinCount;
        }
    }
    return count;
}

[[nodiscard]] uint8_t SamplePaddedBgr(
    const pikasnip::capture::CapturedFrame& frame,
    int32_t pad,
    int32_t x,
    int32_t y,
    int channel) noexcept {
    const int32_t fx = x - pad;
    const int32_t fy = y - pad;
    if (fx < 0 || fx >= frame.width || fy < 0 || fy >= frame.height) {
        return 255U;
    }
    const size_t offset = static_cast<size_t>(fy) * static_cast<size_t>(frame.strideBytes) +
                          static_cast<size_t>(fx) * 4U + static_cast<size_t>(channel);
    return frame.bgraPixels[offset];
}

[[nodiscard]] std::vector<float> BuildDetectionInput(
    const pikasnip::capture::CapturedFrame& frame,
    int32_t pad,
    int32_t paddedW,
    int32_t paddedH,
    int32_t rw,
    int32_t rh) {
    constexpr std::array<float, 3> kMean{0.485f, 0.456f, 0.406f};
    constexpr std::array<float, 3> kStd{0.229f, 0.224f, 0.225f};

    const size_t planeSize = static_cast<size_t>(rw) * static_cast<size_t>(rh);
    std::vector<float> tensor(3U * planeSize, 0.0f);

    const double scaleX = static_cast<double>(paddedW) / static_cast<double>(rw);
    const double scaleY = static_cast<double>(paddedH) / static_cast<double>(rh);

    for (int32_t dy = 0; dy < rh; ++dy) {
        const double sy = std::clamp(
            (static_cast<double>(dy) + 0.5) * scaleY - 0.5,
            0.0,
            static_cast<double>(paddedH - 1));
        const int32_t y0 = static_cast<int32_t>(std::floor(sy));
        const int32_t y1 = std::min(y0 + 1, paddedH - 1);
        const float fy = static_cast<float>(sy - static_cast<double>(y0));

        for (int32_t dx = 0; dx < rw; ++dx) {
            const double sx = std::clamp(
                (static_cast<double>(dx) + 0.5) * scaleX - 0.5,
                0.0,
                static_cast<double>(paddedW - 1));
            const int32_t x0 = static_cast<int32_t>(std::floor(sx));
            const int32_t x1 = std::min(x0 + 1, paddedW - 1);
            const float fx = static_cast<float>(sx - static_cast<double>(x0));

            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;

            const size_t dstPixel = static_cast<size_t>(dy) * static_cast<size_t>(rw) + static_cast<size_t>(dx);
            for (int c = 0; c < 3; ++c) {
                const float p00 = static_cast<float>(SamplePaddedBgr(frame, pad, x0, y0, c));
                const float p10 = static_cast<float>(SamplePaddedBgr(frame, pad, x1, y0, c));
                const float p01 = static_cast<float>(SamplePaddedBgr(frame, pad, x0, y1, c));
                const float p11 = static_cast<float>(SamplePaddedBgr(frame, pad, x1, y1, c));
                const float bgr = (w00 * p00 + w10 * p10 + w01 * p01 + w11 * p11) / 255.0f;
                tensor[static_cast<size_t>(c) * planeSize + dstPixel] = (bgr - kMean[c]) / kStd[c];
            }
        }
    }

    return tensor;
}

[[nodiscard]] std::vector<TextBox> ExtractDetectedBoxes(
    const float* probMap,
    int32_t ow,
    int32_t oh,
    int32_t pad,
    int32_t paddedW,
    int32_t paddedH,
    int32_t origW,
    int32_t origH) {
    constexpr float kThresh = 0.20f;
    constexpr float kBoxThresh = 0.40f;
    constexpr float kUnclipRatio = 1.4f;

    const size_t total = static_cast<size_t>(ow) * static_cast<size_t>(oh);
    std::vector<uint8_t> visited(total, 0U);
    std::queue<int32_t> bfs;
    std::vector<TextBox> boxes;

    const float sx = static_cast<float>(paddedW) / static_cast<float>(ow);
    const float sy = static_cast<float>(paddedH) / static_cast<float>(oh);

    for (int32_t y = 0; y < oh; ++y) {
        for (int32_t x = 0; x < ow; ++x) {
            const size_t startIdx = static_cast<size_t>(y) * static_cast<size_t>(ow) + static_cast<size_t>(x);
            if (visited[startIdx] != 0U || probMap[startIdx] <= kThresh) {
                continue;
            }

            visited[startIdx] = 1U;
            bfs.push(y * ow + x);

            int32_t minX = x;
            int32_t maxX = x;
            int32_t minY = y;
            int32_t maxY = y;
            int32_t pixelCount = 0;
            double probSum = 0.0;

            while (!bfs.empty()) {
                const int32_t cur = bfs.front();
                bfs.pop();

                const int32_t cx = cur % ow;
                const int32_t cy = cur / ow;
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                ++pixelCount;
                probSum += static_cast<double>(probMap[static_cast<size_t>(cur)]);

                for (int32_t dy = -1; dy <= 1; ++dy) {
                    const int32_t ny = cy + dy;
                    if (ny < 0 || ny >= oh) {
                        continue;
                    }
                    for (int32_t dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int32_t nx = cx + dx;
                        if (nx < 0 || nx >= ow) {
                            continue;
                        }
                        const size_t nIdx = static_cast<size_t>(ny) * static_cast<size_t>(ow) + static_cast<size_t>(nx);
                        if (visited[nIdx] == 0U && probMap[nIdx] > kThresh) {
                            visited[nIdx] = 1U;
                            bfs.push(ny * ow + nx);
                        }
                    }
                }
            }

            const int32_t boxW = maxX - minX + 1;
            const int32_t boxH = maxY - minY + 1;
            if (std::min(boxW, boxH) < 3) {
                continue;
            }
            const double meanProb = probSum / static_cast<double>(pixelCount);
            if (meanProb < static_cast<double>(kBoxThresh)) {
                continue;
            }

            const float area = static_cast<float>(boxW) * static_cast<float>(boxH);
            const float perimeter = 2.0f * (static_cast<float>(boxW) + static_cast<float>(boxH));
            const float dist = (perimeter > 0.0f) ? (area * kUnclipRatio / perimeter) : 0.0f;
            const float vPad = std::max(dist, 0.22f * static_cast<float>(boxH));

            const float ux0 = static_cast<float>(minX) - dist;
            const float ux1 = static_cast<float>(maxX + 1) + dist;
            const float uy0 = static_cast<float>(minY) - vPad;
            const float uy1 = static_cast<float>(maxY + 1) + vPad;

            TextBox mapped;
            mapped.x0 = std::clamp(static_cast<int32_t>(std::floor(ux0 * sx)) - pad, 0, origW);
            mapped.x1 = std::clamp(static_cast<int32_t>(std::ceil(ux1 * sx)) - pad, 0, origW);
            mapped.y0 = std::clamp(static_cast<int32_t>(std::floor(uy0 * sy)) - pad, 0, origH);
            mapped.y1 = std::clamp(static_cast<int32_t>(std::ceil(uy1 * sy)) - pad, 0, origH);

            if ((mapped.x1 - mapped.x0) >= 2 && (mapped.y1 - mapped.y0) >= 2) {
                boxes.push_back(mapped);
            }
        }
    }

    if (boxes.empty() && origH <= 200 &&
        static_cast<double>(origW) >= 1.2 * static_cast<double>(origH)) {
        boxes.push_back(TextBox{0, 0, origW, origH});
    }

    return boxes;
}

[[nodiscard]] std::vector<std::vector<TextBox>> GroupIntoRows(std::vector<TextBox> boxes) {
    if (boxes.empty()) {
        return {};
    }

    std::sort(boxes.begin(), boxes.end(), [](const TextBox& a, const TextBox& b) {
        const float cyA = 0.5f * static_cast<float>(a.y0 + a.y1);
        const float cyB = 0.5f * static_cast<float>(b.y0 + b.y1);
        if (cyA != cyB) {
            return cyA < cyB;
        }
        return a.x0 < b.x0;
    });

    struct RowGroup {
        float avgY0 = 0.0f;
        float avgY1 = 0.0f;
        std::vector<TextBox> items;
    };

    std::vector<RowGroup> groups;
    for (const TextBox& box : boxes) {
        const float boxH = static_cast<float>(std::max(1, box.y1 - box.y0));
        size_t bestRow = groups.size();
        float bestOverlap = 0.0f;

        for (size_t i = 0; i < groups.size(); ++i) {
            const float rowH = std::max(1.0f, groups[i].avgY1 - groups[i].avgY0);
            const float overlapY = std::min(static_cast<float>(box.y1), groups[i].avgY1) -
                                   std::max(static_cast<float>(box.y0), groups[i].avgY0);
            if (overlapY >= 0.5f * std::min(boxH, rowH) && overlapY > bestOverlap) {
                bestOverlap = overlapY;
                bestRow = i;
            }
        }

        if (bestRow == groups.size()) {
            groups.push_back(RowGroup{
                static_cast<float>(box.y0),
                static_cast<float>(box.y1),
                {box}
            });
        } else {
            RowGroup& g = groups[bestRow];
            const float prevCount = static_cast<float>(g.items.size());
            g.items.push_back(box);
            const float newCount = static_cast<float>(g.items.size());
            g.avgY0 = (g.avgY0 * prevCount + static_cast<float>(box.y0)) / newCount;
            g.avgY1 = (g.avgY1 * prevCount + static_cast<float>(box.y1)) / newCount;
        }
    }

    std::sort(groups.begin(), groups.end(), [](const RowGroup& a, const RowGroup& b) {
        return (a.avgY0 + a.avgY1) < (b.avgY0 + b.avgY1);
    });

    std::vector<std::vector<TextBox>> rows;
    rows.reserve(groups.size());
    for (auto& g : groups) {
        std::sort(g.items.begin(), g.items.end(), [](const TextBox& a, const TextBox& b) {
            return a.x0 < b.x0;
        });
        rows.push_back(std::move(g.items));
    }
    return rows;
}

[[nodiscard]] std::vector<float> BuildRecognitionInput(
    const pikasnip::capture::CapturedFrame& frame,
    const TextBox& box,
    int32_t& outPaddedW) {
    constexpr int32_t kRecH = 48;
    constexpr int32_t kMaxW = 3200;

    const int32_t boxW = std::max(1, box.x1 - box.x0);
    const int32_t boxH = std::max(1, box.y1 - box.y0);
    const int32_t rw = std::clamp(
        static_cast<int32_t>(std::ceil(static_cast<double>(kRecH) * static_cast<double>(boxW) / static_cast<double>(boxH))),
        1,
        kMaxW);
    outPaddedW = std::max<int32_t>(16, rw);

    // 1. Resize crop to (rw x 48) and convert BGRA -> grayscale luminance.
    std::vector<uint8_t> gray(static_cast<size_t>(rw) * static_cast<size_t>(kRecH), 0U);
    const double scaleX = static_cast<double>(boxW) / static_cast<double>(rw);
    const double scaleY = static_cast<double>(boxH) / static_cast<double>(kRecH);

    for (int32_t dy = 0; dy < kRecH; ++dy) {
        const double sy = std::clamp(
            (static_cast<double>(dy) + 0.5) * scaleY - 0.5,
            0.0,
            static_cast<double>(boxH - 1));
        const int32_t y0 = box.y0 + static_cast<int32_t>(std::floor(sy));
        const int32_t y1 = std::min(y0 + 1, box.y1 - 1);
        const float fy = static_cast<float>(sy - std::floor(sy));

        const uint8_t* row0 = frame.bgraPixels.data() + static_cast<size_t>(y0) * static_cast<size_t>(frame.strideBytes);
        const uint8_t* row1 = frame.bgraPixels.data() + static_cast<size_t>(y1) * static_cast<size_t>(frame.strideBytes);

        for (int32_t dx = 0; dx < rw; ++dx) {
            const double sx = std::clamp(
                (static_cast<double>(dx) + 0.5) * scaleX - 0.5,
                0.0,
                static_cast<double>(boxW - 1));
            const int32_t x0 = box.x0 + static_cast<int32_t>(std::floor(sx));
            const int32_t x1 = std::min(x0 + 1, box.x1 - 1);
            const float fx = static_cast<float>(sx - std::floor(sx));

            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;

            const size_t i00 = static_cast<size_t>(x0) * 4U;
            const size_t i10 = static_cast<size_t>(x1) * 4U;

            const float b = w00 * row0[i00 + 0] + w10 * row0[i10 + 0] + w01 * row1[i00 + 0] + w11 * row1[i10 + 0];
            const float g = w00 * row0[i00 + 1] + w10 * row0[i10 + 1] + w01 * row1[i00 + 1] + w11 * row1[i10 + 1];
            const float r = w00 * row0[i00 + 2] + w10 * row0[i10 + 2] + w01 * row1[i00 + 2] + w11 * row1[i10 + 2];
            const float lum = 0.114f * b + 0.587f * g + 0.299f * r;
            gray[static_cast<size_t>(dy) * static_cast<size_t>(rw) + static_cast<size_t>(dx)] =
                static_cast<uint8_t>(std::clamp(std::lround(lum), 0L, 255L));
        }
    }

    // 2. OpenThai _flatten: per-column top/bottom margin median -> 1D horizontal median filter.
    const int32_t k = std::max<int32_t>(2, kRecH / 6);
    std::vector<uint8_t> bgRaw(static_cast<size_t>(rw), 255U);
    std::vector<uint8_t> marginSamples(static_cast<size_t>(2 * k));
    for (int32_t x = 0; x < rw; ++x) {
        size_t mIdx = 0;
        for (int32_t y = 0; y < k; ++y) {
            marginSamples[mIdx++] = gray[static_cast<size_t>(y) * static_cast<size_t>(rw) + static_cast<size_t>(x)];
        }
        for (int32_t y = kRecH - k; y < kRecH; ++y) {
            marginSamples[mIdx++] = gray[static_cast<size_t>(y) * static_cast<size_t>(rw) + static_cast<size_t>(x)];
        }
        std::sort(marginSamples.begin(), marginSamples.end());
        bgRaw[static_cast<size_t>(x)] = marginSamples[marginSamples.size() / 2U];
    }

    const int32_t win = std::max<int32_t>(3, std::min<int32_t>(kRecH | 1, 255));
    const int32_t halfWin = win / 2;
    std::vector<float> bgSmooth(static_cast<size_t>(rw), 255.0f);
    std::vector<uint8_t> winBuf(static_cast<size_t>(win));
    for (int32_t x = 0; x < rw; ++x) {
        for (int32_t j = 0; j < win; ++j) {
            const int32_t sx = std::clamp(x + j - halfWin, 0, rw - 1);
            winBuf[static_cast<size_t>(j)] = bgRaw[static_cast<size_t>(sx)];
        }
        std::nth_element(winBuf.begin(), winBuf.begin() + halfWin, winBuf.end());
        bgSmooth[static_cast<size_t>(x)] = static_cast<float>(winBuf[static_cast<size_t>(halfWin)]);
    }

    // 3. Contrast difference & 98th percentile normalization.
    std::vector<float> diff(static_cast<size_t>(rw) * static_cast<size_t>(kRecH), 0.0f);
    std::array<uint32_t, 256> hist{};
    for (int32_t y = 0; y < kRecH; ++y) {
        for (int32_t x = 0; x < rw; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(rw) + static_cast<size_t>(x);
            const float d = std::abs(static_cast<float>(gray[idx]) - bgSmooth[static_cast<size_t>(x)]);
            diff[idx] = d;
            const uint8_t bin = static_cast<uint8_t>(std::clamp(static_cast<int32_t>(d), 0, 255));
            ++hist[bin];
        }
    }

    const double targetCount = 0.98 * static_cast<double>(diff.size());
    uint32_t cum = 0;
    int32_t p98 = 255;
    for (int32_t b = 0; b < 256; ++b) {
        cum += hist[static_cast<size_t>(b)];
        if (static_cast<double>(cum) >= targetCount) {
            p98 = b;
            break;
        }
    }
    const float hi = std::max(static_cast<float>(p98), 24.0f);

    // 4. Build NCHW [1, 3, 48, outPaddedW] initialized to 0.0f (mid-grey right padding).
    const size_t planeSize = static_cast<size_t>(kRecH) * static_cast<size_t>(outPaddedW);
    std::vector<float> tensor(3U * planeSize, 0.0f);
    for (int32_t y = 0; y < kRecH; ++y) {
        for (int32_t x = 0; x < rw; ++x) {
            const float d = diff[static_cast<size_t>(y) * static_cast<size_t>(rw) + static_cast<size_t>(x)];
            const float v = 255.0f - std::clamp(d * (255.0f / hi), 0.0f, 255.0f);
            const float norm = (v / 255.0f - 0.5f) / 0.5f;
            const size_t dst = static_cast<size_t>(y) * static_cast<size_t>(outPaddedW) + static_cast<size_t>(x);
            tensor[0U * planeSize + dst] = norm;
            tensor[1U * planeSize + dst] = norm;
            tensor[2U * planeSize + dst] = norm;
        }
    }

    return tensor;
}

struct CtcDecodeResult {
    std::wstring text;
    float confidence = 0.0f;
};

[[nodiscard]] CtcDecodeResult DecodeGreedyCtc(
    const float* probs,
    int64_t timeSteps,
    int64_t numClasses,
    const std::vector<std::wstring>& charset) {
    CtcDecodeResult out;
    if (probs == nullptr || timeSteps <= 0 || numClasses <= 0) {
        return out;
    }

    int64_t prevId = -1;
    double confSum = 0.0;
    size_t charCount = 0;

    for (int64_t t = 0; t < timeSteps; ++t) {
        const float* row = probs + static_cast<size_t>(t) * static_cast<size_t>(numClasses);
        int64_t bestId = 0;
        float bestVal = row[0];
        for (int64_t c = 1; c < numClasses; ++c) {
            if (row[c] > bestVal) {
                bestVal = row[c];
                bestId = c;
            }
        }

        if (bestId != prevId && bestId != 0) {
            if (static_cast<size_t>(bestId) < charset.size()) {
                out.text.append(charset[static_cast<size_t>(bestId)]);
            }
            confSum += static_cast<double>(bestVal);
            ++charCount;
        }
        prevId = bestId;
    }

    out.confidence = (charCount > 0U) ? static_cast<float>(confSum / static_cast<double>(charCount)) : 0.0f;
    return out;
}

/// Validates a CTC recognition output ([batch, timeSteps, numClasses]) against the
/// actual buffer size so DecodeGreedyCtc never reads past the tensor.
[[nodiscard]] bool HasValidCtcOutput(
    const std::vector<int64_t>& shape,
    size_t elemCount) noexcept {
    if (shape.size() < 3U || shape[1] <= 0 || shape[2] <= 0) {
        return false;
    }
    const size_t timeSteps = static_cast<size_t>(shape[1]);
    const size_t numClasses = static_cast<size_t>(shape[2]);
    if (timeSteps > (std::numeric_limits<size_t>::max)() / numClasses) {
        return false;
    }
    return elemCount >= timeSteps * numClasses;
}

} // namespace

bool OpenThaiOcrEngine::AreAssetsPresent() noexcept {
    try {
        const std::filesystem::path exeDir = pikasnip::platform::GetExecutableDirectory();
        if (exeDir.empty()) {
            return false;
        }
        const std::filesystem::path modelDir = exeDir / L"models" / L"openthai-ocr";
        std::error_code ec;
        return std::filesystem::exists(exeDir / L"onnxruntime.dll", ec) &&
               std::filesystem::exists(modelDir / L"det.onnx", ec) &&
               std::filesystem::exists(modelDir / L"rec.onnx", ec) &&
               std::filesystem::exists(modelDir / L"rec_dict.txt", ec);
    } catch (...) {
        return false;
    }
}

OpenThaiOcrEngine& OpenThaiOcrEngine::Instance() {
    static OpenThaiOcrEngine instance;
    return instance;
}

OpenThaiOcrEngine::~OpenThaiOcrEngine() {
    std::lock_guard<std::mutex> lock(mutex_);
    ShutdownLocked();
}

void OpenThaiOcrEngine::ShutdownLocked() noexcept {
    if (api_ != nullptr) {
        if (rec2Model_.session != nullptr) {
            api_->ReleaseSession(rec2Model_.session);
            rec2Model_.session = nullptr;
        }
        if (recModel_.session != nullptr) {
            api_->ReleaseSession(recModel_.session);
            recModel_.session = nullptr;
        }
        if (detModel_.session != nullptr) {
            api_->ReleaseSession(detModel_.session);
            detModel_.session = nullptr;
        }
        if (memoryInfo_ != nullptr) {
            api_->ReleaseMemoryInfo(memoryInfo_);
            memoryInfo_ = nullptr;
        }
        if (env_ != nullptr) {
            api_->ReleaseEnv(env_);
            env_ = nullptr;
        }
        api_ = nullptr;
    }
    if (ortModule_ != nullptr) {
        ::FreeLibrary(ortModule_);
        ortModule_ = nullptr;
    }
    initialized_ = false;
}

bool OpenThaiOcrEngine::LoadModelLocked(
    const std::wstring& modelPath,
    OrtSessionOptions* options,
    LoadedModel& outModel) {
    if (!CheckStatus(api_, api_->CreateSession(env_, modelPath.c_str(), options, &outModel.session), initError_)) {
        return false;
    }

    OrtAllocator* allocator = nullptr;
    if (!CheckStatus(api_, api_->GetAllocatorWithDefaultOptions(&allocator), initError_)) {
        return false;
    }

    char* inName = nullptr;
    if (!CheckStatus(api_, api_->SessionGetInputName(outModel.session, 0, allocator, &inName), initError_)) {
        return false;
    }
    outModel.inputName = (inName != nullptr) ? inName : "";
    if (OrtStatus* freeStatus = api_->AllocatorFree(allocator, inName)) {
        api_->ReleaseStatus(freeStatus);
    }

    char* outName = nullptr;
    if (!CheckStatus(api_, api_->SessionGetOutputName(outModel.session, 0, allocator, &outName), initError_)) {
        return false;
    }
    outModel.outputName = (outName != nullptr) ? outName : "";
    if (OrtStatus* freeStatus = api_->AllocatorFree(allocator, outName)) {
        api_->ReleaseStatus(freeStatus);
    }

    return !outModel.inputName.empty() && !outModel.outputName.empty();
}

bool OpenThaiOcrEngine::EnsureInitializedLocked() {
    if (initAttempted_) {
        return initialized_;
    }
    initAttempted_ = true;

    const std::filesystem::path exePath = pikasnip::platform::GetExecutablePath();
    if (exePath.empty() || !exePath.is_absolute()) {
        initError_ = L"Failed to resolve an absolute executable path for onnxruntime.dll";
        return false;
    }
    const std::filesystem::path exeDir = exePath.parent_path();
    const std::filesystem::path dllPath = exeDir / L"onnxruntime.dll";
    const std::filesystem::path modelDir = exeDir / L"models" / L"openthai-ocr";

    if (!LoadDictionary(modelDir / L"rec_dict.txt", charset_)) {
        initError_ = L"Failed to load OpenThai rec_dict.txt";
        return false;
    }

    // Only ever load onnxruntime.dll from the executable's own directory; never let
    // Windows fall back to the CWD or PATH (DLL preloading / planting defense).
    ortModule_ = ::LoadLibraryExW(
        dllPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (ortModule_ == nullptr) {
        initError_ = L"Failed to load onnxruntime.dll";
        return false;
    }

    using OrtGetApiBaseFn = const OrtApiBase* (ORT_API_CALL*)(void);
    const auto getApiBase = reinterpret_cast<OrtGetApiBaseFn>(
        reinterpret_cast<void*>(::GetProcAddress(ortModule_, "OrtGetApiBase")));
    if (getApiBase == nullptr) {
        initError_ = L"OrtGetApiBase export missing from onnxruntime.dll";
        ShutdownLocked();
        return false;
    }

    const OrtApiBase* apiBase = getApiBase();
    if (apiBase == nullptr) {
        initError_ = L"OrtGetApiBase returned null";
        ShutdownLocked();
        return false;
    }

    api_ = apiBase->GetApi(ORT_API_VERSION);
    if (api_ == nullptr) {
        initError_ = L"Incompatible onnxruntime.dll API version";
        ShutdownLocked();
        return false;
    }

    if (!CheckStatus(api_, api_->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "PikaSnipOCR", &env_), initError_)) {
        ShutdownLocked();
        return false;
    }

    if (!CheckStatus(api_, api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo_), initError_)) {
        ShutdownLocked();
        return false;
    }

    OrtSessionOptions* options = nullptr;
    if (!CheckStatus(api_, api_->CreateSessionOptions(&options), initError_)) {
        ShutdownLocked();
        return false;
    }

    const unsigned int hwThreads = std::max(1U, std::thread::hardware_concurrency());
    const int intraThreads = static_cast<int>(std::min(4U, hwThreads));

    bool ok = CheckStatus(api_, api_->DisableMemPattern(options), initError_) &&
              CheckStatus(api_, api_->DisableCpuMemArena(options), initError_) &&
              CheckStatus(api_, api_->SetIntraOpNumThreads(options, intraThreads), initError_) &&
              CheckStatus(api_, api_->SetInterOpNumThreads(options, 1), initError_) &&
              CheckStatus(api_, api_->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), initError_);
    if (ok) {
        if (OrtStatus* cfgStatus = api_->AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0")) {
            api_->ReleaseStatus(cfgStatus);
        }
        ok = LoadModelLocked((modelDir / L"det.onnx").wstring(), options, detModel_) &&
             LoadModelLocked((modelDir / L"rec.onnx").wstring(), options, recModel_);
        if (ok) {
            std::error_code ec;
            const std::filesystem::path rec2Path = modelDir / L"rec2.onnx";
            if (std::filesystem::exists(rec2Path, ec)) {
                if (!LoadModelLocked(rec2Path.wstring(), options, rec2Model_)) {
                    if (rec2Model_.session != nullptr) {
                        api_->ReleaseSession(rec2Model_.session);
                        rec2Model_.session = nullptr;
                    }
                    initError_.clear();
                }
            }
        }
    }

    api_->ReleaseSessionOptions(options);
    if (!ok) {
        ShutdownLocked();
        return false;
    }

    initialized_ = true;
    return true;
}

bool OpenThaiOcrEngine::RunFloatModelLocked(
    const LoadedModel& model,
    const std::vector<float>& inputData,
    const int64_t* inputShape,
    size_t inputRank,
    std::vector<float>& outData,
    std::vector<int64_t>& outShape) {
    if (model.session == nullptr || inputData.empty()) {
        return false;
    }

    OrtValue* inputTensor = nullptr;
    std::wstring err;
    if (!CheckStatus(
            api_,
            api_->CreateTensorWithDataAsOrtValue(
                memoryInfo_,
                const_cast<float*>(inputData.data()),
                inputData.size() * sizeof(float),
                inputShape,
                inputRank,
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &inputTensor),
            err)) {
        initError_ = std::move(err);
        return false;
    }

    const char* inputNames[] = {model.inputName.c_str()};
    const char* outputNames[] = {model.outputName.c_str()};
    OrtValue* outputTensor = nullptr;

    const bool runOk = CheckStatus(
        api_,
        api_->Run(
            model.session,
            nullptr,
            inputNames,
            &inputTensor,
            1,
            outputNames,
            1,
            &outputTensor),
        err);
    api_->ReleaseValue(inputTensor);
    if (!runOk || outputTensor == nullptr) {
        if (outputTensor != nullptr) {
            api_->ReleaseValue(outputTensor);
            outputTensor = nullptr;
        }
        initError_ = std::move(err);
        return false;
    }

    bool shapeOk = true;
    OrtTensorTypeAndShapeInfo* typeInfo = nullptr;
    ONNXTensorElementDataType elemType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;

    shapeOk = CheckStatus(api_, api_->GetTensorTypeAndShape(outputTensor, &typeInfo), err);
    if (shapeOk && typeInfo == nullptr) {
        shapeOk = false;
        err = L"ONNX Runtime returned a null tensor type/shape info";
    }

    if (shapeOk) {
        shapeOk = CheckStatus(api_, api_->GetTensorElementType(typeInfo, &elemType), err);
        if (shapeOk && elemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            shapeOk = false;
            err = L"Unexpected ONNX output tensor element type (expected float)";
        }
    }

    size_t dimsCount = 0;
    if (shapeOk) {
        shapeOk = CheckStatus(api_, api_->GetDimensionsCount(typeInfo, &dimsCount), err);
    }

    outShape.clear();
    if (shapeOk) {
        if (dimsCount == 0U) {
            shapeOk = false;
            err = L"ONNX Runtime returned a scalar output tensor";
        } else {
            outShape.resize(dimsCount);
            shapeOk = CheckStatus(api_, api_->GetDimensions(typeInfo, outShape.data(), dimsCount), err);
        }
    }
    if (typeInfo != nullptr) {
        api_->ReleaseTensorTypeAndShapeInfo(typeInfo);
    }

    float* rawOut = nullptr;
    if (shapeOk) {
        shapeOk = CheckStatus(api_, api_->GetTensorMutableData(outputTensor, reinterpret_cast<void**>(&rawOut)), err);
        if (shapeOk && rawOut == nullptr) {
            shapeOk = false;
            err = L"ONNX Runtime returned a null output tensor buffer";
        }
    }

    if (shapeOk) {
        size_t elemCount = 1U;
        for (const int64_t d : outShape) {
            if (d <= 0) {
                shapeOk = false;
                err = L"Invalid dimension in ONNX output tensor";
                break;
            }
            if (elemCount > (std::numeric_limits<size_t>::max)() / static_cast<size_t>(d)) {
                shapeOk = false;
                err = L"ONNX output tensor element count overflow";
                break;
            }
            elemCount *= static_cast<size_t>(d);
        }

        if (shapeOk) {
            outData.assign(rawOut, rawOut + elemCount);
        }
    }

    api_->ReleaseValue(outputTensor);
    if (!shapeOk) {
        initError_ = std::move(err);
        return false;
    }
    return true;
}

OcrResult OpenThaiOcrEngine::Recognize(const pikasnip::capture::CapturedFrame& frame) {
    OcrResult result;
    if (!frame.isValid()) {
        result.errorMessage = L"Invalid capture frame";
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!EnsureInitializedLocked()) {
        result.errorMessage = initError_.empty() ? L"Failed to initialize OpenThai OCR" : initError_;
        return result;
    }

    const int32_t origW = frame.width;
    const int32_t origH = frame.height;
    const int32_t pad = std::max<int32_t>(8, static_cast<int32_t>(0.05 * static_cast<double>(std::min(origH, origW))));
    const int32_t paddedW = origW + 2 * pad;
    const int32_t paddedH = origH + 2 * pad;

    constexpr double kLimit = 1280.0;
    const double detScale = std::min(kLimit / static_cast<double>(std::max(paddedW, paddedH)), 4.0);
    const int32_t rw = std::max<int32_t>(32, static_cast<int32_t>(std::llround(static_cast<double>(paddedW) * detScale / 32.0)) * 32);
    const int32_t rh = std::max<int32_t>(32, static_cast<int32_t>(std::llround(static_cast<double>(paddedH) * detScale / 32.0)) * 32);

    const std::vector<float> detInput = BuildDetectionInput(frame, pad, paddedW, paddedH, rw, rh);
    const std::array<int64_t, 4> detShape{1, 3, rh, rw};
    std::vector<float> detOutput;
    std::vector<int64_t> detOutShape;
    if (!RunFloatModelLocked(detModel_, detInput, detShape.data(), detShape.size(), detOutput, detOutShape)) {
        result.errorMessage = initError_.empty() ? L"OpenThai detection failed" : initError_;
        return result;
    }

    if (detOutShape.size() < 4U ||
        detOutShape[0] <= 0 ||
        detOutShape[1] <= 0 ||
        detOutShape[2] <= 0 ||
        detOutShape[3] <= 0) {
        result.errorMessage = L"Unexpected OpenThai detection output shape";
        return result;
    }
    if (detOutShape[2] > (std::numeric_limits<int32_t>::max)() ||
        detOutShape[3] > (std::numeric_limits<int32_t>::max)()) {
        result.errorMessage = L"OpenThai detection output dimensions out of range";
        return result;
    }

    const int32_t oh = static_cast<int32_t>(detOutShape[2]);
    const int32_t ow = static_cast<int32_t>(detOutShape[3]);
    if (ow > (std::numeric_limits<int32_t>::max)() / oh ||
        detOutput.size() < static_cast<size_t>(ow) * static_cast<size_t>(oh)) {
        result.errorMessage = L"OpenThai detection output buffer too small for its shape";
        return result;
    }

    std::vector<TextBox> boxes = ExtractDetectedBoxes(
        detOutput.data(),
        ow,
        oh,
        pad,
        paddedW,
        paddedH,
        origW,
        origH);

    if (boxes.empty()) {
        result.succeeded = true;
        return result;
    }

    const std::vector<std::vector<TextBox>> rows = GroupIntoRows(std::move(boxes));
    std::wstring fullText;

    for (const auto& row : rows) {
        std::wstring rowText;
        for (const TextBox& box : row) {
            int32_t recPaddedW = 16;
            const std::vector<float> recInput = BuildRecognitionInput(frame, box, recPaddedW);
            const std::array<int64_t, 4> recShape{1, 3, 48, recPaddedW};

            std::vector<float> recOutput;
            std::vector<int64_t> recOutShape;
            if (!RunFloatModelLocked(recModel_, recInput, recShape.data(), recShape.size(), recOutput, recOutShape)) {
                continue;
            }
            if (!HasValidCtcOutput(recOutShape, recOutput.size())) {
                continue;
            }

            CtcDecodeResult decoded = DecodeGreedyCtc(
                recOutput.data(),
                recOutShape[1],
                recOutShape[2],
                charset_);

            if (rec2Model_.session != nullptr &&
                (decoded.confidence < 0.90f || CountMixedLatinInsideThai(decoded.text) > 0)) {
                std::vector<float> rec2Output;
                std::vector<int64_t> rec2OutShape;
                if (RunFloatModelLocked(rec2Model_, recInput, recShape.data(), recShape.size(), rec2Output, rec2OutShape) &&
                    HasValidCtcOutput(rec2OutShape, rec2Output.size())) {
                    CtcDecodeResult decoded2 = DecodeGreedyCtc(
                        rec2Output.data(),
                        rec2OutShape[1],
                        rec2OutShape[2],
                        charset_);
                    const int mixed1 = CountMixedLatinInsideThai(decoded.text);
                    const int mixed2 = CountMixedLatinInsideThai(decoded2.text);
                    if (mixed2 < mixed1 || (mixed2 == mixed1 && decoded2.confidence > decoded.confidence)) {
                        decoded = std::move(decoded2);
                    }
                }
            }

            if (decoded.confidence < 0.55f) {
                continue;
            }

            const std::wstring normalized = FixScripts(CanonicalThai(decoded.text));
            if (normalized.empty()) {
                continue;
            }

            if (!rowText.empty()) {
                rowText.push_back(L' ');
            }
            rowText.append(normalized);
        }

        if (!rowText.empty()) {
            if (!fullText.empty()) {
                fullText.append(L"\r\n");
            }
            fullText.append(rowText);
        }
    }

    result.succeeded = true;
    result.text = std::move(fullText);
    return result;
}

} // namespace pikasnip::ocr
