#Requires -Version 5.1
<#
.SYNOPSIS
    Downloads, verifies and stages PikaSnip's offline OCR assets.

.DESCRIPTION
    Fetches the Apache-2.0 OpenThai 2.1 OCR Text models from Hugging Face,
    verifies their SHA-256 checksums against the published SHA256SUMS file,
    and extracts the Microsoft ONNX Runtime native DLL, C/C++ headers and
    license files from the official NuGet package.

    The ONNX Runtime library is staged for lazy LoadLibraryW loading only.
    Nothing here is linked into PikaSnip.exe or pikasnip_tests.exe.

.PARAMETER OnnxRuntimeVersion
    Exact Microsoft.ML.OnnxRuntime version to fetch, or "latest" to query the
    NuGet index and pick the newest stable package containing the win-x64
    native DLL and C API headers.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-ocr-assets.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-ocr-assets.ps1 -OnnxRuntimeVersion latest
#>
[CmdletBinding()]
param(
    [string]$OnnxRuntimeVersion = "1.32.0",
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$modelRepoUrl = "https://huggingface.co/iapp/OpenThai2.1-OCR-Text-20261118/resolve/main"
$modelDir = Join-Path $RepoRoot "assets\models\openthai-ocr"
$ortRoot = Join-Path $RepoRoot "third_party\onnxruntime"
$ortIncludeDir = Join-Path $ortRoot "include"
$ortBinDir = Join-Path $ortRoot "bin"
$licenseCacheDir = Join-Path $RepoRoot "third_party\licenses"
$noticesPath = Join-Path $RepoRoot "THIRD-PARTY-NOTICES.txt"

$nugetFlatContainer = "https://api.nuget.org/v3-flatcontainer"

# onnxruntime.dll is built against the dynamic MSVC runtime. PikaSnip itself is
# /MT, so these four app-local DLLs are needed only by the lazily loaded ORT DLL.
$VcRuntimeDlls = @("msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll")

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Get-RemoteFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $partial = "$Destination.part"
    if (Test-Path -LiteralPath $partial) {
        Remove-Item -LiteralPath $partial -Force
    }

    Write-Host "    GET $Uri"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($null -ne $curl) {
        & $curl.Source --fail --location --silent --show-error --retry 3 --retry-delay 2 --output $partial $Uri
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed with exit code $LASTEXITCODE while downloading '$Uri'."
        }
    }
    else {
        Invoke-WebRequest -Uri $Uri -OutFile $partial -UseBasicParsing
    }

    if (-not (Test-Path -LiteralPath $partial)) {
        throw "Download did not produce a file for '$Uri'."
    }
    $length = (Get-Item -LiteralPath $partial).Length
    if ($length -le 0) {
        Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
        throw "Downloaded an empty file from '$Uri'."
    }

    Move-Item -LiteralPath $partial -Destination $Destination -Force
    Write-Host ("    -> {0:N0} bytes" -f $length)
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-ZipEntryText {
    param(
        [Parameter(Mandatory = $true)][System.IO.Compression.ZipArchive]$Archive,
        [Parameter(Mandatory = $true)][string]$EntryName
    )

    $entry = $Archive.GetEntry($EntryName)
    if ($null -eq $entry) {
        throw "NuGet package is missing required entry '$EntryName'."
    }
    $stream = $entry.Open()
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        try {
            return $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Test-OnnxRuntimePackageLayout {
    param([Parameter(Mandatory = $true)][string]$Path)

    $archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $names = @($archive.Entries | ForEach-Object { $_.FullName })
        return (($names -contains "runtimes/win-x64/native/onnxruntime.dll") -and
                ($names -contains "build/native/include/onnxruntime_c_api.h"))
    }
    finally {
        $archive.Dispose()
    }
}

function Test-PeMachineX64 {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset + 4
        $machine = $reader.ReadUInt16()
        return ($machine -eq 0x8664)
    }
    finally {
        $stream.Dispose()
    }
}

function Find-VcRuntimeRedistDir {
    $candidates = New-Object System.Collections.Generic.List[string]
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsRedistDir) -and
        (Test-Path -LiteralPath $env:VCToolsRedistDir)) {
        $match = Get-ChildItem -LiteralPath $env:VCToolsRedistDir -Recurse -Filter "msvcp140.dll" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\Microsoft\.VC\d+\.CRT\\msvcp140\.dll$" } |
            Select-Object -First 1
        if ($null -ne $match) {
            $candidates.Add((Split-Path -Parent $match.FullName))
        }
    }

    if (Test-Path -LiteralPath $vswhere) {
        $found = @(& $vswhere -latest -products * -find "VC\Redist\MSVC\**\x64\Microsoft.VC*.CRT\msvcp140.dll" 2>$null)
        foreach ($file in $found) {
            if (-not [string]::IsNullOrWhiteSpace($file) -and (Test-Path -LiteralPath $file) -and
                $file -match "\\x64\\Microsoft\.VC\d+\.CRT\\msvcp140\.dll$") {
                $candidates.Add((Split-Path -Parent $file))
            }
        }
    }

    # Last resort: the system-wide x64 runtime installed by the VC++ redistributable.
    $systemDir = Join-Path $env:WINDIR "System32"
    if (Test-Path -LiteralPath (Join-Path $systemDir "msvcp140.dll")) {
        $candidates.Add($systemDir)
    }

    foreach ($dir in $candidates) {
        $complete = $true
        foreach ($dll in $VcRuntimeDlls) {
            if (-not (Test-Path -LiteralPath (Join-Path $dir $dll))) {
                $complete = $false
                break
            }
        }
        if ($complete) {
            foreach ($dll in $VcRuntimeDlls) {
                if (-not (Test-PeMachineX64 -Path (Join-Path $dir $dll))) {
                    throw "VC++ runtime '$dll' at '$dir' is not an x64 binary."
                }
            }
            return $dir
        }
    }

    return $null
}

function Install-VcRuntimeDlls {
    param([Parameter(Mandatory = $true)][string]$TargetDir)

    $missing = @()
    foreach ($dll in $VcRuntimeDlls) {
        if (-not (Test-Path -LiteralPath (Join-Path $TargetDir $dll))) {
            $missing += $dll
        }
    }
    if ($missing.Count -eq 0) {
        return $null
    }

    $sourceDir = Find-VcRuntimeRedistDir
    if ($null -eq $sourceDir) {
        throw ("onnxruntime.dll needs app-local VC++ runtime DLLs ({0}), but no x64 Microsoft.VC*.CRT " +
               "redistributable folder was found. Install the Visual Studio C++ build tools or the x64 " +
               "VC++ redistributable, then rerun this script." -f ($missing -join ", "))
    }

    foreach ($dll in $missing) {
        Copy-Item -LiteralPath (Join-Path $sourceDir $dll) -Destination (Join-Path $TargetDir $dll) -Force
        Write-Host "    staged VC++ runtime $dll from $sourceDir"
    }
    return $sourceDir
}

function Resolve-LatestOnnxRuntimeVersion {
    Write-Step "Querying NuGet for the latest stable Microsoft.ML.OnnxRuntime"

    $index = Invoke-RestMethod -Uri "$nugetFlatContainer/microsoft.ml.onnxruntime/index.json" -UseBasicParsing
    $stable = @($index.versions |
        Where-Object { $_ -notmatch "-" } |
        Sort-Object { [version]($_ -replace "[^0-9.].*$", "") } -Descending)

    if ($stable.Count -eq 0) {
        throw "NuGet index returned no stable Microsoft.ML.OnnxRuntime versions."
    }

    foreach ($candidate in $stable) {
        $probe = Join-Path ([System.IO.Path]::GetTempPath()) ("pikasnip-ort-probe-{0}-{1}.nupkg" -f $candidate, [guid]::NewGuid().ToString("N"))
        try {
            Get-RemoteFile "$nugetFlatContainer/microsoft.ml.onnxruntime/$candidate/microsoft.ml.onnxruntime.$candidate.nupkg" $probe
            if (Test-OnnxRuntimePackageLayout -Path $probe) {
                Write-Host "    Selected Microsoft.ML.OnnxRuntime $candidate" -ForegroundColor Green
                return $candidate
            }
            Write-Host "    $candidate does not contain the win-x64 DLL and C API headers; trying an older release."
        }
        finally {
            if (Test-Path -LiteralPath $probe) {
                Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
            }
        }
    }

    throw "No stable Microsoft.ML.OnnxRuntime package contained 'runtimes/win-x64/native/onnxruntime.dll' and 'build/native/include/onnxruntime_c_api.h'."
}

foreach ($dir in @($modelDir, $ortIncludeDir, $ortBinDir, $licenseCacheDir)) {
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
}

# ---------------------------------------------------------------------------
# 1. OpenThai 2.1 OCR Text model files (Apache-2.0)
# ---------------------------------------------------------------------------
Write-Step "Fetching OpenThai 2.1 OCR Text model files"

$sumsPath = Join-Path $modelDir "SHA256SUMS"
$sumsProbePath = "$sumsPath.probe"
$requiredChecks = @("det.onnx", "rec.onnx", "rec2.onnx", "rec_dict.txt")

# Download into a probe file first: the live SHA256SUMS is only replaced once the
# new copy parses and lists every required model file.
$expectedHashes = @{}
$sumsRefreshed = $false
try {
    Get-RemoteFile "$modelRepoUrl/SHA256SUMS" $sumsProbePath

    $probeHashes = @{}
    foreach ($line in Get-Content -LiteralPath $sumsProbePath) {
        if ($line -match "^\s*([0-9A-Fa-f]{64})\s+\*?(.+?)\s*$") {
            $name = $Matches[2] -replace "^\./", ""
            $probeHashes[$name] = $Matches[1].ToLowerInvariant()
        }
    }
    foreach ($file in $requiredChecks) {
        if (-not $probeHashes.ContainsKey($file)) {
            throw "Downloaded SHA256SUMS does not contain an entry for required file '$file'."
        }
    }

    Move-Item -LiteralPath $sumsProbePath -Destination $sumsPath -Force
    $expectedHashes = $probeHashes
    $sumsRefreshed = $true
}
catch {
    if (Test-Path -LiteralPath $sumsProbePath) {
        Remove-Item -LiteralPath $sumsProbePath -Force -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path -LiteralPath $sumsPath)) {
        throw
    }
    Write-Warning "Could not refresh SHA256SUMS ($($_.Exception.Message)); keeping the local copy at '$sumsPath'."
}

if (-not $sumsRefreshed) {
    foreach ($line in Get-Content -LiteralPath $sumsPath) {
        if ($line -match "^\s*([0-9A-Fa-f]{64})\s+\*?(.+?)\s*$") {
            $name = $Matches[2] -replace "^\./", ""
            $expectedHashes[$name] = $Matches[1].ToLowerInvariant()
        }
    }
    foreach ($file in $requiredChecks) {
        if (-not $expectedHashes.ContainsKey($file)) {
            throw "SHA256SUMS does not contain an entry for required file '$file'."
        }
    }
}

$modelFiles = @(
    "det.onnx",
    "rec.onnx",
    "rec2.onnx",
    "rec_dict.txt",
    "NOTICE",
    "config.json"
)
foreach ($file in $modelFiles) {
    $destination = Join-Path $modelDir $file
    if ($expectedHashes.ContainsKey($file) -and (Test-Path -LiteralPath $destination)) {
        if ((Get-Sha256 -Path $destination) -eq [string]$expectedHashes[$file]) {
            Write-Host "    SKIP $file (already verified)"
            continue
        }
    }
    Get-RemoteFile "$modelRepoUrl/$file" $destination
}

Write-Step "Verifying OpenThai model SHA-256 checksums"

# NOTICE/config.json are also published in SHA256SUMS; verify them when listed.
$optionalChecks = @("NOTICE", "config.json")
foreach ($file in $optionalChecks) {
    if (-not $expectedHashes.ContainsKey($file)) {
        Write-Warning "SHA256SUMS has no entry for '$file'; skipping checksum verification for that file."
    }
}

$verified = 0
foreach ($file in ($requiredChecks + $optionalChecks)) {
    if (-not $expectedHashes.ContainsKey($file)) {
        continue
    }

    $path = Join-Path $modelDir $file
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Expected model file '$path' is missing."
    }

    $actual = Get-Sha256 -Path $path
    $expected = [string]$expectedHashes[$file]
    if ($actual -ne $expected) {
        throw ("SHA-256 mismatch for '{0}': expected {1}, got {2}. Refusing to use a corrupted or tampered model." -f $file, $expected, $actual)
    }

    $size = (Get-Item -LiteralPath $path).Length
    Write-Host ("    OK  {0,-14} {1}  {2,12:N0} bytes" -f $file, $actual, $size) -ForegroundColor Green
    $verified++
}

if ($verified -lt $requiredChecks.Count) {
    throw "Only $verified model files were verified; expected at least $($requiredChecks.Count)."
}

# ---------------------------------------------------------------------------
# 2. Microsoft ONNX Runtime (MIT) from the official NuGet flat container
# ---------------------------------------------------------------------------
if ($OnnxRuntimeVersion -eq "latest") {
    $OnnxRuntimeVersion = Resolve-LatestOnnxRuntimeVersion
}
if ($OnnxRuntimeVersion -notmatch "^\d+\.\d+\.\d+(_\d+)?$") {
    throw "Invalid -OnnxRuntimeVersion '$OnnxRuntimeVersion'. Use an exact stable version such as 1.32.0, or 'latest'."
}

Write-Step "Fetching Microsoft.ML.OnnxRuntime $OnnxRuntimeVersion"

$nupkgUrl = "$nugetFlatContainer/microsoft.ml.onnxruntime/$OnnxRuntimeVersion/microsoft.ml.onnxruntime.$OnnxRuntimeVersion.nupkg"
$nupkgPath = Join-Path ([System.IO.Path]::GetTempPath()) ("pikasnip-onnxruntime-{0}-{1}.nupkg" -f $OnnxRuntimeVersion, [guid]::NewGuid().ToString("N"))
$dllPath = Join-Path $ortBinDir "onnxruntime.dll"
$ortVersionPath = Join-Path $ortRoot "VERSION.txt"
$ortLicenseCachePath = Join-Path $licenseCacheDir "ORT-LICENSE.txt"

# Already extracted and verified package (DLL + C API header + x64 CRT closure)?
# Then the ~20 MB nupkg download is not needed, which keeps repeat runs and
# offline builds working.
$reuseOrt = $false
if ((Test-Path -LiteralPath $ortVersionPath) -and
    (Test-Path -LiteralPath $dllPath) -and
    (Test-Path -LiteralPath (Join-Path $ortIncludeDir "onnxruntime_c_api.h"))) {
    $stampLine = @(Get-Content -LiteralPath $ortVersionPath -TotalCount 1)
    if ($stampLine.Count -gt 0 -and $stampLine[0].Trim() -eq "Microsoft.ML.OnnxRuntime $OnnxRuntimeVersion") {
        $crtComplete = $true
        foreach ($dll in $VcRuntimeDlls) {
            $crtPath = Join-Path $ortBinDir $dll
            if (-not (Test-Path -LiteralPath $crtPath) -or -not (Test-PeMachineX64 -Path $crtPath)) {
                $crtComplete = $false
                break
            }
        }
        if ($crtComplete) {
            $reuseOrt = $true
        }
    }
}

# Declared before the try block so the finally/shared sections never read an
# undefined name under Set-StrictMode.
$nupkgHash = $null
$dllHash = $null
$dllSize = 0
$headerCount = 0
$ortLicense = $null
$vcRuntimeVersion = $null
$recordedDllHash = $null
$fetchedUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")

try {
    if ($reuseOrt) {
        # Preserve the download provenance recorded in VERSION.txt instead of
        # rewriting it with empty values.
        $storedNupkgHash = $null
        $storedFetched = $null
        foreach ($line in Get-Content -LiteralPath $ortVersionPath) {
            if ($line -match "^\s*nupkg SHA-256:\s*([0-9A-Fa-f]{64})\s*$") {
                $storedNupkgHash = $Matches[1].ToLowerInvariant()
            }
            elseif ($line -match "^\s*onnxruntime\.dll SHA-256:\s*([0-9A-Fa-f]{64})\s*$") {
                $recordedDllHash = $Matches[1].ToLowerInvariant()
            }
            elseif ($line -match "^\s*Fetched \(UTC\):\s*(\S+)\s*$") {
                $storedFetched = $Matches[1]
            }
        }

        $ortLicenseSourcePath = $ortLicenseCachePath
        if (-not (Test-Path -LiteralPath $ortLicenseSourcePath)) {
            $legacyLicensePath = Join-Path $ortRoot "LICENSE"
            if (Test-Path -LiteralPath $legacyLicensePath) {
                $ortLicenseSourcePath = $legacyLicensePath
            }
            else {
                throw ("Microsoft.ML.OnnxRuntime $OnnxRuntimeVersion is already extracted, but its MIT license text is not " +
                       "cached at '$ortLicenseCachePath' or '$legacyLicensePath'. Restore network access (or delete " +
                       "'third_party/onnxruntime/VERSION.txt') and rerun this script so the third-party notices stay complete.")
            }
        }
        $ortLicense = (Get-Content -LiteralPath $ortLicenseSourcePath -Raw).TrimEnd()
        if ($ortLicense -notmatch "MIT License") {
            throw "Cached ONNX Runtime license text at '$ortLicenseSourcePath' does not look like the MIT license."
        }

        $headerCount = @(Get-ChildItem -LiteralPath $ortIncludeDir -File |
            Where-Object { $_.Extension -in ".h", ".inc" }).Count
        $nupkgHash = $storedNupkgHash
        if ($null -ne $storedFetched) {
            $fetchedUtc = $storedFetched
        }
        Write-Host "    SKIP Microsoft.ML.OnnxRuntime $OnnxRuntimeVersion nupkg (verified package already staged)"
    }
    else {
        Get-RemoteFile $nupkgUrl $nupkgPath

        if (-not (Test-OnnxRuntimePackageLayout -Path $nupkgPath)) {
            throw "Microsoft.ML.OnnxRuntime $OnnxRuntimeVersion is missing the win-x64 native DLL or the C API headers."
        }

        $nupkgHash = Get-Sha256 -Path $nupkgPath
        Write-Host "    nupkg SHA-256: $nupkgHash"

        $ortThirdPartyNotices = $null

        $archive = [System.IO.Compression.ZipFile]::OpenRead($nupkgPath)
        try {
            # Headers: all .h files (plus the .inc fragment included by the C API header).
            $headerEntries = @($archive.Entries | Where-Object {
                $_.FullName -match "^build/native/include/[^/]+\.(h|inc)$"
            })
            if ($headerEntries.Count -eq 0) {
                throw "No headers found under 'build/native/include/' in the NuGet package."
            }
            foreach ($entry in $headerEntries) {
                $leaf = Split-Path -Leaf $entry.FullName
                $target = Join-Path $ortIncludeDir $leaf
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
                $headerCount++
            }

            $dllEntry = $archive.GetEntry("runtimes/win-x64/native/onnxruntime.dll")
            if ($null -eq $dllEntry) {
                throw "NuGet package is missing 'runtimes/win-x64/native/onnxruntime.dll'."
            }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($dllEntry, $dllPath, $true)
            $dllSize = (Get-Item -LiteralPath $dllPath).Length

            $licenseEntryName = $null
            if ($null -ne $archive.GetEntry("LICENSE.txt")) {
                $licenseEntryName = "LICENSE.txt"
            }
            elseif ($null -ne $archive.GetEntry("LICENSE")) {
                $licenseEntryName = "LICENSE"
            }
            else {
                throw "NuGet package is missing its MIT LICENSE file."
            }
            $ortLicense = (Read-ZipEntryText -Archive $archive -EntryName $licenseEntryName).TrimEnd()

            if ($null -ne $archive.GetEntry("ThirdPartyNotices.txt")) {
                $ortThirdPartyNotices = (Read-ZipEntryText -Archive $archive -EntryName "ThirdPartyNotices.txt").TrimEnd()
                [System.IO.File]::WriteAllText(
                    (Join-Path $ortRoot "ThirdPartyNotices.txt"),
                    $ortThirdPartyNotices + [Environment]::NewLine,
                    (New-Object System.Text.UTF8Encoding($false)))
            }
        }
        finally {
            $archive.Dispose()
        }

        # Cache the MIT text so THIRD-PARTY-NOTICES.txt can still be regenerated offline.
        [System.IO.File]::WriteAllText(
            $ortLicenseCachePath,
            $ortLicense + [Environment]::NewLine,
            (New-Object System.Text.UTF8Encoding($false)))
    }

    $dllSize = (Get-Item -LiteralPath $dllPath).Length
    $dllHash = Get-Sha256 -Path $dllPath
    if ($null -ne $recordedDllHash -and $recordedDllHash -ne $dllHash) {
        Write-Warning "VERSION.txt records onnxruntime.dll SHA-256 $recordedDllHash but the staged file hashes to $dllHash; using the staged file."
    }

    Write-Step "Staging app-local VC++ runtime for onnxruntime.dll"
    $vcSourceDir = Install-VcRuntimeDlls -TargetDir $ortBinDir
    if ($null -eq $vcSourceDir) {
        Write-Host "    VC++ runtime DLLs already present in $ortBinDir"
    }
    $vcRuntimeVersion = (Get-Item -LiteralPath (Join-Path $ortBinDir "msvcp140.dll")).VersionInfo.FileVersion
    foreach ($dll in $VcRuntimeDlls) {
        $path = Join-Path $ortBinDir $dll
        if (-not (Test-Path -LiteralPath $path)) {
            throw "VC++ runtime DLL '$path' is missing after provisioning."
        }
        if (-not (Test-PeMachineX64 -Path $path)) {
            throw "VC++ runtime DLL '$path' is not an x64 binary."
        }
    }
    Write-Host "    VC++ runtime $vcRuntimeVersion"

    # Apache-2.0 text is cached so repeat runs and offline rebuilds still produce a
    # complete notice file. Failure to refresh it is non-fatal when a cache exists.
    $apachePath = Join-Path $licenseCacheDir "Apache-2.0.txt"
    if (-not (Test-Path -LiteralPath $apachePath)) {
        try {
            Get-RemoteFile "https://www.apache.org/licenses/LICENSE-2.0.txt" $apachePath
        }
        catch {
            Write-Warning "Could not download the Apache 2.0 license text: $($_.Exception.Message)"
        }
    }
    if (Test-Path -LiteralPath $apachePath) {
        $apacheText = (Get-Content -LiteralPath $apachePath -Raw).TrimEnd()
        if ($apacheText -notmatch "Apache License" -or $apacheText -notmatch "Version 2\.0") {
            throw "Cached Apache 2.0 license text at '$apachePath' does not look valid."
        }
    }
    else {
        $apacheText = "The full Apache License 2.0 text is available at http://www.apache.org/licenses/LICENSE-2.0"
        Write-Warning "THIRD-PARTY-NOTICES.txt will reference the Apache 2.0 URL instead of embedding the full text."
    }

    $noticeText = (Get-Content -LiteralPath (Join-Path $modelDir "NOTICE") -Raw).TrimEnd()

    # -----------------------------------------------------------------------
    # 3. Consolidated THIRD-PARTY-NOTICES.txt
    # -----------------------------------------------------------------------
    Write-Step "Writing consolidated THIRD-PARTY-NOTICES.txt"

    $now = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd")
    $rule = ("-" * 78)
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.AppendLine("PikaSnip - Third-Party Software & Model Notices")
    [void]$builder.AppendLine("===============================================")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine("This distribution of PikaSnip includes the following third-party components.")
    [void]$builder.AppendLine("Generated by scripts/fetch-ocr-assets.ps1 on $now (UTC).")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("1. OpenThai 2.1 OCR Text (iapp/OpenThai2.1-OCR-Text-20261118)")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("Source:  https://huggingface.co/iapp/OpenThai2.1-OCR-Text-20261118")
    [void]$builder.AppendLine("License: Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine("Files:")
    [void]$builder.AppendLine("  assets/models/openthai-ocr/det.onnx")
    [void]$builder.AppendLine("  assets/models/openthai-ocr/rec.onnx")
    [void]$builder.AppendLine("  assets/models/openthai-ocr/rec2.onnx")
    [void]$builder.AppendLine("  assets/models/openthai-ocr/rec_dict.txt")
    [void]$builder.AppendLine("  assets/models/openthai-ocr/config.json")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine("Upstream NOTICE (assets/models/openthai-ocr/NOTICE):")
    [void]$builder.AppendLine($noticeText)
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine("The full text of the Apache License, Version 2.0 that covers the components")
    [void]$builder.AppendLine("listed in this section is reproduced at the end of this file (Section 4,")
    [void]$builder.AppendLine("""Apache License 2.0 Full Text"").")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("2. Microsoft ONNX Runtime (Microsoft.ML.OnnxRuntime v$OnnxRuntimeVersion)")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("Source:  https://github.com/microsoft/onnxruntime")
    [void]$builder.AppendLine("NuGet:   https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/$OnnxRuntimeVersion")
    [void]$builder.AppendLine("Files:   onnxruntime.dll (loaded lazily at runtime via LoadLibraryW)")
    [void]$builder.AppendLine("         third_party/onnxruntime/include/onnxruntime_c_api.h")
    [void]$builder.AppendLine("License: MIT License (Copyright (c) Microsoft Corporation)")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine($ortLicense)
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("3. Microsoft Visual C++ Redistributable Runtime Libraries (App-Local)")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("Files:   msvcp140.dll, msvcp140_1.dll, vcruntime140.dll, vcruntime140_1.dll")
    [void]$builder.AppendLine("Version: $vcRuntimeVersion")
    [void]$builder.AppendLine("Source:  Microsoft Visual Studio Build Tools VC++ Redistributable (x64)")
    [void]$builder.AppendLine("License: Microsoft Visual C++ Redistributable License Terms")
    [void]$builder.AppendLine("         (Copyright (c) Microsoft Corporation)")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine("These four MSVC x64 C++ runtime DLLs are bundled app-local exclusively to")
    [void]$builder.AppendLine("support the lazily loaded onnxruntime.dll on clean Windows 10/11 systems that")
    [void]$builder.AppendLine("do not have the Microsoft Visual C++ Redistributable installed system-wide.")
    [void]$builder.AppendLine("They are not linked into PikaSnip.exe, which uses the static /MT runtime.")
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("4. Apache License 2.0 Full Text")
    [void]$builder.AppendLine($rule)
    [void]$builder.AppendLine("")
    [void]$builder.AppendLine($apacheText)
    [void]$builder.AppendLine("")

    [System.IO.File]::WriteAllText(
        $noticesPath,
        $builder.ToString(),
        (New-Object System.Text.UTF8Encoding($false)))

    # Provenance record for audits and future updates.
    if ($null -eq $nupkgHash) {
        # Reused package with an unreadable record: never write an empty hash.
        $nupkgHash = "unknown (existing package reused)"
    }
    $versionRecord = @(
        "Microsoft.ML.OnnxRuntime $OnnxRuntimeVersion",
        "Source: $nupkgUrl",
        "nupkg SHA-256: $nupkgHash",
        "onnxruntime.dll SHA-256: $dllHash",
        "VC++ app-local runtime: $vcRuntimeVersion",
        "Fetched (UTC): $fetchedUtc"
    ) -join [Environment]::NewLine
    [System.IO.File]::WriteAllText(
        (Join-Path $ortRoot "VERSION.txt"),
        $versionRecord + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false)))

    # -----------------------------------------------------------------------
    # 4. Final verification and summary
    # -----------------------------------------------------------------------
    Write-Step "Verifying staged assets"

    $requiredOutputs = @(
        (Join-Path $modelDir "det.onnx"),
        (Join-Path $modelDir "rec.onnx"),
        (Join-Path $modelDir "rec2.onnx"),
        (Join-Path $modelDir "rec_dict.txt"),
        (Join-Path $modelDir "config.json"),
        (Join-Path $modelDir "NOTICE"),
        $dllPath,
        (Join-Path $ortIncludeDir "onnxruntime_c_api.h"),
        (Join-Path $ortBinDir "msvcp140.dll"),
        (Join-Path $ortBinDir "msvcp140_1.dll"),
        (Join-Path $ortBinDir "vcruntime140.dll"),
        (Join-Path $ortBinDir "vcruntime140_1.dll"),
        $noticesPath
    )
    foreach ($path in $requiredOutputs) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Expected output '$path' is missing after extraction."
        }
    }
    if ($headerCount -lt 10) {
        throw "Only $headerCount ONNX Runtime headers were extracted; the package layout may have changed."
    }
    if ((Get-Item -LiteralPath $noticesPath).Length -lt 4096) {
        throw "THIRD-PARTY-NOTICES.txt looks incomplete."
    }

    Write-Host ""
    Write-Host "OpenThai 2.1 OCR Text -> $modelDir" -ForegroundColor Green
    foreach ($file in @("det.onnx", "rec.onnx", "rec2.onnx", "rec_dict.txt", "config.json", "NOTICE", "SHA256SUMS")) {
        $p = Join-Path $modelDir $file
        Write-Host ("    {0,-14} {1,12:N0} bytes  SHA-256 {2}" -f $file, (Get-Item -LiteralPath $p).Length, (Get-Sha256 -Path $p))
    }

    Write-Host ""
    Write-Host "Microsoft ONNX Runtime $OnnxRuntimeVersion -> $ortRoot" -ForegroundColor Green
    Write-Host ("    onnxruntime.dll  {0,12:N0} bytes  SHA-256 {1}" -f $dllSize, $dllHash)
    Write-Host ("    headers          {0,12} files" -f $headerCount)
    Write-Host ("    VC++ runtime     {0,12} (app-local for onnxruntime.dll)" -f $vcRuntimeVersion)
    Write-Host ("    notices          {0,12:N0} bytes  {1}" -f (Get-Item -LiteralPath $noticesPath).Length, $noticesPath)

    Write-Host ""
    Write-Host "All OpenThai SHA-256 checksums passed. OCR assets are ready." -ForegroundColor Green
}
finally {
    if (Test-Path -LiteralPath $nupkgPath) {
        Remove-Item -LiteralPath $nupkgPath -Force -ErrorAction SilentlyContinue
    }
}
