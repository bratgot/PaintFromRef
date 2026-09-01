# Runs the headless end-to-end test against each version's fresh build.
#   powershell -ExecutionPolicy Bypass -File scripts\test_all.ps1
param(
    [string[]]$Versions = @("14.1v8", "15.2v9", "16.0v8", "16.1v1", "17.0v4", "17.1v1")
)

$root = Split-Path -Parent $PSScriptRoot
$results = @()

foreach ($v in $Versions) {
    $exe = Get-ChildItem "C:\Program Files\Nuke$v\Nuke*.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^Nuke[0-9.]+\.exe$' } | Select-Object -First 1
    $dll = Join-Path $root "build-$v\Release\PaintFromRef.dll"
    if (-not $exe) { $results += "SKIP  $v (no Nuke exe)"; continue }
    if (-not (Test-Path $dll)) { $results += "SKIP  $v (no build)"; continue }

    $env:PFR_PLUGIN_DIR = (Join-Path $root "build-$v\Release") -replace '\\', '/'
    $env:PFR_BUILDER_DIR = (Join-Path $root "python") -replace '\\', '/'
    $env:PFR_TAG = $v
    Write-Host "=== Nuke $v ($($exe.FullName)) ==="
    & $exe.FullName -ti (Join-Path $root "test\test_end_to_end.py") 2>&1 |
        Select-Object -Last 6
    if ($LASTEXITCODE -eq 0) { $results += "PASS  $v" }
    else { $results += "FAIL  $v (exit $LASTEXITCODE)" }
}

$env:PFR_PLUGIN_DIR = $null
$env:PFR_BUILDER_DIR = $null
$env:PFR_TAG = $null

Write-Host ""
Write-Host "==== test_all summary ===="
$results | ForEach-Object { Write-Host $_ }
