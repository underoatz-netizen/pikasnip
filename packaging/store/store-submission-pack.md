# PikaSnip v0.1.0 — Microsoft Store Submission Pack (Track A)

Ready-to-paste reference pack for Partner Center submission.  
All claims below are verified directly against `README.md`, `packaging/msix/AppxManifest.xml`, and `src/`.

---

## 1. Pre-Upload Guard & Package Verification

> **CRITICAL UPLOAD GUARD:**  
> Upload **ONLY** `dist\PikaSnip_0.1.0.0_x64_Store.msix`.  
> **NEVER** upload `dist\PikaSnip_0.1.0.0_x64.msix` (that is the `CN=OZ Lab` sideload build and Partner Center will reject it).

### Store Identity Summary

| Field | Value |
|---|---|
| `Package/Identity/Name` | `OZLab.PikaSnip` |
| `Package/Identity/Publisher` | `CN=32712C20-BC4B-401E-88B4-4958AC476B66` |
| `Package/Identity/PublisherDisplayName` | `OZ Lab` |
| Package Family Name (PFN) | `OZLab.PikaSnip_81we41zcpabw6` |
| `Version` | `0.1.0.0` (4th part = `0` required for Store) |
| Architecture | `x64` (`Windows.Desktop`, Min `10.0.19041.0`) |
| Store Product ID | `9PG899T27FBZ` |

> **Pre-Live Link Rule:**  
> Do **NOT** publish `9PG899T27FBZ` store links (`apps.microsoft.com/detail/9PG899T27FBZ` or `ms-windows-store://pdp/?productid=9PG899T27FBZ`) in `README.md` or on `ozlab.pages.dev` until the Store listing is live (they return HTTP 404 before publication).

### Verify Store Package Hash Before Upload

Run in PowerShell from `E:\Projects\Pika Suite\PikaSnip`:

```powershell
$expected = (Get-Content .\dist\PikaSnip_0.1.0.0_x64_Store.msix.sha256 -Raw).Trim().Split()[0].ToUpperInvariant()
$actual   = (Get-FileHash .\dist\PikaSnip_0.1.0.0_x64_Store.msix -Algorithm SHA256).Hash.ToUpperInvariant()
[pscustomobject]@{ Expected = $expected; Actual = $actual; Match = ($expected -eq $actual) }
```

Also upload Store logo asset: `packaging/store/StoreLogo-300x300.png` (300×300 PNG).

---

## 2. P6 — Store Listing Copy

### English (`en-US`)

- **App name:** `PikaSnip`
- **Short description (80 / 100 chars):**  
  `Zero-bloat native Win32 screenshot, pin, and 100% offline Thai/English OCR tool.`
- **Full description:**

```text
PikaSnip is a fast, lightweight, 100% offline screenshot, floating pin, and Thai/English OCR utility built natively in C++20 and Win32/Direct2D for Windows 10 and Windows 11 (x64).

Designed with a Zero-Bloat architecture, PikaSnip runs silently in the system tray with under 15 MB idle standby RAM (measured at 10.79–11.33 MB on a clean Windows 11 build 26100 system) and zero background bloat—no Electron, no WebView2, no Qt, no .NET runtime, and zero telemetry. Its 754,688-byte executable links the static CRT (/MT), and Microsoft ONNX Runtime (v1.32.0) is loaded lazily only when you trigger OCR.

Key Capabilities:
• Instant Region & Window Capture: Press PrintScreen or Alt+A (or click the tray icon) to capture across multi-monitor and mixed-DPI displays. Hover to auto-detect windows or drag a custom region with 8 resize handles.
• Live Pixel Magnifier & Color Picker: Inspect exact pixel coordinates and RGB/HEX values while selecting; press C to copy the HEX color code directly to your clipboard.
• Built-in Vector & Mosaic Annotations: Mark up captures directly on the overlay using Rectangle, Ellipse, Arrow, Freehand Pen, Highlighter, Mosaic pixelation, Step Counter badges, and Text—with 6 colors, 3 stroke weights, and full Undo/Redo (Ctrl+Z / Ctrl+Y).
• Always-on-Top Floating Pins: Pin any capture (Ctrl+P) or clipboard image (Alt+P) on screen as a movable reference window. Scroll to zoom (25%–400%), Ctrl+Scroll to adjust opacity (20%–100%), press 0 to reset, or press E / Space to annotate directly on the pin.
• 100% Offline Thai & English OCR: Press Alt+T for Quick OCR or click OCR on any capture/pin to extract Thai and English text locally using bundled OpenThai 2.1 models and ONNX Runtime, copying clean Unicode text straight to your clipboard.
• Delayed Capture & Quick Save: Trigger 3-second or 5-second delayed captures from the tray menu, and press Ctrl+S to save timestamped PNGs to Pictures\PikaSnip.
• Strictly Private & Offline: No accounts, no cloud uploads, no analytics, and no network connections.
```

- **Features (7 / 7):**
  1. `Native C++20 Win32/Direct2D capture across multi-monitor and mixed-DPI setups`
  2. `Under 15 MB idle standby RAM; ONNX Runtime lazy-loaded only on OCR`
  3. `100% offline Thai & English OCR (OpenThai 2.1 + ONNX Runtime)`
  4. `Live pixel magnifier HUD with one-key HEX color copy (C)`
  5. `Annotations: Rectangle, Ellipse, Arrow, Pen, Highlighter, Mosaic, Steps, Text`
  6. `Always-on-top floating pins with zoom, opacity, and in-place editing`
  7. `Zero telemetry, zero network access, and opt-in startup task (off by default)`

- **Search terms (7 / 7):**
  `screenshot`, `snipping tool`, `ocr`, `thai ocr`, `pin image`, `screen capture`, `annotation`

---

### Thai (`th-TH`)

- **ชื่อแอป (App name):** `PikaSnip`
- **คำอธิบายสั้น (Short description, 79 / 100 chars):**  
  `เครื่องมือจับภาพหน้าจอ ปักหมุด และ OCR ภาษาไทย/อังกฤษแบบออฟไลน์ 100% น้ำหนักเบา`
- **คำอธิบายแบบเต็ม (Full description):**

```text
PikaSnip คือเครื่องมือจับภาพหน้าจอ ปักหมุดภาพลอยบนหน้าจอ และอ่านข้อความภาษาไทย/อังกฤษแบบออฟไลน์ 100% พัฒนาแบบ Native ด้วย C++20 และ Win32/Direct2D สำหรับ Windows 10 และ Windows 11 (x64)

ออกแบบตามแนวทาง Zero-Bloat ให้เปิดไว กินแรมต่ำขณะสแตนด์บายไม่ถึง 15 MB (วัดได้ 10.79–11.33 MB บนเครื่องทดสอบ Windows 11 build 26100) ไม่พ่วง Electron, WebView2, Qt หรือ .NET runtime และไม่มี telemetry ตัวโปรแกรม PikaSnip.exe ขนาด 754,688 ไบต์ใช้ static CRT (/MT) และจะโหลด Microsoft ONNX Runtime (v1.32.0) แบบ lazy เฉพาะเมื่อผู้ใช้สั่งรัน OCR เท่านั้น

ความสามารถหลัก:
• จับภาพหน้าต่างและพื้นที่อิสระทันที: กด PrintScreen หรือ Alt+A (หรือคลิกไอคอนใน System Tray) รองรับหลายจอและหลายระดับ DPI ตรวจจับหน้าต่างอัตโนมัติเมื่อชี้เมาส์ หรือลากเลือกพื้นที่พร้อมจุดปรับขนาด 8 ด้าน
• แว่นขยายพิกเซลและดูดสี HEX: แสดงขนาดพื้นที่และค่าสี RGB/HEX แบบเรียลไทม์ กด C เพื่อคัดลอกรหัสสี HEX ลงคลิปบอร์ดทันที
• เครื่องมือวาดและปิดบังข้อมูลในตัว: ครบทั้งกรอบสี่เหลี่ยม วงรี ลูกศร ปากกาอิสระ ปากกาเน้นข้อความ Mosaic เบลอแบบพิกเซล ตัวเลขลำดับขั้น (Step Counter) และข้อความ พร้อม 6 สี 3 ขนาดเส้น และ Undo/Redo (Ctrl+Z / Ctrl+Y)
• ปักหมุดภาพลอยทับหน้าต่างอื่น (Always-on-Top): ปักหมุดภาพที่จับได้ (Ctrl+P) หรือภาพจากคลิปบอร์ด (Alt+P) เลื่อนลูกกลิ้งเมาส์เพื่อซูม (25%–400%), กด Ctrl+ลูกกลิ้งเพื่อปรับความโปร่งใส (20%–100%), กด 0 เพื่อรีเซ็ต และกด E หรือ Space เพื่อแก้ไข/วาดเพิ่มบนภาพที่ปักหมุด
• OCR ภาษาไทยและอังกฤษออฟไลน์ 100%: กด Alt+T สำหรับ Quick OCR หรือกดปุ่ม OCR เพื่อสกัดข้อความไทย/อังกฤษในเครื่องด้วยโมเดล OpenThai 2.1 และ ONNX Runtime แล้วคัดลอกลงคลิปบอร์ดทันที
• จับภาพหน่วงเวลาและบันทึกไฟล์รวดเร็ว: รองรับหน่วงเวลา 3 วินาที / 5 วินาที จากเมนู Tray และกด Ctrl+S เพื่อบันทึก PNG ลงโฟลเดอร์ Pictures\PikaSnip
• ส่วนตัวสมบูรณ์แบบ: ทำงานในเครื่อง 100% ไม่ต่ออินเทอร์เน็ต ไม่อัปโหลดภาพหรือข้อความ
```

- **คุณสมบัติเด่น (Features, 7 / 7):**
  1. `จับภาพหน้าจอแบบ Native C++20/Direct2D รองรับหลายจอและต่าง DPI`
  2. `กินแรมขณะรอใช้งานต่ำกว่า 15 MB โหลด ONNX Runtime เฉพาะตอนใช้ OCR`
  3. `OCR ภาษาไทยและอังกฤษออฟไลน์ 100% (OpenThai 2.1 + ONNX Runtime)`
  4. `แว่นขยายพิกเซลแบบเรียลไทม์ กด C คัดลอกรหัสสี HEX ทันที`
  5. `เครื่องมือแก้ไขครบ: สี่เหลี่ยม วงรี ลูกศร ปากกา ไฮไลต์ Mosaic ตัวเลข ข้อความ`
  6. `ปักหมุดภาพลอย Always-on-Top ซูม ปรับความโปร่งใส และวาดทับได้ทันที`
  7. `ไม่ส่งข้อมูลออกนอกเครื่อง ไม่มีโฆษณา/ตัวติดตาม และปิดการเริ่มอัตโนมัติไว้เป็นค่าเริ่มต้น`

- **คำค้นหา (Search terms, 7 / 7):**
  `จับภาพหน้าจอ`, `แคปหน้าจอ`, `ocr ภาษาไทย`, `ปักหมุดรูป`, `screenshot`, `snip`, `oz lab`

---

## 3. P6 — Screenshot Shotlist (4 Screenshots, `1920×1080` or `1280×720` PNG)

Capture on a real interactive Windows 11 desktop (not headless sessions) at **1920×1080** or **1280×720** (`16:9` PNG):

1. **Shot 1 — Capture Overlay, Auto Window Detection & Live Pixel Magnifier**
   - **Scene:** Press `PrintScreen` / `Alt+A` over an app window and editor; hover or select a region so the `#38BDF8` border, `W x H` dimension badge, bottom hint bar, and live pixel magnifier HUD (showing RGB/HEX) are clearly visible.
   - **Caption (EN):** `Instant multi-monitor capture with window detection and live pixel magnifier.`
   - **Caption (TH):** `จับภาพหน้าจอทันที ตรวจจับหน้าต่างอัตโนมัติ พร้อมแว่นขยายและรหัสสี HEX`

2. **Shot 2 — Floating Annotation Toolbar on Captured Region**
   - **Scene:** Drag a region and use Arrow, Rectangle, Mosaic pixelation (e.g., hiding a sample token/line), Step Counter (`1`, `2`), and Text annotations with the floating bottom toolbar visible.
   - **Caption (EN):** `Annotate with shapes, arrows, mosaic blur, step counters, and text before copying.`
   - **Caption (TH):** `วาดกรอบ ลูกศร เบลอ Mosaic ตัวเลขลำดับ และข้อความได้ทันทีบนภาพที่จับ`

3. **Shot 3 — Always-on-Top Floating Pin Window + Pin Edit Toolbar**
   - **Scene:** Pin a capture (`Ctrl+P` or `Alt+P`) over a code editor or browser, press `E` to reveal the Pin Edit Toolbar, and scroll slightly so the crisp floating border and zoom HUD badge are visible.
   - **Caption (EN):** `Pin captures always-on-top with smooth zoom, opacity control, and in-place editing.`
   - **Caption (TH):** `ปักหมุดภาพลอยทับทุกหน้าต่าง ซูม ปรับความโปร่งใส และวาดแก้ไขเพิ่มได้ทันที`

4. **Shot 4 — Offline Thai/English OCR + System Tray Menu**
   - **Scene:** Trigger Quick OCR (`Alt+T`) on a mixed Thai/English snippet (showing the PikaSnip toast `"Copied OCR text (... chars)"` alongside pasted Thai/English text in Notepad), with the right-click System Tray menu visible (`Capture Now (PrtSc / Alt+A)`, `Quick OCR (Alt+T)`, `Pin Clipboard (Alt+P)`, `Delayed Capture (3s)`, `Delayed Capture (5s)`, `Run at Startup`).
   - **Caption (EN):** `100% offline Thai & English OCR copied straight to your clipboard with zero cloud calls.`
   - **Caption (TH):** `อ่านข้อความภาษาไทย/อังกฤษแบบออฟไลน์ 100% คัดลอกลงคลิปบอร์ดทันทีโดยไม่ต่อเน็ต`

---

## 4. P7 — Privacy Policy Deployment Note (`ozlab.pages.dev`)

1. Copy `packaging/store/privacy-pikasnip.html` into the website repository `E:\Projects\underoatz-netizen.github.io`:
   - Recommended path: `E:\Projects\underoatz-netizen.github.io\privacy\pikasnip\index.html` (resolves to `https://ozlab.pages.dev/privacy/pikasnip/`)  
   - Alternative path: `E:\Projects\underoatz-netizen.github.io\pikasnip-privacy.html` (resolves to `https://ozlab.pages.dev/pikasnip-privacy.html`)
2. Commit and push `underoatz-netizen.github.io` to deploy to `ozlab.pages.dev`.
3. Verify the live URL returns **HTTP 200** before pasting it into Partner Center:
   ```powershell
   Invoke-WebRequest -Uri "https://ozlab.pages.dev/privacy/pikasnip/" -Method Head | Select-Object StatusCode
   ```

---

## 5. P8 — Age Rating (IARC), Category & Pricing

- **Category:** `Utilities & tools`
- **Pricing:** `Free` (no in-app purchases, no trials, no ads)
- **Markets / Availability:** All available markets (default)
- **IARC Age Rating Questionnaire Guidance:**
  - App type / Category: `Utility, Productivity, or Other`
  - Violence / Fear / Sexuality / Drugs / Gambling / Crude Humor: **No** to all
  - User-generated content / online social interaction / sharing: **No** (local clipboard & local file save only)
  - Shares personal information / location with third parties: **No**
  - In-app purchases / Ads: **No**
  - Result: **3+ / Everyone (IARC)**

---

## 6. P9 — Export Compliance / Encryption

- **Uses / includes encryption?** Select **No** (or if prompted about standard OS APIs only: indicate that the app relies solely on standard Windows operating system APIs and performs **no** network communication, **no** TLS/HTTPS payload exchange, and **no** custom or third-party cryptography).
- **Justification note (if Partner Center asks for details):**
  - PikaSnip is a 100% offline Win32 desktop utility with zero network sockets, zero web requests, and zero telemetry.
  - Bundled `onnxruntime.dll` and OpenThai 2.1 `.onnx` models perform local matrix inference on screen pixel buffers only.

---

## 7. P10 — Notes for Certification (`runFullTrust` + `StartupTask`)

Copy-paste the text below into **Partner Center → Submission → Notes for certification**:

```text
PikaSnip v0.1.0 is a native C++20 Win32/Direct2D desktop screenshot, floating pin, and 100% offline Thai/English OCR utility by OZ Lab.

1. Why runFullTrust is required:
   - Multi-monitor and mixed-DPI desktop capture via DXGI Desktop Duplication with GDI fallback (requires full desktop window/surface access).
   - System-wide global hotkeys (PrintScreen, Alt+A, Alt+T, Alt+P) and system tray icon hosting while running in the background.
   - Always-on-top layered floating pin windows (WS_EX_TOPMOST | WS_EX_LAYERED) for keeping reference snips above other applications.
   - Lazy loading of app-local native DLLs (onnxruntime.dll v1.32.0 and local MSVC CRT DLLs via LoadLibraryExW) only when the user runs offline OCR.

2. StartupTask behavior:
   - Declares desktop:StartupTask (TaskId="PikaSnipStartupTask") with Enabled="false" by default.
   - Startup launch is strictly opt-in and only enabled if the user checks "Run at Startup" in the system tray context menu.

3. Offline operation, privacy, and local storage:
   - PikaSnip makes zero network connections and collects zero telemetry or analytics.
   - All screen captures and Thai/English OCR inference run 100% locally on the user's device.
   - Preferences are stored locally at %LOCALAPPDATA%\OZLab\PikaSnip\settings.json; user-saved PNGs are written to %USERPROFILE%\Pictures\PikaSnip only when explicitly saved.

4. Quick verification steps for reviewers:
   - Launch PikaSnip from Start; confirm the PikaSnip icon appears in the Windows system tray (notification area).
   - Left-click the tray icon (or press PrintScreen / Alt+A) to open the capture overlay; drag a region or click a window.
   - On the floating toolbar, test annotations (e.g., Rectangle/Arrow), click Pin (Ctrl+P) to create an always-on-top pin window (scroll wheel zooms, middle-click or Esc closes), or click OCR (Ctrl+T) / press Alt+T to copy extracted text to the clipboard.
   - Right-click the tray icon and choose Exit to close cleanly.
```
