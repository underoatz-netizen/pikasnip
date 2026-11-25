#pragma once

#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/ocr/OcrEngine.h"

#include <windows.h>

#include <mutex>
#include <string>
#include <vector>

struct OrtApi;
struct OrtEnv;
struct OrtMemoryInfo;
struct OrtSession;
struct OrtSessionOptions;

namespace pikasnip::ocr {

/// Lazy-loaded OpenThai 2.1 OCR engine (det.onnx + rec.onnx + optional rec2.onnx)
/// backed by dynamically loaded onnxruntime.dll (never statically linked).
class OpenThaiOcrEngine {
public:
    /// Checks filesystem presence of onnxruntime.dll and required OpenThai models
    /// without loading onnxruntime.dll.
    [[nodiscard]] static bool AreAssetsPresent() noexcept;

    [[nodiscard]] static OpenThaiOcrEngine& Instance();

    ~OpenThaiOcrEngine();

    OpenThaiOcrEngine(const OpenThaiOcrEngine&) = delete;
    OpenThaiOcrEngine& operator=(const OpenThaiOcrEngine&) = delete;

    [[nodiscard]] OcrResult Recognize(const pikasnip::capture::CapturedFrame& frame);

private:
    OpenThaiOcrEngine() = default;

    struct LoadedModel {
        OrtSession* session = nullptr;
        std::string inputName;
        std::string outputName;
    };

    [[nodiscard]] bool EnsureInitializedLocked();
    void ShutdownLocked() noexcept;

    [[nodiscard]] bool LoadModelLocked(
        const std::wstring& modelPath,
        OrtSessionOptions* options,
        LoadedModel& outModel);

    [[nodiscard]] bool RunFloatModelLocked(
        const LoadedModel& model,
        const std::vector<float>& inputData,
        const int64_t* inputShape,
        size_t inputRank,
        std::vector<float>& outData,
        std::vector<int64_t>& outShape);

    std::mutex mutex_;
    bool initAttempted_ = false;
    bool initialized_ = false;
    std::wstring initError_;

    HMODULE ortModule_ = nullptr;
    const OrtApi* api_ = nullptr;
    OrtEnv* env_ = nullptr;
    OrtMemoryInfo* memoryInfo_ = nullptr;
    LoadedModel detModel_;
    LoadedModel recModel_;
    LoadedModel rec2Model_;
    std::vector<std::wstring> charset_;
};

} // namespace pikasnip::ocr
