# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
<#
.SYNOPSIS
    Regenerates the application icon (.ico) from its source PNG.

.DESCRIPTION
    Reads src/Chipestra.App/Assets/chipestra-icon.png (a square PNG, 256x256 or larger) and writes
    src/Chipestra.App/Assets/chipestra.ico with one image per size (16, 24, 32, 48, 64 and 256 pixels).
    Each image is stored as PNG inside the .ico (supported by Windows Vista and later). It uses only
    System.Drawing, which ships with Windows PowerShell and PowerShell 7 on Windows; nothing needs to be installed.

    The .ico is committed, so a normal build does not run this script. Run it only after changing the PNG:
        pwsh tools/make-app-icon.ps1

.PARAMETER Source
    The source PNG. Default: src/Chipestra.App/Assets/chipestra-icon.png.

.PARAMETER Output
    The .ico to write. Default: src/Chipestra.App/Assets/chipestra.ico.

.PARAMETER PreviewDir
    Optional folder; if given, each size is also written there as a PNG (for looking at the result).
#>

param(
    [string]$Source = "src/Chipestra.App/Assets/chipestra-icon.png",
    [string]$Output = "src/Chipestra.App/Assets/chipestra.ico",
    [string]$PreviewDir = ""
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 256)

if (-not (Test-Path $Source)) { throw "Source PNG not found: $Source" }
$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source).Path)
try {
    if ($src.Width -ne $src.Height) { throw "The source image must be square (it is $($src.Width)x$($src.Height))." }
    if ($src.Width -lt 256) { throw "The source image must be at least 256x256 (it is $($src.Width)x$($src.Height))." }

    # Shrinking in one step from a large image drops detail; halve the image until it is at most twice the target
    # size, then take the last step with the high-quality filter.
    function Resize-Image([System.Drawing.Image]$image, [int]$size) {
        $current = $image
        $owned = $false
        while ($current.Width -ge $size * 2) {
            $half = [Math]::Max($size, [int]($current.Width / 2))
            $next = New-Object System.Drawing.Bitmap($half, $half, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $g = [System.Drawing.Graphics]::FromImage($next)
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $g.DrawImage($current, 0, 0, $half, $half)
            $g.Dispose()
            if ($owned) { $current.Dispose() }
            $current = $next
            $owned = $true
        }
        $out = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($out)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.DrawImage($current, 0, 0, $size, $size)
        $g.Dispose()
        if ($owned) { $current.Dispose() }
        return $out
    }

    $pngs = @()
    foreach ($size in $sizes) {
        $bitmap = Resize-Image $src $size
        $stream = New-Object System.IO.MemoryStream
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        $pngs += , $stream.ToArray()
        if ($PreviewDir) {
            New-Item -ItemType Directory -Path $PreviewDir -Force | Out-Null
            $bitmap.Save((Join-Path $PreviewDir "icon-$size.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        }
        $bitmap.Dispose()
        $stream.Dispose()
    }
}
finally {
    $src.Dispose()
}

# ICONDIR (6 bytes) + one ICONDIRENTRY (16 bytes) per image + the PNG data.
$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($out)
$writer.Write([uint16]0)              # reserved
$writer.Write([uint16]1)              # type: icon
$writer.Write([uint16]$sizes.Count)   # image count
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $size = $sizes[$i]
    $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))   # width (0 means 256)
    $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))   # height
    $writer.Write([byte]0)            # palette colors
    $writer.Write([byte]0)            # reserved
    $writer.Write([uint16]1)          # color planes
    $writer.Write([uint16]32)         # bits per pixel
    $writer.Write([uint32]$pngs[$i].Length)
    $writer.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($png in $pngs) { $writer.Write($png) }
$writer.Flush()

$outDir = Split-Path -Parent $Output
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
[System.IO.File]::WriteAllBytes((Join-Path $RepoRoot $Output), $out.ToArray())
Write-Host ("Wrote {0} ({1:N0} bytes; sizes: {2})" -f $Output, $out.Length, ($sizes -join ', ')) -ForegroundColor Green
