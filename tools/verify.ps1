# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
<#
.SYNOPSIS
    Chipestra Build gate (see README.md, "Building from source").

.DESCRIPTION
    Native (CMake build + native test executable) -> Managed (dotnet build) ->
    Tests (dotnet test) -> git status check, as one script. Each step runs as an
    independent command (the dotnet SDK cannot build the C++ project as part of
    the solution). The first failing step stops the script with a non-zero exit code.

.PARAMETER Longrun
    Also build and run chipestra_stress_longrun.exe (handle/thread leak check over
    repeated LoadGame/Play/SelectTrack/Stop cycles, plus sustained real WASAPI
    playback watching QueueOverflowCount). Off by default because it takes 30-60
    minutes; for longer runs raise -LongrunSustainSeconds or start the exe directly.
    Needs the optional stress-tool sources; skipped when they are absent.

.EXAMPLE
    pwsh tools/verify.ps1

.EXAMPLE
    pwsh tools/verify.ps1 -Longrun
#>

param(
    [switch]$Longrun,
    [int]$LongrunCycles = 150,
    [int]$LongrunSustainSeconds = 1800,
    [int]$LongrunSamplePeriodSeconds = 60
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$script:StepNumber = 0

function Write-StepHeader {
    param([string]$Name)
    $script:StepNumber++
    Write-Host ""
    Write-Host "==> [$script:StepNumber] $Name" -ForegroundColor Cyan
}

function Write-StepOk {
    param([string]$Name)
    Write-Host "OK  [$script:StepNumber] $Name" -ForegroundColor Green
}

function Write-StepFail {
    param([string]$Name, [string]$Detail)
    Write-Host ""
    Write-Host "FAIL [$script:StepNumber] $Name" -ForegroundColor Red
    if ($Detail) {
        Write-Host $Detail -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "Build gate: FAIL (stopped at step $script:StepNumber - $Name)" -ForegroundColor Red
}

function Invoke-BuildGateStep {
    param(
        [string]$Name,
        [scriptblock]$ScriptBlock
    )
    Write-StepHeader -Name $Name
    $global:LASTEXITCODE = 0
    & $ScriptBlock
    if ($LASTEXITCODE -ne 0) {
        Write-StepFail -Name $Name -Detail "exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
    Write-StepOk -Name $Name
}

Write-Host "Chipestra Build gate" -ForegroundColor Yellow
Write-Host "Repo root: $RepoRoot"

# libsoxr via vcpkg manifest mode (src/Chipestra.Native/vcpkg.json):
# resolves a vcpkg toolchain file so CMake can find/build the soxr port.
# Prefers $env:VCPKG_ROOT (a standalone vcpkg checkout, the common convention)
# and falls back to the vcpkg instance bundled with Visual Studio (located via
# vswhere.exe - no separate vcpkg install is required on a fresh machine).
function Find-VcpkgToolchain {
    if ($env:VCPKG_ROOT -and (Test-Path "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake")) {
        return "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath "VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }
    return $null
}
$script:VcpkgToolchain = Find-VcpkgToolchain
if (-not $script:VcpkgToolchain) {
    Write-StepFail -Name "vcpkg toolchain detection" `
        -Detail "Could not find a vcpkg installation (checked `$env:VCPKG_ROOT and the Visual Studio-bundled copy via vswhere). libsoxr is a vcpkg manifest dependency - install vcpkg or set VCPKG_ROOT."
    exit 1
}
Write-Host "Using vcpkg toolchain: $script:VcpkgToolchain"

# 1. Native (Release, x64, C++20) - CMake owns this build, never dotnet build.
Invoke-BuildGateStep -Name "Native: cmake configure (x64)" -ScriptBlock {
    cmake -S src/Chipestra.Native -B build/native -A x64 "-DCMAKE_TOOLCHAIN_FILE=$script:VcpkgToolchain" -DVCPKG_TARGET_TRIPLET=x64-windows
}
Invoke-BuildGateStep -Name "Native: cmake build (Release)" -ScriptBlock {
    cmake --build build/native --config Release
}
Invoke-BuildGateStep -Name "Native: chipestra_native_tests.exe (lifecycle/determinism)" -ScriptBlock {
    & "$RepoRoot/build/native/Release/chipestra_native_tests.exe"
}

if ($Longrun -and -not (Test-Path "$RepoRoot/src/Chipestra.Native/tools/CMakeLists.txt")) {
    Write-Host "SKIP: -Longrun needs src/Chipestra.Native/tools (the diagnostic tools are not part of this checkout)." -ForegroundColor Yellow
} elseif ($Longrun) {
    Invoke-BuildGateStep -Name "Native: cmake build chipestra_stress_longrun (Release)" -ScriptBlock {
        cmake --build build/native --config Release --target chipestra_stress_longrun
    }
    Invoke-BuildGateStep -Name "Native: chipestra_stress_longrun.exe (long-run stress test, ~$([math]::Round($LongrunSustainSeconds / 60)) min)" -ScriptBlock {
        & "$RepoRoot/build/native/Release/chipestra_stress_longrun.exe" $LongrunCycles $LongrunSustainSeconds $LongrunSamplePeriodSeconds
    }
}

# 2. Managed (Core / App / Core.Tests) - dotnet SDK owns this build, Native is excluded
#    from Chipestra.slnx's build graph (the dotnet SDK cannot build the C++ project).
Invoke-BuildGateStep -Name "Managed: dotnet build (Release)" -ScriptBlock {
    dotnet build Chipestra.slnx -c Release
}

# 2.5. Deployment check: the App's bin folder must hold the same chipestra_native.dll
#      that step 1 built. An incremental dotnet build can leave a stale copy even
#      though the Native build and tests passed.
Write-StepHeader -Name "Deployment: chipestra_native.dll MD5 match (build output vs App bin)"
$nativeBuildDll = "$RepoRoot/build/native/Release/chipestra_native.dll"
$appBinDll = "$RepoRoot/src/Chipestra.App/bin/Release/net10.0-windows/chipestra_native.dll"
if (-not (Test-Path $nativeBuildDll)) {
    Write-StepFail -Name "Deployment: chipestra_native.dll MD5 match" -Detail "Native build DLL not found: $nativeBuildDll"
    exit 1
}
if (-not (Test-Path $appBinDll)) {
    Write-StepFail -Name "Deployment: chipestra_native.dll MD5 match" -Detail "App bin DLL not found: $appBinDll"
    exit 1
}
$hashNative = (Get-FileHash -Algorithm MD5 -Path $nativeBuildDll).Hash
$hashApp = (Get-FileHash -Algorithm MD5 -Path $appBinDll).Hash
if ($hashNative -ne $hashApp) {
    Write-StepFail -Name "Deployment: chipestra_native.dll MD5 match" `
        -Detail "build/native/Release ($hashNative) != App bin ($hashApp) - the App is NOT running the DLL you just built. Re-run 'dotnet build Chipestra.slnx -c Release' (or delete the App's bin/obj) and try again."
    exit 1
}
Write-StepOk -Name "Deployment: chipestra_native.dll MD5 match ($hashNative)"

# 2.6. version.props shape: three integers, Major >= 0, Minor 0-999, Patch 0-99.
#      Format check only - whether the number was bumped for a change is a commit-review matter.
Write-StepHeader -Name "Version: version.props format (Major >= 0, Minor 0-999, Patch 0-99)"
$versionPropsPath = "$RepoRoot/version.props"
if (-not (Test-Path $versionPropsPath)) {
    Write-StepFail -Name "Version: version.props format" -Detail "version.props not found at the repo root."
    exit 1
}
[xml]$versionPropsXml = Get-Content $versionPropsPath -Raw
$versionGroup = @($versionPropsXml.Project.PropertyGroup) | Where-Object { $_.ChipestraMajor -ne $null } | Select-Object -First 1
$versionProblems = @()
foreach ($part in @(@('ChipestraMajor', 0, [int]::MaxValue), @('ChipestraMinor', 0, 999), @('ChipestraPatch', 0, 99))) {
    $raw = if ($versionGroup) { [string]$versionGroup.($part[0]) } else { '' }
    $parsed = 0
    if (-not [int]::TryParse($raw.Trim(), [ref]$parsed) -or $raw.Trim() -notmatch '^[0-9]+$') {
        $versionProblems += "$($part[0]) is '$raw' (must be a non-negative integer)"
    } elseif ($parsed -lt $part[1] -or $parsed -gt $part[2]) {
        $versionProblems += "$($part[0]) is $parsed (allowed range $($part[1])-$($part[2]))"
    }
}
if ($versionProblems.Count -gt 0) {
    Write-StepFail -Name "Version: version.props format" -Detail (($versionProblems | ForEach-Object { "  $_" }) -join "`n")
    exit 1
}
Write-StepOk -Name ("Version: version.props = {0}.{1}.{2}" -f $versionGroup.ChipestraMajor.Trim(), $versionGroup.ChipestraMinor.Trim(), $versionGroup.ChipestraPatch.Trim())

# 3. Tests
Invoke-BuildGateStep -Name "Tests: dotnet test (Release)" -ScriptBlock {
    dotnet test tests/Chipestra.Core.Tests -c Release
}
Invoke-BuildGateStep -Name "Tests: Chipestra.App.Tests (Release)" -ScriptBlock {
    dotnet test tests/Chipestra.App.Tests -c Release
}

# 4. No unexpected untracked/generated files left behind.
Write-StepHeader -Name "Git: unexpected file check (git status --porcelain)"
$global:LASTEXITCODE = 0
$gitStatus = git status --porcelain
if ($LASTEXITCODE -ne 0) {
    Write-StepFail -Name "Git: unexpected file check" -Detail "git status --porcelain failed (exit code $LASTEXITCODE)"
    exit $LASTEXITCODE
}
if ($gitStatus) {
    $detail = ($gitStatus | ForEach-Object { "  $_" }) -join "`n"
    Write-StepFail -Name "Git: unexpected file check" -Detail "Unexpected untracked/modified files:`n$detail"
    exit 1
}
Write-StepOk -Name "Git: no unexpected files"

Write-Host ""
Write-Host "Build gate: PASS" -ForegroundColor Green
exit 0
