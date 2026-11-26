# Screenshot Shotlist — 4 × 1920×1080 PNG

Capture on a **real interactive Windows desktop** (never a headless/RDP session), at 1920×1080,
16:9, PNG. Clean background, no personal data (no real names, emails, tokens, file paths, or
notification popups), and no UI that is not in the shipped build.

Save into `dist\store-screenshots\` with exactly these filenames — the preflight check verifies the
names and the pixel size:

| # | Filename | Scene focus |
|---|---|---|
| 1 | `01-capture-magnifier.png` | capture overlay + live pixel magnifier |
| 2 | `02-annotations-toolbar.png` | annotation toolbar with markup in progress |
| 3 | `03-floating-pin-edit.png` | always-on-top floating pin + edit toolbar |
| 4 | `04-offline-thai-en-ocr.png` | offline Thai/English OCR result |

## 1 — `01-capture-magnifier.png`

Show the selection overlay over a normal app window: accent-coloured border, `W × H` dimension
badge, bottom hint bar, and the magnifier HUD reading out pixel coordinates with RGB/HEX values.

## 2 — `02-annotations-toolbar.png`

Show a region already marked up with arrow, rectangle, mosaic pixelation (over sample text, never a
real secret), step badges `1` / `2`, and a text note — with the floating toolbar visible and one tool
highlighted.

## 3 — `03-floating-pin-edit.png`

Show a pinned capture floating above another application, with the pin edit toolbar revealed and the
zoom/opacity indicator readable. Keep the pinned content generic (diagram or snippet), not a real
document.

## 4 — `04-offline-thai-en-ocr.png`

Show the OCR result: a mixed Thai/English snippet in a text editor plus the app's "text copied"
toast/badge. Make the offline claim visible in the caption, not by faking a disconnected state.

## Captions (approved wording — EN / TH)

| # | Caption (EN) | Caption (TH) |
|---|---|---|
| 1 | Region capture with magnifier and pixel-level zoom | จับภาพพื้นที่พร้อมแว่นขยายและซูมระดับพิกเซล |
| 2 | Annotation toolbar for markup before saving | แถบเครื่องมือวาดเขียนก่อนบันทึก |
| 3 | Floating pin window with edit toolbar | หน้าต่างปักหมุดลอยพร้อมแถบเครื่องมือแก้ไข |
| 4 | 100% offline Thai and English OCR | OCR ไทยและอังกฤษ ทำงานออฟไลน์ 100% |

## Final checks before upload

- [ ] All four files exist with the exact names above, each 1920×1080.
- [ ] No personal data, tokens, emails, or local user paths visible.
- [ ] Text in the shots is legible at 50% scale (Store listing thumbnails are small).
- [ ] Captions pasted in both languages; no claim that contradicts `facts.md`.
- [ ] Files are the same bytes as the ones referenced by the website (byte-copy, no re-encode).
