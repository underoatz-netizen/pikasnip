#include "pikasnip/annotations/AnnotationRenderer.h"
#include "pikasnip/annotations/AnnotationSession.h"
#include "pikasnip/capture/CaptureEngine.h"
#include "pikasnip/capture/WindowDetector.h"
#include "pikasnip/overlay/FloatingToolbar.h"
#include "pikasnip/overlay/OverlayWindow.h"
#include "pikasnip/overlay/SelectionController.h"
#include "pikasnip/platform/AppEnvironment.h"
#include "pikasnip/platform/Clipboard.h"
#include "pikasnip/platform/ComInitializer.h"
#include "pikasnip/platform/D2dContext.h"
#include "pikasnip/platform/SettingsStore.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

#define EXPECT_TRUE(cond)                                                              \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] " #cond "\n";  \
            ++gFailures;                                                               \
        }                                                                              \
    } while (false)

#define EXPECT_EQ(a, b)                                                                \
    do {                                                                               \
        const auto& _va = (a);                                                         \
        const auto& _vb = (b);                                                         \
        if (!(_va == _vb)) {                                                           \
            std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "] "               \
                      << #a " != " #b << "\n";                                         \
            ++gFailures;                                                               \
        }                                                                              \
    } while (false)

void TestAppEnvironment() {
    EXPECT_TRUE(!pikasnip::platform::IsPackaged());
    EXPECT_TRUE(!pikasnip::platform::GetExecutableDirectory().empty());
    EXPECT_TRUE(!pikasnip::platform::GetConfigDirectory().empty());
    EXPECT_TRUE(pikasnip::platform::GetSettingsFilePath().filename() == L"settings.json");
    EXPECT_TRUE(!pikasnip::platform::GetDefaultSaveDirectory().empty());
}

void TestSettingsStoreRoundTrip() {
    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / L"pikasnip_test_settings";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);

    const std::filesystem::path filePath = tempDir / L"settings.json";
    pikasnip::platform::AppSettings input;
    input.defaultHotkey = "Ctrl+Shift+S";
    input.altHotkey = "Alt+A";
    input.ocrHotkey = "Alt+T";
    input.pinHotkey = "Alt+P";
    input.delaySeconds = 3;
    input.runAtStartup = true;
    input.strokeThickness = 4.5f;
    input.accentHex = "#22D3EE";

    EXPECT_TRUE(pikasnip::platform::SaveSettings(filePath, input));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    const pikasnip::platform::AppSettings loaded = pikasnip::platform::LoadSettings(filePath);
    EXPECT_EQ(loaded.defaultHotkey, input.defaultHotkey);
    EXPECT_EQ(loaded.altHotkey, input.altHotkey);
    EXPECT_EQ(loaded.ocrHotkey, input.ocrHotkey);
    EXPECT_EQ(loaded.pinHotkey, input.pinHotkey);
    EXPECT_EQ(loaded.delaySeconds, input.delaySeconds);
    EXPECT_EQ(loaded.runAtStartup, input.runAtStartup);
    EXPECT_TRUE(loaded.strokeThickness == input.strokeThickness);
    EXPECT_EQ(loaded.accentHex, input.accentHex);

    std::filesystem::remove_all(tempDir, ec);
}

void TestClipboardDibV5AndPng() {
    pikasnip::platform::D2dContext d2d;
    EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    constexpr int32_t kW = 4;
    constexpr int32_t kH = 3;
    constexpr int32_t kStride = kW * 4;
    std::vector<uint8_t> bgra(static_cast<size_t>(kStride) * kH, 0);
    // Pixel (0, 0): R=0x12, G=0x34, B=0x56, A=0xFF -> BGRA {0x56, 0x34, 0x12, 0xFF}
    bgra[0] = 0x56;
    bgra[1] = 0x34;
    bgra[2] = 0x12;
    bgra[3] = 0xFF;

    const std::vector<uint8_t> dib =
        pikasnip::platform::BuildDibV5PayloadFromBgra(bgra.data(), kW, kH, kStride);
    EXPECT_EQ(dib.size(), sizeof(BITMAPV5HEADER) + bgra.size());

    BITMAPV5HEADER hdr{};
    std::memcpy(&hdr, dib.data(), sizeof(BITMAPV5HEADER));
    EXPECT_EQ(hdr.bV5Size, static_cast<DWORD>(sizeof(BITMAPV5HEADER)));
    EXPECT_EQ(hdr.bV5Width, kW);
    EXPECT_EQ(hdr.bV5Height, -kH);
    EXPECT_EQ(hdr.bV5Compression, static_cast<DWORD>(BI_BITFIELDS));
    EXPECT_EQ(hdr.bV5CSType, static_cast<DWORD>(LCS_sRGB));

    const std::vector<uint8_t> png =
        pikasnip::platform::EncodePngBytesFromBgra(d2d.wicFactory(), bgra.data(), kW, kH, kStride);
    EXPECT_TRUE(png.size() > 8);
    // PNG magic bytes: 89 50 4E 47 0D 0A 1A 0A
    EXPECT_EQ(png[0], static_cast<uint8_t>(0x89));
    EXPECT_EQ(png[1], static_cast<uint8_t>(0x50));
    EXPECT_EQ(png[2], static_cast<uint8_t>(0x4E));
    EXPECT_EQ(png[3], static_cast<uint8_t>(0x47));
    EXPECT_EQ(png[4], static_cast<uint8_t>(0x0D));
    EXPECT_EQ(png[5], static_cast<uint8_t>(0x0A));
    EXPECT_EQ(png[6], static_cast<uint8_t>(0x1A));
    EXPECT_EQ(png[7], static_cast<uint8_t>(0x0A));
}

void TestSelectionController() {
    pikasnip::capture::CapturedFrame frame;
    frame.virtualBounds = pikasnip::platform::RectI{-100, 0, 100, 100};
    frame.width = 200;
    frame.height = 100;
    frame.strideBytes = 200 * 4;
    frame.bgraPixels.assign(static_cast<size_t>(frame.strideBytes) * frame.height, 255);

    // Paint pixel at virtual (-90, 10) -> local (10, 10) as RGB(0x38, 0xBD, 0xF8) -> BGRA(0xF8, 0xBD, 0x38, 0xFF)
    const size_t pxOffset = static_cast<size_t>(10) * frame.strideBytes + static_cast<size_t>(10) * 4U;
    frame.bgraPixels[pxOffset + 0] = 0xF8;
    frame.bgraPixels[pxOffset + 1] = 0xBD;
    frame.bgraPixels[pxOffset + 2] = 0x38;
    frame.bgraPixels[pxOffset + 3] = 0xFF;

    pikasnip::overlay::SelectionController ctrl;
    ctrl.BeginSession(&frame, nullptr);

    const auto color = ctrl.GetPixelRgbHex(-90, 10);
    EXPECT_TRUE(color.valid);
    EXPECT_EQ(color.r, static_cast<uint8_t>(0x38));
    EXPECT_EQ(color.g, static_cast<uint8_t>(0xBD));
    EXPECT_EQ(color.b, static_cast<uint8_t>(0xF8));
    EXPECT_EQ(color.hex, std::string("#38BDF8"));

    // Reverse drag normalization + clamping to virtualBounds [-100, 0, 100, 100]
    ctrl.OnLeftButtonDown(50, 80);
    ctrl.OnPointerMove(-150, -20);
    ctrl.OnLeftButtonUp(-150, -20);

    EXPECT_EQ(ctrl.mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(ctrl.selectedRect(), (pikasnip::platform::RectI{-100, 0, 50, 80}));

    // Dragging inside selection moves it without requesting commit
    ctrl.OnLeftButtonDown(-50, 40);
    ctrl.OnPointerMove(-40, 50);
    ctrl.OnLeftButtonUp(-40, 50);
    EXPECT_EQ(ctrl.selectedRect(), (pikasnip::platform::RectI{-90, 10, 60, 90}));
    EXPECT_TRUE(!ctrl.consumeCommitRequest());

    // Stationary click inside selection sets commit request (consumed once)
    ctrl.OnLeftButtonDown(-40, 50);
    ctrl.OnLeftButtonUp(-40, 50);
    EXPECT_TRUE(ctrl.consumeCommitRequest());
    EXPECT_TRUE(!ctrl.consumeCommitRequest());

    // Round 2 regression: drag out beyond the click threshold, then drag back to
    // the exact press point. This MUST NOT be treated as a click-to-commit.
    ctrl.OnLeftButtonDown(-40, 50);
    ctrl.OnPointerMove(-20, 50);
    ctrl.OnPointerMove(-40, 50);
    ctrl.OnLeftButtonUp(-40, 50);
    EXPECT_TRUE(!ctrl.consumeCommitRequest());
    EXPECT_EQ(ctrl.mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(ctrl.selectedRect(), (pikasnip::platform::RectI{-90, 10, 60, 90}));

    // A fresh press resets the sticky drag flag, so a real click still commits.
    ctrl.OnLeftButtonDown(-40, 50);
    ctrl.OnLeftButtonUp(-40, 50);
    EXPECT_TRUE(ctrl.consumeCommitRequest());

    // Cancel returns to HoverWindow
    EXPECT_TRUE(ctrl.CancelSelection());
    EXPECT_EQ(ctrl.mode(), pikasnip::overlay::SelectionMode::HoverWindow);

    // HoverWindow 1-click capture vs drag-out-and-back over a detected window
    pikasnip::capture::WindowDetector hoverDetector({
        pikasnip::capture::DetectedWindow{nullptr, pikasnip::platform::RectI{-80, 10, 20, 70}}
    });
    ctrl.BeginSession(&frame, &hoverDetector);
    ctrl.OnPointerMove(-50, 30);
    EXPECT_EQ(ctrl.mode(), pikasnip::overlay::SelectionMode::HoverWindow);
    EXPECT_EQ(ctrl.hoveredRect(), (pikasnip::platform::RectI{-80, 10, 20, 70}));

    // Drag > 3px out and back over hovered window must NOT trigger 1-click commit
    ctrl.OnLeftButtonDown(-50, 30);
    ctrl.OnPointerMove(-20, 30);
    ctrl.OnPointerMove(-50, 30);
    ctrl.OnLeftButtonUp(-50, 30);
    EXPECT_TRUE(!ctrl.consumeCommitRequest());
    EXPECT_EQ(ctrl.mode(), pikasnip::overlay::SelectionMode::HoverWindow);

    // Stationary click (<= 3px) on hovered window selects its rect AND requests immediate commit
    ctrl.OnPointerMove(-50, 30);
    ctrl.OnLeftButtonDown(-50, 30);
    ctrl.OnLeftButtonUp(-50, 30);
    EXPECT_EQ(ctrl.mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(ctrl.selectedRect(), (pikasnip::platform::RectI{-80, 10, 20, 70}));
    EXPECT_TRUE(ctrl.consumeCommitRequest());
    EXPECT_TRUE(!ctrl.consumeCommitRequest());
}

void TestCaptureEngineAndWindowDetector() {
    pikasnip::capture::CaptureEngine engine;
    pikasnip::capture::CapturedFrame frame;
    EXPECT_TRUE(engine.CaptureAll(frame));
    EXPECT_TRUE(frame.isValid());
    EXPECT_TRUE(frame.width > 0);
    EXPECT_TRUE(frame.height > 0);
    EXPECT_TRUE(!frame.monitors.empty());

    pikasnip::capture::WindowDetector detector;
    detector.Refresh();
    // Should enumerate without crashing; verify HitTestWindow works safely.
    (void)detector.HitTestWindow(frame.virtualBounds.left + 10, frame.virtualBounds.top + 10);
}

void TestAnnotationSessionUndoRedo() {
    pikasnip::annotations::AnnotationSession session(4);
    EXPECT_TRUE(session.empty());
    pikasnip::annotations::RectAnnotation r1;
    r1.bounds = {0.0f, 0.0f, 10.0f, 10.0f};
    session.Add(r1);
    EXPECT_TRUE(!session.empty());
    EXPECT_EQ(session.items().size(), static_cast<size_t>(1));
    EXPECT_TRUE(session.CanUndo());
    EXPECT_TRUE(session.Undo());
    EXPECT_EQ(session.items().size(), static_cast<size_t>(0));
    EXPECT_TRUE(session.CanRedo());
    EXPECT_TRUE(session.Redo());
    EXPECT_EQ(session.items().size(), static_cast<size_t>(1));

    session.TranslateAll(5.0f, -2.0f);
    const auto* movedRect = std::get_if<pikasnip::annotations::RectAnnotation>(&session.items()[0]);
    EXPECT_TRUE(movedRect != nullptr);
    EXPECT_TRUE(movedRect->bounds.left == 5.0f && movedRect->bounds.top == -2.0f);
    EXPECT_TRUE(movedRect->bounds.right == 15.0f && movedRect->bounds.bottom == 8.0f);
}

void TestPngSaveAndAnnotationFlatten() {
    pikasnip::platform::D2dContext d2d;
    EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    pikasnip::capture::CapturedFrame frame;
    frame.virtualBounds = pikasnip::platform::RectI{100, 100, 164, 164};
    frame.width = 64;
    frame.height = 64;
    frame.strideBytes = 64 * 4;
    frame.bgraPixels.assign(static_cast<size_t>(frame.strideBytes) * 64, 0xFF);
    // Give top-left quadrant a distinct dark color so mosaic averaging produces non-white pixels.
    for (int32_t y = 0; y < 32; ++y) {
        uint8_t* row = frame.bgraPixels.data() + static_cast<size_t>(y) * frame.strideBytes;
        for (int32_t x = 0; x < 32; ++x) {
            row[x * 4 + 0] = 0x20;
            row[x * 4 + 1] = 0x40;
            row[x * 4 + 2] = 0x80;
            row[x * 4 + 3] = 0xFF;
        }
    }

    pikasnip::annotations::AnnotationSession session;
    pikasnip::annotations::MosaicAnnotation mosaic;
    mosaic.bounds = {100.0f, 100.0f, 140.0f, 140.0f};
    mosaic.blockSize = 8;
    session.Add(mosaic);

    pikasnip::annotations::RectAnnotation rect;
    rect.bounds = {104.0f, 104.0f, 150.0f, 150.0f};
    rect.stroke = {239, 68, 68, 255};
    rect.thickness = 3.0f;
    session.Add(rect);

    pikasnip::annotations::ArrowAnnotation arrow;
    arrow.start = {110.0f, 150.0f};
    arrow.end = {150.0f, 110.0f};
    session.Add(arrow);

    pikasnip::annotations::StepCounterAnnotation step;
    step.center = {130.0f, 130.0f};
    step.number = 1;
    session.Add(step);

    pikasnip::annotations::TextAnnotation txt;
    txt.origin = {106.0f, 120.0f};
    txt.text = L"ทดสอบ";
    txt.fontSize = 16.0f;
    session.Add(txt);

    pikasnip::capture::CapturedFrame flattened;
    EXPECT_TRUE(pikasnip::annotations::AnnotationRenderer::FlattenRegion(
        d2d,
        frame,
        frame.virtualBounds,
        session,
        flattened));
    EXPECT_TRUE(flattened.isValid());
    EXPECT_EQ(flattened.width, 64);
    EXPECT_EQ(flattened.height, 64);
    // Verify alpha remains 255 across the flattened output
    EXPECT_EQ(flattened.bgraPixels[3], static_cast<uint8_t>(255));

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / L"pikasnip_test_png";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    const std::filesystem::path outFile = tempDir / L"nested" / L"test.png";
    EXPECT_TRUE(pikasnip::platform::SavePngToFile(
        d2d.wicFactory(),
        outFile,
        flattened.bgraPixels.data(),
        flattened.width,
        flattened.height,
        flattened.strideBytes));
    EXPECT_TRUE(std::filesystem::exists(outFile));
    EXPECT_TRUE(std::filesystem::file_size(outFile) > 8);
    std::filesystem::remove_all(tempDir, ec);

    // Mosaic crop-parity regression: flattening a sub-region cutting mid-block through
    // a MosaicAnnotation must match flattening the full frame and cropping that sub-region.
    pikasnip::capture::CapturedFrame stripeFrame;
    stripeFrame.virtualBounds = pikasnip::platform::RectI{0, 0, 64, 64};
    stripeFrame.width = 64;
    stripeFrame.height = 64;
    stripeFrame.strideBytes = 64 * 4;
    stripeFrame.bgraPixels.resize(static_cast<size_t>(stripeFrame.strideBytes) * 64, 0);
    for (int32_t y = 0; y < 64; ++y) {
        uint8_t* row = stripeFrame.bgraPixels.data() + static_cast<size_t>(y) * stripeFrame.strideBytes;
        for (int32_t x = 0; x < 64; ++x) {
            row[x * 4 + 0] = static_cast<uint8_t>((x * 7 + y * 3) & 0xFF);
            row[x * 4 + 1] = static_cast<uint8_t>((x * 13 + y * 5) & 0xFF);
            row[x * 4 + 2] = static_cast<uint8_t>((x * 19 + y * 11) & 0xFF);
            row[x * 4 + 3] = 255;
        }
    }

    pikasnip::annotations::AnnotationSession mosaicSession;
    pikasnip::annotations::MosaicAnnotation parityMosaic;
    parityMosaic.bounds = {10.0f, 10.0f, 50.0f, 50.0f};
    parityMosaic.blockSize = 10;
    mosaicSession.Add(parityMosaic);

    const pikasnip::platform::RectI subRegion{15, 15, 45, 45};
    pikasnip::capture::CapturedFrame directSub;
    pikasnip::capture::CapturedFrame fullFlat;
    EXPECT_TRUE(pikasnip::annotations::AnnotationRenderer::FlattenRegion(
        d2d, stripeFrame, subRegion, mosaicSession, directSub));
    EXPECT_TRUE(pikasnip::annotations::AnnotationRenderer::FlattenRegion(
        d2d, stripeFrame, stripeFrame.virtualBounds, mosaicSession, fullFlat));
    const pikasnip::capture::CapturedFrame croppedFull = fullFlat.CropBgra(subRegion);
    EXPECT_TRUE(directSub.isValid());
    EXPECT_TRUE(croppedFull.isValid());
    EXPECT_EQ(directSub.bgraPixels, croppedFull.bgraPixels);
}

void TestOverlayWindowLifecycle() {
    pikasnip::platform::D2dContext d2d;
    EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    pikasnip::capture::CapturedFrame frame;
    frame.virtualBounds = pikasnip::platform::RectI{0, 0, 800, 600};
    frame.width = 800;
    frame.height = 600;
    frame.strideBytes = 800 * 4;
    frame.bgraPixels.assign(static_cast<size_t>(frame.strideBytes) * 600, 0xFF);
    pikasnip::capture::MonitorInfo mon;
    mon.bounds = frame.virtualBounds;
    mon.dpi = 96;
    mon.isPrimary = true;
    frame.monitors.push_back(mon);

    pikasnip::capture::WindowDetector detector;
    pikasnip::overlay::OverlayWindow overlay(d2d);
    bool closedCalled = false;
    bool copiedFlag = true;

    const bool shown = overlay.Show(
        ::GetModuleHandleW(nullptr),
        std::move(frame),
        std::move(detector),
        [&closedCalled, &copiedFlag](bool copied) {
            closedCalled = true;
            copiedFlag = copied;
        });
    EXPECT_TRUE(shown);
    EXPECT_TRUE(overlay.isOpen());
    EXPECT_TRUE(overlay.hwnd() != nullptr);

    // Real Win32 drag sequence: WM_LBUTTONDOWN -> WM_MOUSEMOVE -> WM_LBUTTONUP
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(40, 50));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(40, 50));
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(overlay.controller().selectedRect(), (pikasnip::platform::RectI{10, 10, 40, 50}));

    // Dragging inside {10,10,40,50} moves the rect without committing/closing
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(25, 25));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(35, 35));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(35, 35));
    EXPECT_TRUE(overlay.isOpen());
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(overlay.controller().selectedRect(), (pikasnip::platform::RectI{20, 20, 50, 60}));

    // Unexpected WM_CAPTURECHANGED mid-drag cancels DraggingNew back to HoverWindow
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1, 1));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(15, 15));
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::DraggingNew);
    ::SendMessageW(overlay.hwnd(), WM_CAPTURECHANGED, 0, 0);
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::HoverWindow);
    EXPECT_TRUE(overlay.controller().selectedRect().isEmpty());

    // Re-select {10,10,40,50}, then single-click inside (25,25) commits & closes overlay
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(40, 50));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(40, 50));
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::Selected);

    // Round 2 regression: inside the selection, drag +20px out and back to the
    // press point over real Win32 messages. Must not commit or close the overlay.
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(25, 25));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(45, 25));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(25, 25));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(25, 25));
    EXPECT_TRUE(overlay.isOpen());
    EXPECT_TRUE(overlay.hwnd() != nullptr);
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(overlay.controller().selectedRect(), (pikasnip::platform::RectI{10, 10, 40, 50}));

    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(25, 25));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(25, 25));
    EXPECT_TRUE(!overlay.isOpen());
    EXPECT_TRUE(overlay.hwnd() == nullptr);
    EXPECT_TRUE(closedCalled);
    (void)copiedFlag;
}

void TestFloatingToolbarLayoutAndHitTest() {
    pikasnip::overlay::FloatingToolbar toolbar;
    const pikasnip::platform::RectI hdMon{0, 0, 1920, 1080};

    // Normal placement below selection
    const pikasnip::platform::RectI selMid{200, 200, 900, 600};
    toolbar.ComputeLayout(selMid, hdMon, 96);
    EXPECT_TRUE(!toolbar.bounds().isEmpty());
    EXPECT_TRUE(toolbar.bounds().top >= selMid.bottom);
    EXPECT_TRUE(toolbar.bounds().right <= hdMon.right);
    EXPECT_TRUE(toolbar.bounds().bottom <= hdMon.bottom);

    const auto rectBtn = toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::ToolRectangle);
    EXPECT_TRUE(!rectBtn.isEmpty());
    const auto hitRect = toolbar.HitTest(rectBtn.left + rectBtn.width() / 2, rectBtn.top + rectBtn.height() / 2);
    EXPECT_EQ(hitRect.kind, pikasnip::overlay::ToolbarActionKind::ToolRectangle);
    EXPECT_EQ(hitRect.tool, pikasnip::annotations::AnnotationTool::Rectangle);
    EXPECT_EQ(toolbar.HitTest(-10, -10).kind, pikasnip::overlay::ToolbarActionKind::None);

    // Bottom edge overflow flips above selection
    const pikasnip::platform::RectI selBottom{200, 900, 900, 1070};
    toolbar.ComputeLayout(selBottom, hdMon, 96);
    EXPECT_TRUE(toolbar.bounds().bottom <= selBottom.top);
    EXPECT_TRUE(toolbar.bounds().top >= hdMon.top);

    // Full-screen selection clamps inside monitor bounds
    toolbar.ComputeLayout(hdMon, hdMon, 96);
    EXPECT_TRUE(toolbar.bounds().left >= hdMon.left);
    EXPECT_TRUE(toolbar.bounds().top >= hdMon.top);
    EXPECT_TRUE(toolbar.bounds().right <= hdMon.right);
    EXPECT_TRUE(toolbar.bounds().bottom <= hdMon.bottom);

    // Regression: with a full-screen selection the toolbar lands in the bottom strip, exactly where the
    // pre-selection bottom hint bar used to render. The hint bar must therefore be hidden in Selected mode.
    const float oldHintBarTop = static_cast<float>(hdMon.bottom) - 28.0f - 18.0f;
    EXPECT_TRUE(static_cast<float>(toolbar.bounds().top) < oldHintBarTop + 28.0f);
    EXPECT_TRUE(static_cast<float>(toolbar.bounds().bottom) > oldHintBarTop);

    // Narrow 200x200 monitor wraps cleanly so all buttons stay inside bounds
    const pikasnip::platform::RectI tinyMon{0, 0, 200, 200};
    toolbar.ComputeLayout(pikasnip::platform::RectI{20, 20, 180, 180}, tinyMon, 96);
    EXPECT_TRUE(toolbar.bounds().left >= tinyMon.left);
    EXPECT_TRUE(toolbar.bounds().right <= tinyMon.right);
    EXPECT_TRUE(toolbar.bounds().top >= tinyMon.top);
    EXPECT_TRUE(toolbar.bounds().bottom <= tinyMon.bottom);
    EXPECT_TRUE(!toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Undo).isEmpty());
    EXPECT_TRUE(!toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Redo).isEmpty());
    EXPECT_TRUE(!toolbar.GetButtonRect(pikasnip::overlay::ToolbarActionKind::Cancel).isEmpty());
}

void TestOverlayAnnotationWin32Flow() {
    pikasnip::platform::D2dContext d2d;
    EXPECT_TRUE(SUCCEEDED(d2d.Initialize()));

    pikasnip::capture::CapturedFrame frame;
    frame.virtualBounds = pikasnip::platform::RectI{0, 0, 200, 200};
    frame.width = 200;
    frame.height = 200;
    frame.strideBytes = 200 * 4;
    frame.bgraPixels.assign(static_cast<size_t>(frame.strideBytes) * 200, 0xFF);
    pikasnip::capture::MonitorInfo mon;
    mon.bounds = frame.virtualBounds;
    mon.dpi = 96;
    mon.isPrimary = true;
    frame.monitors.push_back(mon);

    pikasnip::capture::WindowDetector detector;
    pikasnip::overlay::OverlayWindow overlay(d2d);
    bool closedCalled = false;
    bool copiedFlag = true;

    EXPECT_TRUE(overlay.Show(
        ::GetModuleHandleW(nullptr),
        std::move(frame),
        std::move(detector),
        [&closedCalled, &copiedFlag](bool copied) {
            closedCalled = true;
            copiedFlag = copied;
        }));

    auto clickPoint = [&](int32_t x, int32_t y) {
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    auto clickToolbar = [&](pikasnip::overlay::ToolbarActionKind kind) {
        const auto r = overlay.toolbar().GetButtonRect(kind);
        EXPECT_TRUE(!r.isEmpty());
        clickPoint(r.left + r.width() / 2, r.top + r.height() / 2);
    };

    // Drag-select {20, 20, 180, 180}
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(180, 180));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(180, 180));
    EXPECT_EQ(overlay.controller().mode(), pikasnip::overlay::SelectionMode::Selected);
    EXPECT_EQ(overlay.controller().selectedRect(), (pikasnip::platform::RectI{20, 20, 180, 180}));

    // Click Rectangle tool on toolbar
    clickToolbar(pikasnip::overlay::ToolbarActionKind::ToolRectangle);

    // Draw rectangle {40, 40} -> {120, 120}
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 40));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 120));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(120, 120));
    EXPECT_EQ(overlay.annotations().items().size(), static_cast<size_t>(1));

    // Toolbar Undo -> 0, Redo -> 1
    clickToolbar(pikasnip::overlay::ToolbarActionKind::Undo);
    EXPECT_EQ(overlay.annotations().items().size(), static_cast<size_t>(0));
    clickToolbar(pikasnip::overlay::ToolbarActionKind::Redo);
    EXPECT_EQ(overlay.annotations().items().size(), static_cast<size_t>(1));

    // Regression 1: mid-stroke cancel via WM_RBUTTONUP releases mouse capture & discards draft
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(45, 45));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(90, 60));
    ::SendMessageW(overlay.hwnd(), WM_RBUTTONUP, 0, MAKELPARAM(90, 60));
    EXPECT_EQ(overlay.annotations().items().size(), static_cast<size_t>(1));
    EXPECT_TRUE(::GetCapture() != overlay.hwnd());

    // Regression 2: toggle tool back to None, move selection (+20, +10), and verify
    // existing annotation stays anchored to frozen screen coordinates (not translated).
    clickToolbar(pikasnip::overlay::ToolbarActionKind::ToolRectangle);
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 30));
    ::SendMessageW(overlay.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(50, 40));
    ::SendMessageW(overlay.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(50, 40));
    EXPECT_EQ(overlay.controller().selectedRect(), (pikasnip::platform::RectI{40, 30, 200, 190}));
    const auto* anchored = std::get_if<pikasnip::annotations::RectAnnotation>(&overlay.annotations().items()[0]);
    EXPECT_TRUE(anchored != nullptr);
    EXPECT_TRUE(anchored->bounds.left == 40.0f && anchored->bounds.top == 40.0f);
    EXPECT_TRUE(anchored->bounds.right == 120.0f && anchored->bounds.bottom == 120.0f);

    // Regression 3: Failed save preserves overlay, crop, and annotations without closing
    bool saveInvoked = false;
    overlay.SetSaveFileCallback([&saveInvoked](
                                    IWICImagingFactory* /*wic*/,
                                    const uint8_t* /*bgra*/,
                                    int32_t /*w*/,
                                    int32_t /*h*/,
                                    int32_t /*stride*/) -> std::filesystem::path {
        saveInvoked = true;
        return {};
    });
    clickToolbar(pikasnip::overlay::ToolbarActionKind::Save);
    EXPECT_TRUE(saveInvoked);
    EXPECT_TRUE(overlay.isOpen());
    EXPECT_EQ(overlay.annotations().items().size(), static_cast<size_t>(1));

    // Toolbar Cancel -> closes cleanly
    clickToolbar(pikasnip::overlay::ToolbarActionKind::Cancel);
    EXPECT_TRUE(!overlay.isOpen());
    EXPECT_TRUE(closedCalled);
    EXPECT_TRUE(!copiedFlag);
}

} // namespace

int RunMilestone3Tests();

int main() {
    const pikasnip::platform::ComInitializer com;
    EXPECT_TRUE(com.ok());

    TestAppEnvironment();
    TestSettingsStoreRoundTrip();
    TestClipboardDibV5AndPng();
    TestSelectionController();
    TestOverlayWindowLifecycle();
    TestFloatingToolbarLayoutAndHitTest();
    TestOverlayAnnotationWin32Flow();
    TestCaptureEngineAndWindowDetector();
    TestAnnotationSessionUndoRedo();
    TestPngSaveAndAnnotationFlatten();
    gFailures += RunMilestone3Tests();

    if (gFailures != 0) {
        std::cerr << gFailures << " test assertion(s) failed.\n";
        return 1;
    }
    std::cout << "All Milestone 1, 2, and 3 tests passed.\n";
    return 0;
}
