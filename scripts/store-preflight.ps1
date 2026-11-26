<#
.SYNOPSIS  Microsoft Store pre-upload gate for OZ LAB Windows apps (Windows PowerShell 5.1). Read-only.
.DESCRIPTION  10 invariants, one PASS/FAIL/WARN line each, then a summary; exit 1 on any FAIL; Store IDs in
  internal packaging/store/ docs are WARN, a leak in any other tracked public file is FAIL.
.PARAMETER SkipHttp  Skip the network probe.  .PARAMETER SkipGit  Skip git; checks 4, 8 and 9 WARN.
.EXAMPLE  powershell -ExecutionPolicy Bypass -File scripts\store-preflight.ps1
#>
[CmdletBinding()]
param(
    [string]$DistPath = '.\dist', [string]$RepoRoot = '',
    [string]$PrivacyUrl = 'https://ozlab.pages.dev/privacy-pikasnip', [switch]$SkipHttp, [switch]$SkipGit
)
$ErrorActionPreference = 'Continue'; $script:P = 0; $script:W = 0; $script:F = 0
$ShaRec = '^\s*([0-9A-Fa-f]{64})(\s+\*?(.+?))?\s*$'; $StoreSite = 'apps.' + 'microsoft' + '.com'
function Report([string]$Status, [string]$Id, [string]$Message) {
    if ($Status -eq 'PASS') { $script:P++ } elseif ($Status -eq 'WARN') { $script:W++ } else { $script:F++ }
    Write-Output ("{0} [{1}] {2}" -f $Status, $Id, $Message)
}
function Read-Sha([string]$Path) {
    $r = @(); $bad = 0; $lines = [IO.File]::ReadAllLines($Path)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $ln = $lines[$i].Trim(); if ($ln -eq '') { continue }
        $m = [regex]::Match($ln, $ShaRec)
        if (-not $m.Success) { $bad = $i + 1; break }
        $r += [pscustomobject]@{ Hash = $m.Groups[1].Value.ToUpperInvariant(); Name = $m.Groups[3].Value.Trim(); Line = $i + 1 }
    }
    [pscustomobject]@{ Records = $r; Bad = $bad; Count = $r.Count }
}
function Test-Image([string]$Path, [int]$W, [int]$H) {
    $img = $null; try {
        $img = [System.Drawing.Image]::FromFile($Path)
        if ($img.Width -ne $W -or $img.Height -ne $H) { return ("size {0}x{1} (expected {2}x{3})" -f $img.Width, $img.Height, $W, $H) }
        if ($img.RawFormat.Guid -ne [System.Drawing.Imaging.ImageFormat]::Png.Guid) { return 'not PNG' }
        return ''
    } catch { return 'cannot be decoded' } finally { if ($null -ne $img) { $img.Dispose() } }
}
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = [IO.Path]::GetDirectoryName($PSScriptRoot) } elseif (-not [IO.Path]::IsPathRooted($RepoRoot)) { $RepoRoot = [IO.Path]::Combine((Get-Location).Path, $RepoRoot) }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (-not [IO.Path]::IsPathRooted($DistPath)) { $DistPath = [IO.Path]::Combine($RepoRoot, $DistPath) }; $DistPath = [IO.Path]::GetFullPath($DistPath)
Write-Output ("REPO {0}" -f $RepoRoot); Write-Output ("DIST {0}" -f $DistPath); Write-Output ''

# git state, resolved once: every call's exit code is kept so one success cannot mask another failure
$tracked = @(); $ignored = @(); $dirty = @(); $gitFail = @(); $gitWhy = 'skipped (-SkipGit)'
$enc = [Console]::OutputEncoding
try {
    if (-not $SkipGit) {
        try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }
        if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $gitFail += 'git unavailable' }
        else {
            $o = @(& git -c core.quotepath=false -C $RepoRoot ls-files -z 2>$null); $rc = $LASTEXITCODE
            if ($rc -ne 0) { $gitFail += ("git ls-files exit {0}" -f $rc) }
            else { $tracked = @((($o -join "`n") -split "`0") | Where-Object { $_ -ne '' }) }
            $o = @(& git -c core.quotepath=false -C $RepoRoot ls-files --others --ignored --exclude-standard -z 2>$null); $rc = $LASTEXITCODE
            if ($rc -ne 0) { $gitFail += ("git ls-files --others --ignored exit {0}" -f $rc) }
            else { $ignored = @((($o -join "`n") -split "`0") | Where-Object { $_ -ne '' }) }
            $o = @(& git -c core.quotepath=false -C $RepoRoot status --porcelain 2>$null); $rc = $LASTEXITCODE
            if ($rc -ne 0) { $gitFail += ("git status --porcelain exit {0}" -f $rc) }
            else { $dirty = @($o | Where-Object { $_ -ne '' }) }
        }
    }
} finally { [Console]::OutputEncoding = $enc }
if ($gitFail.Count -gt 0) { $gitWhy = ($gitFail -join '; ') }
$msix = @()   # 1 - exactly one Store package
if (Test-Path -LiteralPath $DistPath -PathType Container) { $msix = @(Get-ChildItem -LiteralPath $DistPath -Filter '*_Store.msix' -File -ErrorAction SilentlyContinue) }
if ($msix.Count -eq 1) { Report 'PASS' '1-msix' ("{0} ({1} bytes)" -f $msix[0].Name, $msix[0].Length) }
elseif ($msix.Count -eq 0) { Report 'FAIL' '1-msix' ("no *_Store.msix in {0}" -f $DistPath) }
else { Report 'FAIL' '1-msix' ("expected exactly one *_Store.msix, found {0}: {1}" -f $msix.Count, (($msix | ForEach-Object { $_.Name }) -join ', ')) }
if ($msix.Count -ne 1) { Report 'WARN' '2-hash' 'skipped: no *_Store.msix selected' }   # 2 - SHA-256 sidecar
else {
    $shaFile = $msix[0].FullName + '.sha256'
    if (-not (Test-Path -LiteralPath $shaFile)) { Report 'FAIL' '2-hash' ("missing {0}" -f ([IO.Path]::GetFileName($shaFile))) }
    else {
        try {
            $h = Read-Sha $shaFile
            if ($h.Bad) { Report 'FAIL' '2-hash' ("malformed .sha256 line {0}" -f $h.Bad) }
            elseif ($h.Count -eq 0) { Report 'FAIL' '2-hash' 'no SHA-256 record in the .sha256 file' }
            elseif ($h.Count -gt 1) { Report 'FAIL' '2-hash' ("ambiguous .sha256 ({0} records)" -f $h.Count) }
            elseif ($h.Records[0].Name -and $h.Records[0].Name -ne $msix[0].Name) { Report 'FAIL' '2-hash' ("sha256 record names a different file: {0}" -f $h.Records[0].Name) }
            else {
                $actual = (Get-FileHash -LiteralPath $msix[0].FullName -Algorithm SHA256).Hash
                if ($h.Records[0].Hash -eq $actual) { Report 'PASS' '2-hash' ("SHA-256 match {0}" -f $actual) }
                else { Report 'FAIL' '2-hash' ("expected {0}, actual {1}" -f $h.Records[0].Hash, $actual) }
            }
        } catch { Report 'FAIL' '2-hash' ("cannot verify: {0}" -f $_.Exception.Message) }
    }
}
# 3 - manifest comes from the package only; the repo copy is diagnostics, never a PASS path
$mfText = ''; $mfErr = ''
if ($msix.Count -ne 1) { $mfErr = 'no *_Store.msix selected' }
else {
    $zip = $null
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
        $zip = [System.IO.Compression.ZipFile]::OpenRead($msix[0].FullName)
        $entry = $zip.GetEntry('AppxManifest.xml')
        if ($null -eq $entry) { throw 'AppxManifest.xml missing from package' }
        $sr = New-Object IO.StreamReader($entry.Open())
        try { $mfText = $sr.ReadToEnd() } finally { $sr.Dispose() }
    } catch { $mfErr = $_.Exception.Message } finally { if ($null -ne $zip) { $zip.Dispose() } }
}
if ([string]::IsNullOrWhiteSpace($mfText)) {
    if (-not $mfErr) { $mfErr = 'AppxManifest.xml is empty' }
    $rp = [IO.Path]::Combine($RepoRoot, 'packaging\msix\AppxManifest.xml')
    $diag = if (Test-Path -LiteralPath $rp) { 'repo copy is diagnostics only: packaging\msix\AppxManifest.xml' } else { 'repo copy packaging\msix\AppxManifest.xml also missing' }
    Report 'FAIL' '3-manifest' ("package manifest unusable ({0}); {1}" -f $mfErr, $diag)
} else {
    try {
        [xml]$doc = $mfText; $missing = @()
        if ($doc.SelectNodes("//*[local-name()='Capabilities']/*[local-name()='Capability' and @Name='runFullTrust']").Count -eq 0) { $missing += 'Capabilities/runFullTrust' }
        $idn = $doc.SelectSingleNode("//*[local-name()='Identity']")
        if (-not $idn -or [string]::IsNullOrWhiteSpace($idn.GetAttribute('Name'))) { $missing += 'Identity Name=' }
        if (-not $idn -or [string]::IsNullOrWhiteSpace($idn.GetAttribute('Version'))) { $missing += 'Identity Version=' }
        if (-not $idn -or $idn.GetAttribute('ProcessorArchitecture') -ne 'x64') { $missing += 'ProcessorArchitecture="x64"' }
        $st = @($doc.SelectNodes("//*[local-name()='StartupTask']"))
        if ($st.Count -eq 0) { $missing += 'StartupTask node' }
        elseif (@($st | Where-Object { $_.GetAttribute('Enabled') -ne 'false' }).Count -gt 0) { $missing += 'every StartupTask Enabled="false"' }
        if ($missing.Count -eq 0) { Report 'PASS' '3-manifest' 'package manifest: runFullTrust + StartupTask Enabled="false" + Name/Version/x64' }
        else { Report 'FAIL' '3-manifest' ("package manifest missing: {0}" -f ($missing -join ', ')) }
    } catch { Report 'FAIL' '3-manifest' ("package manifest XML parse failed: {0}" -f $_.Exception.Message) }
}
# 4 - Store ID leak scan (regex assembled so the script never matches itself)
$idPattern = '9' + 'PG[0-9A-Z]{9,11}' + '|' + ($StoreSite -replace '\.', '\.')
$textExt = @('.md', '.txt', '.html', '.htm', '.json', '.xml', '.ps1', '.psm1', '.cmd', '.bat', '.yml', '.yaml', '.cs', '.cpp', '.h', '.hpp', '.rc', '.cmake', '.iss', '.nsi', '.props', '.targets', '.js', '.css', '.svg')
if ($SkipGit) { Report 'WARN' '4-storeid' 'skipped (-SkipGit)' }
elseif ($gitFail.Count -gt 0) { Report 'FAIL' '4-storeid' ("cannot verify ({0}) - Store ID / Store domain leak scan not run" -f $gitWhy) }
else {
    $leaks = @(); $internal = @(); $unread = @()
    foreach ($rel in $tracked) {
        $norm = $rel -replace '\\', '/'
        if ([string]::IsNullOrWhiteSpace($rel) -or $norm -match '^(\.planning|dist|third_party|build)/') { continue }
        if ($textExt -notcontains [IO.Path]::GetExtension($norm).ToLowerInvariant() -and [IO.Path]::GetFileName($norm) -ne 'CMakeLists.txt') { continue }
        $full = [IO.Path]::Combine($RepoRoot, ($rel -replace '/', '\'))
        try { $lines = [IO.File]::ReadAllLines($full) } catch { $unread += $norm; continue }
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -notmatch $idPattern) { continue }
            if ($norm -match '^packaging/store/') { $internal += ("{0}:{1}" -f $norm, ($i + 1)) }
            elseif ($leaks -notcontains $norm) { $leaks += $norm }
        }
    }
    if ($leaks.Count -gt 0) { Report 'FAIL' '4-storeid' ("Store ID / Store domain in public files: {0}" -f ($leaks -join ', ')) }
    elseif ($unread.Count -eq 0) { Report 'PASS' '4-storeid' 'no Store ID or Store link in tracked public files' }
    if ($unread.Count -gt 0) { Report 'WARN' '4-storeid' ("scan incomplete - unreadable: {0}" -f ($unread -join ', ')) }
    if ($internal.Count -gt 0) { Report 'WARN' '4-storeid' ("internal doc(s) mention the Store ID (keep unpublished): {0}" -f ($internal -join ', ')) }
}
$shotNames = @('01-capture-magnifier.png', '02-annotations-toolbar.png', '03-floating-pin-edit.png', '04-offline-thai-en-ocr.png')   # 5 - 4 exact names, PNG, 1920x1080
$shotDir = [IO.Path]::Combine($DistPath, 'store-screenshots'); $drawOk = $true
try { Add-Type -AssemblyName System.Drawing -ErrorAction Stop }
catch { $drawOk = $false; Report 'FAIL' '5-shots' ("System.Drawing could not be loaded: {0}" -f $_.Exception.Message) }
if ($drawOk) { $bad = @()
    foreach ($name in $shotNames) {
        $path = [IO.Path]::Combine($shotDir, $name)
        if (-not (Test-Path -LiteralPath $path)) { $bad += "$name missing"; continue }
        $why = Test-Image $path 1920 1080
        if ($why) { $bad += ("{0}: {1}" -f $name, $why) }
    }
    if ($bad.Count -eq 0) { Report 'PASS' '5-shots' '4 PNG screenshots, all 1920x1080' } else { Report 'FAIL' '5-shots' ($bad -join '; ') }
}
$logoPath = [IO.Path]::Combine($RepoRoot, 'packaging\store\StoreLogo-300x300.png')   # 6 - Store listing logo 300x300, really PNG
if (-not $drawOk) { Report 'FAIL' '6-logo' 'System.Drawing unavailable - cannot verify StoreLogo-300x300.png' }
elseif (-not (Test-Path -LiteralPath $logoPath)) { Report 'FAIL' '6-logo' 'packaging\store\StoreLogo-300x300.png not found' }
else { $why = Test-Image $logoPath 300 300
    if ($why) { Report 'FAIL' '6-logo' ("StoreLogo-300x300.png: {0}" -f $why) }
    else { Report 'PASS' '6-logo' 'StoreLogo-300x300.png is 300x300 PNG' }
}
# 7 - privacy policy URL must be live
if ($SkipHttp) { Report 'WARN' '7-privacy' 'skipped (-SkipHttp)' }
else {
    $status = $null; $err = ''
    try { $status = (Invoke-WebRequest -Uri $PrivacyUrl -Method Head -UseBasicParsing -TimeoutSec 20).StatusCode }
    catch { try { $status = (Invoke-WebRequest -Uri $PrivacyUrl -UseBasicParsing -TimeoutSec 20).StatusCode } catch { $err = $_.Exception.Message } }
    if ($status -eq 200) { Report 'PASS' '7-privacy' ("HTTP 200 {0}" -f $PrivacyUrl) }
    elseif ($null -ne $status) { Report 'FAIL' '7-privacy' ("HTTP {0} {1}" -f $status, $PrivacyUrl) }
    else { Report 'FAIL' '7-privacy' ("unreachable {0}: {1}" -f $PrivacyUrl, $err) }
}
# 8 - no private keys / model or binary blobs tracked or sitting ignored on disk
if ($SkipGit) { Report 'WARN' '8-binaries' 'skipped (-SkipGit)' }
elseif ($gitFail.Count -gt 0) { Report 'FAIL' '8-binaries' ("cannot verify ({0}) - tracked/ignored binary scan not run" -f $gitWhy) }
else {
    $binTracked = @($tracked | Where-Object { $_ -match '(?i)\.(pfx|pvk|onnx|dll)$' })
    if ($binTracked.Count -gt 0) { Report 'FAIL' '8-binaries' ("tracked binaries: {0}" -f ($binTracked -join ', ')) }
    else { Report 'PASS' '8-binaries' 'no .pfx/.pvk/.onnx/.dll tracked' }
    $ign = @($ignored | Where-Object { $_ -match '(?i)\.(pfx|pvk|onnx|dll)$' })
    if ($ign.Count -gt 0) { Report 'WARN' '8-binaries' ("{0} ignored binary file(s) on disk, untracked (expected for third_party/ payload)" -f $ign.Count) }
}
# 9 - working tree state (informational only, never FAIL)
if ($SkipGit) { Report 'WARN' '9-git' 'skipped (-SkipGit)' }
elseif ($gitFail.Count -gt 0) { Report 'WARN' '9-git' ("unknown working tree state ({0})" -f $gitWhy) }
elseif ($dirty.Count -eq 0) { Report 'PASS' '9-git' 'working tree clean' }
else { Report 'WARN' '9-git' ("{0} changed/untracked entr(ies) - confirm none are artifacts" -f $dirty.Count) }
# 10 - SHA256SUMS.txt must list the selected package by exact filename
$sumsPath = [IO.Path]::Combine($DistPath, 'SHA256SUMS.txt')
if (-not (Test-Path -LiteralPath $sumsPath)) { Report 'WARN' '10-sums' ("{0} not found" -f ([IO.Path]::GetFileName($sumsPath))) }
else {
    try {
        $s = Read-Sha $sumsPath; $names = @($s.Records | Where-Object { $_.Name } | ForEach-Object { $_.Name })
        $hashOnly = @($s.Records | Where-Object { -not $_.Name })
        if ($s.Bad) { Report 'FAIL' '10-sums' ("malformed line {0} in SHA256SUMS.txt" -f $s.Bad) }
        elseif ($s.Count -eq 0) { Report 'FAIL' '10-sums' 'SHA256SUMS.txt is empty' }
        elseif ($hashOnly.Count -gt 0) { Report 'FAIL' '10-sums' ("line {0} has a hash but no filename" -f $hashOnly[0].Line) }
        elseif ($msix.Count -ne 1) { Report 'FAIL' '10-sums' 'no *_Store.msix selected - cannot match a record' }
        elseif (@($s.Records | Where-Object { $_.Name -ieq $msix[0].Name }).Count -eq 0) { Report 'FAIL' '10-sums' ("no record for {0} (listed: {1})" -f $msix[0].Name, ($names -join ', ')) }
        else {
            $missingFiles = @($names | Where-Object { -not (Test-Path -LiteralPath ([IO.Path]::Combine($DistPath, ($_ -replace '/', '\')))) })
            if ($missingFiles.Count -eq 0) { Report 'PASS' '10-sums' ("{0} entries incl. {1}, all listed files present" -f $names.Count, $msix[0].Name) } else { Report 'WARN' '10-sums' ("{0} entries, missing: {1}" -f $names.Count, ($missingFiles -join ', ')) }
        }
    } catch { Report 'FAIL' '10-sums' ("cannot verify: {0}" -f $_.Exception.Message) }
}
Write-Output ''; Write-Output ("SUMMARY: PASS={0} WARN={1} FAIL={2}" -f $script:P, $script:W, $script:F)
if ($script:F -gt 0) { Write-Output 'RESULT: FAIL - do not upload'; exit 1 }
Write-Output 'RESULT: PASS - safe to upload'; exit 0
