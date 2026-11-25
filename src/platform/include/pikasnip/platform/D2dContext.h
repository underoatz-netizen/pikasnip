#pragma once

#include <objbase.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace pikasnip::platform {

/// Shared factory holder for Direct2D 1.1, DirectWrite, and WIC Imaging.
class D2dContext {
public:
    HRESULT Initialize() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept {
        return d2dFactory_ != nullptr && dwriteFactory_ != nullptr && wicFactory_ != nullptr;
    }

    [[nodiscard]] ID2D1Factory1* d2dFactory() const noexcept { return d2dFactory_.Get(); }
    [[nodiscard]] IDWriteFactory* dwriteFactory() const noexcept { return dwriteFactory_.Get(); }
    [[nodiscard]] IWICImagingFactory2* wicFactory() const noexcept { return wicFactory_.Get(); }

    [[nodiscard]] HRESULT CreatePathGeometry(
        Microsoft::WRL::ComPtr<ID2D1PathGeometry>& outGeometry) const noexcept;

private:
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IWICImagingFactory2> wicFactory_;
};

} // namespace pikasnip::platform
