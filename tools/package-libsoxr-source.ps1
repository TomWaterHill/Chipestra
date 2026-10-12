# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
<#
.SYNOPSIS
    Creates libsoxr-source-for-Chipestra-<a.bbb.cc>.zip, the source that corresponds to the bundled soxr.dll.

.DESCRIPTION
    Chipestra bundles soxr.dll (libsoxr, LGPL-2.1-or-later). Each Release carries this zip so that the
    source of the library is available with the binary. The zip holds one folder,
    libsoxr-source-for-Chipestra-<version>\, with:
      README.txt                       (tools/libsoxr-source-README.template.txt with the version filled in)
      soxr-0.1.3-Source.tar.xz         the original source, as vcpkg downloaded it (checked against its SHA-512)
      vcpkg-port-soxr\                 the vcpkg port files (portfile.cmake, vcpkg.json, four patches)

    Everything except README.txt is taken, unchanged, from the vcpkg that built the library: the
    download folder (default %LOCALAPPDATA%\vcpkg\downloads, or $env:VCPKG_DOWNLOADS) and the port
    version 0.1.3 port-version 9 under %LOCALAPPDATA%\vcpkg\registries\git-trees\ (or -PortDir).
    Run the Native build (tools\verify.ps1) once first so vcpkg has downloaded the source.

.PARAMETER OutputRoot
    Where to write the zip. Default: dist.

.PARAMETER Version
    The Chipestra version as a.bbb.cc. Default: formatted from version.props.

.PARAMETER PortDir
    The folder holding the soxr port files. Default: searched under the vcpkg registries.
#>

param(
    [string]$OutputRoot = "dist",
    [string]$Version = "",
    [string]$PortDir = ""
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$TarName = "soxr-0.1.3-Source.tar.xz"
$TarSha512 = "f4883ed298d5650399283238aac3dbe78d605b988246bea51fa343d4a8ce5ce97c6e143f6c3f50a3ff81795d9c19e7a07217c586d4020f6ced102aceac46aaa8"
$PortFiles = @("001_initialize-resampler.patch", "002_disable_warning.patch", "003_detect_arm.patch",
               "004_install-pkgconfig-windows.patch", "portfile.cmake", "vcpkg.json")

if (-not $Version) {
    [xml]$versionXml = Get-Content "version.props" -Raw
    $group = @($versionXml.Project.PropertyGroup) | Where-Object { $_.ChipestraMajor -ne $null } | Select-Object -First 1
    if (-not $group) { throw "Could not read the version from version.props." }
    $Version = "{0}.{1:D3}.{2:D2}" -f [int]$group.ChipestraMajor, [int]$group.ChipestraMinor, [int]$group.ChipestraPatch
}

$downloads = if ($env:VCPKG_DOWNLOADS) { $env:VCPKG_DOWNLOADS } else { Join-Path $env:LOCALAPPDATA "vcpkg\downloads" }
$tarPath = Join-Path $downloads $TarName
if (-not (Test-Path $tarPath)) {
    throw "$TarName not found in '$downloads'. Run '.\tools\verify.ps1' once so vcpkg downloads it (or set VCPKG_DOWNLOADS)."
}
$actual = (Get-FileHash $tarPath -Algorithm SHA512).Hash.ToLowerInvariant()
if ($actual -ne $TarSha512) {
    throw "$tarPath does not match the expected SHA-512 ($TarSha512); found $actual."
}

if (-not $PortDir) {
    $trees = Join-Path $env:LOCALAPPDATA "vcpkg\registries\git-trees"
    $found = @()
    if (Test-Path $trees) {
        foreach ($dir in Get-ChildItem $trees -Directory) {
            $manifest = Join-Path $dir.FullName "vcpkg.json"
            if (-not (Test-Path $manifest)) { continue }
            $json = Get-Content $manifest -Raw | ConvertFrom-Json
            if ($json.name -eq "soxr" -and $json.version -eq "0.1.3" -and $json.'port-version' -eq 9) { $found += $dir.FullName }
        }
    }
    if ($found.Count -eq 0) { throw "The vcpkg soxr port (0.1.3, port-version 9) was not found under '$trees'. Pass -PortDir." }
    $PortDir = $found[0]
}
foreach ($file in $PortFiles) {
    if (-not (Test-Path (Join-Path $PortDir $file))) { throw "Port file '$file' not found in '$PortDir'." }
}

$name = "libsoxr-source-for-Chipestra-$Version"
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("libsoxr-src-" + [guid]::NewGuid().ToString("N"))
try {
    $root = Join-Path $stage $name
    New-Item -ItemType Directory -Path (Join-Path $root "vcpkg-port-soxr") -Force | Out-Null
    $template = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot "libsoxr-source-README.template.txt"))
    [System.IO.File]::WriteAllText((Join-Path $root "README.txt"), $template.Replace("@VERSION@", $Version), (New-Object System.Text.UTF8Encoding($false)))
    Copy-Item $tarPath (Join-Path $root $TarName)
    foreach ($file in $PortFiles) {
        Copy-Item (Join-Path $PortDir $file) (Join-Path $root "vcpkg-port-soxr\$file")
    }

    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $zipPath = Join-Path (Resolve-Path $OutputRoot).Path "$name.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($root, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $true)
    Write-Host "libsoxr source package: $zipPath" -ForegroundColor Green
}
finally {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
}
