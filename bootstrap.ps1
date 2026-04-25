$ErrorActionPreference = "Stop"

param(
    [string]$VcpkgRoot = "$PSScriptRoot\\.vcpkg",
    [string]$Triplet = "x64-windows-static-md",
    [switch]$SkipInstall
)

function Invoke-Step {
    param(
        [string]$Message,
        [scriptblock]$Action
    )

    Write-Host "==> $Message" -ForegroundColor Cyan
    & $Action
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifestPath = Join-Path $repoRoot "vcpkg.json"

if (-not (Test-Path $manifestPath)) {
    throw "vcpkg.json not found at $manifestPath"
}

if (-not (Test-Path $VcpkgRoot)) {
    Invoke-Step "Cloning vcpkg into $VcpkgRoot" {
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    }
}

$bootstrapScript = Join-Path $VcpkgRoot "bootstrap-vcpkg.ps1"
if (-not (Test-Path $bootstrapScript)) {
    throw "bootstrap-vcpkg.ps1 not found under $VcpkgRoot"
}

Invoke-Step "Bootstrapping vcpkg" {
    & $bootstrapScript -disableMetrics
}

$env:VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$vcpkgExe = Join-Path $env:VcpkgRoot "vcpkg.exe"

if (-not (Test-Path $vcpkgExe)) {
    throw "vcpkg.exe not found after bootstrap"
}

if (-not $SkipInstall) {
    Invoke-Step "Installing manifest dependencies for triplet $Triplet" {
        & $vcpkgExe install --triplet $Triplet --x-manifest-root=$repoRoot
    }
}

Write-Host ""
Write-Host "VcpkgRoot = $env:VcpkgRoot" -ForegroundColor Green
Write-Host "Triplet   = $Triplet" -ForegroundColor Green
Write-Host ""
Write-Host "This manifest covers common third-party packages used by the repo." -ForegroundColor Yellow
Write-Host "PhysX is not installed through this script and still needs to be prepared separately for JamPx/TestApp." -ForegroundColor Yellow
