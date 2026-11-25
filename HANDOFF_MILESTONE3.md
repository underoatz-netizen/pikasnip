# PikaSnip — Milestone 3 Handoff Document

**Project:** `PikaSnip` (OZ Lab — Pure Native C++20 Windows 10/11 x64 Utility)  
**Workspace:** `E:\Projects\Pika Suite\PikaSnip`  
**Status at Handoff:** Milestone 1 & Milestone 2 **COMPLETE & APPROVED** (Google Gemini 4 Neon High + OpenAI GPT-6.2 Sol)  
**Next Milestone:** **Milestone 3 — Pin Window (`Alt+P` / `Ctrl+P`) + Offline WinRT OCR (`Alt+T` / `Ctrl+T`) + Final Dual-Mode Packaging & Polish**

---

## 1. Verified Baseline State & Toolchain

- **Compiler / Generator:** Visual Studio 18 2026 BuildTools (`x64`), C++20, Windows SDK `10.0.26100.0`
- **CMake Binary:** `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- **Build Command:**
  ```powershell
  & "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "E:\Projects\Pika Suite\PikaSnip\build\release" --config Release
  ```
- **Test Binary:** `E:\Projects\Pika Suite\PikaSnip\build\release\bin\Release\pikasnip_tests.exe`
- **Packaging Scripts:**
  - `powershell -ExecutionPolicy Bypass -File "E:\Projects\Pika Suite\PikaSnip\scripts\build-msix.ps1"`
  - `powershell -ExecutionPolicy Bypass -File "E:\Projects\Pika Suite\PikaSnip\scripts\build-portable.ps1"`
- **Current Verified Baseline Metrics (Build `3e10a817...`):**
  - `PikaSnip.exe`: `495,616` bytes (~`484.0 KB`), Static CRT (`/MT`), 44 inbox Windows OS DLLs/API sets, **0 dynamic CRT / 0 third-party bloat DLLs**
  - GUI Standby RAM: **`8.71 MB`** working set (`< 15 MB` target)
  - Warm DXGI Capture Latency (`WinSta0\Default` verified): **`2.64 ms`** (`< 50 ms` target)
  - Unit & Integration Tests (`pikasnip_tests.exe`): **10/10 passed** when run on interactive desktop (`9/10` inside isolated `WinSta0\exebox-*` runner desktop where `BitBlt`/`DuplicateOutput` are denied by OS)
  - `dist/SHA256SUMS.txt` verified byte-for-byte:
    - `PikaSnip.exe`: `3e10a817728d9b477f50daecaec2d0367503f65d00c3f8de1d12301be24763a0`
    - `PikaSnip_0.1.0.0_x64.msix`: `92bdabd97741d045efa916f66596a777e6129bf8e7110a89b91419ca2960b7d3`
    - `PikaSnip_0.1.0_Portable_x64.zip`: `ed6e06beb1a9e46303dbbe0e9cfdf217e1e6035d50832bb4ed9206451a21d238`

---

## 2. Current Module Map

| Target / Folder | Purpose | Status |
| :--- | :--- | :--- |
| `src/platform/` (`pikasnip_platform`) | `AppEnvironment::IsPackaged()`, `SettingsStore`, `StartupManager` (WinRT `StartupTask` vs `HKCU Run`), `ComInitializer`, `Dpi`, `D2dContext`, `Clipboard` (`CF_DIBV5` + PNG + `SavePngToFile` + `SaveTimestampedPngToDefaultDir`) | Complete (needs `ReadImageFromClipboard` for `Alt+P`) |
| `src/capture/` (`pikasnip_capture`) | `CapturedFrame`, `DxgiDuplicator` (cached + `WarmUp` + `TrimStandbyResources`), `GdiCaptureFallback`, `CaptureEngine`, `WindowDetector` | Complete |
| `src/annotations/` (`pikasnip_annotations`) | `AnnotationTypes`, `AnnotationSession` (Undo/Redo + `TranslateAll`), `AnnotationRenderer` (8 tools + 2-pass Mosaic/Vector WYSIWYG + `FlattenRegion` via WIC) | Complete |
| `src/overlay/` (`pikasnip_overlay`) | `SelectionController` (hover 1-click + drag + 1-click commit + sticky drag threshold), `MagnifierHud`, `FloatingToolbar`, `OverlayWindow` (8 tools, Thai/EN text input, `Ctrl+Z/Y/S/C`, error toast) | Complete (needs `Pin` & `OCR` toolbar buttons + `Ctrl+P` / `Ctrl+T` / Quick-OCR mode callbacks) |
| `src/pin/` (`pikasnip_pin`) | `PinWindow` (`include/pikasnip/pin/PinWindow.h`, `src/PinWindow.cpp`, `CMakeLists.txt`) | **Stub — Implement in Milestone 3** |
| `src/ocr/` (`pikasnip_ocr`) | `OcrEngine` (`include/pikasnip/ocr/OcrEngine.h`, `src/OcrEngine.cpp`, `CMakeLists.txt`) | **Stub — Implement in Milestone 3** |
| `src/app/` (`PikaSnip`) | `main.cpp`, `HotkeyManager` (`PrintScreen` via `WH_KEYBOARD_LL`, `Alt+A`, `Alt+T`, `Alt+P`), `TrayHostWindow` (currently `kHotkeyOcr` & `kHotkeyPin` are no-op placeholders at `TrayHostWindow.cpp:272-274`) | Ready to wire `Alt+T` & `Alt+P` + `PinManager` + toast |
| `packaging/msix/` & `scripts/` | `AppxManifest.xml` (`runFullTrust` + `PikaSnipStartupTask`), `priconfig.xml`, Store PNG assets, `build-msix.ps1`, `build-portable.ps1` | Complete |

---

## 3. Critical Engineering Invariants (DO NOT REGRESS)

1. **`WM_NCCREATE` / `WM_NCDESTROY` `HWND` Handling:** Always pass `HWND hwnd` into `HandleMessage(HWND hwnd, ...)`, set `self->hwnd_ = hwnd` on `WM_NCCREATE`, pass `hwnd` to `DefWindowProcW`/`SetWindowLongPtrW`, and clear `hwnd_ = nullptr` inside `WM_NCDESTROY` (never before `DestroyWindow`). Apply this exact pattern to `PinWindow` and any toast window!
2. **C++/WinRT STA Deadlock Rule:** The UI thread uses `RoInitialize(RO_INIT_SINGLETHREADED)` (STA). **NEVER** call `.get()` on any WinRT `IAsyncOperation` (`Windows.Media.Ocr`, `StartupTask`, etc.) on the UI thread! Always run WinRT async calls on a short-lived MTA `std::thread` with RAII `winrt::init_apartment(winrt::apartment_type::multi_threaded)` / `winrt::uninit_apartment()` (see `src/platform/src/StartupManager.cpp:20-48` for the exact pattern).
3. **`OverlayWindow` Mouse Capture & 1-Click UX:** Keep `OnLeftButtonUp` before `ReleaseCapture()` under `handlingButtonUp_`, keep `dragMovedBeyondThreshold_` sticky, keep `HoverWindow` 1-click instant capture, keep annotations anchored in virtual-screen coordinates, and keep the bottom hint bar hidden in `SelectionMode::Selected`.
4. **Mosaic & Save Parity:** Keep `ApplyMosaicsToBgra` stepping on `sourceFrame->virtualBounds` for pixel-identical cropped mosaic output, and keep `OverlayWindow` open with error feedback if a file save fails.
5. **Before Building / Linking `PikaSnip.exe`:** Always check/stop any running `build\release\bin\Release\PikaSnip.exe` process first so the linker doesn't hit `LNK1104`.
6. **Zero-Bloat Standard (`ozlab-core`):** Pure Native C++20, Static CRT (`/MT`), no Electron/WebView2/Qt/.NET/third-party bloat, keep Standby RAM `< 15 MB` by releasing transient buffers and trimming working set (`SetProcessWorkingSetSize(-1, -1)`) when windows close.

---

## 4. Milestone 3 Implementation Scope

### 4.1 Clipboard Image Readback (`src/platform/Clipboard.h/.cpp`)
Needed by `Alt+P` (Pin Clipboard):
- Add `[[nodiscard]] bool ReadImageFromClipboard(HWND owner, IWICImagingFactory* wic, pikasnip::capture::CapturedFrame& outFrame)` (or return raw BGRA + dimensions in a platform struct `ClipboardImageData` to avoid circular dependency between `pikasnip_platform` and `pikasnip_capture`!).
  - **Important dependency check:** `pikasnip_capture` depends on `pikasnip_platform`, so `pikasnip_platform` MUST NOT depend on `pikasnip_capture`. Return a small `struct ClipboardImage { int32_t width; int32_t height; int32_t strideBytes; std::vector<uint8_t> bgraPixels; }` from `Clipboard.h`.
  - Support registered `"PNG"` format first (decode via WIC `IWICBitmapDecoder` -> `GUID_WICPixelFormat32bppBGRA`), falling back to `CF_DIBV5` / `CF_DIB` (both bottom-up positive `biHeight` and top-down negative `biHeight`, `BI_RGB` / `BI_BITFIELDS` 32bpp & 24bpp), and `CF_BITMAP` if needed. Normalize alpha to `255` where opaque DIBs lack alpha.

### 4.2 Borderless Topmost Pin Window (`src/pin/`)
Implement `PinWindow` + multi-window `PinManager` (so users can pin 1 or more screenshots simultaneously):
- **Window Style:** `WS_POPUP`, `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED`, `CS_DBLCLKS`.
- **Rendering:** Direct2D (`ID2D1HwndRenderTarget` + `ID2D1Bitmap` sized to image) with crisp 1px dark/accent border (`#3A3A3A` / `#38BDF8` on hover/focus) and `SetLayeredWindowAttributes(hwnd, 0, alphaByte, LWA_ALPHA)` for smooth opacity (`0.20f`–`1.00f`).
- **Interactions:**
  - **Drag anywhere (`WM_LBUTTONDOWN` / `WM_MOUSEMOVE`):** Move window smoothly across monitors (supporting mixed-DPI monitors cleanly!).
  - **Mouse wheel (`WM_MOUSEWHEEL`):** Zoom `0.25f`–`4.00f` (centered around cursor or window center, resizing window bounds cleanly).
  - **`Ctrl + Wheel`:** Adjust opacity `20%`–`100%` (`±10%` per notch) via `SetLayeredWindowAttributes`.
  - **Double-click:** Copy pinned image back to clipboard (`Clipboard::CopyImageToClipboard`).
  - **Right-click / Context Menu:** Dark-friendly Win32 popup menu (`Copy Image`, `Save PNG...`, `Reset Zoom (100%)`, `Opacity 100% / 75% / 50%`, `Close (Esc)`), OR direct middle/right-click close option? Spec says: "`Esc`/Right-click = close" — let's close immediately on `Esc` or `WM_RBUTTONUP` (and optionally offer `Ctrl+S` to save PNG and `Ctrl+C` / double-click to copy, keeping `Esc` / Right-click instant-close exact to the spec!).
- **Memory Discipline:** On `Close()` / `WM_NCDESTROY`, release Direct2D bitmap/render target + `CapturedFrame` pixel buffer and notify `PinManager` to clean up closed entries and trim working set.

### 4.3 Offline WinRT OCR (`src/ocr/`)
Implement `OcrEngine` using C++/WinRT `winrt::Windows::Media::Ocr::OcrEngine` & `winrt::Windows::Graphics::Imaging::SoftwareBitmap`:
- **Headers:** `<winrt/Windows.Foundation.h>`, `<winrt/Windows.Foundation.Collections.h>`, `<winrt/Windows.Globalization.h>`, `<winrt/Windows.Graphics.Imaging.h>`, `<winrt/Windows.Media.Ocr.h>`, `<winrt/Windows.Storage.Streams.h>` (or `IBuffer` via `CryptographicBuffer::CreateFromByteArray` / `Windows::Storage::Streams::DataWriter` / `SoftwareBitmap::CreateCopyFromBuffer`).
- **MTA Execution:** Run all WinRT OCR creation & `RecognizeAsync(...).get()` strictly inside a short-lived MTA `std::thread` (`ScopedMtaApartment`)!
- **Language Handling:**
  - Try `OcrEngine::TryCreateFromUserProfileLanguages()`.
  - Fallback: check `OcrEngine::AvailableRecognizerLanguages()` for Thai (`th`) / English (`en-US`, `en`) if user profile returns null.
  - If no OCR language packs are installed, return `OcrResult{false, L"", L"No Windows OCR language pack installed"}` cleanly without crashing.
- **Image Prep:**
  - Note `Windows.Media.Ocr` fails on very tiny images (`< 40px` width/height sometimes returns empty) or images exceeding `OcrEngine::MaxImageDimension()`; handle small/oversized bounds safely (e.g. pad/check dimensions cleanly).
  - Join `result.Lines()` with `L"\r\n"` (or preserve line order).

### 4.4 Subtle Toast Notification & Wiring (`src/overlay/` & `src/app/`)
- **Toast Window (`ToastWindow` in `src/app/` or `src/platform/`):**
  - Lightweight borderless `WS_POPUP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT` dark-charcoal pill (`#1E1E1E`, 1px `#38BDF8` or `#3A3A3A` border, `#F9FAFB` text via Direct2D/DirectWrite) at bottom-right of primary/cursor monitor, auto-closing after ~2000ms via `SetTimer` (e.g. `"Copied OCR text (142 chars)"`, `"No text detected"`, `"Pinned to screen"`, `"No image in clipboard"`).
- **`OverlayWindow` (`Ctrl+P`, `Ctrl+T`, Quick-OCR Mode, Toolbar Buttons):**
  - Add `ToolbarActionKind::Pin` and `ToolbarActionKind::Ocr` (with crisp Direct2D vector Pin & OCR/Text-Scan icons) to `FloatingToolbar`, plus callbacks `OnPinRequested` and `OnOcrRequested` (or mode `OverlayMode::Standard` vs `OverlayMode::QuickOcr`) on `OverlayWindow`.
  - **`Ctrl+P` / Toolbar Pin:** Flattens region + annotations (`AnnotationRenderer::FlattenRegion`), passes `CapturedFrame` to `PinManager`, closes overlay.
  - **`Ctrl+T` / Toolbar OCR:** Flattens region + annotations, runs `OcrEngine::RecognizeFrame`, copies text to clipboard if non-empty, closes overlay, shows `ToastWindow`!
- **`TrayHostWindow` Hotkeys (`Alt+P` & `Alt+T`):**
  - `Alt+P` (`kHotkeyPin`): Calls `Clipboard::ReadImageFromClipboard`, creates `PinWindow` via `PinManager` (or shows toast if clipboard has no image).
  - `Alt+T` (`kHotkeyOcr`): Captures screen (`CaptureAll` + `WindowDetector`), opens `OverlayWindow` in **Quick OCR mode** (selecting/1-clicking a window or region immediately runs OCR, copies text, closes overlay, and shows `ToastWindow`).
  - Also add `"Pin Clipboard (Alt+P)"` and `"Quick OCR (Alt+T)"` items to the System Tray right-click menu!

### 4.5 Tests & Gate Checks
- Extend `tests/` (add `tests/milestone3_tests.cpp` or add to `pikasnip_tests.exe`):
  - `Clipboard` PNG & `CF_DIBV5` round-trip (`BuildDibV5PayloadFromBgra` / `EncodePngBytesFromBgra` -> decode helper).
  - `PinWindow` lifecycle (`Create`, zoom clamping `0.25..4.0`, opacity clamping `0.20..1.0`, `Close`, zero leaks).
  - `OcrEngine` test: render high-contrast `"PIKASNIP OCR 12345"` onto a synthetic `320x100` BGRA frame via DirectWrite + WIC and run `OcrEngine::RecognizeFrame` (when `IsSupported()` is true, assert `"PIKASNIP"` / `"12345"` recognized!).
- Build Release (`/MT`, 0 warnings), verify PE imports (0 dynamic CRT/bloat DLLs), Standby RAM `< 15 MB`, repackage `dist/` + `SHA256SUMS.txt`, and run full OZ Lab Gate 1 (`jev_evidence_triage`) + Gate 2 Two-Round Cross-Vendor Review (`gemini-4-neon-high` -> `openai_final_review` on `gpt-6.2-sol`).

---

## 5. Recommended Starter Prompt for New Session

> อ่านไฟล์ `E:\Projects\Pika Suite\PikaSnip\HANDOFF_MILESTONE3.md` แล้วเริ่มวางแผนและลงมือทำ **Milestone 3 (Pin Window `Alt+P`/`Ctrl+P` + Offline WinRT OCR `Alt+T`/`Ctrl+T` + Toast Notification)** ต่อได้เลยครับ โดยรักษามาตรฐาน OZ Lab (`ozlab-core`, Dual-Mode MSIX/Portable, Static CRT `/MT`, 2-Round Review Gate)
