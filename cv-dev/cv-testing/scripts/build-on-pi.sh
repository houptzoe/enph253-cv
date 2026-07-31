#!/usr/bin/env bash
# Run this script ON the Raspberry Pi 5 after cloning/copying the project.
# Usage:
#   bash scripts/build-on-pi.sh              # configure + build only (no sudo)
#   bash scripts/build-on-pi.sh --install-deps  # first-time setup (requires sudo)
set -euo pipefail

INSTALL_DEPS=0
if [[ "${1:-}" == "--install-deps" ]]; then
    INSTALL_DEPS=1
elif [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    echo "Usage: $0 [--install-deps]"
    echo "  --install-deps  Run apt-get to install build tools (requires sudo)"
    exit 0
elif [[ $# -gt 0 ]]; then
    echo "Unknown argument: $1" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

if [[ "$INSTALL_DEPS" -eq 1 ]]; then
    echo "==> Installing build dependencies"
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        libopencv-dev \
        libgpiod-dev
else
    for tool in cmake ninja g++; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "Missing $tool. Re-run with --install-deps on the Pi (requires sudo)." >&2
            exit 1
        fi
    done
fi

echo "==> Configuring (rpi-native preset)"
cmake --preset rpi-native

echo "==> Building"
cmake --build build-rpi

echo ""
echo "Done. Run (active model: models/teletubby-yolov8n-320.onnx):"
echo "  ./build-rpi/mars-cv --dual --model models/teletubby-yolov8n-320.onnx"
echo "  ./build-rpi/mars-cv --dual --model models/teletubby-yolov8n-320.onnx --no-esp-handshake"
echo "  ./build-rpi/mars-cv --camera --device 0 --loop --model models/teletubby-yolov8n-320.onnx --no-display"
echo "  ./build-rpi/mars-cv --camera --device 1 --loop --model models/teletubby-yolov8n-320.onnx --no-display"
echo "  ./build-rpi/mars-cv --camera --loop --model models/teletubby-yolov8n-320.onnx --stream-port 8080"
echo "See cv-dev/ESP32-GPIO-HANDSHAKE.md for START/DETECT pin contract."
