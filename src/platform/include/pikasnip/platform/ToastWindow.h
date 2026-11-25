#pragma once

#include "pikasnip/platform/D2dContext.h"

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace pikasnip::platform {

/// Non-activating topmost D2D toast popup near the bottom-right of the cursor monitor.
class ToastWindow {
public:
    explicit ToastWindow(D2dContext& d2d) noexcept;
    ~ToastWindow();

    ToastWindow(const ToastWindow&) = delete;
    ToastWindow& operator=(const ToastWindow&) = delete;

    bool Show(
        HINSTANCE hInstance,
        std::wstring_view message,
        bool isError = false,
        UINT durationMs = 2000);

    void Close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return hwnd_ != nullptr; }
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] const std::wstring& message() const noexcept { return message_; }
    [[nodiscard]] bool isError() const noexcept { return isError_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool EnsureRenderResources(int32_t width, int32_t height);
    void ReleaseRenderResources() noexcept;
    void Render();

    D2dContext& d2d_;
    HWND hwnd_ = nullptr;
    std::wstring message_;
    bool isError_ = false;
    UINT dpi_ = 96;
    int32_t width_ = 0;
    int32_t height_ = 0;
    float padX_ = 16.0f;
    float padY_ = 10.0f;
    bool closing_ = false;

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout_;
};

} // namespace pikasnip::platform
