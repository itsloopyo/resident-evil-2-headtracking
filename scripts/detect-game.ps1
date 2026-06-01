#!/usr/bin/env pwsh
# Detect Resident Evil 2 Remake installation directory

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Find-RE2Installation {
    # Check environment variable override first
    if ($env:RE2_PATH) {
        $gamePath = $env:RE2_PATH
        if (Test-RE2Installation $gamePath) {
            return $gamePath
        }
        Write-Warning "RE2_PATH is set but path is invalid: $gamePath"
    }

    # Find Steam installation
    $steamPath = $null

    # Try registry (64-bit)
    try {
        $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam" -ErrorAction Stop).InstallPath
    } catch { }

    # Try registry (32-bit fallback)
    if (-not $steamPath) {
        try {
            $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Valve\Steam" -ErrorAction Stop).InstallPath
        } catch { }
    }

    if (-not $steamPath) {
        return $null
    }

    # Parse libraryfolders.vdf to find all Steam library paths
    $libraryFolders = @($steamPath)
    $vdfPath = Join-Path $steamPath "steamapps\libraryfolders.vdf"

    if (Test-Path $vdfPath) {
        $content = Get-Content $vdfPath -Raw
        $matches = [regex]::Matches($content, '"path"\s+"([^"]+)"')
        foreach ($match in $matches) {
            $path = $match.Groups[1].Value -replace '\\\\', '\'
            if ($path -and (Test-Path $path)) {
                $libraryFolders += $path
            }
        }
    }

    # Known folder names for RE2 Remake
    $folderNames = @(
        "RESIDENT EVIL 2  BIOHAZARD RE2",
        "Resident Evil 2",
        "RESIDENT EVIL 2"
    )

    # Search each library for RE2
    foreach ($library in $libraryFolders) {
        foreach ($folderName in $folderNames) {
            $gamePath = Join-Path $library "steamapps\common\$folderName"
            if (Test-RE2Installation $gamePath) {
                return $gamePath
            }
        }
    }

    return $null
}

function Test-RE2Installation {
    param([string]$path)

    if (-not (Test-Path $path)) {
        return $false
    }

    $exePath = Join-Path $path "re2.exe"
    return (Test-Path $exePath)
}

# Main
$gamePath = Find-RE2Installation

if ($gamePath) {
    Write-Output $gamePath
    exit 0
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  ERROR: Resident Evil 2 not found" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "To fix this:" -ForegroundColor Yellow
    Write-Host "  1. Find your RE2 installation folder" -ForegroundColor White
    Write-Host "  2. Set the environment variable:" -ForegroundColor White
    Write-Host "     `$env:RE2_PATH = 'C:\path\to\RE2'" -ForegroundColor Green
    Write-Host "  3. Run deploy again:" -ForegroundColor White
    Write-Host "     pixi run deploy" -ForegroundColor Green
    Write-Host ""
    exit 1
}
