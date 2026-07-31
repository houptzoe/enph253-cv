# Copy project source to a Raspberry Pi over SSH, then build natively on the Pi.
# Usage:
#   .\scripts\deploy-to-pi.ps1 -PiHost zpi@marspi.local -RemoteDir /home/zpi/cv-testing
param(
    [Parameter(Mandatory = $true)]
    [string]$PiHost,

    [string]$RemoteDir = "/home/zpi/cv-testing"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

# Single active model on the Pi (keep extras locally if you want rollback).
$ActiveModel = "teletubby-yolov8n-320.onnx"
$ModelPath = Join-Path $ProjectRoot "models\$ActiveModel"

function Invoke-Checked {
    param(
        [string]$Label,
        [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed (exit $LASTEXITCODE)"
    }
}

if (-not (Test-Path $ModelPath)) {
    throw "Missing active model: $ModelPath`nExport with: python export_onnx.py --tag 320"
}

Write-Host "==> Syncing to ${PiHost}:${RemoteDir}"
Invoke-Checked "ssh mkdir" { ssh $PiHost "mkdir -p $RemoteDir" }

# Windows OpenSSH scp does not support --exclude; copy explicit paths only (no models/).
$Sources = @(
    "$ProjectRoot\CMakeLists.txt",
    "$ProjectRoot\CMakePresets.json",
    "$ProjectRoot\cmake",
    "$ProjectRoot\include",
    "$ProjectRoot\scripts",
    "$ProjectRoot\src"
)
Invoke-Checked "scp" { scp -r @Sources "${PiHost}:${RemoteDir}/" }

Write-Host "==> Replacing remote models/ with $ActiveModel only"
Invoke-Checked "wipe models" { ssh $PiHost "rm -rf $RemoteDir/models && mkdir -p $RemoteDir/models" }
Invoke-Checked "scp model" { scp $ModelPath "${PiHost}:${RemoteDir}/models/$ActiveModel" }

Write-Host "==> Normalizing shell script line endings on Pi"
Invoke-Checked "line-ending fix" {
    ssh $PiHost "sed -i 's/\r$//' $RemoteDir/scripts/build-on-pi.sh $RemoteDir/scripts/install-pi-service.sh"
}

Write-Host "==> Building on Pi"
Invoke-Checked "remote build" { ssh $PiHost "cd $RemoteDir && bash scripts/build-on-pi.sh" }

Write-Host ""
Write-Host "Deploy complete. Active model: models/$ActiveModel"
Write-Host "Run on Pi (waits for ESP START on GPIO4 by default):"
Write-Host "  ssh $PiHost `"$RemoteDir/build-rpi/mars-cv --dual --model $RemoteDir/models/$ActiveModel`""
Write-Host "Lab without handshake:"
Write-Host "  ssh $PiHost `"$RemoteDir/build-rpi/mars-cv --dual --model $RemoteDir/models/$ActiveModel --no-esp-handshake`""
Write-Host "See cv-dev/ESP32-GPIO-HANDSHAKE.md for the pin contract."
