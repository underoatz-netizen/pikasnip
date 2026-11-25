param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$DistDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($DistDir)) {
    $DistDir = Join-Path $repoRoot "dist"
}

$binDir = Join-Path $BuildDir "bin\$Configuration"
$exePath = Join-Path $binDir "PikaSnip.exe"
if (-not (Test-Path $exePath)) {
    throw "PikaSnip.exe not found at '$exePath'. Build the release preset first."
}

$stagingDir = Join-Path $repoRoot "build\portable-staging"
if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Copy-Item $exePath (Join-Path $stagingDir "PikaSnip.exe") -Force

$ortDllSrc = Join-Path $binDir "onnxruntime.dll"
if (-not (Test-Path $ortDllSrc)) {
    $ortDllSrc = Join-Path $repoRoot "third_party\onnxruntime\bin\onnxruntime.dll"
}
if (-not (Test-Path $ortDllSrc)) {
    throw "onnxruntime.dll not found. Run scripts/fetch-ocr-assets.ps1 and rebuild."
}
Copy-Item $ortDllSrc (Join-Path $stagingDir "onnxruntime.dll") -Force

foreach ($crtDll in @("msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    $crtSrc = Join-Path $binDir $crtDll
    if (-not (Test-Path $crtSrc)) {
        $crtSrc = Join-Path $repoRoot "third_party\onnxruntime\bin\$crtDll"
    }
    if (-not (Test-Path $crtSrc)) {
        throw "Required VC++ CRT runtime '$crtDll' not found in '$binDir' or 'third_party\onnxruntime\bin'."
    }
    Copy-Item $crtSrc (Join-Path $stagingDir $crtDll) -Force
}

$noticesSrc = Join-Path $binDir "THIRD-PARTY-NOTICES.txt"
if (-not (Test-Path $noticesSrc)) {
    $noticesSrc = Join-Path $repoRoot "THIRD-PARTY-NOTICES.txt"
}
if (-not (Test-Path $noticesSrc)) {
    throw "THIRD-PARTY-NOTICES.txt not found. Run scripts/fetch-ocr-assets.ps1."
}
Copy-Item $noticesSrc (Join-Path $stagingDir "THIRD-PARTY-NOTICES.txt") -Force

$modelsSrcDir = Join-Path $binDir "models\openthai-ocr"
if (-not (Test-Path $modelsSrcDir)) {
    $modelsSrcDir = Join-Path $repoRoot "assets\models\openthai-ocr"
}
if (-not (Test-Path $modelsSrcDir)) {
    throw "OpenThai OCR model directory not found. Run scripts/fetch-ocr-assets.ps1."
}
$modelsDestDir = Join-Path $stagingDir "models\openthai-ocr"
New-Item -ItemType Directory -Force -Path $modelsDestDir | Out-Null
foreach ($modelFile in @("det.onnx", "rec.onnx", "rec2.onnx", "rec_dict.txt", "NOTICE", "config.json", "SHA256SUMS")) {
    $srcFile = Join-Path $modelsSrcDir $modelFile
    if (-not (Test-Path $srcFile)) {
        throw "Required OCR model asset missing: '$srcFile'."
    }
    Copy-Item $srcFile (Join-Path $modelsDestDir $modelFile) -Force
}

$zipOut = Join-Path $DistDir "PikaSnip_0.1.0_Portable_x64.zip"
if (Test-Path $zipOut) {
    Remove-Item $zipOut -Force
}

Compress-Archive -Path (Join-Path $stagingDir "*") -DestinationPath $zipOut -CompressionLevel Optimal

$sumLines = @()
$artifacts = Get-ChildItem $DistDir -File | Where-Object { $_.Extension -in ".zip", ".msix", ".exe" } | Sort-Object Name
foreach ($a in $artifacts) {
    $h = (Get-FileHash -Path $a.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $sumLines += "$h *$($a.Name)"
}
$sumsPath = Join-Path $DistDir "SHA256SUMS.txt"
Set-Content -Path $sumsPath -Value $sumLines -Encoding ASCII

Write-Output "Portable ZIP built: $zipOut ($((Get-Item $zipOut).Length) bytes)"
Write-Output "Checksums written: $sumsPath"
