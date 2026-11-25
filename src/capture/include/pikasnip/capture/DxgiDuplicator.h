#pragma once

#include "pikasnip/capture/CapturedFrame.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <vector>

namespace pikasnip::capture {

/// Cached DXGI Desktop Duplication backend.
///
/// Keeps one device + duplication session per attached output so repeated
/// snips do not pay the D3D11 setup cost. WarmUp() primes the sessions;
/// ReleaseResources() drops them for idle RAM.
class DxgiDuplicator {
public:
    DxgiDuplicator() noexcept = default;
    ~DxgiDuplicator() = default;

    DxgiDuplicator(const DxgiDuplicator&) = delete;
    DxgiDuplicator& operator=(const DxgiDuplicator&) = delete;

    /// Initialises duplication for every attached output and acquires one frame
    /// per output so the first real capture has content.
    [[nodiscard]] bool WarmUp() const;

    void ReleaseResources() const noexcept;

    /// Composes all monitors into one virtual-screen frame.
    /// Returns false when duplication is unavailable; the caller must fall back
    /// to CaptureDesktopGdi().
    [[nodiscard]] bool Capture(const platform::RectI& virtualBounds, CapturedFrame& outFrame) const;

private:
    enum class AcquireResult {
        Ok,
        NeedReinit,
        Failed
    };

    struct CachedOutput {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
        DXGI_OUTPUT_DESC outputDesc{};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTex;
        D3D11_TEXTURE2D_DESC stagingDesc{};
        bool hasCachedStagingFrame = false;
    };

    bool InitializeOutputs(const platform::RectI& virtualBounds) const;

    AcquireResult AcquireOutputFrame(
        CachedOutput& out,
        bool allowBlit,
        const platform::RectI& virtualBounds,
        CapturedFrame& outFrame) const;

    mutable std::vector<CachedOutput> outputs_;
    mutable platform::RectI cachedVirtualBounds_{};
};

} // namespace pikasnip::capture
