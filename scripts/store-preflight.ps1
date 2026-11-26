<#
.SYNOPSIS  Microsoft Store pre-upload gate for OZ LAB Windows apps (Windows PowerShell 5.1).
.DESCRIPTION  Read-only. 10 invariants, one PASS / FAIL / WARN line each, then a summary; exit 1 on
  any FAIL. No external dependencies (built-in cmdlets + System.Drawing only). Store IDs quoted by
  internal packaging/store/ submission docs are WARN; a leak in any other tracked public file is FAIL.
.PARAMETER DistPath    Folder with *_Store.msix, store-screenshots\ and SHA256SUMS.txt. Default .\dist
.PARAMETER RepoRoot    Repository root. Default: the parent folder of this script.
.PARAMETER PrivacyUrl  Live privacy policy URL to probe (HEAD, then GET fallback).
.PARAMETER SkipHttp    Skip the network probe.
.EXAMPLE  powershell -ExecutionPolicy Bypass -File scripts\store-preflight.ps1
#>
[CmdletBinding()]
param(
    [string]$DistPath = '.\dist',
    [string]$RepoRoot = '',
    [string]$PrivacyUrl = 'https://ozlab.pages.dev/privacy-pikasnip',
    [switch]$SkipHttp
)

$ErrorActionPreference = 'Continue'
$script:P = 0; $script:W = 0; $script:F = 0
function Report([string]$Status, [string]$Id, [string]$Message) {
    if ($Status -eq 'PASS') { $script:P++ } elseif ($Status -eq 'WARN') { $script:W++ } else { $script:F++ }
    Write-Output ("{0} [{1}] {2}" -f $Status, $Id, $Message)
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = [IO.Path]::GetDirectoryName($PSScriptRoot) }
elseif (-not [IO.Path]::IsPathRooted($RepoRoot)) { $RepoRoot = [IO.Path]::Combine((Get-Location).Path, $RepoRoot) }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (-not [IO.Path]::IsPathRooted($DistPath)) { $DistPath = [IO.Path]::Combine($RepoRoot, $DistPath) }
$DistPath = [IO.Path]::GetFullPath($DistPath)
Write-Output ("REPO {0}" -f $RepoRoot)
Write-Output ("DIST {0}" -f $DistPath)
Write-Output ''
$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) { Report 'WARN' 'git' 'git not found - git-based checks (4, 8, 9) will be skipped' }

# 1 - exactly one Store package
$msix = @()
if (Test-Path -LiteralPath $DistPath -PathType Container) { $msix = @(Get-ChildItem -LiteralPath $DistPath -Filter '*_Store.msix' -File -ErrorAction SilentlyContinue) }
if ($msix.Count -eq 1) { Report 'PASS' '1-msix' ("{0} ({1} bytes)" -f $msix[0].Name, $msix[0].Length) }
elseif ($msix.Count -eq 0) { Report 'FAIL' '1-msix' ("no *_Store.msix in {0}" -f $DistPath) }
else { Report 'FAIL' '1-msix' ("expected exactly one *_Store.msix, found {0}: {1}" -f $msix.Count, (($msix | ForEach-Object { $_.Name }) -join ', ')) }

# 2 - SHA-256 matches the sibling .sha256 file (tolerates '*name' / 'name' suffixes)
if ($msix.Count -eq 1) {
    $shaFile = $msix[0].FullName + '.sha256'
    if (-not (Test-Path -LiteralPath $shaFile)) { Report 'FAIL' '2-hash' ("missing {0}" -f ([IO.Path]::GetFileName($shaFile))) }
    else {
        $shaMatch = [regex]::Match([IO.File]::ReadAllText($shaFile), '[0-9A-Fa-f]{64}')
        $actual = (Get-FileHash -LiteralPath $msix[0].FullName -Algorithm SHA256).Hash
        if (-not $shaMatch.Success) { Report 'FAIL' '2-hash' 'no 64-hex SHA-256 token in the .sha256 file' }
        elseif ($shaMatch.Value.ToUpperInvariant() -eq $actual) { Report 'PASS' '2-hash' ("SHA-256 match {0}" -f $actual) }
        else { Report 'FAIL' '2-hash' ("expected {0}, actual {1}" -f $shaMatch.Value.ToUpperInvariant(), $actual) }
    }
}

# 3 - manifest identity and capabilities
$manifestPath = [IO.Path]::Combine($RepoRoot, 'packaging\msix\AppxManifest.xml')
if (-not (Test-Path -LiteralPath $manifestPath)) { Report 'FAIL' '3-manifest' 'packaging\msix\AppxManifest.xml not found' }
else {
    $mf = [IO.File]::ReadAllText($manifestPath); $missing = @()
    if ($mf -notmatch 'runFullTrust') { $missing += 'runFullTrust' }
    if ($mf -notmatch '(?s)<(?:desktop:)?StartupTask\b[^>]*\bEnabled\s*=\s*["'']false["'']') { $missing += 'StartupTask Enabled="false"' }
    if ($mf -notmatch '(?s)<Identity\b[^>]*\bName\s*=\s*"[^"]+"') { $missing += 'Identity Name=' }
    if ($mf -notmatch '(?s)<Identity\b[^>]*\bVersion\s*=\s*"[^"]+"') { $missing += 'Identity Version=' }
    if ($mf -notmatch '(?s)<Identity\b[^>]*\bProcessorArchitecture\s*=\s*"x64"') { $missing += 'ProcessorArchitecture="x64"' }
    if ($missing.Count -eq 0) { Report 'PASS' '3-manifest' 'runFullTrust + StartupTask Enabled="false" + Name/Version/x64 present' }
    else { Report 'FAIL' '3-manifest' ("missing: {0}" -f ($missing -join ', ')) }
}

$tracked = @()
if ($git) {
    $tracked = @(& git -C $RepoRoot ls-files 2>$null)
    if ($LASTEXITCODE -ne 0) { Report 'WARN' 'git' ("git ls-files failed (exit {0})" -f $LASTEXITCODE); $tracked = @() }
}

# 4 - no Store ID / Store link leak in tracked public text files
$idPattern = '9' + 'PG[0-9A-Z]{10}' + '|apps\.' + 'microsoft\.com'
$textExt = @('.md', '.txt', '.html', '.htm', '.json', '.xml', '.ps1', '.psm1', '.cmd', '.bat', '.yml', '.yaml', '.cs', '.cpp', '.h', '.hpp', '.rc', '.cmake', '.iss', '.nsi', '.props', '.targets', '.js', '.css', '.svg')
$leaks = @(); $internal = @()
foreach ($rel in $tracked) {
    if ([string]::IsNullOrWhiteSpace($rel)) { continue }
    $norm = $rel -replace '\\', '/'
    if ($norm -match '^(\.planning|dist|third_party|build)/') { continue }
    $ext = [IO.Path]::GetExtension($norm).ToLowerInvariant()
    if ($textExt -notcontains $ext -and [IO.Path]::GetFileName($norm) -ne 'CMakeLists.txt') { continue }
    $full = [IO.Path]::Combine($RepoRoot, ($rel -replace '/', '\'))
    if (-not (Test-Path -LiteralPath $full)) { continue }
    $lines = [IO.File]::ReadAllLines($full)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch $idPattern) { continue }
        if ($norm -match '^packaging/store/') { $internal += ("{0}:{1}" -f $norm, ($i + 1)) }
        elseif ($leaks -notcontains $norm) { $leaks += $norm }
    }
}
if ($leaks.Count -gt 0) { Report 'FAIL' '4-storeid' ("Store ID / apps.microsoft.com in public files: {0}" -f ($leaks -join ', ')) }
else { Report 'PASS' '4-storeid' 'no Store ID or Store link in tracked public files' }
if ($internal.Count -gt 0) { Report 'WARN' '4-storeid' ("internal doc(s) mention the Store ID (keep unpublished): {0}" -f ($internal -join ', ')) }

# 5 - screenshots: 4 exact names, valid PNG, exactly 1920x1080
$shotNames = @('01-capture-magnifier.png', '02-annotations-toolbar.png', '03-floating-pin-edit.png', '04-offline-thai-en-ocr.png')
$shotDir = [IO.Path]::Combine($DistPath, 'store-screenshots')
$drawOk = $true
try { Add-Type -AssemblyName System.Drawing -ErrorAction Stop }
catch { $drawOk = $false; Report 'FAIL' '5-shots' ("System.Drawing could not be loaded: {0}" -f $_.Exception.Message) }
if ($drawOk) {
    $bad = @()
    foreach ($name in $shotNames) {
        $path = [IO.Path]::Combine($shotDir, $name); $img = $null
        if (-not (Test-Path -LiteralPath $path)) { $bad += "$name missing"; continue }
        try {
            $img = [System.Drawing.Image]::FromFile($path)
            if ($img.Width -ne 1920 -or $img.Height -ne 1080) { $bad += ("{0} is {1}x{2}" -f $name, $img.Width, $img.Height) }
            elseif ($img.RawFormat.Guid -ne [System.Drawing.Imaging.ImageFormat]::Png.Guid) { $bad += "$name is not PNG" }
        } catch { $bad += "$name cannot be decoded" } finally { if ($null -ne $img) { $img.Dispose() } }
    }
    if ($bad.Count -eq 0) { Report 'PASS' '5-shots' '4 PNG screenshots, all 1920x1080' } else { Report 'FAIL' '5-shots' ($bad -join '; ') }
}

# 6 - Store listing logo 300x300
$logoPath = [IO.Path]::Combine($RepoRoot, 'packaging\store\StoreLogo-300x300.png')
if (-not $drawOk) { Report 'FAIL' '6-logo' 'System.Drawing unavailable - cannot verify StoreLogo-300x300.png' }
elseif (-not (Test-Path -LiteralPath $logoPath)) { Report 'FAIL' '6-logo' 'packaging\store\StoreLogo-300x300.png not found' }
else {
    $logo = $null
    try {
        $logo = [System.Drawing.Image]::FromFile($logoPath)
        if ($logo.Width -eq 300 -and $logo.Height -eq 300) { Report 'PASS' '6-logo' 'StoreLogo-300x300.png is 300x300' }
        else { Report 'FAIL' '6-logo' ("StoreLogo-300x300.png is {0}x{1}" -f $logo.Width, $logo.Height) }
    } catch { Report 'FAIL' '6-logo' ("StoreLogo-300x300.png cannot be decoded: {0}" -f $_.Exception.Message) }
    finally { if ($null -ne $logo) { $logo.Dispose() } }
}

# 7 - privacy policy URL must be live
if ($SkipHttp) { Report 'WARN' '7-privacy' 'skipped (-SkipHttp)' }
else {
    $status = $null; $err = ''
    try { $status = (Invoke-WebRequest -Uri $PrivacyUrl -Method Head -UseBasicParsing -TimeoutSec 20).StatusCode }
    catch {
        try { $status = (Invoke-WebRequest -Uri $PrivacyUrl -UseBasicParsing -TimeoutSec 20).StatusCode }
        catch { $err = $_.Exception.Message }
    }
    if ($status -eq 200) { Report 'PASS' '7-privacy' ("HTTP 200 {0}" -f $PrivacyUrl) }
    elseif ($null -ne $status) { Report 'FAIL' '7-privacy' ("HTTP {0} {1}" -f $status, $PrivacyUrl) }
    else { Report 'FAIL' '7-privacy' ("unreachable {0}: {1}" -f $PrivacyUrl, $err) }
}

# 8 - no private keys / model or binary blobs tracked
if ($git) {
    $binTracked = @($tracked | Where-Object { $_ -match '(?i)\.(pfx|pvk|onnx|dll)$' })
    if ($binTracked.Count -gt 0) { Report 'FAIL' '8-binaries' ("tracked binaries: {0}" -f ($binTracked -join ', ')) }
    else { Report 'PASS' '8-binaries' 'no .pfx/.pvk/.onnx/.dll tracked' }
    $ignoredBins = @(& git -C $RepoRoot ls-files --others --ignored --exclude-standard 2>$null | Where-Object { $_ -match '(?i)\.(pfx|pvk|onnx|dll)$' })
    if ($ignoredBins.Count -gt 0) { Report 'WARN' '8-binaries' ("{0} ignored binary file(s) on disk, untracked (expected for third_party/ payload)" -f $ignoredBins.Count) }
}

# 9 - working tree state (informational only, never FAIL)
if ($git) {
    $dirty = @(& git -C $RepoRoot status --porcelain 2>$null)
    if ($dirty.Count -eq 0) { Report 'PASS' '9-git' 'working tree clean' }
    else { Report 'WARN' '9-git' ("{0} changed/untracked entr(ies) - confirm none are artifacts" -f $dirty.Count) }
}

# 10 - SHA256SUMS.txt coverage
$sumsPath = [IO.Path]::Combine($DistPath, 'SHA256SUMS.txt')
if (-not (Test-Path -LiteralPath $sumsPath)) { Report 'WARN' '10-sums' ("{0} not found" -f ([IO.Path]::GetFileName($sumsPath))) }
else {
    $sumLines = @(Get-Content -LiteralPath $sumsPath | Where-Object { $_.Trim() })
    $missingFiles = @()
    foreach ($line in $sumLines) {
        if ($line -match '^\s*[0-9A-Fa-f]{64}\s+\*?(.+?)\s*$') {
            $listed = [IO.Path]::Combine($DistPath, ($matches[1] -replace '/', '\'))
            if (-not (Test-Path -LiteralPath $listed)) { $missingFiles += $matches[1] }
        }
    }
    if ($missingFiles.Count -eq 0) { Report 'PASS' '10-sums' ("{0} entries, all listed files present" -f $sumLines.Count) }
    else { Report 'WARN' '10-sums' ("{0} entries, missing: {1}" -f $sumLines.Count, ($missingFiles -join ', ')) }
}

Write-Output ''
Write-Output ("SUMMARY: PASS={0} WARN={1} FAIL={2}" -f $script:P, $script:W, $script:F)
if ($script:F -gt 0) { Write-Output 'RESULT: FAIL - do not upload'; exit 1 }
Write-Output 'RESULT: PASS - safe to upload'
exit 0
