#include "pikasnip/platform/D2dContext.h"

namespace pikasnip::platform {

HRESULT D2dContext::Initialize() noexcept {
    if (!d2dFactory_) {
        D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        HRESULT hr = ::D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory1),
            &options,
            reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr) && options.debugLevel != D2D1_DEBUG_LEVEL_NONE) {
            // Retry without debug layer if SDK debug layer is unavailable.
            options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
            hr = ::D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory1),
                &options,
                reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()));
        }
        if (FAILED(hr)) {
            return hr;
        }
    }

    if (!dwriteFactory_) {
        const HRESULT hr = ::DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            return hr;
        }
    }

    if (!wicFactory_) {
        const HRESULT hr = ::CoCreateInstance(
            CLSID_WICImagingFactory2,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT D2dContext::CreatePathGeometry(
    Microsoft::WRL::ComPtr<ID2D1PathGeometry>& outGeometry) const noexcept {
    outGeometry.Reset();
    if (!d2dFactory_) {
        return E_NOT_VALID_STATE;
    }
    return d2dFactory_->CreatePathGeometry(outGeometry.ReleaseAndGetAddressOf());
}

} // namespace pikasnip::platform
