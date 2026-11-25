param(
    [string]$SourceImage = (Join-Path (Split-Path -Parent $PSScriptRoot) "assets\icons\pikasnip-master.png")
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$repoRoot = Split-Path -Parent $PSScriptRoot
$iconsDir = Join-Path $repoRoot "assets\icons"
$msixAssetsDir = Join-Path $repoRoot "packaging\msix\Assets"

New-Item -ItemType Directory -Force -Path $iconsDir | Out-Null
New-Item -ItemType Directory -Force -Path $msixAssetsDir | Out-Null

if (-not (Test-Path $SourceImage)) {
    throw "Source image not found at '$SourceImage'."
}

function Resize-BitmapHighQuality {
    param(
        [Parameter(Mandatory)][System.Drawing.Bitmap]$Source,
        [Parameter(Mandatory)][int]$Width,
        [Parameter(Mandatory)][int]$Height
    )

    $dest = New-Object System.Drawing.Bitmap $Width, $Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $dest.SetResolution(96.0, 96.0)
    $g = [System.Drawing.Graphics]::FromImage($dest)
    $attr = New-Object System.Drawing.Imaging.ImageAttributes
    try {
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)

        $destRect = New-Object System.Drawing.Rectangle 0, 0, $Width, $Height
        $g.DrawImage($Source, $destRect, 0, 0, $Source.Width, $Source.Height, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    }
    finally {
        $attr.Dispose()
        $g.Dispose()
    }
    return $dest
}

function Get-PngBytes {
    param(
        [Parameter(Mandatory)][System.Drawing.Bitmap]$Bitmap
    )
    $ms = New-Object System.IO.MemoryStream
    try {
        $Bitmap.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        [byte[]]$arr = $ms.ToArray()
        return ,$arr
    }
    finally {
        $ms.Dispose()
    }
}

$rawBitmap = [System.Drawing.Bitmap]::FromFile($SourceImage)
try {
    if ($rawBitmap.Width -eq 1024 -and $rawBitmap.Height -eq 1024 -and $rawBitmap.PixelFormat -eq [System.Drawing.Imaging.PixelFormat]::Format32bppArgb) {
        $master = New-Object System.Drawing.Bitmap $rawBitmap
    }
    else {
        $master = Resize-BitmapHighQuality -Source $rawBitmap -Width 1024 -Height 1024
    }
}
finally {
    $rawBitmap.Dispose()
}

try {
    $w = $master.Width
    $h = $master.Height
    $pixelCount = $w * $h

    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $bmpData = $master.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $bmpData.Stride
    $bytes = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($bmpData.Scan0, $bytes, 0, $bytes.Length)

    try {
        $transparentMask = New-Object byte[] $pixelCount
        $visited = New-Object byte[] $pixelCount
        $queue = New-Object int[] $pixelCount
        $head = 0
        $tail = 0

        # Seed BFS from the 4 outer corners
        $corners = @(
            0,
            ($w - 1),
            (($h - 1) * $w),
            (($h - 1) * $w + ($w - 1))
        )

        foreach ($seed in $corners) {
            if ($visited[$seed] -eq 0) {
                $visited[$seed] = 1
                $px = [int]($seed % $w)
                $py = [int][Math]::Floor($seed / $w)
                $off = $py * $stride + $px * 4
                $b = $bytes[$off]
                $gVal = $bytes[$off + 1]
                $r = $bytes[$off + 2]
                if ($r -ge 242 -and $gVal -ge 242 -and $b -ge 242) {
                    $queue[$tail] = $seed
                    $tail++
                }
            }
        }

        while ($head -lt $tail) {
            $idx = $queue[$head]
            $head++
            $transparentMask[$idx] = 1

            $x = $idx % $w
            $y = ($idx - $x) / $w

            $off = $y * $stride + $x * 4
            $bytes[$off + 3] = 0

            # Left
            if ($x -gt 0) {
                $n = $idx - 1
                if ($visited[$n] -eq 0) {
                    $visited[$n] = 1
                    $nOff = $off - 4
                    if ($bytes[$nOff + 2] -ge 242 -and $bytes[$nOff + 1] -ge 242 -and $bytes[$nOff] -ge 242) {
                        $queue[$tail] = $n
                        $tail++
                    }
                }
            }
            # Right
            if ($x -lt ($w - 1)) {
                $n = $idx + 1
                if ($visited[$n] -eq 0) {
                    $visited[$n] = 1
                    $nOff = $off + 4
                    if ($bytes[$nOff + 2] -ge 242 -and $bytes[$nOff + 1] -ge 242 -and $bytes[$nOff] -ge 242) {
                        $queue[$tail] = $n
                        $tail++
                    }
                }
            }
            # Up
            if ($y -gt 0) {
                $n = $idx - $w
                if ($visited[$n] -eq 0) {
                    $visited[$n] = 1
                    $nOff = $off - $stride
                    if ($bytes[$nOff + 2] -ge 242 -and $bytes[$nOff + 1] -ge 242 -and $bytes[$nOff] -ge 242) {
                        $queue[$tail] = $n
                        $tail++
                    }
                }
            }
            # Down
            if ($y -lt ($h - 1)) {
                $n = $idx + $w
                if ($visited[$n] -eq 0) {
                    $visited[$n] = 1
                    $nOff = $off + $stride
                    if ($bytes[$nOff + 2] -ge 242 -and $bytes[$nOff + 1] -ge 242 -and $bytes[$nOff] -ge 242) {
                        $queue[$tail] = $n
                        $tail++
                    }
                }
            }
        }

        # 1px soft anti-alias feather on opaque boundary pixels bordering transparent exterior
        for ($i = 0; $i -lt $head; $i++) {
            $tIdx = $queue[$i]
            $tx = $tIdx % $w
            $ty = ($tIdx - $tx) / $w

            $neighbors = @()
            if ($tx -gt 0) { $neighbors += ($tIdx - 1) }
            if ($tx -lt ($w - 1)) { $neighbors += ($tIdx + 1) }
            if ($ty -gt 0) { $neighbors += ($tIdx - $w) }
            if ($ty -lt ($h - 1)) { $neighbors += ($tIdx + $w) }

            foreach ($nIdx in $neighbors) {
                if ($transparentMask[$nIdx] -eq 0 -and $visited[$nIdx] -eq 1) {
                    # Mark as processed for feathering by setting visited = 2
                    $visited[$nIdx] = 2
                    $nx = $nIdx % $w
                    $ny = ($nIdx - $nx) / $w
                    $nOff = $ny * $stride + $nx * 4
                    $nb = $bytes[$nOff]
                    $ng = $bytes[$nOff + 1]
                    $nr = $bytes[$nOff + 2]
                    $minRgb = $nr
                    if ($ng -lt $minRgb) { $minRgb = $ng }
                    if ($nb -lt $minRgb) { $minRgb = $nb }

                    if ($minRgb -ge 225 -and $minRgb -le 241) {
                        $alpha = [int][Math]::Round(((242.0 - $minRgb) / 17.0) * 255.0)
                        if ($alpha -lt 0) { $alpha = 0 }
                        if ($alpha -gt 255) { $alpha = 255 }
                        $bytes[$nOff + 3] = [byte]$alpha
                    }
                }
            }
        }

        [System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $bmpData.Scan0, $bytes.Length)
    }
    finally {
        $master.UnlockBits($bmpData)
    }

    # Save 1024x1024 transparent master PNG
    $masterPngPath = Join-Path $iconsDir "pikasnip-master.png"
    $master.Save($masterPngPath, [System.Drawing.Imaging.ImageFormat]::Png)

    # Generate multi-resolution PNG-in-ICO (Vista+ format)
    $icoSizes = @(16, 24, 32, 48, 64, 128, 256)
    $icoEntries = @()
    foreach ($sz in $icoSizes) {
        $resized = Resize-BitmapHighQuality -Source $master -Width $sz -Height $sz
        try {
            $pngData = Get-PngBytes -Bitmap $resized
            $icoEntries += [pscustomobject]@{
                Size = $sz
                Data = $pngData
            }
        }
        finally {
            $resized.Dispose()
        }
    }

    $icoPath = Join-Path $iconsDir "pikasnip.ico"
    $fs = [System.IO.File]::Open($icoPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $bw = New-Object System.IO.BinaryWriter $fs
    try {
        # ICONDIR (6 bytes)
        $bw.Write([uint16]0)
        $bw.Write([uint16]1)
        $bw.Write([uint16]$icoEntries.Count)

        $offset = [uint32](6 + 16 * $icoEntries.Count)
        foreach ($entry in $icoEntries) {
            [byte[]]$dataBytes = $entry.Data
            $dimByte = if ($entry.Size -eq 256) { [byte]0 } else { [byte]$entry.Size }
            $bw.Write($dimByte)                      # bWidth
            $bw.Write($dimByte)                      # bHeight
            $bw.Write([byte]0)                       # bColorCount
            $bw.Write([byte]0)                       # bReserved
            $bw.Write([uint16]1)                     # wPlanes
            $bw.Write([uint16]32)                    # wBitCount
            $bw.Write([uint32]$dataBytes.Length)     # dwBytesInRes
            $bw.Write([uint32]$offset)               # dwImageOffset
            $offset += [uint32]$dataBytes.Length
        }

        foreach ($entry in $icoEntries) {
            [byte[]]$dataBytes = $entry.Data
            $bw.Write($dataBytes)
        }
    }
    finally {
        $bw.Dispose()
        $fs.Dispose()
    }

    # Generate MSIX Store PNG assets
    $msixSpecs = @(
        @{ Name = "Square44x44Logo.png";   Size = 44 },
        @{ Name = "Square150x150Logo.png"; Size = 150 },
        @{ Name = "StoreLogo.png";         Size = 50 }
    )
    foreach ($spec in $msixSpecs) {
        $outPath = Join-Path $msixAssetsDir $spec.Name
        $resized = Resize-BitmapHighQuality -Source $master -Width $spec.Size -Height $spec.Size
        try {
            $resized.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $resized.Dispose()
        }
    }

    # Verify all 5 output files
    $expectedPngs = @(
        @{ Path = $masterPngPath; Width = 1024; Height = 1024 },
        @{ Path = (Join-Path $msixAssetsDir "Square44x44Logo.png"); Width = 44; Height = 44 },
        @{ Path = (Join-Path $msixAssetsDir "Square150x150Logo.png"); Width = 150; Height = 150 },
        @{ Path = (Join-Path $msixAssetsDir "StoreLogo.png"); Width = 50; Height = 50 }
    )

    foreach ($item in $expectedPngs) {
        if (-not (Test-Path $item.Path)) {
            throw "Missing expected output PNG: $($item.Path)"
        }
        $fi = Get-Item $item.Path
        if ($fi.Length -le 0) {
            throw "Output PNG is empty: $($item.Path)"
        }
        $checkBmp = [System.Drawing.Bitmap]::FromFile($item.Path)
        try {
            if ($checkBmp.Width -ne $item.Width -or $checkBmp.Height -ne $item.Height) {
                throw "Dimension mismatch for $($item.Path): got $($checkBmp.Width)x$($checkBmp.Height), expected $($item.Width)x$($item.Height)"
            }
            Write-Output ("Verified PNG: {0} ({1}x{2}, {3} bytes)" -f $item.Path, $checkBmp.Width, $checkBmp.Height, $fi.Length)
        }
        finally {
            $checkBmp.Dispose()
        }
    }

    if (-not (Test-Path $icoPath)) {
        throw "Missing expected output ICO: $icoPath"
    }
    $icoFi = Get-Item $icoPath
    if ($icoFi.Length -le 0) {
        throw "Output ICO is empty: $icoPath"
    }
    Write-Output ("Verified ICO: {0} ({1} bytes, {2} entries: {3})" -f $icoPath, $icoFi.Length, $icoEntries.Count, ($icoSizes -join ", "))
}
finally {
    $master.Dispose()
}
