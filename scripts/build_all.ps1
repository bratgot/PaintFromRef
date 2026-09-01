# Builds PaintFromRef for every requested Nuke version into build-<ver>/.
#   powershell -ExecutionPolicy Bypass -File scripts\build_all.ps1 [-Install]
# VS2019 toolchain for all versions (NDK_NOTES 1.1; verify if a future
# version's configure rejects it).
param(
    [string[]]$Versions = @("14.1v8", "15.2v9", "16.0v8", "16.1v1", "17.0v4", "17.1v1"),
    [switch]$Install
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$results = @()

foreach ($v in $Versions) {
    $nukeRoot = "C:\Program Files\Nuke$v"
    if (-not (Test-Path "$nukeRoot\cmake\NukeConfig.cmake")) {
        $results += "SKIP  $v (no $nukeRoot\cmake\NukeConfig.cmake)"
        continue
    }
    $bld = Join-Path $root "build-$v"
    Write-Host "=== Nuke $v ==="
    & cmake -G "Visual Studio 16 2019" -A x64 `
        -DNuke_DIR="$($nukeRoot -replace '\\','/')/cmake" -S $root -B $bld
    if ($LASTEXITCODE -ne 0) { $results += "FAIL  $v (configure)"; continue }
    & cmake --build $bld --config Release
    if ($LASTEXITCODE -ne 0) { $results += "FAIL  $v (build)"; continue }
    if ($Install) {
        & cmake --install $bld --config Release --prefix "$env:USERPROFILE/.nuke"
        if ($LASTEXITCODE -ne 0) { $results += "FAIL  $v (install)"; continue }
    }
    $results += "OK    $v"
}

Write-Host ""
Write-Host "==== build_all summary ===="
$results | ForEach-Object { Write-Host $_ }
