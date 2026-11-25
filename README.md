# PikaSnip (v0.1.0)

Zero-bloat native Windows screenshot, pin, and offline Thai/English OCR utility by **OZ Lab**.

## Zero-Bloat

- `PikaSnip.exe` is **754,688 bytes**, built with the static CRT (`/MT`): **0 dynamic CRT imports**.
- Idle standby RAM stays **under 15 MB**; `onnxruntime.dll` (v1.32.0) is loaded lazily with
  `LoadLibraryExW` only when OCR runs, so the OCR engine stays unloaded while the app idles.
- No Electron, WebView2, Qt, .NET runtime, or telemetry.

The lazily loaded ONNX Runtime DLL needs its own MSVC runtime, so four app-local x64 CRT DLLs ship
next to it: `msvcp140.dll`, `msvcp140_1.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`
(version `14.51.36247.0`). This keeps offline OCR working on clean Windows 10/11 machines that
have no system-wide VC++ Redistributable.

## Requirements

- Windows 10 / 11, x64
- Visual Studio 2026 Build Tools (or IDE) with C++20 and Windows SDK `10.0.26100.0`
- CMake 3.24+ and CTest (both ship inside the Visual Studio CMake component)
- Network access on the first run of Step 1 only; later runs work offline

## Step 1 — Get the OCR assets

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\fetch-ocr-assets.ps1
```

This stages everything the build and both packages need:

- OpenThai 2.1 OCR Text models from `iapp/OpenThai2.1-OCR-Text-20261118` — Apache License 2.0
- `onnxruntime.dll` + C API headers from `Microsoft.ML.OnnxRuntime` 1.32.0 — MIT License
- The four app-local MSVC x64 CRT DLLs listed above, plus `THIRD-PARTY-NOTICES.txt`

Model files are verified against the published `SHA256SUMS`. The script is idempotent: files whose
SHA-256 already matches are reported as `SKIP` and are not downloaded again, and an already
extracted ONNX Runtime package is reused. Keep `third_party/licenses/` in place for offline runs.

The upstream `NOTICE` file still says "OpenThai2.0-OCR-Text" as the publisher shipped it; the model artifact used here is the 2.1 release.

## Step 2 — Build and test

```powershell
cmake --preset release
cmake --build --preset release --config Release
ctest --preset release --output-on-failure
```

Close any running `PikaSnip.exe` before rebuilding (a locked executable breaks the relink); smoke run: `.\build\release\bin\Release\PikaSnip.exe --smoke-test`

## Step 3 — Package

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-portable.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-msix.ps1 -SelfSign
```

Outputs in `dist\`:

- `PikaSnip_0.1.0_Portable_x64.zip` — portable folder with the exe, OCR runtime, models, and notices
- `PikaSnip_0.1.0.0_x64.msix` and `PikaSnip_0.1.0.0_x64.msix.sha256`
- `PikaSnip_0.1.0.0_x64.cer` — public signing certificate for the trusting machine (`-SelfSign` only)
- `PikaSnip_0.1.0.0_x64_Store.msix` and `PikaSnip_0.1.0.0_x64_Store.msix.sha256` — Store submission build (`-Store`)
- `PikaSnip_0.1.0.0_x64_Store.cer` — public certificate of the Store-publisher build (`-Store` only)
- `SHA256SUMS.txt` — checksums of every `.zip`, `.msix`, and `.exe` in `dist\`

`-SelfSign` reuses the existing `CN=OZ Lab` development certificate when it is still valid for more
than 30 days, so repeat packaging runs keep the same identity. The private key stays in
`Cert:\CurrentUser\My` (that is what makes reuse possible); only a temporary PFX is written to
`%TEMP%` for `signtool` and deleted in `finally`. Only the public `.cer` is left in `dist\`.

`-Store` builds the package for Partner Center instead of for sideloading. It rewrites only the
generated copy in `build\msix-staging-store\AppxManifest.xml`, setting `Identity/Publisher` to
`CN=32712C20-BC4B-401E-88B4-4958AC476B66`, signs it with a self-signed certificate of that exact
subject, and writes `PikaSnip_0.1.0.0_x64_Store.msix`. `packaging\msix\AppxManifest.xml` is never
modified, so `-SelfSign` alone still produces the `CN=OZ Lab` sideload build. Never upload the
`CN=OZ Lab` package: the Store re-signs the uploaded package, but the upload itself must carry the
Store publisher subject. The two builds have different package family names, so they install side
by side; both keep settings in `%LOCALAPPDATA%\OZLab\PikaSnip`, and the startup task must be
enabled again in the Store build.

## Step 4 — Install

### Portable

Extract `PikaSnip_0.1.0_Portable_x64.zip` to any writable folder and run `PikaSnip.exe`. Settings are
stored in `settings.json` beside the executable.

### MSIX

The package is self-signed with a **test certificate** — install it on test machines only. Before
trusting it, confirm the thumbprint is `9F536B494A2FC9724B848064E0E40873A0984408`, then import it
as administrator:

```powershell
Import-Certificate -FilePath .\dist\PikaSnip_0.1.0.0_x64.cer -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath .\dist\PikaSnip_0.1.0.0_x64.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople
```

Then install, launch, and uninstall (no elevation needed):

```powershell
Add-AppxPackage -Path .\dist\PikaSnip_0.1.0.0_x64.msix
$devPkg = Get-AppxPackage OZLab.PikaSnip | Where-Object Publisher -eq 'CN=OZ Lab'
explorer.exe "shell:AppsFolder\$($devPkg.PackageFamilyName)!PikaSnip"
$devPkg | Remove-AppxPackage
```

If a dev and a Store build are installed side by side, the `$devPkg` filter above targets only the dev identity.

MSIX mode stores data in `%LOCALAPPDATA%\OZLab\PikaSnip`. The test certificate expires in 2029; a
later `-SelfSign` run creates a new certificate, so re-import the new `.cer`. After uninstalling the
app, delete the certificate from `Cert:\LocalMachine\Root` and `Cert:\LocalMachine\TrustedPeople`.

## Verification

`v0.1.0` was verified on a clean Windows 11 build 26100 virtual machine with no system VC++
Redistributable installed:

- MSIX install, activation, and uninstall succeeded; portable and MSIX builds both ran Thai and
  English OCR offline
- Standby RAM measured 10.79–11.33 MB on that clean VM, idle after launch — an observation on one
  machine, not a guarantee for every system
- `SHA-256` of `PikaSnip.exe` matched between the MSIX payload and the portable package

Neither the app nor its smoke test prints `ORT_LOADED_AT_IDLE`. To confirm the OCR engine stays
unloaded while the app is running and idle:

```powershell
(Get-Process PikaSnip).Modules | Where-Object ModuleName -eq 'onnxruntime.dll'   # must be empty
```

## Troubleshooting

- **`onnxruntime.dll` not found / OCR unavailable** — run `scripts\fetch-ocr-assets.ps1`, then rebuild.
- **MSIX install fails with `0x800B0109`** — the signing certificate is not trusted yet; import the `.cer` into `LocalMachine\Root` and `LocalMachine\TrustedPeople` as shown above.
- **OCR starts but returns nothing** — confirm `models\openthai-ocr\` and the four CRT DLLs sit beside `PikaSnip.exe`; both are required for the OCR engine to load.

## Third-Party Notices

Both packages include `THIRD-PARTY-NOTICES.txt`, covering OpenThai 2.1 OCR (Apache License 2.0),
Microsoft ONNX Runtime (MIT License), and the app-local Microsoft Visual C++ Redistributable
runtime libraries. Regenerated by `scripts\fetch-ocr-assets.ps1`.
