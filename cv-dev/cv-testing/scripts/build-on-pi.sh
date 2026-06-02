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
echo "  ./build-rpi/mars-cv              # demo image (headless saves output.jpg)"
echo "  ./build-rpi/mars-cv --camera     # capture from /dev/video0"
