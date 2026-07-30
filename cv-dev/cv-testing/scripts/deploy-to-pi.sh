#!/usr/bin/env bash
# Copy project source to a Raspberry Pi over SSH, then build natively on the Pi.
# Usage (under Git Bash, avoid unquoted ~/ — it expands to a Windows path):
#   bash scripts/deploy-to-pi.sh zpi@marspi
#   bash scripts/deploy-to-pi.sh zpi@marspi /home/zpi/cv-testing
#   bash scripts/deploy-to-pi.sh zpi@marspi '~/cv-testing'
set -euo pipefail

# Git Bash/MSYS otherwise rewrites paths like /home/zpi/... into Windows paths.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Single active model on the Pi (keep extras locally if you want rollback).
ACTIVE_MODEL="teletubby-yolov8n-320.onnx"

PI_HOST="${1:?Usage: $0 <user@host> [remote-dir]}"
# Prefer /home/zpi/... under Git Bash; unquoted ~/ expands to a Windows path.
REMOTE_DIR="${2:-/home/zpi/cv-testing}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODEL_PATH="${PROJECT_ROOT}/models/${ACTIVE_MODEL}"

if [[ ! -f "$MODEL_PATH" ]]; then
    echo "Missing active model: $MODEL_PATH" >&2
    echo "Export with: python export_onnx.py --tag 320" >&2
    exit 1
fi

echo "==> Syncing to ${PI_HOST}:${REMOTE_DIR}"
ssh "$PI_HOST" "mkdir -p ${REMOTE_DIR}"

scp -r \
    "$PROJECT_ROOT/CMakeLists.txt" \
    "$PROJECT_ROOT/CMakePresets.json" \
    "$PROJECT_ROOT/cmake" \
    "$PROJECT_ROOT/include" \
    "$PROJECT_ROOT/scripts" \
    "$PROJECT_ROOT/src" \
    "${PI_HOST}:${REMOTE_DIR}/"

echo "==> Replacing remote models/ with ${ACTIVE_MODEL} only"
ssh "$PI_HOST" "rm -rf ${REMOTE_DIR}/models && mkdir -p ${REMOTE_DIR}/models"
scp "$MODEL_PATH" "${PI_HOST}:${REMOTE_DIR}/models/${ACTIVE_MODEL}"

echo "==> Normalizing shell script line endings on Pi"
ssh "$PI_HOST" "sed -i 's/\r$//' ${REMOTE_DIR}/scripts/build-on-pi.sh ${REMOTE_DIR}/scripts/deploy-to-pi.sh"

echo "==> Building on Pi (no sudo; use --install-deps on Pi for first-time setup)"
ssh "$PI_HOST" "cd ${REMOTE_DIR} && bash scripts/build-on-pi.sh"

echo ""
echo "Deploy complete. Active model: models/${ACTIVE_MODEL}"
echo "Run on Pi:"
echo "  ssh $PI_HOST \"${REMOTE_DIR}/build-rpi/mars-cv --dual --model ${REMOTE_DIR}/models/${ACTIVE_MODEL}\""
echo "  ssh $PI_HOST \"${REMOTE_DIR}/build-rpi/mars-cv --camera --device 0 --loop --model ${REMOTE_DIR}/models/${ACTIVE_MODEL} --no-display\""
echo "  ssh $PI_HOST \"${REMOTE_DIR}/build-rpi/mars-cv --camera --device 1 --loop --model ${REMOTE_DIR}/models/${ACTIVE_MODEL} --no-display\""
