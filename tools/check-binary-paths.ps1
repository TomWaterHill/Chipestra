# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Tom Water Hill
<#
.SYNOPSIS
    Fails if a binary to be shipped contains a path of the machine that built it.

.DESCRIPTION
    Searches every .exe and .dll under -Path (ASCII and UTF-16 text) for: a drive-letter path
    (C:\...), the repository folder, the user profile folder, the user name, and \Users\ paths.
    Debug-information paths in the binaries are the usual source (the PDB path of a .NET assembly,
    the PDB path of a native DLL), so a hit means a build setting is missing, not that a file is wrong.
    tools\package-release.ps1 runs this on the package folder before it finishes.

.PARAMETER Path
    A folder (searched recursively) or a single file.

.EXAMPLE
    pwsh tools/check-binary-paths.ps1 -Path dist/Chipestra-1.002.01
#>

param(
    [Parameter(Mandatory = $true)][string]$Path
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$needles = @($repoRoot, $env:USERPROFILE, $env:USERNAME, $env:LOCALAPPDATA) |
    Where-Object { $_ -and $_.Length -ge 3 } | Select-Object -Unique
$patterns = @(
    '(?i)(?<![A-Za-z0-9])[A-Za-z]:\\[\x21-\x7e]{2,}',
    '(?i)[\\/]Users[\\/][\x21-\x7e]+'
)
foreach ($needle in $needles) { $patterns += '(?i)' + [regex]::Escape($needle) }
$regexes = @($patterns | ForEach-Object { [regex]::new($_) })

$files = if ((Get-Item $Path).PSIsContainer) {
    Get-ChildItem -Path $Path -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' }
} else {
    Get-Item $Path
}

$found = 0
foreach ($file in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $texts = [ordered]@{
        'ascii' = [System.Text.Encoding]::Latin1.GetString($bytes)
    }
    # UTF-16 text can start at an odd offset, so both alignments are searched.
    $even = $bytes.Length - ($bytes.Length % 2)
    $texts['utf-16'] = [System.Text.Encoding]::Unicode.GetString($bytes, 0, $even)
    if ($bytes.Length -gt 2) {
        $odd = ($bytes.Length - 1) - (($bytes.Length - 1) % 2)
        $texts['utf-16 (odd)'] = [System.Text.Encoding]::Unicode.GetString($bytes, 1, $odd)
    }
    foreach ($kind in $texts.Keys) {
        foreach ($regex in $regexes) {
            foreach ($m in $regex.Matches($texts[$kind])) {
                $value = $m.Value
                # The .NET SDK's application host template carries the path of Microsoft's build machine
                # (the PDB path of apphost.exe). It is not ours, and the file is the SDK's, so it is allowed.
                if ($file.Extension -eq '.exe' -and $value -match '^[A-Za-z]:\\a\\_work\\1\\s\\src\\runtime\\.*apphost\.pdb$') { continue }
                if ($value.Length -gt 120) { $value = $value.Substring(0, 120) + '...' }
                Write-Host ("FOUND  {0}  [{1}]  {2}" -f $file.Name, $kind, $value) -ForegroundColor Red
                $found++
            }
        }
    }
}

if ($found -gt 0) {
    Write-Host "$found build-machine path(s) found in the binaries." -ForegroundColor Red
    exit 1
}
Write-Host ("OK: no build-machine path in {0} binary file(s) under {1}." -f @($files).Count, $Path) -ForegroundColor Green
exit 0
