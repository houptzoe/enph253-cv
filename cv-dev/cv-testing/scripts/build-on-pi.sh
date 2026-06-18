#!/usr/bin/env bash
# Run this script ON the Raspberry Pi 5 after cloning/copying the project.
set -euo pipefail

cd "$(dirname "$0")/.."

echo "==> Installing build dependencies (safe to re-run)"
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libopencv-dev

echo "==> Configuring (rpi-native preset)"
cmake --preset rpi-native

echo "==> Building"
cmake --build build-rpi

echo ""
echo "Done. Run:"
echo "  ./build-rpi/mars-cv --camera --loop --model models/teletubby-yolov8n.onnx"
echo "  ./build-rpi/mars-cv --camera --loop --model models/teletubby-yolov8n.onnx --no-display"
echo "  ./build-rpi/mars-cv --camera --loop --model models/teletubby-yolov8n.onnx --confidence 0.6 --debounce 3"
