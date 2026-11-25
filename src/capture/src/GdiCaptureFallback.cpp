#include "pikasnip/capture/GdiCaptureFallback.h"

#include <windows.h>

#include <cstring>
#include <utility>

namespace pikasnip::capture {

namespace {

/// Releases GDI objects in the correct order (deselect, delete bitmap,
/// delete DC, release screen DC).
struct GdiResources {
    HDC screen = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;

    ~GdiResources() {
        if (memory != nullptr && previousBitmap != nullptr) {
            ::SelectObject(memory, previousBitmap);
        }
        if (bitmap != nullptr) {
            ::DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            ::DeleteDC(memory);
        }
        if (screen != nullptr) {
            ::ReleaseDC(nullptr, screen);
        }
    }
};

} // namespace

bool CaptureDesktopGdi(
    const pikasnip::platform::RectI& virtualBounds,
    CapturedFrame& outFrame) {
    const int32_t width = virtualBounds.width();
    const int32_t height = virtualBounds.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    GdiResources resources;
    resources.screen = ::GetDC(nullptr);
    if (resources.screen == nullptr) {
        return false;
    }

    resources.memory = ::CreateCompatibleDC(resources.screen);
    if (resources.memory == nullptr) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down DIB, matching the frame layout
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    resources.bitmap =
        ::CreateDIBSection(resources.screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (resources.bitmap == nullptr || bits == nullptr) {
        return false;
    }

    const HGDIOBJ previous = ::SelectObject(resources.memory, resources.bitmap);
    if (previous == nullptr || previous == HGDI_ERROR) {
        return false;
    }
    resources.previousBitmap = previous;

    if (::BitBlt(resources.memory, 0, 0, width, height, resources.screen, virtualBounds.left,
                 virtualBounds.top, SRCCOPY | CAPTUREBLT) == FALSE) {
        return false;
    }
    ::GdiFlush();

    CapturedFrame frame;
    frame.virtualBounds = virtualBounds;
    frame.width = width;
    frame.height = height;
    frame.strideBytes = width * 4;
    frame.usedDxgi = false;
    frame.bgraPixels.resize(static_cast<size_t>(frame.strideBytes) * static_cast<size_t>(height));
    std::memcpy(frame.bgraPixels.data(), bits, frame.bgraPixels.size());

    // GDI leaves the alpha byte untouched; a screenshot must be fully opaque.
    for (size_t i = 3; i < frame.bgraPixels.size(); i += 4) {
        frame.bgraPixels[i] = 0xFF;
    }

    outFrame = std::move(frame);
    return true;
}

} // namespace pikasnip::capture
