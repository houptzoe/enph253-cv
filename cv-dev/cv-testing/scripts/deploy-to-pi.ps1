# Copy project source to a Raspberry Pi over SSH, then build natively on the Pi.
# Usage:
#   .\scripts\deploy-to-pi.ps1 -PiHost pi@192.168.1.42
param(
    [Parameter(Mandatory = $true)]
    [string]$PiHost,

    [string]$RemoteDir = "~/mars-cv"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

$Exclude = @(
    "build",
    "build-rpi",
    ".git",
    "output.jpg"
)

$ExcludeArgs = $Exclude | ForEach-Object { "--exclude=$_" }

Write-Host "==> Syncing to ${PiHost}:${RemoteDir}"
ssh $PiHost "mkdir -p $RemoteDir"
scp -r @ExcludeArgs "$ProjectRoot\CMakeLists.txt" "$ProjectRoot\CMakePresets.json" `
    "$ProjectRoot\cmake" "$ProjectRoot\include" "$ProjectRoot\scripts" `
    "$ProjectRoot\src" "${PiHost}:${RemoteDir}/"

Write-Host "==> Building on Pi"
ssh $PiHost "cd $RemoteDir && bash scripts/build-on-pi.sh"

Write-Host ""
Write-Host "Deploy complete. Run on Pi:"
Write-Host "  ssh $PiHost `"$RemoteDir/build-rpi/mars-cv`""
