# Stages dist/PaintFromRef-<version>-win64/ from all built Nuke versions
# (build-*/Release) and zips it. Run after scripts\build_all.ps1.
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1
param(
    [string]$Version = "1.1.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$builds = Get-ChildItem (Join-Path $root "build-*") -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName "Release\PaintFromRef.dll") }
if (-not $builds) {
    throw "No build-<ver>/Release/PaintFromRef.dll found. Run scripts\build_all.ps1 first."
}

$name = "PaintFromRef-$Version-win64"
$top = Join-Path $root "dist\$name"
$stage = Join-Path $top "PaintFromRef"
if (Test-Path $top) { Remove-Item $top -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

$nukeVersions = @()
foreach ($b in $builds) {
    # build-16.0v8 -> 16.0
    if ($b.Name -match '^build-([0-9]+\.[0-9]+)v') {
        $mm = $Matches[1]
        $nukeVersions += $mm
        New-Item -ItemType Directory -Force (Join-Path $stage $mm) | Out-Null
        Copy-Item (Join-Path $b.FullName "Release\PaintFromRef.dll") (Join-Path $stage $mm)
    }
}
$nukeVersions = $nukeVersions | Sort-Object -Unique

Copy-Item (Join-Path $root "python\pfr_builder.py") $stage
Copy-Item (Join-Path $root "python\menu.py") $stage
Copy-Item (Join-Path $root "python\init.py") $stage
Copy-Item (Join-Path $root "LICENSE") $stage
Copy-Item (Join-Path $root "README.md") $stage

@"
PaintFromRef $Version  (Windows x64)
Included Nuke versions: $($nukeVersions -join ', ')

Recreates a reference image as paint strokes in a standalone RotoPaint node.

INSTALL
1. Copy the PaintFromRef folder (the one containing init.py) into your
   .nuke folder:  %USERPROFILE%\.nuke\PaintFromRef
2. Add this line to %USERPROFILE%\.nuke\init.py (create the file if needed):

   nuke.pluginAddPath('./PaintFromRef')

3. Restart Nuke. PaintFromRef appears in the Draw menu. The right binary
   for your Nuke version is picked automatically at startup.

USE
Connect a reference image, pick a quality preset, press "Create RotoPaint".
The generated RotoPaint node is standalone - it keeps the image format and
all strokes, so the original image is no longer needed.

License: MIT (see LICENSE). No third-party libraries bundled.
"@ | Set-Content (Join-Path $top "INSTALL.txt")

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $top "*") -DestinationPath $zip

Write-Host "Staged: dist\$name  (Nuke $($nukeVersions -join ', '))"
Write-Host "Zipped: dist\$name.zip"
