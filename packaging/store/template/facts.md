# Store Fact Sheet — `{{APP_NAME}}`

Blank sheet. Fill every row with evidence taken from the release artifact and the source tree.
Write `n/a` explicitly instead of leaving a cell empty, and never guess a number.

## Identity (copy from Partner Center, never retype)

| Field | Value | Source |
|---|---|---|
| Product name (listing) | `{{APP_NAME}}` | Partner Center |
| `Package/Identity/Name` | `{{IDENTITY_NAME}}` | Partner Center → Product identity |
| `Package/Identity/Publisher` | `{{PUBLISHER_CN}}` | Partner Center → Product identity |
| `PublisherDisplayName` | `{{PUBLISHER_DISPLAY_NAME}}` | Partner Center → Product identity |
| Package Family Name (PFN) | `{{PFN}}` | `Get-AppxPackage <Name>` |
| Version (4 parts, 4th = 0) | `{{VERSION}}` | `AppxManifest.xml` |
| Architecture | `x64` | `AppxManifest.xml` |
| Store product ID (hold until Published) | `{{STORE_ID}}` | Partner Center |
| Account type | `{{individual \| company}}` | Partner Center |

## Executable

| Field | Value |
|---|---|
| Main executable name | `{{APP_EXE}}` |
| Size (bytes) | `{{EXE_BYTES}}` |
| Compiler / language | `{{C++20 / Rust / …}}` |
| Runtime linkage | `{{static CRT /MT \| dynamic}}` |
| Signing (shipped) | `{{dev self-signed \| Store re-signed}}` |

## Resource use (measured on `{{OS_BUILD}}`, n≥3)

| Metric | Value | Method |
|---|---|---|
| Idle RAM (private working set) | `{{IDLE_RAM_MB}}` | `WorkingSetPrivate` at idle |
| Cold start to first window | `{{COLD_START_MS}}` | first launch after reboot |
| Install size (unpacked) | `{{INSTALL_MB}}` | payload directory size |

## Runtimes, models, dependencies

| Item | Version | When loaded | Shipped path |
|---|---|---|---|
| `{{RUNTIME_1}}` | `{{VER}}` | `{{lazy on feature X}}` | `{{path in package}}` |
| `{{MODEL_1}}` | `{{VER}}` | `{{on OCR}}` | `{{path in package}}` |
| `{{none / other}}` | — | — | — |

Framework bloat check: `{{no Electron / no WebView2 / no Qt / no .NET runtime}}` — confirm by
listing the shipped payload, not by intent.

## Hotkeys and UI entry points (read from `src/`)

| Action | Binding | Source file |
|---|---|---|
| `{{capture}}` | `{{PrtSc / Alt+A}}` | `{{src/…}}` |
| `{{ocr}}` | `{{Alt+T}}` | `{{src/…}}` |
| `{{pin clipboard}}` | `{{Alt+P}}` | `{{src/…}}` |
| `{{save}}` | `{{Ctrl+S}}` | `{{src/…}}` |
| Tray menu labels | `{{…}}` | `{{src/…}}` |

## Data paths and storage

| Data | Path | Created when |
|---|---|---|
| Settings | `{{%LOCALAPPDATA%\Vendor\App\settings.json}}` | first run |
| Saved output | `{{%USERPROFILE%\Pictures\App}}` | explicit save only |
| Logs | `{{none / path}}` | `{{…}}` |
| Cache | `{{none / path}}` | `{{…}}` |

## Network, telemetry, privacy claims

| Claim | Value | Evidence |
|---|---|---|
| Outbound network connections | `{{none}}` | `{{source / firewall check}}` |
| Telemetry / analytics | `{{none}}` | `{{source}}` |
| Crash reporting | `{{none}}` | `{{source}}` |
| Ads / in-app purchases | `{{none}}` | `{{source}}` |
| Accounts / sign-in | `{{none}}` | `{{source}}` |
| Privacy policy URL | `{{https://ozlab.pages.dev/privacy-<app>}}` | HTTP 200 verified `{{date}}` |
| Restricted capabilities | `{{runFullTrust}}` | `AppxManifest.xml` |
| Startup task default | `{{Enabled="false"}}` | `AppxManifest.xml` |

## Artifacts and hashes (fill after the final build)

| Artifact | Bytes | SHA-256 |
|---|---|---|
| `{{App}}_{{VERSION}}_x64_Store.msix` | `{{…}}` | `{{…}}` |
| `{{App}}_{{VERSION}}_x64_Store.msix.sha256` | `{{…}}` | `{{…}}` |
| `StoreLogo-300x300.png` | `{{…}}` | `{{…}}` |
| Screenshot 1–4 | `{{…}}` | `{{…}}` |

Note: a signed MSIX is not reproducible — record the hash of the exact uploaded file and never claim
that a rebuild will produce the same bytes.
