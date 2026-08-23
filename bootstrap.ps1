param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Triplet = "x64-windows-static-md",
    [switch]$SkipVcpkgUpdate,
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Message,
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
    & $Action

    if ($LASTEXITCODE -ne 0) {
        throw "$Message failed. ExitCode=$LASTEXITCODE"
    }
}

$repoRoot = $PSScriptRoot
$manifestPath = Join-Path $repoRoot "vcpkg.json"
$installRoot = Join-Path $repoRoot "vcpkg_installed"

if (-not (Test-Path $manifestPath)) {
    throw "vcpkg.json not found: $manifestPath"
}

# Always use the shared vcpkg checkout.
$VcpkgRoot = [System.IO.Path]::GetFullPath($VcpkgRoot)
$env:VCPKG_ROOT = $VcpkgRoot

Write-Host "Project Root : $repoRoot" -ForegroundColor Green
Write-Host "VCPKG_ROOT   : $VcpkgRoot" -ForegroundColor Green
Write-Host "Install Root : $installRoot" -ForegroundColor Green
Write-Host "Triplet      : $Triplet" -ForegroundColor Green

if (-not (Test-Path $VcpkgRoot)) {
    Invoke-Step "Cloning vcpkg into $VcpkgRoot" {
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    }
}

$gitDir = Join-Path $VcpkgRoot ".git"
if (-not (Test-Path $gitDir)) {
    throw "VCPKG_ROOT exists but is not a git checkout: $VcpkgRoot"
}

# Update both ports/scripts and vcpkg.exe so VS 2026 support is current.
if (-not $SkipVcpkgUpdate) {
    Invoke-Step "Updating vcpkg repository" {
        git -C $VcpkgRoot pull --ff-only
    }
}

$bootstrapScript = Join-Path $VcpkgRoot "bootstrap-vcpkg.bat"
if (-not (Test-Path $bootstrapScript)) {
    throw "bootstrap-vcpkg.bat not found: $bootstrapScript"
}

Invoke-Step "Bootstrapping vcpkg" {
    & $bootstrapScript -disableMetrics
}

$vcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $vcpkgExe)) {
    throw "vcpkg.exe not found after bootstrap: $vcpkgExe"
}

# Locate Visual Studio 2026 (18.x) with the C++ workload.
$vswhereCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
)

$vswhere = $vswhereCandidates |
    Where-Object { $_ -and (Test-Path $_) } |
    Select-Object -First 1

if (-not $vswhere) {
    throw "vswhere.exe not found. Visual Studio Installer may be missing."
}

$vsPath = & $vswhere `
    -latest `
    -products * `
    -version "[18.0,19.0)" `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($vsPath)) {
    throw "Visual Studio 2026 (18.x) with C++ tools was not found."
}

$vsPath = $vsPath.Trim()
$env:VCPKG_VISUAL_STUDIO_PATH = $vsPath

Write-Host ""
Write-Host "Visual Studio: $vsPath" -ForegroundColor Green

if (-not $SkipInstall) {
    Invoke-Step "Installing manifest dependencies" {
        & $vcpkgExe install `
            --triplet $Triplet `
            --x-manifest-root="$repoRoot" `
            --x-install-root="$installRoot"
    }
}

Write-Host ""
Write-Host "Bootstrap completed." -ForegroundColor Green
Write-Host "VCPKG_ROOT   = $env:VCPKG_ROOT"
Write-Host "VS Path      = $env:VCPKG_VISUAL_STUDIO_PATH"
Write-Host "Install Root = $installRoot"
