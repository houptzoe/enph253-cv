# Copy project source to a Raspberry Pi over SSH, then build natively on the Pi.
# Usage:
#   .\scripts\deploy-to-pi.ps1 -PiHost zpi@marspi.local -RemoteDir ~/cv-testing
param(
    [Parameter(Mandatory = $true)]
    [string]$PiHost,

    [string]$RemoteDir = "~/mars-cv"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

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

Write-Host "==> Syncing to ${PiHost}:${RemoteDir}"
Invoke-Checked "ssh mkdir" { ssh $PiHost "mkdir -p $RemoteDir" }

# Windows OpenSSH scp does not support --exclude; copy explicit paths only.
$Sources = @(
    "$ProjectRoot\CMakeLists.txt",
    "$ProjectRoot\CMakePresets.json",
    "$ProjectRoot\cmake",
    "$ProjectRoot\include",
    "$ProjectRoot\models",
    "$ProjectRoot\scripts",
    "$ProjectRoot\src"
)
Invoke-Checked "scp" { scp -r @Sources "${PiHost}:${RemoteDir}/" }

Write-Host "==> Normalizing shell script line endings on Pi"
Invoke-Checked "line-ending fix" {
    ssh $PiHost "sed -i 's/\r$//' $RemoteDir/scripts/build-on-pi.sh"
}

Write-Host "==> Building on Pi"
Invoke-Checked "remote build" { ssh $PiHost "cd $RemoteDir && bash scripts/build-on-pi.sh" }

Write-Host ""
Write-Host "Deploy complete. Run on Pi:"
Write-Host "  ssh $PiHost `"$RemoteDir/build-rpi/mars-cv --camera --loop --model $RemoteDir/models/teletubby-yolov8n.onnx`""
Write-Host "  ssh $PiHost `"$RemoteDir/build-rpi/mars-cv --camera --loop --model $RemoteDir/models/teletubby-yolov8n.onnx --no-display`""
