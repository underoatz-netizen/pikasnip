param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$DistDir = "",
    [switch]$SelfSign,
    [switch]$Store,
    [string]$IdentityName = "",
    [string]$Publisher = "",
    [string]$PublisherDisplayName = "",
    [string]$CertSubject = ""
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

# Package identity is resolved before anything is staged. -Store fixes the identity to the
# Partner Center values; any identity override alongside -Store is rejected instead of ignored.
$sourceManifestPath = Join-Path $repoRoot "packaging\msix\AppxManifest.xml"
$sourceManifestHash = (Get-FileHash -LiteralPath $sourceManifestPath -Algorithm SHA256).Hash

$devIdentity = @{
    Name                 = "OZLab.PikaSnip"
    Publisher            = "CN=OZ Lab"
    PublisherDisplayName = "OZ Lab"
    CertSubject          = "CN=OZ Lab"
}
$storeIdentity = @{
    Name                 = "OZLab.PikaSnip"
    Publisher            = "CN=32712C20-BC4B-401E-88B4-4958AC476B66"
    PublisherDisplayName = "OZ Lab"
    CertSubject          = "CN=32712C20-BC4B-401E-88B4-4958AC476B66"
}

if ($Store) {
    if ($PSBoundParameters.ContainsKey("IdentityName") -or
        $PSBoundParameters.ContainsKey("Publisher") -or
        $PSBoundParameters.ContainsKey("PublisherDisplayName")) {
        throw "-Store fixes the identity to the Partner Center values; do not pass -IdentityName, -Publisher or -PublisherDisplayName with -Store."
    }
    $IdentityName = $storeIdentity.Name
    $Publisher = $storeIdentity.Publisher
    $PublisherDisplayName = $storeIdentity.PublisherDisplayName
    if ([string]::IsNullOrWhiteSpace($CertSubject)) { $CertSubject = $storeIdentity.CertSubject }
}
else {
    if ([string]::IsNullOrWhiteSpace($IdentityName)) { $IdentityName = $devIdentity.Name }
    if ([string]::IsNullOrWhiteSpace($Publisher)) { $Publisher = $devIdentity.Publisher }
    if ([string]::IsNullOrWhiteSpace($PublisherDisplayName)) { $PublisherDisplayName = $devIdentity.PublisherDisplayName }
    if ([string]::IsNullOrWhiteSpace($CertSubject)) { $CertSubject = $devIdentity.CertSubject }
}

if ($IdentityName -notmatch '^[A-Za-z0-9.\-]+$' -or $IdentityName.Length -lt 3 -or $IdentityName.Length -gt 50) {
    throw "IdentityName '$IdentityName' is not a valid package name (letters, digits, '.', '-', 3-50 chars)."
}
if ($Publisher -notmatch '^CN=.+$') {
    throw "Publisher '$Publisher' must be a distinguished name of the form 'CN=...'."
}
if ([string]::IsNullOrWhiteSpace($PublisherDisplayName) -or $PublisherDisplayName.Length -gt 256) {
    throw "PublisherDisplayName must be 1-256 characters."
}

# Windows refuses a package whose signature publisher differs from the manifest.
# Checked against the effective identity before staging and re-checked on the staged
# file before makepri.exe, so a mismatch can never reach $DistDir.
$signPackage = $SelfSign -or $Store
if ($CertSubject -cne $Publisher) {
    throw "CertSubject '$CertSubject' does not match manifest Publisher '$Publisher'; the signed package would not install (Store validation requires them to be identical)."
}

$sdkBinRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
$sdkVersions = Get-ChildItem $sdkBinRoot -Directory | Sort-Object Name -Descending
$sdkBin = $null
foreach ($v in $sdkVersions) {
    $candidate = Join-Path $v.FullName "x64"
    if ((Test-Path (Join-Path $candidate "makeappx.exe")) -and (Test-Path (Join-Path $candidate "makepri.exe"))) {
        $sdkBin = $candidate
        break
    }
}
if ($null -eq $sdkBin) {
    throw "Windows SDK x64 tools (makepri.exe, makeappx.exe) not found under '$sdkBinRoot'."
}

$makepri = Join-Path $sdkBin "makepri.exe"
$makeappx = Join-Path $sdkBin "makeappx.exe"
$signtool = Join-Path $sdkBin "signtool.exe"
if ($signPackage -and -not (Test-Path $signtool)) {
    throw "signtool.exe not found at '$signtool'."
}

$msixSourceDir = Join-Path $repoRoot "packaging\msix"
# Dev and Store builds stage into separate directories so a run in one mode can never
# satisfy a gate with the other mode's manifest.
$stagingName = if ($Store) { "msix-staging-store" } else { "msix-staging" }
$stagingDir = Join-Path $repoRoot ("build\" + $stagingName)
if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
# eg. -DistDir .\dist becomes one canonical path for the whole run, so publishing, the lock key and
# the rollback bookkeeping always refer to the same folder regardless of later Set-Location.
$DistDir = (Resolve-Path -LiteralPath $DistDir).ProviderPath

Copy-Item $exePath (Join-Path $stagingDir "PikaSnip.exe") -Force
Copy-Item (Join-Path $msixSourceDir "AppxManifest.xml") (Join-Path $stagingDir "AppxManifest.xml") -Force
Copy-Item (Join-Path $msixSourceDir "Assets") (Join-Path $stagingDir "Assets") -Recurse -Force

# Identity rewrite happens on the generated copy only. The source manifest keeps the dev
# publisher (CN=OZ Lab) so -SelfSign sideload builds keep the same package identity.
$stagingManifestPath = Join-Path $stagingDir "AppxManifest.xml"
$stagingRaw = [System.IO.File]::ReadAllText($stagingManifestPath)
$stagingXml = [xml]$stagingRaw
$needsRewrite =
    $stagingXml.Package.Identity.Name -cne $IdentityName -or
    $stagingXml.Package.Identity.Publisher -cne $Publisher -or
    $stagingXml.Package.Properties.PublisherDisplayName -cne $PublisherDisplayName

if ($needsRewrite) {
    # Only real elements are rewritten: comment spans are skipped by matching them first so a
    # commented-out <Identity>/<PublisherDisplayName> can never be picked as the target.
    $identityMatch = $null
    foreach ($m in [regex]::Matches($stagingRaw, '(?s)(?:<!--.*?-->)|(?<id><Identity\b(?:"[^"]*"|''[^'']*''|[^>"''])*>)')) {
        if ($m.Groups['id'].Success) { $identityMatch = $m; break }
    }
    if ($null -eq $identityMatch) {
        throw "Staging manifest has no <Identity> element: '$stagingManifestPath'."
    }
    $identityBlock = $identityMatch.Value
    if ($identityBlock -notmatch '\bName=["'']' -or $identityBlock -notmatch '\bPublisher=["'']') {
        throw "Staging manifest <Identity> is missing Name or Publisher."
    }
    # Attribute values are matched together with the quote that opened them (' or "), so a manifest
    # written with single quotes keeps single quotes instead of being silently left unchanged.
    # Values are XML-escaped and injected through a MatchEvaluator, so '$' in a value can never be
    # read as a regex substitution and an already-escaped value is not double-decoded.
    $escapedName = [System.Security.SecurityElement]::Escape($IdentityName)
    $identityBlock = [regex]::Replace($identityBlock, '\bName=(["''])(?:(?!\1).)*\1', { param($m) 'Name=' + $m.Groups[1].Value + $escapedName + $m.Groups[1].Value })
    $escapedPublisher = [System.Security.SecurityElement]::Escape($Publisher)
    $identityBlock = [regex]::Replace($identityBlock, '\bPublisher=(["''])(?:(?!\1).)*\1', { param($m) 'Publisher=' + $m.Groups[1].Value + $escapedPublisher + $m.Groups[1].Value })
    $newRaw =
        $stagingRaw.Substring(0, $identityMatch.Index) +
        $identityBlock +
        $stagingRaw.Substring($identityMatch.Index + $identityMatch.Length)

    $pdnMatch = $null
    foreach ($m in [regex]::Matches($newRaw, '(?s)(?:<!--.*?-->)|(?<pdn><PublisherDisplayName>[^<]*</PublisherDisplayName>)')) {
        if ($m.Groups['pdn'].Success) { $pdnMatch = $m; break }
    }
    if ($null -eq $pdnMatch) {
        throw "Staging manifest has no <PublisherDisplayName> element: '$stagingManifestPath'."
    }
    $pdnReplacement = '<PublisherDisplayName>' + [System.Security.SecurityElement]::Escape($PublisherDisplayName) + '</PublisherDisplayName>'
    $newRaw =
        $newRaw.Substring(0, $pdnMatch.Index) +
        $pdnReplacement +
        $newRaw.Substring($pdnMatch.Index + $pdnMatch.Length)

    [System.IO.File]::WriteAllText($stagingManifestPath, $newRaw, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output "Manifest identity rewritten in staging: $Publisher"
}
else {
    Write-Output "Manifest identity unchanged (dev): $Publisher"
}

# Gate 1: the bytes makepri.exe and makeappx.exe will consume carry the intended identity.
$stagingCheck = [xml]([System.IO.File]::ReadAllText($stagingManifestPath))
if ($stagingCheck.Package.Identity.Name -cne $IdentityName -or
    $stagingCheck.Package.Identity.Publisher -cne $Publisher -or
    $stagingCheck.Package.Properties.PublisherDisplayName -cne $PublisherDisplayName) {
    throw "Staging manifest identity mismatch after rewrite: Name='$($stagingCheck.Package.Identity.Name)' Publisher='$($stagingCheck.Package.Identity.Publisher)' PublisherDisplayName='$($stagingCheck.Package.Properties.PublisherDisplayName)'."
}
if ($stagingCheck.Package.Identity.Publisher -cne $CertSubject) {
    throw "Staged manifest Publisher '$($stagingCheck.Package.Identity.Publisher)' does not match signing CertSubject '$CertSubject'; the signed package would not install."
}

# Gate 2: prove the source manifest was not touched by this run.
if ((Get-FileHash -LiteralPath $sourceManifestPath -Algorithm SHA256).Hash -ne $sourceManifestHash) {
    throw "Source manifest '$sourceManifestPath' changed during the build; packaging aborted."
}

$priConfig = Join-Path $msixSourceDir "priconfig.xml"
$priOutput = Join-Path $stagingDir "resources.pri"
& $makepri new /pr "$stagingDir" /cf "$priConfig" /mn (Join-Path $stagingDir "AppxManifest.xml") /of "$priOutput" /o
if ($LASTEXITCODE -ne 0) {
    throw "makepri.exe failed with exit code $LASTEXITCODE"
}

# Stage OCR runtime, notices, and OpenThai 2.1 models after makepri.exe and before makeappx.exe pack
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

$msixBaseName = "PikaSnip_0.1.0.0_x64"
if ($Store) { $msixBaseName += "_Store" }
$msixOut = Join-Path $DistDir ($msixBaseName + ".msix")
$cerPath = Join-Path $DistDir ($msixBaseName + ".cer")

# Pack, sign and export into a temp subdirectory on the same volume as $DistDir, then publish
# under a cross-process mutex. Same volume keeps the final Move-Item a rename (atomic, no
# copy across drives); the lock serializes publishes because a parallel -SelfSign/-Store run
# writes the shared dist\SHA256SUMS.txt and must never interleave with this one.
$publishTempDir = Join-Path $DistDir (".tmp-msix-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $publishTempDir | Out-Null
# The temp package must keep the .msix extension: makeappx/signtool infer the SIP from it.
$msixTemp = Join-Path $publishTempDir ($msixBaseName + ".msix")
$cerTemp = Join-Path $publishTempDir ($msixBaseName + ".cer")
# Set when a rollback cannot restore dist\ completely: the backups in $publishTempDir are then the
# only copy of the pre-run artifacts, so the finally block must not delete them.
$keepPublishTempDir = $false
try {
    & $makeappx pack /o /d "$stagingDir" /p "$msixTemp"
    if ($LASTEXITCODE -ne 0) {
        throw "makeappx.exe failed with exit code $LASTEXITCODE"
    }

    if ($signPackage) {
        # Reuse the existing certificate for this mode when it is still valid for more than 30 days
        # so repeat packaging runs keep the same identity (and the same pinned thumbprint).
        # The dev and Store subjects differ, so one mode can never reuse the other's certificate.
        $friendlyName = "PikaSnip Dev Sign"
        if ($Store) { $friendlyName = "PikaSnip Store Upload" }
        $cert = Get-ChildItem "Cert:\CurrentUser\My" |
            Where-Object { $_.Subject -ceq $CertSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(30) } |
            Sort-Object NotAfter -Descending |
            Select-Object -First 1
        if ($null -eq $cert) {
            $cert = New-SelfSignedCertificate -Type Custom -Subject $CertSubject -KeyUsage DigitalSignature -FriendlyName $friendlyName -CertStoreLocation "Cert:\CurrentUser\My" -NotAfter (Get-Date).AddYears(3) -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
            Write-Output "Created self-signed certificate $($cert.Thumbprint)"
        }
        else {
            Write-Output "Reusing self-signed certificate $($cert.Thumbprint)"
        }

        # The PFX carries the private key, so it lives in %TEMP% and is removed in finally.
        $pfxPrefix = if ($Store) { "pikasnip-store" } else { "pikasnip-dev" }
        $pfxPath = Join-Path ([System.IO.Path]::GetTempPath()) ("$pfxPrefix-{0}.pfx" -f [guid]::NewGuid().ToString("N"))
        $pwd = ConvertTo-SecureString -String "pikasnip-dev" -Force -AsPlainText
        $signExit = -1
        try {
            Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $pwd | Out-Null
            & $signtool sign /fd SHA256 /f "$pfxPath" /p "pikasnip-dev" "$msixTemp"
            $signExit = $LASTEXITCODE
        }
        finally {
            if (Test-Path -LiteralPath $pfxPath) {
                Remove-Item -LiteralPath $pfxPath -Force -ErrorAction SilentlyContinue
            }
        }
        if ($signExit -ne 0) {
            throw "signtool.exe failed with exit code $signExit"
        }

        # Public certificate for the trusting machine. It is exported only after the package signed
        # successfully, and it must never be staged into the MSIX payload.
        Export-Certificate -Cert $cert -FilePath $cerTemp -Type CERT -Force | Out-Null
    }

    # Publish lock key: the normalized dist path, so all builds targeting the same folder contend.
    $distKeySource = $DistDir.TrimEnd('\').ToLowerInvariant()
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $distKeyBytes = $sha256.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($distKeySource))
    }
    finally {
        $sha256.Dispose()
    }
    $distKey = -join ($distKeyBytes[0..7] | ForEach-Object { $_.ToString("x2") })

    $mutex = New-Object System.Threading.Mutex($false, ("Local\PikaSnip-Dist-" + $distKey))
    $acquired = $false
    try {
        try {
            $acquired = $mutex.WaitOne([TimeSpan]::FromSeconds(60))
        }
        catch [System.Threading.AbandonedMutexException] {
            $acquired = $true
        }
        if (-not $acquired) {
            throw "Timed out waiting for the dist publish lock '$distKey' (another MSIX build holds it)."
        }

        # Every artifact byte is produced before a single file in $DistDir is touched, and each
        # publish step backs the previous file up inside $publishTempDir. A failure halfway (for
        # example a .cer still open in another process) restores dist\ to its exact pre-run state
        # instead of leaving a mixed set of old and new artifacts.
        $hash = (Get-FileHash -LiteralPath $msixTemp -Algorithm SHA256).Hash.ToLowerInvariant()
        $shaTemp = Join-Path $publishTempDir ($msixBaseName + ".msix.sha256")
        Set-Content -Path $shaTemp -Value "$hash *$(Split-Path -Leaf $msixOut)" -Encoding ASCII

        $shaOut = "$msixOut.sha256"
        $sumsPath = Join-Path $DistDir "SHA256SUMS.txt"
        $msixBackup = Join-Path $publishTempDir ($msixBaseName + ".msix.bak")
        $cerBackup = Join-Path $publishTempDir ($msixBaseName + ".cer.bak")
        $shaBackup = Join-Path $publishTempDir ($msixBaseName + ".msix.sha256.bak")
        $sumsBackup = Join-Path $publishTempDir "SHA256SUMS.txt.bak"

        $hadMsix = $false
        $hadCer = $false
        $hadSha = $false
        $hadSums = $false
        $msixMoved = $false
        $cerMoved = $false
        $shaMoved = $false
        $sumsMoved = $false

        try {
            # Package first, certificate second: a watcher in dist\ must never see a .cer whose .msix
            # has not been published yet.
            if (Test-Path -LiteralPath $msixOut) {
                Move-Item -LiteralPath $msixOut -Destination $msixBackup -Force
                $hadMsix = $true
            }
            Move-Item -LiteralPath $msixTemp -Destination $msixOut -Force
            $msixMoved = $true

            if ($signPackage) {
                if (Test-Path -LiteralPath $cerPath) {
                    Move-Item -LiteralPath $cerPath -Destination $cerBackup -Force
                    $hadCer = $true
                }
                Move-Item -LiteralPath $cerTemp -Destination $cerPath -Force
                $cerMoved = $true
            }

            if (Test-Path -LiteralPath $shaOut) {
                Move-Item -LiteralPath $shaOut -Destination $shaBackup -Force
                $hadSha = $true
            }
            Move-Item -LiteralPath $shaTemp -Destination $shaOut -Force
            $shaMoved = $true

            $sumLines = @()
            $artifacts = Get-ChildItem $DistDir -File | Where-Object { $_.Extension -in ".zip", ".msix", ".exe" } | Sort-Object Name
            foreach ($a in $artifacts) {
                $h = (Get-FileHash -Path $a.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                $sumLines += "$h *$($a.Name)"
            }
            $sumTemp = Join-Path $publishTempDir "SHA256SUMS.txt"
            Set-Content -Path $sumTemp -Value $sumLines -Encoding ASCII
            if (Test-Path -LiteralPath $sumsPath) {
                Move-Item -LiteralPath $sumsPath -Destination $sumsBackup -Force
                $hadSums = $true
            }
            Move-Item -LiteralPath $sumTemp -Destination $sumsPath -Force
            $sumsMoved = $true
        }
        catch {
            $publishErr = $_
            # Roll back in reverse publish order. A file that existed before the run is restored from
            # its backup; a file this run created is removed. Artifacts this run never reached are
            # left untouched, so their pre-run content survives a failure. A restore that fails must
            # not be swallowed: the backup is the only remaining copy, so keep the temp directory.
            $rollbackItems = @(
                [pscustomobject]@{ Live = $sumsPath; Backup = $sumsBackup; Had = $hadSums; Moved = $sumsMoved }
                [pscustomobject]@{ Live = $shaOut; Backup = $shaBackup; Had = $hadSha; Moved = $shaMoved }
                [pscustomobject]@{ Live = $cerPath; Backup = $cerBackup; Had = $hadCer; Moved = $cerMoved }
                [pscustomobject]@{ Live = $msixOut; Backup = $msixBackup; Had = $hadMsix; Moved = $msixMoved }
            )
            foreach ($item in $rollbackItems) {
                if ($item.Had -and $item.Backup) {
                    try {
                        Move-Item -LiteralPath $item.Backup -Destination $item.Live -Force -ErrorAction Stop
                    }
                    catch {
                        $keepPublishTempDir = $true
                        Write-Warning ("Rollback could not restore '{0}': {1}. Original backup preserved at '{2}'." -f $item.Live, $_.Exception.Message, $item.Backup)
                    }
                }
                elseif ($item.Moved -and $item.Live) {
                    try {
                        Remove-Item -LiteralPath $item.Live -Force -ErrorAction Stop
                    }
                    catch {
                        Write-Warning ("Rollback could not remove '{0}': {1}." -f $item.Live, $_.Exception.Message)
                    }
                }
            }
            throw $publishErr
        }
        if ($signPackage) {
            Write-Output "Certificate exported: $cerPath"
        }
    }
    finally {
        # Release only if this process actually owns the mutex; Dispose always runs.
        if ($acquired) {
            $mutex.ReleaseMutex()
        }
        $mutex.Dispose()
    }
}
finally {
    if ((-not $keepPublishTempDir) -and (Test-Path -LiteralPath $publishTempDir)) {
        Remove-Item -LiteralPath $publishTempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    elseif ($keepPublishTempDir) {
        Write-Warning "Preserved backup directory after incomplete rollback: $publishTempDir"
    }
}

Write-Output "MSIX built: $msixOut ($((Get-Item $msixOut).Length) bytes)"
Write-Output "SHA256: $hash"
Write-Output "Checksums written: $sumsPath"
