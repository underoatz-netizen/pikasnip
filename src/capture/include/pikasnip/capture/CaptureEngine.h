#pragma once

#include "pikasnip/capture/CapturedFrame.h"
#include "pikasnip/capture/DxgiDuplicator.h"

#include <vector>

namespace pikasnip::capture {

/// Capture entry point: DXGI Desktop Duplication first, GDI BitBlt fallback.
///
/// CaptureAll() fills one virtual-screen frame with physical pixels, monitor
/// metadata, and a QueryPerformanceCounter-based captureMs value.
class CaptureEngine {
public:
    CaptureEngine() noexcept = default;

    /// Pre-initialises the DXGI duplication path so the first snip is fast.
    [[nodiscard]] bool WarmUp();

    /// Releases cached D3D11 / duplication resources (idle RAM back to standby).
    void TrimStandbyResources() noexcept;

    [[nodiscard]] std::vector<MonitorInfo> EnumerateMonitors() const;

    [[nodiscard]] bool CaptureAll(CapturedFrame& outFrame) const;

private:
    DxgiDuplicator dxgiDuplicator_;
};

} // namespace pikasnip::capture
