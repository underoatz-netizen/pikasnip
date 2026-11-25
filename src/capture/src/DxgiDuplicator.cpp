#include "pikasnip/capture/DxgiDuplicator.h"

#include "pikasnip/platform/Dpi.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

namespace pikasnip::capture {

namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kBytesPerPixel = 4;

bool AdapterHasAttachedOutput(IDXGIAdapter1* adapter) {
    if (adapter == nullptr) {
        return false;
    }
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(index, output.ReleaseAndGetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!output) {
            break;
        }
        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(output->GetDesc(&desc)) && desc.AttachedToDesktop) {
            return true;
        }
    }
    return false;
}

/// Releases the duplicated desktop frame exactly once, if it was acquired.
class ScopedFrameRelease {
public:
    explicit ScopedFrameRelease(IDXGIOutputDuplication* duplication) noexcept
        : duplication_(duplication) {}

    ~ScopedFrameRelease() {
        releaseNow();
    }

    ScopedFrameRelease(const ScopedFrameRelease&) = delete;
    ScopedFrameRelease& operator=(const ScopedFrameRelease&) = delete;

    void markAcquired() noexcept { acquired_ = true; }

    void releaseNow() noexcept {
        if (acquired_ && duplication_ != nullptr) {
            duplication_->ReleaseFrame();
            acquired_ = false;
        }
    }

private:
    IDXGIOutputDuplication* duplication_ = nullptr;
    bool acquired_ = false;
};

/// Maps a staging texture for CPU read and unmaps it on scope exit.
class ScopedTextureMap {
public:
    ScopedTextureMap(ID3D11DeviceContext* context, ID3D11Texture2D* texture) noexcept
        : context_(context), texture_(texture) {
        if (context_ != nullptr && texture_ != nullptr &&
            SUCCEEDED(context_->Map(texture_, 0, D3D11_MAP_READ, 0, &mapped_))) {
            mappedOk_ = true;
        }
    }

    ~ScopedTextureMap() {
        if (mappedOk_) {
            context_->Unmap(texture_, 0);
        }
    }

    ScopedTextureMap(const ScopedTextureMap&) = delete;
    ScopedTextureMap& operator=(const ScopedTextureMap&) = delete;

    [[nodiscard]] bool isMapped() const noexcept { return mappedOk_; }
    [[nodiscard]] const D3D11_MAPPED_SUBRESOURCE& mapped() const noexcept { return mapped_; }

private:
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    D3D11_MAPPED_SUBRESOURCE mapped_{};
    bool mappedOk_ = false;
};

bool EnsureStagingTexture(
    ID3D11Device* device,
    const D3D11_TEXTURE2D_DESC& gpuDesc,
    ComPtr<ID3D11Texture2D>& stagingTex,
    D3D11_TEXTURE2D_DESC& stagingDesc) {
    if (device == nullptr) {
        return false;
    }
    if (stagingTex && stagingDesc.Width == gpuDesc.Width && stagingDesc.Height == gpuDesc.Height &&
        stagingDesc.Format == gpuDesc.Format) {
        return true;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = gpuDesc.Width;
    desc.Height = gpuDesc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = gpuDesc.Format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    stagingTex.Reset();
    if (FAILED(device->CreateTexture2D(&desc, nullptr, stagingTex.ReleaseAndGetAddressOf())) ||
        !stagingTex) {
        return false;
    }
    stagingDesc = desc;
    return true;
}

/// Copies one duplicated monitor surface into the shared virtual-screen frame,
/// applying the output rotation and the monitor offset.
bool BlitOutputToFrame(
    const D3D11_MAPPED_SUBRESOURCE& mapped,
    const D3D11_TEXTURE2D_DESC& stagingDesc,
    const DXGI_OUTPUT_DESC& outputDesc,
    const platform::RectI& virtualBounds,
    CapturedFrame& frame) {
    if (mapped.pData == nullptr || frame.bgraPixels.empty() || frame.width <= 0 ||
        frame.height <= 0) {
        return false;
    }

    const int32_t sourceWidth = static_cast<int32_t>(stagingDesc.Width);
    const int32_t sourceHeight = static_cast<int32_t>(stagingDesc.Height);
    const int32_t sourceStride = static_cast<int32_t>(mapped.RowPitch);
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceStride < sourceWidth * 4) {
        return false;
    }

    const int32_t offsetX = outputDesc.DesktopCoordinates.left - virtualBounds.left;
    const int32_t offsetY = outputDesc.DesktopCoordinates.top - virtualBounds.top;

    const auto* source = static_cast<const uint8_t*>(mapped.pData);
    const DXGI_MODE_ROTATION rotation = outputDesc.Rotation;

    const bool rotated90 = rotation == DXGI_MODE_ROTATION_ROTATE90;
    const bool rotated270 = rotation == DXGI_MODE_ROTATION_ROTATE270;
    const int32_t rotatedWidth = (rotated90 || rotated270) ? sourceHeight : sourceWidth;
    const int32_t rotatedHeight = (rotated90 || rotated270) ? sourceWidth : sourceHeight;

    const int32_t clipLeft = std::max(0, -offsetX);
    const int32_t clipTop = std::max(0, -offsetY);
    const int32_t clipRight = std::min(rotatedWidth, frame.width - offsetX);
    const int32_t clipBottom = std::min(rotatedHeight, frame.height - offsetY);
    if (clipRight <= clipLeft || clipBottom <= clipTop) {
        return true;
    }

    if (rotation == DXGI_MODE_ROTATION_IDENTITY || rotation == DXGI_MODE_ROTATION_UNSPECIFIED) {
        const size_t rowBytes = static_cast<size_t>(clipRight - clipLeft) * kBytesPerPixel;
        for (int32_t y = clipTop; y < clipBottom; ++y) {
            uint8_t* destination = frame.bgraPixels.data() +
                                   static_cast<size_t>(offsetY + y) *
                                       static_cast<size_t>(frame.strideBytes) +
                                   static_cast<size_t>(offsetX + clipLeft) * kBytesPerPixel;
            const uint8_t* row = source + static_cast<size_t>(y) * static_cast<size_t>(sourceStride) +
                                 static_cast<size_t>(clipLeft) * kBytesPerPixel;
            std::memcpy(destination, row, rowBytes);
        }
        return true;
    }

    for (int32_t y = clipTop; y < clipBottom; ++y) {
        uint8_t* destinationRow = frame.bgraPixels.data() +
                                  static_cast<size_t>(offsetY + y) *
                                      static_cast<size_t>(frame.strideBytes);
        for (int32_t x = clipLeft; x < clipRight; ++x) {
            int32_t sourceX = x;
            int32_t sourceY = y;
            switch (rotation) {
            case DXGI_MODE_ROTATION_ROTATE90:
                // Clockwise: destination (x, y) <- source (y, height - 1 - x).
                sourceX = y;
                sourceY = sourceHeight - 1 - x;
                break;
            case DXGI_MODE_ROTATION_ROTATE180:
                sourceX = sourceWidth - 1 - x;
                sourceY = sourceHeight - 1 - y;
                break;
            case DXGI_MODE_ROTATION_ROTATE270:
                // Counter-clockwise: destination (x, y) <- source (width - 1 - y, x).
                sourceX = sourceWidth - 1 - y;
                sourceY = x;
                break;
            default:
                break;
            }

            const uint8_t* sourcePixel =
                source + static_cast<size_t>(sourceY) * static_cast<size_t>(sourceStride) +
                static_cast<size_t>(sourceX) * kBytesPerPixel;
            uint8_t* destinationPixel =
                destinationRow + static_cast<size_t>(offsetX + x) * kBytesPerPixel;
            std::memcpy(destinationPixel, sourcePixel, kBytesPerPixel);
        }
    }
    return true;
}

} // namespace

void DxgiDuplicator::ReleaseResources() const noexcept {
    outputs_.clear();
    cachedVirtualBounds_ = {};
}

bool DxgiDuplicator::InitializeOutputs(const platform::RectI& virtualBounds) const {
    outputs_.clear();
    cachedVirtualBounds_ = {};

    if (virtualBounds.isEmpty()) {
        return false;
    }

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = ::CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || !factory) {
        return false;
    }

    std::vector<CachedOutput> initialized;
    for (UINT adapterIdx = 0;; ++adapterIdx) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIdx, adapter.ReleaseAndGetAddressOf()) ==
            DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!AdapterHasAttachedOutput(adapter.Get())) {
            continue;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0};
        hr = ::D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                 static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                 device.ReleaseAndGetAddressOf(), nullptr,
                                 context.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            // Pre-Windows 8.1 runtimes reject 11_1 with E_INVALIDARG; retry without it.
            const D3D_FEATURE_LEVEL fallbackLevels[] = {D3D_FEATURE_LEVEL_11_0,
                                                        D3D_FEATURE_LEVEL_10_1,
                                                        D3D_FEATURE_LEVEL_10_0};
            device.Reset();
            context.Reset();
            hr = ::D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, fallbackLevels,
                                     static_cast<UINT>(std::size(fallbackLevels)),
                                     D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf(), nullptr,
                                     context.ReleaseAndGetAddressOf());
        }
        if (FAILED(hr) || !device || !context) {
            return false;
        }

        for (UINT outIdx = 0;; ++outIdx) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outIdx, output.ReleaseAndGetAddressOf()) ==
                DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (!output) {
                break;
            }
            DXGI_OUTPUT_DESC outDesc{};
            if (FAILED(output->GetDesc(&outDesc)) || !outDesc.AttachedToDesktop) {
                continue;
            }

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1)) || !output1) {
                return false;
            }

            ComPtr<IDXGIOutputDuplication> duplication;
            hr = output1->DuplicateOutput(device.Get(), duplication.ReleaseAndGetAddressOf());
            if (FAILED(hr) || !duplication) {
                return false;
            }

            CachedOutput entry;
            entry.device = device;
            entry.context = context;
            entry.output1 = std::move(output1);
            entry.duplication = std::move(duplication);
            entry.outputDesc = outDesc;
            initialized.push_back(std::move(entry));
        }
    }

    if (initialized.empty()) {
        return false;
    }

    outputs_ = std::move(initialized);
    cachedVirtualBounds_ = virtualBounds;
    return true;
}

DxgiDuplicator::AcquireResult DxgiDuplicator::AcquireOutputFrame(
    CachedOutput& out,
    bool allowBlit,
    const platform::RectI& virtualBounds,
    CapturedFrame& outFrame) const {
    if (!out.device || !out.context || !out.duplication) {
        return AcquireResult::NeedReinit;
    }

    ScopedFrameRelease frameGuard(out.duplication.Get());
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    bool gotNewFrame = false;

    if (out.hasCachedStagingFrame) {
        const HRESULT hr = out.duplication->AcquireNextFrame(
            0, &frameInfo, desktopResource.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
            return AcquireResult::NeedReinit;
        }
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // Desktop unchanged since cached staging frame; reuse persistent staging texture directly.
            if (!allowBlit) {
                return AcquireResult::Ok;
            }
            ScopedTextureMap mapGuard(out.context.Get(), out.stagingTex.Get());
            if (!mapGuard.isMapped()) {
                return AcquireResult::Failed;
            }
            return BlitOutputToFrame(mapGuard.mapped(), out.stagingDesc, out.outputDesc,
                                     virtualBounds, outFrame)
                       ? AcquireResult::Ok
                       : AcquireResult::Failed;
        }
        if (FAILED(hr) || !desktopResource) {
            return AcquireResult::Failed;
        }
        frameGuard.markAcquired();
        gotNewFrame = true;
    } else {
        constexpr int kMaxAttempts = 5;
        constexpr UINT kTimeoutMs = 15;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            desktopResource.Reset();
            frameInfo = {};
            const HRESULT hr = out.duplication->AcquireNextFrame(
                kTimeoutMs, &frameInfo, desktopResource.ReleaseAndGetAddressOf());
            if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
                return AcquireResult::NeedReinit;
            }
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            if (FAILED(hr) || !desktopResource) {
                return AcquireResult::Failed;
            }
            frameGuard.markAcquired();

            if (frameInfo.LastPresentTime.QuadPart == 0 && frameInfo.AccumulatedFrames == 0 &&
                attempt + 1 < kMaxAttempts) {
                frameGuard.releaseNow();
                continue;
            }
            gotNewFrame = true;
            break;
        }

        if (!gotNewFrame ||
            (frameInfo.LastPresentTime.QuadPart == 0 && frameInfo.AccumulatedFrames == 0)) {
            return AcquireResult::Failed;
        }
    }

    ComPtr<ID3D11Texture2D> gpuTex;
    if (FAILED(desktopResource.As(&gpuTex)) || !gpuTex) {
        return AcquireResult::Failed;
    }

    D3D11_TEXTURE2D_DESC gpuDesc{};
    gpuTex->GetDesc(&gpuDesc);
    if (!EnsureStagingTexture(out.device.Get(), gpuDesc, out.stagingTex, out.stagingDesc)) {
        return AcquireResult::Failed;
    }

    out.context->CopyResource(out.stagingTex.Get(), gpuTex.Get());
    out.hasCachedStagingFrame = true;
    frameGuard.releaseNow();

    if (!allowBlit) {
        return AcquireResult::Ok;
    }

    ScopedTextureMap mapGuard(out.context.Get(), out.stagingTex.Get());
    if (!mapGuard.isMapped()) {
        return AcquireResult::Failed;
    }

    return BlitOutputToFrame(mapGuard.mapped(), out.stagingDesc, out.outputDesc, virtualBounds,
                             outFrame)
               ? AcquireResult::Ok
               : AcquireResult::Failed;
}

bool DxgiDuplicator::WarmUp() const {
    const pikasnip::platform::RectI virtualBounds = pikasnip::platform::GetVirtualScreenRect();
    if (!InitializeOutputs(virtualBounds)) {
        return false;
    }
    CapturedFrame dummy;
    for (auto& out : outputs_) {
        (void)AcquireOutputFrame(out, false, virtualBounds, dummy);
    }
    return true;
}

bool DxgiDuplicator::Capture(
    const pikasnip::platform::RectI& virtualBounds,
    CapturedFrame& outFrame) const {
    if (virtualBounds.isEmpty()) {
        return false;
    }

    if (outputs_.empty() || cachedVirtualBounds_ != virtualBounds) {
        if (!InitializeOutputs(virtualBounds)) {
            return false;
        }
    }

    const int32_t width = virtualBounds.width();
    const int32_t height = virtualBounds.height();
    const int32_t stride = width * 4;

    CapturedFrame candidate;
    candidate.virtualBounds = virtualBounds;
    candidate.width = width;
    candidate.height = height;
    candidate.strideBytes = stride;
    candidate.usedDxgi = true;
    candidate.bgraPixels.resize(static_cast<size_t>(stride) * static_cast<size_t>(height));

    for (int pass = 0; pass < 2; ++pass) {
        bool needReinit = false;
        bool failed = false;

        for (auto& out : outputs_) {
            const AcquireResult res = AcquireOutputFrame(out, true, virtualBounds, candidate);
            if (res == AcquireResult::NeedReinit) {
                needReinit = true;
                break;
            }
            if (res == AcquireResult::Failed) {
                failed = true;
                break;
            }
        }

        if (needReinit && pass == 0) {
            ReleaseResources();
            if (!InitializeOutputs(virtualBounds)) {
                return false;
            }
            continue;
        }

        if (needReinit || failed) {
            return false;
        }

        outFrame = std::move(candidate);
        return true;
    }

    return false;
}

} // namespace pikasnip::capture
