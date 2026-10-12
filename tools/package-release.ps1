# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
<#
.SYNOPSIS
    Assembles a distributable Release package folder.

.DESCRIPTION
    Copies the Managed Release build output (Chipestra.App's bin
    folder, which already includes chipestra_native.dll, xml/, and the license
    files (THIRD-PARTY-NOTICES.md, licenses/, LICENSE) via the .csproj's own Content/None items - see
    Chipestra.App.csproj) into a clean dist/Chipestra-<a.bbb.cc>/
    folder, strips debug symbols (.pdb), adds the end-user README
    (README.md and README.ja.md from the repository root), THIRD-PARTY-NOTICES.md, the LICENSE and licenses/ texts, and verifies no archive-extension files (a
    proxy for "did a ROM file leak into the build output") ended up in the
    result. Optionally zips the folder.

    This script does NOT build anything itself - it only packages existing
    build output, so run '.\tools\verify.ps1' (or at least a Native CMake
    Release build followed by 'dotnet build Chipestra.slnx -c
    Release') first. Keeping packaging separate from the Build gate avoids
    duplicating that build/test logic here.

.PARAMETER OutputRoot
    Where to create the versioned package folder (repo-relative). Default: dist.

.PARAMETER Zip
    Also produce a dist/Chipestra-<a.bbb.cc>.zip of the assembled folder (the zip's top level is the Chipestra-<a.bbb.cc>\ folder itself).

.EXAMPLE
    pwsh tools/verify.ps1
    pwsh tools/package-release.ps1

.EXAMPLE
    pwsh tools/package-release.ps1 -Zip
#>

param(
    [string]$OutputRoot = "dist",
    [switch]$Zip
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$AppBinDir = "src/Chipestra.App/bin/Release/net10.0-windows"
$VersionProps = "version.props"
$ReadmeSources = @("README.md", "README.ja.md")

if (-not (Test-Path $AppBinDir)) {
    throw "Release build output not found at '$AppBinDir'. Run '.\tools\verify.ps1' (or 'dotnet build Chipestra.slnx -c Release') first."
}

if (-not (Test-Path "$AppBinDir/Chipestra.exe")) {
    throw "Chipestra.exe not found in '$AppBinDir'. Run '.\tools\verify.ps1' first."
}

if (-not (Test-Path "$AppBinDir/chipestra_native.dll")) {
    throw "chipestra_native.dll not found in '$AppBinDir'. The Native (CMake) Release build must run before the Managed build picks it up - run '.\tools\verify.ps1' first."
}

foreach ($readme in $ReadmeSources) {
    if (-not (Test-Path $readme)) {
        throw "$readme not found."
    }
}

# Version: read from the repo-root version.props (single source of truth - it also drives
# AssemblyVersion/FileVersion/InformationalVersion via the App/Core .csproj files) and formatted
# the same way the app shows it: "Ver a.bbb.cc" -> folder name Chipestra-a.bbb.cc (e.g. Chipestra-1.000.00).
[xml]$versionXml = Get-Content $VersionProps -Raw
$versionGroup = @($versionXml.Project.PropertyGroup) | Where-Object { $_.ChipestraMajor -ne $null } | Select-Object -First 1
if (-not $versionGroup) {
    throw "Could not read ChipestraMajor/ChipestraMinor/ChipestraPatch from $VersionProps."
}
$version = "{0}.{1:D3}.{2:D2}" -f [int]$versionGroup.ChipestraMajor, [int]$versionGroup.ChipestraMinor, [int]$versionGroup.ChipestraPatch

$packageName = "Chipestra-$version"
$packageDir = Join-Path $OutputRoot $packageName

Write-Host "Packaging $packageName from $AppBinDir" -ForegroundColor Cyan

if (Test-Path $packageDir) {
    Remove-Item $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

# Copy everything except debug symbols (.pdb) - not needed by end users and
# not something we want to imply is source-equivalent to hand out.
$sourceRoot = (Resolve-Path $AppBinDir).Path
Get-ChildItem -Path $AppBinDir -Recurse -File | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object {
    $relativePath = $_.FullName.Substring($sourceRoot.Length + 1)
    $destPath = Join-Path $packageDir $relativePath
    $destDir = Split-Path -Parent $destPath
    if ($destDir -and -not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }
    Copy-Item $_.FullName $destPath
}

# End-user READMEs: the English and Japanese READMEs from the repository root are the same files shown
# on the repository page; the package carries both.
foreach ($readme in $ReadmeSources) {
    Copy-Item $readme (Join-Path $packageDir $readme) -Force
}
# The READMEs link to docs/xml-format.md, so the package carries it at the same relative path.
$xmlFormatDoc = "docs/xml-format.md"
if (-not (Test-Path $xmlFormatDoc)) {
    throw "$xmlFormatDoc not found."
}
New-Item -ItemType Directory -Path (Join-Path $packageDir "docs") -Force | Out-Null
Copy-Item $xmlFormatDoc (Join-Path $packageDir $xmlFormatDoc) -Force

# License texts that must accompany a distribution: THIRD-PARTY-NOTICES.md, the GPL-2.0 LICENSE (repo root)
# and the full texts of the other licenses (licenses/). The .csproj already puts them next to the EXE; this
# copies the repo-root originals again so the package never depends on that. Missing files are an error.
foreach ($licenseSource in @("THIRD-PARTY-NOTICES.md", "LICENSE", "licenses")) {
    if (-not (Test-Path $licenseSource)) {
        throw "$licenseSource not found - the package must include the license texts."
    }
}
Copy-Item "THIRD-PARTY-NOTICES.md" (Join-Path $packageDir "THIRD-PARTY-NOTICES.md") -Force
Copy-Item "LICENSE" (Join-Path $packageDir "LICENSE") -Force
$packageLicensesDir = Join-Path $packageDir "licenses"
New-Item -ItemType Directory -Path $packageLicensesDir -Force | Out-Null
Copy-Item (Join-Path "licenses" "*") $packageLicensesDir -Recurse -Force

# The old license folder (thirdparty/licenses/) is no longer shipped. Seeing it in the
# package means stale files from an old bin/ were copied; stop instead of distributing them.
if (Test-Path (Join-Path $packageDir "thirdparty/licenses")) {
    Remove-Item $packageDir -Recurse -Force
    throw "Refusing to leave this package assembled: it contained the old thirdparty/licenses/ folder (stale files in '$AppBinDir'). The package folder was removed. Delete the App's bin/obj, rebuild with tools/verify.ps1, then package again."
}

# The native DLL was renamed vgm_native.dll -> chipestra_native.dll. An old DLL in the package means stale
# files from an old bin/ were copied (the app would not load it); stop instead of distributing them.
if (Test-Path (Join-Path $packageDir "vgm_native.dll")) {
    Remove-Item $packageDir -Recurse -Force
    throw "Refusing to leave this package assembled: it contained the old vgm_native.dll (stale files in '$AppBinDir'). The package folder was removed. Delete the App's bin/obj, rebuild with tools/verify.ps1, then package again."
}

# The projects/assemblies were renamed VideoGameMusicPlayer.* -> Chipestra.* (App exe: Chipestra.exe). Any
# VideoGameMusicPlayer.* file in the package is a stale leftover from an old bin/; stop instead of shipping it.
$oldNameFiles = @(Get-ChildItem -Path $packageDir -Recurse -File -Filter "VideoGameMusicPlayer.*")
if ($oldNameFiles.Count -gt 0) {
    Remove-Item $packageDir -Recurse -Force
    throw "Refusing to leave this package assembled: it contained files with the old VideoGameMusicPlayer.* name ($($oldNameFiles.Name -join ', ')) - stale files in '$AppBinDir'. The package folder was removed. Delete the App's bin/obj, rebuild with tools/verify.ps1, then package again."
}

# Defensive re-check that no ROM-like data made it into the
# package. The app never copies ROM files into its own output directory in
# the first place (ROM Root is a user-configured external path, read at
# runtime by ResourceResolver, never bundled) - this should always find
# nothing. It exists to catch a future regression, not a known risk.
$suspiciousExtensions = @('.zip', '.7z', '.rar', '.bin', '.rom')
$suspicious = Get-ChildItem -Path $packageDir -Recurse -File |
    Where-Object { $suspiciousExtensions -contains $_.Extension.ToLowerInvariant() }
if ($suspicious) {
    Write-Host "Files with ROM-like extensions found in the package:" -ForegroundColor Red
    $suspicious | ForEach-Object { Write-Host "  $($_.FullName)" -ForegroundColor Red }
    throw "Refusing to leave this package assembled: found $($suspicious.Count) ROM-like file(s) - see above. Investigate before distributing."
}

Write-Host "Package assembled at: $packageDir" -ForegroundColor Green
$fileCount = (Get-ChildItem -Path $packageDir -Recurse -File).Count
$totalSizeMb = (Get-ChildItem -Path $packageDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host ("  {0} files, {1:N1} MB" -f $fileCount, $totalSizeMb)

if ($Zip) {
    $zipPath = Join-Path $OutputRoot "$packageName.zip"
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }
    # The folder itself goes in as the top level (includeBaseDirectory), so extracting gives one
    # Chipestra-<version>\ folder instead of loose files. ZipFile writes '/' separators.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        (Resolve-Path $packageDir).Path,
        (Join-Path (Resolve-Path $OutputRoot).Path "$packageName.zip"),
        [System.IO.Compression.CompressionLevel]::Optimal,
        $true)
    Write-Host "Zipped: $zipPath" -ForegroundColor Green
}

    # The Release also carries the source that corresponds to the bundled soxr.dll (LGPL).
    & (Join-Path $PSScriptRoot "package-libsoxr-source.ps1") -OutputRoot $OutputRoot -Version $version
