<#
.SYNOPSIS  Microsoft Store pre-upload gate for OZ LAB Windows apps (Windows PowerShell 5.1).
.DESCRIPTION  Read-only. 10 invariants, one PASS / FAIL / WARN line each, then a summary; exit 1 on any FAIL.
  No external dependencies (built-in cmdlets + System.Drawing only). Store IDs quoted by internal
  packaging/store/ docs are WARN; a leak in any other tracked public file is FAIL.
.PARAMETER SkipHttp  Skip the network probe.  .PARAMETER SkipGit  Skip git; checks 4 and 8 WARN, not FAIL.
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
    $r = @(); $bad = 0; $n = 0
    foreach ($ln in [IO.File]::ReadAllLines($Path)) {
        $n++; if ($ln.Trim() -eq '') { continue }
        $m = [regex]::Match($ln, $ShaRec)
        if (-not $m.Success) { $bad = $n; break }
        $r += [pscustomobject]@{ Hash = $m.Groups[1].Value.ToUpperInvariant(); Name = $m.Groups[3].Value }
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

if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = [IO.Path]::GetDirectoryName($PSScriptRoot) }
elseif (-not [IO.Path]::IsPathRooted($RepoRoot)) { $RepoRoot = [IO.Path]::Combine((Get-Location).Path, $RepoRoot) }
$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if (-not [IO.Path]::IsPathRooted($DistPath)) { $DistPath = [IO.Path]::Combine($RepoRoot, $DistPath) }
$DistPath = [IO.Path]::GetFullPath($DistPath)
Write-Output ("REPO {0}" -f $RepoRoot); Write-Output ("DIST {0}" -f $DistPath); Write-Output ''

# git access: -z emits raw paths (no C-quoting) so non-ASCII and newline names survive
$tracked = @(); $gitOk = $false; $gitWhy = 'skipped (-SkipGit)'
$git = if ($SkipGit) { $null } else { Get-Command git -ErrorAction SilentlyContinue }
if (-not $SkipGit) {
    if (-not $git) { $gitWhy = 'git unavailable' }
    else {
        $out = @(& git -c core.quotepath=false -C $RepoRoot ls-files -z 2>$null); $rc = $LASTEXITCODE
        if ($rc -ne 0) { $gitWhy = ("git ls-files failed (exit {0})" -f $rc) }
        else { $tracked = @((($out -join "`n") -split "`0") | Where-Object { $_ -ne '' }); $gitOk = $true }
    }
}

$msix = @()   # 1 - exactly one Store package
if (Test-Path -LiteralPath $DistPath -PathType Container) { $msix = @(Get-ChildItem -LiteralPath $DistPath -Filter '*_Store.msix' -File -ErrorAction SilentlyContinue) }
if ($msix.Count -eq 1) { Report 'PASS' '1-msix' ("{0} ({1} bytes)" -f $msix[0].Name, $msix[0].Length) }
elseif ($msix.Count -eq 0) { Report 'FAIL' '1-msix' ("no *_Store.msix in {0}" -f $DistPath) }
else { Report 'FAIL' '1-msix' ("expected exactly one *_Store.msix, found {0}: {1}" -f $msix.Count, (($msix | ForEach-Object { $_.Name }) -join ', ')) }

if ($msix.Count -eq 1) {   # 2 - SHA-256: one well-formed record, optional '*name' must be this package
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

$mfText = ''; $mfFromPkg = $true   # 3 - manifest inside the package, parsed as XML
if ($msix.Count -eq 1) {
    $zip = $null
    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
        $zip = [System.IO.Compression.ZipFile]::OpenRead($msix[0].FullName)
        $sr = New-Object IO.StreamReader($zip.GetEntry('AppxManifest.xml').Open())
        try { $mfText = $sr.ReadToEnd() } finally { $sr.Dispose() }
    } catch { $mfText = '' } finally { if ($null -ne $zip) { $zip.Dispose() } }
}
if (-not $mfText) {
    $mfFromPkg = $false; $manifestPath = [IO.Path]::Combine($RepoRoot, 'packaging\msix\AppxManifest.xml')
    if (Test-Path -LiteralPath $manifestPath) { $mfText = [IO.File]::ReadAllText($manifestPath) }
}
if (-not $mfText) { Report 'FAIL' '3-manifest' 'manifest unreadable: not in the package, packaging\msix\AppxManifest.xml not found' }
else {
    try {
        [xml]$doc = $mfText; $missing = @()
        if ($doc.SelectNodes("//*[local-name()='Capability' and @Name='runFullTrust']").Count -eq 0) { $missing += 'runFullTrust capability' }
        if ($doc.SelectNodes("//*[local-name()='StartupTask' and translate(@Enabled,'FALSE','false')='false']").Count -eq 0) { $missing += 'StartupTask Enabled="false"' }
        $idn = $doc.SelectSingleNode("//*[local-name()='Identity']")
        if (-not $idn -or -not $idn.GetAttribute('Name')) { $missing += 'Identity Name=' }
        if (-not $idn -or -not $idn.GetAttribute('Version')) { $missing += 'Identity Version=' }
        if (-not $idn -or $idn.GetAttribute('ProcessorArchitecture') -ne 'x64') { $missing += 'ProcessorArchitecture="x64"' }
        if ($missing.Count -eq 0) { Report 'PASS' '3-manifest' ("{0}: runFullTrust + StartupTask Enabled=`"false`" + Name/Version/x64" -f $(if ($mfFromPkg) { 'package manifest' } else { 'repo manifest' })) }
        else { Report 'FAIL' '3-manifest' ("missing: {0}" -f ($missing -join ', ')) }
    } catch { Report 'FAIL' '3-manifest' ("XML parse failed: {0}" -f $_.Exception.Message) }
    if (-not $mfFromPkg) { Report 'WARN' '3-manifest' 'package manifest could not be read - verified packaging\msix\AppxManifest.xml instead' }
}
$idPattern = '9' + 'PG[0-9A-Z]{10}' + '|' + ($StoreSite -replace '\.', '\.')   # 4 - Store ID leak scan
$textExt = @('.md', '.txt', '.html', '.htm', '.json', '.xml', '.ps1', '.psm1', '.cmd', '.bat', '.yml', '.yaml', '.cs', '.cpp', '.h', '.hpp', '.rc', '.cmake', '.iss', '.nsi', '.props', '.targets', '.js', '.css', '.svg')
if (-not $gitOk) { Report 'FAIL' '4-storeid' ("cannot verify ({0}) - Store ID / {1} leak scan not run" -f $gitWhy, $StoreSite) }
else {
    $leaks = @(); $internal = @()
    foreach ($rel in $tracked) {
        $norm = $rel -replace '\\', '/'
        if ([string]::IsNullOrWhiteSpace($rel) -or $norm -match '^(\.planning|dist|third_party|build)/') { continue }
        if ($textExt -notcontains [IO.Path]::GetExtension($norm).ToLowerInvariant() -and [IO.Path]::GetFileName($norm) -ne 'CMakeLists.txt') { continue }
        $full = [IO.Path]::Combine($RepoRoot, ($rel -replace '/', '\'))
        if (-not (Test-Path -LiteralPath $full)) { continue }
        foreach ($hit in @(Select-String -LiteralPath $full -Pattern $idPattern -ErrorAction SilentlyContinue)) {
            if ($norm -match '^packaging/store/') { $internal += ("{0}:{1}" -f $norm, $hit.LineNumber) }
            elseif ($leaks -notcontains $norm) { $leaks += $norm }
        }
    }
    if ($leaks.Count -gt 0) { Report 'FAIL' '4-storeid' ("Store ID / {0} in public files: {1}" -f $StoreSite, ($leaks -join ', ')) }
    else { Report 'PASS' '4-storeid' 'no Store ID or Store link in tracked public files' }
    if ($internal.Count -gt 0) { Report 'WARN' '4-storeid' ("internal doc(s) mention the Store ID (keep unpublished): {0}" -f ($internal -join ', ')) }
}
# 5 - screenshots: 4 exact names, valid PNG, exactly 1920x1080
$shotNames = @('01-capture-magnifier.png', '02-annotations-toolbar.png', '03-floating-pin-edit.png', '04-offline-thai-en-ocr.png')
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
# 6 - Store listing logo 300x300 and really PNG
$logoPath = [IO.Path]::Combine($RepoRoot, 'packaging\store\StoreLogo-300x300.png')
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
# 8 - no private keys / model or binary blobs tracked
if (-not $gitOk) { Report 'FAIL' '8-binaries' ("cannot verify ({0}) - tracked/ignored binary scan not run" -f $gitWhy) }
else {
    $binTracked = @($tracked | Where-Object { $_ -match '(?i)\.(pfx|pvk|onnx|dll)$' })
    if ($binTracked.Count -gt 0) { Report 'FAIL' '8-binaries' ("tracked binaries: {0}" -f ($binTracked -join ', ')) }
    else { Report 'PASS' '8-binaries' 'no .pfx/.pvk/.onnx/.dll tracked' }
    $ign = @(((& git -c core.quotepath=false -C $RepoRoot ls-files --others --ignored --exclude-standard -z 2>$null) -join "`n") -split "`0" | Where-Object { $_ -match '(?i)\.(pfx|pvk|onnx|dll)$' })
    if ($LASTEXITCODE -ne 0) { Report 'WARN' '8-binaries' ("cannot list ignored files (git exit {0})" -f $LASTEXITCODE) }
    elseif ($ign.Count -gt 0) { Report 'WARN' '8-binaries' ("{0} ignored binary file(s) on disk, untracked (expected for third_party/ payload)" -f $ign.Count) }
}
# 9 - working tree state (informational only, never FAIL)
if ($SkipGit -or -not $git) { Report 'WARN' '9-git' ("skipped ({0})" -f $gitWhy) }
else {
    $dirty = @(& git -c core.quotepath=false -C $RepoRoot status --porcelain 2>$null); $rc = $LASTEXITCODE
    if ($rc -ne 0) { Report 'WARN' '9-git' ("unknown working tree state (git status exit {0})" -f $rc) }
    elseif ($dirty.Count -eq 0) { Report 'PASS' '9-git' 'working tree clean' }
    else { Report 'WARN' '9-git' ("{0} changed/untracked entr(ies) - confirm none are artifacts" -f $dirty.Count) }
}
# 10 - SHA256SUMS.txt coverage
$sumsPath = [IO.Path]::Combine($DistPath, 'SHA256SUMS.txt')
if (-not (Test-Path -LiteralPath $sumsPath)) { Report 'WARN' '10-sums' ("{0} not found" -f ([IO.Path]::GetFileName($sumsPath))) }
else {
    try {
        $s = Read-Sha $sumsPath; $names = @($s.Records | Where-Object { $_.Name } | ForEach-Object { $_.Name })
        if ($s.Count -eq 0) { Report 'FAIL' '10-sums' 'SHA256SUMS.txt is empty' }
        elseif ($s.Bad) { Report 'FAIL' '10-sums' ("malformed line {0} in SHA256SUMS.txt" -f $s.Bad) }
        elseif (@($names | Where-Object { $_ -match '(?i)_Store\.msix$' }).Count -eq 0) { Report 'FAIL' '10-sums' 'no *_Store.msix entry listed in SHA256SUMS.txt' }
        else {
            $missingFiles = @($names | Where-Object { -not (Test-Path -LiteralPath ([IO.Path]::Combine($DistPath, ($_ -replace '/', '\')))) })
            if ($missingFiles.Count -eq 0) { Report 'PASS' '10-sums' ("{0} entries, all listed files present" -f $names.Count) } else { Report 'WARN' '10-sums' ("{0} entries, missing: {1}" -f $names.Count, ($missingFiles -join ', ')) }
        }
    } catch { Report 'FAIL' '10-sums' ("cannot verify: {0}" -f $_.Exception.Message) }
}
Write-Output ''; Write-Output ("SUMMARY: PASS={0} WARN={1} FAIL={2}" -f $script:P, $script:W, $script:F)
if ($script:F -gt 0) { Write-Output 'RESULT: FAIL - do not upload'; exit 1 }
Write-Output 'RESULT: PASS - safe to upload'; exit 0
