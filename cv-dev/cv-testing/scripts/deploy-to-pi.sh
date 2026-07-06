#!/usr/bin/env bash
# Copy project source to a Raspberry Pi over SSH, then build natively on the Pi.
# Usage:
#   bash scripts/deploy-to-pi.sh zpi@marspi ~/cv-testing
set -euo pipefail

PI_HOST="${1:?Usage: $0 <user@host> [remote-dir]}"
REMOTE_DIR="${2:-~/cv-testing}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "==> Syncing to ${PI_HOST}:${REMOTE_DIR}"
ssh "$PI_HOST" "mkdir -p $REMOTE_DIR"

scp -r \
    "$PROJECT_ROOT/CMakeLists.txt" \
    "$PROJECT_ROOT/CMakePresets.json" \
    "$PROJECT_ROOT/cmake" \
    "$PROJECT_ROOT/include" \
    "$PROJECT_ROOT/models" \
    "$PROJECT_ROOT/scripts" \
    "$PROJECT_ROOT/src" \
    "${PI_HOST}:${REMOTE_DIR}/"

echo "==> Normalizing shell script line endings on Pi"
ssh "$PI_HOST" "sed -i 's/\r$//' $REMOTE_DIR/scripts/build-on-pi.sh $REMOTE_DIR/scripts/deploy-to-pi.sh"

echo "==> Building on Pi (no sudo; use --install-deps on Pi for first-time setup)"
ssh "$PI_HOST" "cd $REMOTE_DIR && bash scripts/build-on-pi.sh"

echo ""
echo "Deploy complete. Run on Pi:"
echo "  ssh $PI_HOST \"$REMOTE_DIR/build-rpi/mars-cv --camera --loop --model $REMOTE_DIR/models/teletubby-yolov8n.onnx --no-display\""
