#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/capture/WindowDetector.h"
#include "pikasnip/ocr/OcrEngine.h"
#include "pikasnip/ocr/ThaiNorm.h"
#include "pikasnip/overlay/FloatingToolbar.h"
#include "pikasnip/overlay/OverlayWindow.h"
#include "pikasnip/pin/PinManager.h"
#include "pikasnip/pin/PinWindow.h"
#include "pikasnip/platform/Clipboard.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/ToastWindow.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int gM3Failures = 0;

#define M3_EXPECT_TRUE(cond)                                                           \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #cond "\n";  \
            ++gM3Failures;                                                             \
        }                                                                              \
    } while (false)

#define M3_EXPECT_EQ(a, b)                                                             \
    do {                                                                               \
        const auto& _va = (a);                                                         \
        const auto& _vb = (b);                                                         \
        if (!(_va == _vb)) {                                                           \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] "               \
                      << #a " != " #b << "\n";                                         \
            ++gM3Failures;                                                             \
        }                                                                              \
    } while (false)

pikasnip::capture::CapturedFrame MakeSolidFrame(
    int32_t width,
    int32_t height,
    uint8_t b,
    uint8_t g,
    uint8_t r,
    uint8_t a = 255) {
    pikasnip::capture::CapturedFrame frame;
    frame.virtualBounds = pikasnip::platform::RectI{0, 0, width, height};
    frame.width = width;
    frame.height = height;
    frame.strideBytes = width * 4;
    frame.bgraPixels.resize(static_cast<size_t>(frame.strideBytes) * static_cast<size_t>(height));
    for (size_t i = 0; i < frame.bgraPixels.size(); i += 4U) {
        frame.bgraPixels[i + 0] = b;
        frame.bgraPixels[i + 1] = g;
        frame.bgraPixels[i + 2] = r;
        frame.bgraPixels[i + 3] = a;
    }
    pikasnip::capture::MonitorInfo mon;
    mon.bounds = frame.virtualBounds;
    mon.dpi = 96;
    mon.isPrimary = true;
    frame.monitors.push_back(mon);
    return frame;
}

std::wstring ToUpperAscii(std::wstring_view in) {
    std::wstring out(in);
    for (wchar_t& ch : out) {
        if (ch >= L'a' && ch <= L'z') {
            ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
    }
    return out;
}

void TestClipboardImageDecodeRoundTrip() {
    pikasnip::platform::D2dContext d2d;
    M3_EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    constexpr int32_t kW = 5;
    constexpr int32_t kH = 4;
    constexpr int32_t kStride = kW * 4;
    std::vector<uint8_t> src(static_cast<size_t>(kStride) * kH, 0U);
    for (int32_t y = 0; y < kH; ++y) {
        for (int32_t x = 0; x < kW; ++x) {
            const size_t idx = static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 4U;
            src[idx + 0] = static_cast<uint8_t>(10 + x * 20);
            src[idx + 1] = static_cast<uint8_t>(40 + y * 30);
            src[idx + 2] = static_cast<uint8_t>(90 + x + y);
            src[idx + 3] = 255U;
        }
    }

    // 1. PNG encode -> decode round-trip
    const std::vector<uint8_t> png =
        pikasnip::platform::EncodePngBytesFromBgra(d2d.wicFactory(), src.data(), kW, kH, kStride);
    M3_EXPECT_TRUE(!png.empty());

    pikasnip::platform::ClipboardImage pngDecoded;
    M3_EXPECT_TRUE(pikasnip::platform::DecodePngBytesToBgra(
        d2d.wicFactory(), png.data(), png.size(), pngDecoded));
    M3_EXPECT_TRUE(pngDecoded.isValid());
    M3_EXPECT_EQ(pngDecoded.width, kW);
    M3_EXPECT_EQ(pngDecoded.height, kH);
    M3_EXPECT_EQ(pngDecoded.bgraPixels, src);

    // 2. BuildDibV5PayloadFromBgra -> DecodeDibPayloadToBgra top-down 32bpp
    const std::vector<uint8_t> dibV5 =
        pikasnip::platform::BuildDibV5PayloadFromBgra(src.data(), kW, kH, kStride);
    M3_EXPECT_TRUE(!dibV5.empty());

    pikasnip::platform::ClipboardImage dibV5Decoded;
    M3_EXPECT_TRUE(pikasnip::platform::DecodeDibPayloadToBgra(
        dibV5.data(), dibV5.size(), dibV5Decoded));
    M3_EXPECT_TRUE(dibV5Decoded.isValid());
    M3_EXPECT_EQ(dibV5Decoded.width, kW);
    M3_EXPECT_EQ(dibV5Decoded.height, kH);
    M3_EXPECT_EQ(dibV5Decoded.bgraPixels, src);

    // 3. Bottom-up 24bpp padded BI_RGB (width=3 -> 9 bytes pixel data + 3 bytes padding = 12 bytes/row)
    {
        constexpr int32_t w24 = 3;
        constexpr int32_t h24 = 2;
        constexpr size_t srcStride24 = 12;
        std::vector<uint8_t> dib24(sizeof(BITMAPINFOHEADER) + srcStride24 * h24, 0U);
        BITMAPINFOHEADER bih{};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = w24;
        bih.biHeight = h24; // positive = bottom-up
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = BI_RGB;
        std::memcpy(dib24.data(), &bih, sizeof(bih));

        uint8_t* px = dib24.data() + sizeof(BITMAPINFOHEADER);
        // Bottom row (file row 0 -> image y=1): pixel (0,1) = {0x11, 0x22, 0x33}
        px[0] = 0x11;
        px[1] = 0x22;
        px[2] = 0x33;
        // Top row (file row 1 -> image y=0): pixel (0,0) = {0xAA, 0xBB, 0xCC}
        px[srcStride24 + 0] = 0xAA;
        px[srcStride24 + 1] = 0xBB;
        px[srcStride24 + 2] = 0xCC;

        pikasnip::platform::ClipboardImage out24;
        M3_EXPECT_TRUE(pikasnip::platform::DecodeDibPayloadToBgra(dib24.data(), dib24.size(), out24));
        M3_EXPECT_EQ(out24.width, w24);
        M3_EXPECT_EQ(out24.height, h24);
        M3_EXPECT_EQ(out24.bgraPixels[0], static_cast<uint8_t>(0xAA));
        M3_EXPECT_EQ(out24.bgraPixels[1], static_cast<uint8_t>(0xBB));
        M3_EXPECT_EQ(out24.bgraPixels[2], static_cast<uint8_t>(0xCC));
        M3_EXPECT_EQ(out24.bgraPixels[3], static_cast<uint8_t>(255));
        const size_t row1Off = static_cast<size_t>(out24.strideBytes);
        M3_EXPECT_EQ(out24.bgraPixels[row1Off + 0], static_cast<uint8_t>(0x11));
        M3_EXPECT_EQ(out24.bgraPixels[row1Off + 1], static_cast<uint8_t>(0x22));
        M3_EXPECT_EQ(out24.bgraPixels[row1Off + 2], static_cast<uint8_t>(0x33));
        M3_EXPECT_EQ(out24.bgraPixels[row1Off + 3], static_cast<uint8_t>(255));
    }

    // 4. 32bpp BI_BITFIELDS with non-zero alphaMask and all-zero alpha normalized to 255
    {
        std::vector<uint8_t> dibZeroAlpha = dibV5;
        uint8_t* px = dibZeroAlpha.data() + sizeof(BITMAPV5HEADER);
        for (size_t i = 3; i < dibZeroAlpha.size() - sizeof(BITMAPV5HEADER); i += 4U) {
            px[i] = 0U;
        }
        pikasnip::platform::ClipboardImage outNorm;
        M3_EXPECT_TRUE(pikasnip::platform::DecodeDibPayloadToBgra(
            dibZeroAlpha.data(), dibZeroAlpha.size(), outNorm));
        M3_EXPECT_EQ(outNorm.bgraPixels, src);
    }

    // 4b. 40-byte BITMAPINFOHEADER 32bpp BI_BITFIELDS (header + 3 DWORD masks + pixels)
    {
        constexpr int32_t wBf = 2;
        constexpr int32_t hBf = 1;
        const size_t maskOffset = sizeof(BITMAPINFOHEADER);
        const size_t pixelOffset = maskOffset + 3U * sizeof(DWORD);
        std::vector<uint8_t> dibBf(pixelOffset + static_cast<size_t>(wBf) * 4U, 0U);

        BITMAPINFOHEADER bih{};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = wBf;
        bih.biHeight = -hBf; // Top-down
        bih.biPlanes = 1;
        bih.biBitCount = 32;
        bih.biCompression = BI_BITFIELDS;
        std::memcpy(dibBf.data(), &bih, sizeof(bih));

        const DWORD masks[3] = {0x000000FFU, 0x0000FF00U, 0x00FF0000U}; // R, G, B
        std::memcpy(dibBf.data() + maskOffset, masks, sizeof(masks));

        const uint32_t pixels[2] = {0x00CCBBAAU, 0x00112233U}; // px0: R=AA,G=BB,B=CC; px1: R=33,G=22,B=11
        std::memcpy(dibBf.data() + pixelOffset, pixels, sizeof(pixels));

        pikasnip::platform::ClipboardImage outBf;
        M3_EXPECT_TRUE(pikasnip::platform::DecodeDibPayloadToBgra(dibBf.data(), dibBf.size(), outBf));
        M3_EXPECT_EQ(outBf.width, wBf);
        M3_EXPECT_EQ(outBf.height, hBf);
        const std::vector<uint8_t> expected{0xCC, 0xBB, 0xAA, 0xFF, 0x11, 0x22, 0x33, 0xFF};
        M3_EXPECT_EQ(outBf.bgraPixels, expected);
    }

    // 5. Malformed / truncated buffer safety
    {
        pikasnip::platform::ClipboardImage bad;
        const std::array<uint8_t, 8> garbage{1, 2, 3, 4, 5, 6, 7, 8};
        M3_EXPECT_TRUE(!pikasnip::platform::DecodePngBytesToBgra(d2d.wicFactory(), nullptr, 0, bad));
        M3_EXPECT_TRUE(!pikasnip::platform::DecodePngBytesToBgra(
            d2d.wicFactory(), garbage.data(), garbage.size(), bad));
        M3_EXPECT_TRUE(!pikasnip::platform::DecodeDibPayloadToBgra(nullptr, 0, bad));
        M3_EXPECT_TRUE(!pikasnip::platform::DecodeDibPayloadToBgra(garbage.data(), garbage.size(), bad));
        M3_EXPECT_TRUE(!pikasnip::platform::DecodeDibPayloadToBgra(
            dibV5.data(), sizeof(BITMAPV5HEADER) + 4U, bad));
        M3_EXPECT_TRUE(!bad.isValid());
    }

    // 6. Live CopyImageToClipboard -> ReadImageFromClipboard round-trip
    if (pikasnip::platform::CopyImageToClipboard(nullptr, d2d.wicFactory(), src.data(), kW, kH, kStride)) {
        pikasnip::platform::ClipboardImage liveImg;
        M3_EXPECT_TRUE(pikasnip::platform::ReadImageFromClipboard(nullptr, d2d.wicFactory(), liveImg));
        M3_EXPECT_TRUE(liveImg.isValid());
        M3_EXPECT_EQ(liveImg.width, kW);
        M3_EXPECT_EQ(liveImg.height, kH);
        M3_EXPECT_EQ(liveImg.bgraPixels, src);
    }
}

/// MUST run before any OCR recognition test: onnxruntime.dll (~20 MB) is only
/// loaded on the first real recognition, never by asset probing (IsSupported()).
void TestLazyOnnxRuntimeLoad() {
    M3_EXPECT_TRUE(::GetModuleHandleW(L"onnxruntime.dll") == nullptr);

    pikasnip::ocr::OcrEngine engine;
    const bool supported = engine.IsSupported();
    M3_EXPECT_TRUE(supported);
    // Proves IsSupported() never eagerly loads the 20 MB ONNX Runtime DLL.
    M3_EXPECT_TRUE(::GetModuleHandleW(L"onnxruntime.dll") == nullptr);

    const pikasnip::capture::CapturedFrame frame = MakeSolidFrame(64, 64, 0xFF, 0xFF, 0xFF);
    const pikasnip::ocr::OcrResult res = engine.RecognizeFrame(frame);
    (void)res;
    M3_EXPECT_TRUE(::GetModuleHandleW(L"onnxruntime.dll") != nullptr);
}

void TestPinWindowLifecycleAndManager() {
    pikasnip::platform::D2dContext d2d;
    M3_EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    const HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    const pikasnip::capture::CapturedFrame frame = MakeSolidFrame(120, 80, 0xCC, 0x88, 0x44);

    // Single PinWindow lifecycle, clamping, wheel zoom/opacity, Esc close
    {
        pikasnip::pin::PinWindow pin(d2d);
        pikasnip::pin::PinOptions opts;
        opts.alwaysOnTop = true;
        opts.initialPosition = POINT{40, 40};
        bool closedFired = false;

        M3_EXPECT_TRUE(pin.Create(
            hInst,
            frame,
            opts,
            [&closedFired](pikasnip::pin::PinWindow*) { closedFired = true; }));
        M3_EXPECT_TRUE(pin.isOpen());
        M3_EXPECT_TRUE(pin.hwnd() != nullptr);
        M3_EXPECT_TRUE(std::abs(pin.scale() - 1.0f) < 0.001f);
        M3_EXPECT_TRUE(std::abs(pin.opacity() - 1.0f) < 0.001f);

        M3_EXPECT_TRUE(pin.SetScale(0.05f));
        M3_EXPECT_TRUE(std::abs(pin.scale() - 0.25f) < 0.001f);
        M3_EXPECT_TRUE(pin.SetScale(9.0f));
        M3_EXPECT_TRUE(std::abs(pin.scale() - 4.00f) < 0.001f);
        M3_EXPECT_TRUE(pin.SetScale(1.0f));

        M3_EXPECT_TRUE(pin.SetOpacity(0.05f));
        M3_EXPECT_TRUE(std::abs(pin.opacity() - 0.20f) < 0.001f);
        M3_EXPECT_TRUE(pin.SetOpacity(2.5f));
        M3_EXPECT_TRUE(std::abs(pin.opacity() - 1.00f) < 0.001f);

        // Default state: clean borderless pin (edit mode off, toolbar hidden)
        M3_EXPECT_TRUE(!pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() == nullptr);

        // Mouse wheel zoom + Ctrl+Wheel opacity
        ::SendMessageW(pin.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        M3_EXPECT_TRUE(pin.scale() > 1.05f);
        ::SendMessageW(pin.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -WHEEL_DELTA), 0);
        M3_EXPECT_TRUE(pin.opacity() < 0.95f);

        // 'E' key toggles edit mode on and opens companion toolbar
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, 'E', 0);
        M3_EXPECT_TRUE(pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() != nullptr);

        // Reset scale to 1.0 so client coordinates map 1:1 to image coordinates
        M3_EXPECT_TRUE(pin.SetScale(1.0f));

        // Click ToolRectangle (first button on toolbar)
        const UINT tbDpi = pikasnip::platform::GetWindowDpi(pin.toolbarHwnd());
        const float tbScale = static_cast<float>(tbDpi != 0U ? tbDpi : 96U) / 96.0f;
        const int32_t padX = std::max(4, static_cast<int32_t>(std::lround(6.0f * tbScale)));
        const int32_t padY = std::max(4, static_cast<int32_t>(std::lround(4.0f * tbScale)));
        const int32_t btnW = std::max(6, static_cast<int32_t>(std::lround(26.0f * tbScale)));
        const int32_t rowH = std::max(24, static_cast<int32_t>(std::lround(28.0f * tbScale)));
        const LPARAM rectBtnPt = MAKELPARAM(padX + btnW / 2, padY + rowH / 2);
        ::SendMessageW(pin.toolbarHwnd(), WM_LBUTTONDOWN, MK_LBUTTON, rectBtnPt);
        ::SendMessageW(pin.toolbarHwnd(), WM_LBUTTONUP, 0, rectBtnPt);

        // Draw a rectangle on the pin window
        ::SendMessageW(pin.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        ::SendMessageW(pin.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(80, 60));
        ::SendMessageW(pin.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(80, 60));
        M3_EXPECT_EQ(pin.annotations().items().size(), static_cast<size_t>(1));

        // Ctrl+Z undo & Ctrl+Y redo
        BYTE oldKeys[256]{};
        BYTE ctrlKeys[256]{};
        (void)::GetKeyboardState(oldKeys);
        std::memcpy(ctrlKeys, oldKeys, sizeof(ctrlKeys));
        ctrlKeys[VK_CONTROL] = 0x80;
        (void)::SetKeyboardState(ctrlKeys);
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, 'Z', 0);
        M3_EXPECT_EQ(pin.annotations().items().size(), static_cast<size_t>(0));
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, 'Y', 0);
        (void)::SetKeyboardState(oldKeys);
        M3_EXPECT_EQ(pin.annotations().items().size(), static_cast<size_t>(1));

        // BuildFlattenedFrame renders annotations into output pixels
        pikasnip::capture::CapturedFrame flattened;
        M3_EXPECT_TRUE(pin.BuildFlattenedFrame(flattened));
        M3_EXPECT_TRUE(flattened.isValid());
        M3_EXPECT_TRUE(flattened.bgraPixels != frame.bgraPixels);

        // Step-wise Esc: first deselects active Rectangle tool, second exits edit mode
        // (hides toolbar, keeps pin open & preserves annotation), third closes pin
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        M3_EXPECT_TRUE(pin.isOpen());
        M3_EXPECT_TRUE(pin.isEditMode());
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        M3_EXPECT_TRUE(pin.isOpen());
        M3_EXPECT_TRUE(!pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() == nullptr);
        M3_EXPECT_EQ(pin.annotations().items().size(), static_cast<size_t>(1));

        // Final VK_ESCAPE closes and fires onClosed
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        M3_EXPECT_TRUE(!pin.isOpen());
        M3_EXPECT_TRUE(pin.hwnd() == nullptr);
        M3_EXPECT_TRUE(closedFired);
    }

    // Middle-click release inside client rect closes PinWindow
    {
        pikasnip::pin::PinWindow pin(d2d);
        pikasnip::pin::PinOptions opts;
        opts.initialPosition = POINT{60, 60};
        M3_EXPECT_TRUE(pin.Create(hInst, frame, opts));
        M3_EXPECT_TRUE(pin.isOpen());

        ::SendMessageW(pin.hwnd(), WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(10, 10));
        ::SendMessageW(pin.hwnd(), WM_MBUTTONUP, 0, MAKELPARAM(10, 10));
        M3_EXPECT_TRUE(!pin.isOpen());
    }

    // Context menu WM_COMMAND 2001 (IDM_PIN_EDIT) and 2006 (IDM_PIN_CLOSE)
    {
        pikasnip::pin::PinWindow pin(d2d);
        pikasnip::pin::PinOptions opts;
        opts.initialPosition = POINT{80, 80};
        M3_EXPECT_TRUE(pin.Create(hInst, frame, opts));
        M3_EXPECT_TRUE(!pin.isEditMode());

        ::SendMessageW(pin.hwnd(), WM_COMMAND, MAKEWPARAM(2001, 0), 0);
        M3_EXPECT_TRUE(pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() != nullptr);

        ::SendMessageW(pin.hwnd(), WM_COMMAND, MAKEWPARAM(2001, 0), 0);
        M3_EXPECT_TRUE(!pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() == nullptr);

        ::SendMessageW(pin.hwnd(), WM_COMMAND, MAKEWPARAM(2006, 0), 0);
        M3_EXPECT_TRUE(!pin.isOpen());
    }

    // Post-edit hardening: Space/Ctrl+Space, Text authoring with surrogate pairs,
    // toolbar no-activate + Pin-less layout, OCR callback, capture loss, external destroy
    {
        pikasnip::pin::PinWindow pin(d2d);
        pikasnip::pin::PinOptions opts;
        opts.initialPosition = POINT{100, 100};

        bool closedFired = false;
        int ocrCalls = 0;
        pikasnip::capture::CapturedFrame ocrFrame;
        M3_EXPECT_TRUE(pin.Create(
            hInst,
            frame,
            opts,
            [&closedFired](pikasnip::pin::PinWindow*) { closedFired = true; },
            {},
            [&ocrCalls, &ocrFrame](pikasnip::capture::CapturedFrame&& flat) {
                ++ocrCalls;
                ocrFrame = std::move(flat);
            }));
        M3_EXPECT_TRUE(pin.isOpen());
        M3_EXPECT_TRUE(!closedFired);

        // VK_SPACE with Ctrl up toggles edit mode and opens the companion toolbar
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_SPACE, 0);
        M3_EXPECT_TRUE(pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() != nullptr);

        // Companion toolbar must never steal focus from the pin (WS_EX_NOACTIVATE)
        M3_EXPECT_EQ(
            ::SendMessageW(pin.toolbarHwnd(), WM_MOUSEACTIVATE, 0, 0),
            static_cast<LRESULT>(MA_NOACTIVATE));

        // The pin toolbar omits the (already pinned) Pin action but keeps OCR
        {
            pikasnip::overlay::FloatingToolbar layout;
            layout.ComputeLayout(
                pin.windowRect(),
                pikasnip::platform::GetVirtualScreenRect(),
                96,
                false);
            M3_EXPECT_TRUE(layout.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Pin).isEmpty());
            M3_EXPECT_TRUE(!layout.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Ocr).isEmpty());
        }

        BYTE oldKeys[256]{};
        BYTE ctrlKeys[256]{};
        (void)::GetKeyboardState(oldKeys);
        std::memcpy(ctrlKeys, oldKeys, sizeof(ctrlKeys));
        ctrlKeys[VK_CONTROL] = 0x80;
        (void)::SetKeyboardState(ctrlKeys);

        // Ctrl+Space is the Thai/English IME toggle while typing: it must NOT exit edit mode
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_SPACE, 0);
        M3_EXPECT_TRUE(pin.isEditMode());
        M3_EXPECT_TRUE(pin.toolbarHwnd() != nullptr);

        // Activate toolbar buttons through the real toolbar hit-test path
        auto clickToolbar = [&pin](pikasnip::overlay::ToolbarActionKind kind) {
            const HWND toolbarHwnd = pin.toolbarHwnd();
            if (toolbarHwnd == nullptr) {
                return false;
            }
            pikasnip::platform::RectI work = pikasnip::platform::GetVirtualScreenRect();
            UINT dpi = 96U;
            const HMONITOR monitor = ::MonitorFromWindow(toolbarHwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor != nullptr) {
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (::GetMonitorInfoW(monitor, &mi)) {
                    work = pikasnip::platform::RectI::fromWin32(mi.rcWork);
                }
            }
            const UINT winDpi = pikasnip::platform::GetWindowDpi(toolbarHwnd);
            if (winDpi != 0U) {
                dpi = winDpi;
            }

            pikasnip::overlay::FloatingToolbar layout;
            layout.ComputeLayout(pin.windowRect(), work, dpi, false);
            const pikasnip::platform::RectI target = layout.GetButtonRect(kind);
            const pikasnip::platform::RectI bounds = layout.bounds();
            if (target.isEmpty() || bounds.isEmpty()) {
                return false;
            }
            const LPARAM pt = MAKELPARAM(
                (target.left + target.right) / 2 - bounds.left,
                (target.top + target.bottom) / 2 - bounds.top);
            ::SendMessageW(toolbarHwnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
            ::SendMessageW(toolbarHwnd, WM_LBUTTONUP, 0, pt);
            return true;
        };

        M3_EXPECT_TRUE(clickToolbar(pikasnip::overlay::ToolbarActionKind::ToolText));
        ::SendMessageW(pin.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        ::SendMessageW(pin.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(20, 20));

        const size_t beforeText = pin.annotations().items().size();
        // A surrogate pair (U+1F600) must be deleted as one unit by a single VK_BACK
        ::SendMessageW(pin.hwnd(), WM_CHAR, 0xD83DU, 0);
        ::SendMessageW(pin.hwnd(), WM_CHAR, 0xDE00U, 0);
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_BACK, 0);
        // Thai "ก" then Enter commits the text annotation
        ::SendMessageW(pin.hwnd(), WM_CHAR, static_cast<WPARAM>(L'\x0E01'), 0);
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, VK_RETURN, 0);

        M3_EXPECT_EQ(pin.annotations().items().size(), beforeText + 1U);
        if (!pin.annotations().items().empty()) {
            const auto* txt = std::get_if<pikasnip::annotations::TextAnnotation>(
                &pin.annotations().items().back());
            M3_EXPECT_TRUE(txt != nullptr);
            if (txt != nullptr) {
                M3_EXPECT_EQ(txt->text, std::wstring(L"\x0E01"));
            }
        }

        // Ctrl+T and the context-menu OCR command both flatten annotations into the frame
        ::SendMessageW(pin.hwnd(), WM_KEYDOWN, 'T', 0);
        M3_EXPECT_EQ(ocrCalls, 1);
        M3_EXPECT_TRUE(ocrFrame.isValid());
        M3_EXPECT_EQ(ocrFrame.width, frame.width);
        M3_EXPECT_EQ(ocrFrame.height, frame.height);
        M3_EXPECT_TRUE(ocrFrame.bgraPixels != frame.bgraPixels);

        ::SendMessageW(pin.hwnd(), WM_COMMAND, MAKEWPARAM(2002, 0), 0);
        M3_EXPECT_EQ(ocrCalls, 2);

        (void)::SetKeyboardState(oldKeys);

        // Losing mouse capture mid-stroke drops the draft and never commits it
        M3_EXPECT_TRUE(clickToolbar(pikasnip::overlay::ToolbarActionKind::ToolRectangle));
        const size_t beforeCaptureLoss = pin.annotations().items().size();
        ::SendMessageW(pin.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        ::SendMessageW(pin.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(60, 50));
        ::SendMessageW(pin.hwnd(), WM_CAPTURECHANGED, 0, 0);
        M3_EXPECT_EQ(pin.annotations().items().size(), beforeCaptureLoss);
        ::SendMessageW(pin.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(60, 50));
        M3_EXPECT_EQ(pin.annotations().items().size(), beforeCaptureLoss);

        // External DestroyWindow while in edit mode must close pin + toolbar cleanly
        const HWND pinHwnd = pin.hwnd();
        M3_EXPECT_TRUE(pin.isEditMode());
        M3_EXPECT_TRUE(::IsWindow(pinHwnd) != FALSE);
        M3_EXPECT_TRUE(::DestroyWindow(pinHwnd) != FALSE);
        M3_EXPECT_TRUE(!pin.isOpen());
        M3_EXPECT_TRUE(pin.hwnd() == nullptr);
        M3_EXPECT_TRUE(pin.toolbarHwnd() == nullptr);
        M3_EXPECT_TRUE(closedFired);
    }

    // PinManager multi-pin + PruneClosed + CloseAll
    {
        constexpr UINT kTestCleanupMsg = WM_APP + 210;
        HWND notifyHwnd = ::CreateWindowExW(
            0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
        M3_EXPECT_TRUE(notifyHwnd != nullptr);

        pikasnip::pin::PinManager manager(d2d);
        pikasnip::pin::PinOptions opts1;
        opts1.initialPosition = POINT{30, 30};
        pikasnip::pin::PinOptions opts2;
        opts2.initialPosition = POINT{180, 30};

        M3_EXPECT_TRUE(manager.CreatePin(hInst, notifyHwnd, kTestCleanupMsg, frame, opts1));
        M3_EXPECT_TRUE(manager.CreatePin(hInst, notifyHwnd, kTestCleanupMsg, frame, opts2));
        M3_EXPECT_EQ(manager.count(), static_cast<size_t>(2));

        // Close first pin via Esc, pump message queue to verify cleanup message posted
        HWND firstPinHwnd = manager.pins()[0]->hwnd();
        M3_EXPECT_TRUE(firstPinHwnd != nullptr);
        ::SendMessageW(firstPinHwnd, WM_KEYDOWN, VK_ESCAPE, 0);

        bool sawCleanupMsg = false;
        MSG msg{};
        while (::PeekMessageW(&msg, notifyHwnd, 0, 0, PM_REMOVE)) {
            if (msg.message == kTestCleanupMsg) {
                sawCleanupMsg = true;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        M3_EXPECT_TRUE(sawCleanupMsg);

        manager.PruneClosed();
        M3_EXPECT_EQ(manager.count(), static_cast<size_t>(1));

        manager.CloseAll();
        M3_EXPECT_EQ(manager.count(), static_cast<size_t>(0));

        if (notifyHwnd != nullptr) {
            ::DestroyWindow(notifyHwnd);
        }
    }
}

void TestOcrEngineSyntheticText() {
    // Pure fallback language ranking order: th* -> en-US -> other en* -> remaining
    {
        const std::vector<std::wstring> tags1{L"fr-FR", L"en-GB", L"en-US", L"th-TH"};
        const std::vector<size_t> expected1{3, 2, 1, 0};
        M3_EXPECT_EQ(pikasnip::ocr::RankFallbackOcrLanguageTags(tags1), expected1);

        const std::vector<std::wstring> tags2{L"ja-JP", L"en-AU", L"de-DE"};
        const std::vector<size_t> expected2{1, 0, 2};
        M3_EXPECT_EQ(pikasnip::ocr::RankFallbackOcrLanguageTags(tags2), expected2);

        const std::vector<std::wstring> tagsEmpty{};
        M3_EXPECT_TRUE(pikasnip::ocr::RankFallbackOcrLanguageTags(tagsEmpty).empty());
    }

    pikasnip::ocr::OcrEngine ocr;

    // Invalid frame must fail cleanly
    {
        const pikasnip::capture::CapturedFrame invalid;
        const pikasnip::ocr::OcrResult res = ocr.RecognizeFrame(invalid);
        M3_EXPECT_TRUE(!res.succeeded);
    }

    pikasnip::platform::D2dContext d2d;
    M3_EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    const bool supported = ocr.IsSupported();
    if (supported) {
        // Render high-contrast black text "PIKASNIP OCR 12345" on white 400x120 BGRA
        constexpr int32_t kW = 400;
        constexpr int32_t kH = 120;
        Microsoft::WRL::ComPtr<IWICBitmap> wicBmp;
        M3_EXPECT_TRUE(SUCCEEDED(d2d.wicFactory()->CreateBitmap(
            kW,
            kH,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad,
            wicBmp.ReleaseAndGetAddressOf())));

        const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
        M3_EXPECT_TRUE(SUCCEEDED(d2d.d2dFactory()->CreateWicBitmapRenderTarget(
            wicBmp.Get(),
            rtProps,
            rt.ReleaseAndGetAddressOf())));

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blackBrush;
        M3_EXPECT_TRUE(SUCCEEDED(rt->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black),
            blackBrush.ReleaseAndGetAddressOf())));

        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        M3_EXPECT_TRUE(SUCCEEDED(d2d.dwriteFactory()->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            28.0f,
            L"en-US",
            fmt.ReleaseAndGetAddressOf())));
        (void)fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        (void)fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(D2D1::ColorF::White));
        constexpr wchar_t kSampleText[] = L"PIKASNIP OCR 12345";
        rt->DrawText(
            kSampleText,
            static_cast<UINT32>(std::wcslen(kSampleText)),
            fmt.Get(),
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(kW), static_cast<float>(kH)),
            blackBrush.Get());
        M3_EXPECT_TRUE(SUCCEEDED(rt->EndDraw()));

        pikasnip::capture::CapturedFrame textFrame;
        textFrame.virtualBounds = pikasnip::platform::RectI{0, 0, kW, kH};
        textFrame.width = kW;
        textFrame.height = kH;
        textFrame.strideBytes = kW * 4;
        textFrame.bgraPixels.resize(static_cast<size_t>(textFrame.strideBytes) * kH);
        M3_EXPECT_TRUE(SUCCEEDED(wicBmp->CopyPixels(
            nullptr,
            static_cast<UINT>(textFrame.strideBytes),
            static_cast<UINT>(textFrame.bgraPixels.size()),
            textFrame.bgraPixels.data())));

        // Called on STA test thread to prove no STA deadlock occurs
        const pikasnip::ocr::OcrResult res = ocr.RecognizeFrame(textFrame);
        M3_EXPECT_TRUE(res.succeeded);
        const std::wstring upper = ToUpperAscii(res.text);
        M3_EXPECT_TRUE(upper.find(L"PIKASNIP") != std::wstring::npos);
        M3_EXPECT_TRUE(upper.find(L"12345") != std::wstring::npos);
    }

    // Tiny (24x24) and oversized (3000x100) bounds must not crash or fail bounds checks
    const pikasnip::capture::CapturedFrame tinyFrame = MakeSolidFrame(24, 24, 0xFF, 0xFF, 0xFF);
    const pikasnip::ocr::OcrResult tinyRes = ocr.RecognizeFrame(tinyFrame);
    M3_EXPECT_EQ(tinyRes.succeeded, supported);

    const pikasnip::capture::CapturedFrame wideFrame = MakeSolidFrame(3000, 100, 0xFF, 0xFF, 0xFF);
    const pikasnip::ocr::OcrResult wideRes = ocr.RecognizeFrame(wideFrame);
    M3_EXPECT_EQ(wideRes.succeeded, supported);
}

std::string WideToUtf8ForTest(std::wstring_view ws) {
    if (ws.empty()) {
        return {};
    }
    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(needed), '\0');
    (void)::WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

pikasnip::capture::CapturedFrame RenderTextFrameForOcr(
    pikasnip::platform::D2dContext& d2d,
    int32_t width,
    int32_t height,
    std::wstring_view fontFamily,
    std::wstring_view text,
    D2D1::ColorF bgColor,
    D2D1::ColorF fgColor) {
    pikasnip::capture::CapturedFrame frame;
    Microsoft::WRL::ComPtr<IWICBitmap> wicBmp;
    if (FAILED(d2d.wicFactory()->CreateBitmap(
            static_cast<UINT>(width),
            static_cast<UINT>(height),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad,
            wicBmp.ReleaseAndGetAddressOf()))) {
        return frame;
    }

    const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d.d2dFactory()->CreateWicBitmapRenderTarget(
            wicBmp.Get(),
            rtProps,
            rt.ReleaseAndGetAddressOf()))) {
        return frame;
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fgBrush;
    if (FAILED(rt->CreateSolidColorBrush(fgColor, fgBrush.ReleaseAndGetAddressOf()))) {
        return frame;
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(d2d.dwriteFactory()->CreateTextFormat(
            std::wstring(fontFamily).c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            28.0f,
            L"th-TH",
            fmt.ReleaseAndGetAddressOf()))) {
        return frame;
    }
    (void)fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    (void)fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    rt->BeginDraw();
    rt->Clear(bgColor);
    rt->DrawText(
        text.data(),
        static_cast<UINT32>(text.size()),
        fmt.Get(),
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
        fgBrush.Get());
    if (FAILED(rt->EndDraw())) {
        return frame;
    }

    frame.virtualBounds = pikasnip::platform::RectI{0, 0, width, height};
    frame.width = width;
    frame.height = height;
    frame.strideBytes = width * 4;
    frame.bgraPixels.resize(static_cast<size_t>(frame.strideBytes) * static_cast<size_t>(height));
    if (FAILED(wicBmp->CopyPixels(
            nullptr,
            static_cast<UINT>(frame.strideBytes),
            static_cast<UINT>(frame.bgraPixels.size()),
            frame.bgraPixels.data()))) {
        frame.bgraPixels.clear();
        frame.width = 0;
        frame.height = 0;
    }
    return frame;
}

void TestThaiNormAndOpenThaiOcr() {
    // 1. ThaiNorm::CanonicalThai
    // PUA mapping + zero-width stripping + NBSP -> space
    const std::wstring puaIn = L"\xF700\xF701\x200B\x00A0\xF70F\xF718\xFEFF";
    M3_EXPECT_EQ(pikasnip::ocr::CanonicalThai(puaIn), std::wstring(L"\x0E10\x0E34 \x0E0D\x0E38"));

    // Mark ordering (tone before vowel -> vowel before tone) + doubled mark collapse
    const std::wstring disordered = L"\x0E01\x0E48\x0E48\x0E34"; // ก่่ิ -> กิ่
    M3_EXPECT_EQ(pikasnip::ocr::CanonicalThai(disordered), std::wstring(L"\x0E01\x0E34\x0E48"));

    // Sara am composition in both typed orders: นํ้า / น้ํา -> น้ำ
    M3_EXPECT_EQ(pikasnip::ocr::CanonicalThai(L"\x0E19\x0E4D\x0E49\x0E32"), std::wstring(L"\x0E19\x0E49\x0E33"));
    M3_EXPECT_EQ(pikasnip::ocr::CanonicalThai(L"\x0E19\x0E49\x0E4D\x0E32"), std::wstring(L"\x0E19\x0E49\x0E33"));

    // เเ -> แ, ำำ -> ำ, whitespace collapse
    M3_EXPECT_EQ(pikasnip::ocr::CanonicalThai(L"  \x0E40\x0E40\x0E21\x0E48\x0E19  \x0E04\x0E33\x0E33 \r\n "),
                 std::wstring(L"\x0E41\x0E21\x0E48\x0E19 \x0E04\x0E33"));

    // 2. ThaiNorm::FixScripts
    M3_EXPECT_EQ(pikasnip::ocr::FixScripts(L"W.\x0E28. 2569"), std::wstring(L"\x0E1E.\x0E28. 2569"));
    M3_EXPECT_EQ(pikasnip::ocr::FixScripts(L"w\x0E24\x0E29\x0E20\x0E32\x0E04\x0E21"), std::wstring(L"\x0E1E\x0E24\x0E29\x0E20\x0E32\x0E04\x0E21"));
    M3_EXPECT_EQ(pikasnip::ocr::FixScripts(L"\x0E57o.\x0E58\x0E52"), std::wstring(L"\x0E57\x0E50.\x0E58\x0E52"));
    M3_EXPECT_EQ(pikasnip::ocr::FixScripts(L"00\x0E51"), std::wstring(L"\x0E50\x0E50\x0E51"));
    M3_EXPECT_EQ(pikasnip::ocr::FixScripts(L"Word Web 100"), std::wstring(L"Word Web 100"));

    // 3. Real OpenThai 2.1 OCR on light & dark backgrounds
    pikasnip::ocr::OcrEngine ocr;
    M3_EXPECT_TRUE(ocr.IsSupported());

    pikasnip::platform::D2dContext d2d;
    M3_EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    // Light background: "สวัสดีประเทศไทย 2026"
    const pikasnip::capture::CapturedFrame lightFrame = RenderTextFrameForOcr(
        d2d,
        520,
        120,
        L"Tahoma",
        L"\x0E2A\x0E27\x0E31\x0E2A\x0E14\x0E35\x0E1B\x0E23\x0E30\x0E40\x0E17\x0E28\x0E44\x0E17\x0E22 2026",
        D2D1::ColorF(D2D1::ColorF::White),
        D2D1::ColorF(D2D1::ColorF::Black));
    M3_EXPECT_TRUE(lightFrame.isValid());
    const pikasnip::ocr::OcrResult lightRes = ocr.RecognizeFrame(lightFrame);
    std::cout << "[OpenThai OCR light] succeeded=" << lightRes.succeeded
              << " text=\"" << WideToUtf8ForTest(lightRes.text) << "\"\n";
    M3_EXPECT_TRUE(lightRes.succeeded);
    M3_EXPECT_TRUE(lightRes.text.find(L"\x0E2A\x0E27\x0E31\x0E2A\x0E14\x0E35") != std::wstring::npos);
    M3_EXPECT_TRUE(lightRes.text.find(L"\x0E1B\x0E23\x0E30\x0E40\x0E17\x0E28\x0E44\x0E17\x0E22") != std::wstring::npos);
    M3_EXPECT_TRUE(lightRes.text.find(L"2026") != std::wstring::npos);

    // Dark background (#1E1E1E): "ทดสอบ PikaSnip OCR"
    const pikasnip::capture::CapturedFrame darkFrame = RenderTextFrameForOcr(
        d2d,
        520,
        120,
        L"Segoe UI",
        L"\x0E17\x0E14\x0E2A\x0E2D\x0E1A PikaSnip OCR",
        D2D1::ColorF(0x1E1E1EU),
        D2D1::ColorF(0xF9FAFBU));
    M3_EXPECT_TRUE(darkFrame.isValid());
    const pikasnip::ocr::OcrResult darkRes = ocr.RecognizeFrame(darkFrame);
    std::cout << "[OpenThai OCR dark] succeeded=" << darkRes.succeeded
              << " text=\"" << WideToUtf8ForTest(darkRes.text) << "\"\n";
    M3_EXPECT_TRUE(darkRes.succeeded);
    M3_EXPECT_TRUE(darkRes.text.find(L"\x0E17\x0E14\x0E2A\x0E2D\x0E1A") != std::wstring::npos);
    M3_EXPECT_TRUE(ToUpperAscii(darkRes.text).find(L"PIKASNIP") != std::wstring::npos);
    M3_EXPECT_TRUE(ToUpperAscii(darkRes.text).find(L"OCR") != std::wstring::npos);
}

void TestToastWindowLifecycle() {
    pikasnip::platform::D2dContext d2d;
    M3_EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    const HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    pikasnip::platform::ToastWindow toast(d2d);

    M3_EXPECT_TRUE(toast.Show(hInst, L"Pinned to screen", false, 1500));
    M3_EXPECT_TRUE(toast.isOpen());
    M3_EXPECT_TRUE(toast.hwnd() != nullptr);
    M3_EXPECT_EQ(toast.message(), std::wstring(L"Pinned to screen"));
    M3_EXPECT_TRUE(!toast.isError());
    {
        RECT rgnBox{};
        const int rgnType = ::GetWindowRgnBox(toast.hwnd(), &rgnBox);
        M3_EXPECT_EQ(rgnType, COMPLEXREGION);
    }

    // In-place update while open
    M3_EXPECT_TRUE(toast.Show(hInst, L"No image in clipboard", true, 1500));
    M3_EXPECT_TRUE(toast.isOpen());
    M3_EXPECT_EQ(toast.message(), std::wstring(L"No image in clipboard"));
    M3_EXPECT_TRUE(toast.isError());

    toast.Close();
    M3_EXPECT_TRUE(!toast.isOpen());
    M3_EXPECT_TRUE(toast.hwnd() == nullptr);
}

void TestOverlayPinAndQuickOcrFlow() {
    // 1. FloatingToolbar Pin & Ocr layout/hit-test on 1920x1080 and 200x200
    {
        pikasnip::overlay::FloatingToolbar toolbar;
        const pikasnip::platform::RectI hdMon{0, 0, 1920, 1080};
        toolbar.ComputeLayout(pikasnip::platform::RectI{200, 200, 900, 600}, hdMon, 96);
        const auto pinBtn = toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Pin);
        const auto ocrBtn = toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Ocr);
        M3_EXPECT_TRUE(!pinBtn.isEmpty());
        M3_EXPECT_TRUE(!ocrBtn.isEmpty());
        M3_EXPECT_EQ(
            toolbar.HitTest(pinBtn.left + pinBtn.width() / 2, pinBtn.top + pinBtn.height() / 2).kind,
            pikasnip::overlay::ToolbarActionKind::Pin);
        M3_EXPECT_EQ(
            toolbar.HitTest(ocrBtn.left + ocrBtn.width() / 2, ocrBtn.top + ocrBtn.height() / 2).kind,
            pikasnip::overlay::ToolbarActionKind::Ocr);

        const pikasnip::platform::RectI tinyMon{0, 0, 200, 200};
        toolbar.ComputeLayout(pikasnip::platform::RectI{20, 20, 180, 180}, tinyMon, 96);
        M3_EXPECT_TRUE(!toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Pin).isEmpty());
        M3_EXPECT_TRUE(!toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Ocr).isEmpty());
    }

    pikasnip::platform::D2dContext d2d;
    M3_EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));
    const HINSTANCE hInst = ::GetModuleHandleW(nullptr);

    // 2. OverlayWindow Toolbar Pin + Ctrl+P callbacks deliver flattened frame & close
    {
        pikasnip::overlay::OverlayWindow overlay(d2d);
        int pinCalls = 0;
        pikasnip::platform::RectI pinnedRegion{};
        pikasnip::capture::CapturedFrame pinnedFrame;

        overlay.SetPinCallback([&](pikasnip::capture::CapturedFrame&& f, pikasnip::platform::RectI region) {
            ++pinCalls;
            pinnedRegion = region;
            pinnedFrame = std::move(f);
        });

        pikasnip::capture::CapturedFrame frame1 = MakeSolidFrame(200, 200, 0x10, 0x20, 0x30);
        pikasnip::capture::WindowDetector det1;
        M3_EXPECT_TRUE(overlay.Show(hInst, std::move(frame1), std::move(det1)));

        ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(160, 160));
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(160, 160));

        const auto pinRect = overlay.toolbar().GetButtonRect(pikasnip::overlay::ToolbarActionKind::Pin);
        M3_EXPECT_TRUE(!pinRect.isEmpty());
        const int32_t px = pinRect.left + pinRect.width() / 2;
        const int32_t py = pinRect.top + pinRect.height() / 2;
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(px, py));
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(px, py));

        M3_EXPECT_EQ(pinCalls, 1);
        M3_EXPECT_TRUE(!overlay.isOpen());
        M3_EXPECT_EQ(pinnedRegion, (pikasnip::platform::RectI{20, 20, 160, 160}));
        M3_EXPECT_TRUE(pinnedFrame.isValid());
        M3_EXPECT_EQ(pinnedFrame.width, 140);
        M3_EXPECT_EQ(pinnedFrame.height, 140);

        // Ctrl+P also triggers persistent pin callback
        pikasnip::capture::CapturedFrame frame2 = MakeSolidFrame(200, 200, 0x40, 0x50, 0x60);
        pikasnip::capture::WindowDetector det2;
        M3_EXPECT_TRUE(overlay.Show(hInst, std::move(frame2), std::move(det2)));

        ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 30));
        ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(130, 110));
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(130, 110));

        BYTE oldKeys[256]{};
        BYTE modKeys[256]{};
        (void)::GetKeyboardState(oldKeys);
        std::memcpy(modKeys, oldKeys, sizeof(modKeys));
        modKeys[VK_CONTROL] = 0x80;
        (void)::SetKeyboardState(modKeys);
        ::SendMessageW(overlay.hwnd(), WM_KEYDOWN, 'P', 0);
        (void)::SetKeyboardState(oldKeys);

        M3_EXPECT_EQ(pinCalls, 2);
        M3_EXPECT_TRUE(!overlay.isOpen());
        M3_EXPECT_EQ(pinnedRegion, (pikasnip::platform::RectI{30, 30, 130, 110}));
        M3_EXPECT_EQ(pinnedFrame.width, 100);
        M3_EXPECT_EQ(pinnedFrame.height, 80);
    }

    // 3. OverlayMode::QuickOcr drag-release immediately triggers OCR callback and closes overlay
    {
        pikasnip::overlay::OverlayWindow overlay(d2d);
        int ocrCalls = 0;
        pikasnip::capture::CapturedFrame ocrFrame;

        overlay.SetOcrCallback([&](pikasnip::capture::CapturedFrame&& f) {
            ++ocrCalls;
            ocrFrame = std::move(f);
        });

        pikasnip::capture::CapturedFrame frame = MakeSolidFrame(200, 200, 0xAA, 0xBB, 0xCC);
        pikasnip::capture::WindowDetector det;
        M3_EXPECT_TRUE(overlay.Show(
            hInst,
            std::move(frame),
            std::move(det),
            {},
            pikasnip::overlay::OverlayMode::QuickOcr));

        ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(25, 35));
        ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(165, 115));
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(165, 115));

        M3_EXPECT_EQ(ocrCalls, 1);
        M3_EXPECT_TRUE(!overlay.isOpen());
        M3_EXPECT_TRUE(ocrFrame.isValid());
        M3_EXPECT_EQ(ocrFrame.width, 140);
        M3_EXPECT_EQ(ocrFrame.height, 80);
    }
}

} // namespace

int RunMilestone3Tests() {
    gM3Failures = 0;
    // Lazy ONNX Runtime load check MUST run before any OCR recognition test.
    TestLazyOnnxRuntimeLoad();
    TestClipboardImageDecodeRoundTrip();
    TestPinWindowLifecycleAndManager();
    TestOcrEngineSyntheticText();
    TestThaiNormAndOpenThaiOcr();
    TestToastWindowLifecycle();
    TestOverlayPinAndQuickOcrFlow();
    return gM3Failures;
}
