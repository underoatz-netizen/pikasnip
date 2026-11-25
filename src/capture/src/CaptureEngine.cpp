#include "pikasnip/capture/CaptureEngine.h"
#include "pikasnip/capture/GdiCaptureFallback.h"

#include <ShellScalingApi.h>

namespace pikasnip::capture {

namespace {

BOOL CALLBACK EnumMonitorProc(
    HMONITOR hMonitor,
    HDC /*hdcMonitor*/,
    LPRECT /*lprcMonitor*/,
    LPARAM dwData) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
    if (monitors == nullptr) {
        return FALSE;
    }

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfoW(hMonitor, &mi)) {
        return TRUE;
    }

    MonitorInfo info;
    info.handle = hMonitor;
    info.bounds = pikasnip::platform::RectI::fromWin32(mi.rcMonitor);
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(::GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0) {
        info.dpi = dpiX;
    } else {
        info.dpi = 96;
    }

    monitors->push_back(info);
    return TRUE;
}

double ElapsedMilliseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& end, const LARGE_INTEGER& freq) noexcept {
    if (freq.QuadPart <= 0) {
        return 0.0;
    }
    return static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 /
           static_cast<double>(freq.QuadPart);
}

} // namespace

bool CaptureEngine::WarmUp() {
    return dxgiDuplicator_.WarmUp();
}

void CaptureEngine::TrimStandbyResources() noexcept {
    dxgiDuplicator_.ReleaseResources();
}

std::vector<MonitorInfo> CaptureEngine::EnumerateMonitors() const {
    std::vector<MonitorInfo> monitors;
    ::EnumDisplayMonitors(nullptr, nullptr, EnumMonitorProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

bool CaptureEngine::CaptureAll(CapturedFrame& outFrame) const {
    const pikasnip::platform::RectI virtualBounds = pikasnip::platform::GetVirtualScreenRect();
    if (virtualBounds.isEmpty()) {
        return false;
    }

    LARGE_INTEGER freq{};
    LARGE_INTEGER start{};
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&start);

    std::vector<MonitorInfo> monitors = EnumerateMonitors();

    CapturedFrame frame;
    bool ok = dxgiDuplicator_.Capture(virtualBounds, frame);
    if (!ok) {
        ok = CaptureDesktopGdi(virtualBounds, frame);
    }

    if (!ok || !frame.isValid()) {
        return false;
    }

    LARGE_INTEGER end{};
    ::QueryPerformanceCounter(&end);

    frame.monitors = std::move(monitors);
    frame.captureMs = ElapsedMilliseconds(start, end, freq);
    outFrame = std::move(frame);
    return true;
}

} // namespace pikasnip::capture
