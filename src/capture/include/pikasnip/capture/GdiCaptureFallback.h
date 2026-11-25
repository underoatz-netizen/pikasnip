#pragma once

#include "pikasnip/capture/CapturedFrame.h"

namespace pikasnip::capture {

/// GDI BitBlt fallback for machines where DXGI desktop duplication is
/// unavailable (secure desktop, remote sessions, unsupported drivers).
///
/// Captures the virtual screen with SRCCOPY | CAPTUREBLT into a top-down
/// 32-bit DIB section and normalises the alpha channel to 255.
[[nodiscard]] bool CaptureDesktopGdi(
    const pikasnip::platform::RectI& virtualBounds,
    CapturedFrame& outFrame);

} // namespace pikasnip::capture
