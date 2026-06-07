#!/usr/bin/env pwsh
#Requires -Version 5.1
param(
    [Parameter(Position=0)]
    [string]$Version = "",
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$manifestPath = Join-Path $projectDir "manifest.json"
$modManifestPath = Join-Path $projectDir "launcher-manifest.json"
$cmakePath = Join-Path $projectDir "CMakeLists.txt"
$constantsPath = Join-Path $projectDir "src\core\constants.h"

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

function Get-CurrentVersion {
    $json = Get-Content $manifestPath -Raw | ConvertFrom-Json
    return $json.version
}

function Set-Version {
    param([string]$NewVersion)

    # manifest.json is the read source for package-release.ps1; the other two
    # are kept in lockstep so the built DLL and CMake metadata never drift.
    $json = Get-Content $manifestPath -Raw | ConvertFrom-Json
    $json.version = $NewVersion
    $json | ConvertTo-Json -Depth 10 | Set-Content $manifestPath -NoNewline

    # launcher-manifest.json is the launcher's canonical manifest (the file
    # lopari reads); keep mod_info.version in lockstep so lopari never reads a
    # stale package version.
    $modJson = Get-Content $modManifestPath -Raw | ConvertFrom-Json
    $modJson.mod_info.version = $NewVersion
    $modJson | ConvertTo-Json -Depth 10 | Set-Content $modManifestPath -NoNewline

    (Get-Content $cmakePath -Raw) `
        -replace 'project\(RE2HeadTracking VERSION \d+\.\d+\.\d+ LANGUAGES CXX\)', "project(RE2HeadTracking VERSION $NewVersion LANGUAGES CXX)" `
        | Set-Content $cmakePath -NoNewline

    (Get-Content $constantsPath -Raw) `
        -replace 'RE2HT_VERSION = "\d+\.\d+\.\d+"', "RE2HT_VERSION = `"$NewVersion`"" `
        | Set-Content $constantsPath -NoNewline
}

Write-Host "=== RE2 Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

$currentVersion = Get-CurrentVersion

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: " -NoNewline -ForegroundColor Yellow
    Write-Host $currentVersion -ForegroundColor White
    Write-Host ""
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor Yellow
    exit 0
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

$Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "Error: Invalid version format '$Version'" -ForegroundColor Red
    Write-Host "Use semantic versioning: X.Y.Z" -ForegroundColor Yellow
    exit 1
}

$tagName = "v$Version"

$currentBranch = git rev-parse --abbrev-ref HEAD
if ($currentBranch -ne "main") {
    Write-Host "Error: Must be on 'main' branch (currently on '$currentBranch')" -ForegroundColor Red
    exit 1
}

$status = git status --porcelain
if ($status) {
    Write-Host "Error: Working directory has uncommitted changes" -ForegroundColor Red
    exit 1
}

$existingTag = git tag -l $tagName
if ($existingTag) {
    Write-Host "Error: Tag '$tagName' already exists" -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# Generate CHANGELOG from commits since last tag. This is the gate that
# aborts when there are no user-facing commits, so run it BEFORE mutating
# any version files or building - a failure here then leaves a clean tree
# instead of stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$changelogPath = Join-Path $projectDir "CHANGELOG.md"
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    $firstEntry = "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
    Set-Content $changelogPath $firstEntry
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version = $Version
            ArtifactPaths = @("src/", "cameraunlock-core/", "scripts/install.cmd", "scripts/uninstall.cmd")
        }
        New-ChangelogFromCommits @changelogArgs
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
    }
}

# Update version
Write-Host "Updating version to $Version..." -ForegroundColor Cyan
Set-Version $Version

# Update MOD_VERSION in install.cmd
$installCmdPath = Join-Path $scriptDir "install.cmd"
(Get-Content $installCmdPath -Raw) -replace 'set "MOD_VERSION=.*?"', "set `"MOD_VERSION=$Version`"" | Set-Content $installCmdPath -NoNewline

# Release build - fail the release if the new version doesn't compile
Write-Host "Building release configuration..." -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: release build failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

# Commit
Write-Host "Committing version change..." -ForegroundColor Cyan
git add $manifestPath $modManifestPath $cmakePath $constantsPath $changelogPath $installCmdPath
git commit -m "Release v$Version"

# Tag
Write-Host "Creating tag $tagName..." -ForegroundColor Cyan
git tag $tagName

# Push
Write-Host "Pushing to GitHub..." -ForegroundColor Cyan
git push origin main
git push origin $tagName

Write-Host ""
Write-Host "Release $tagName initiated!" -ForegroundColor Green
