#!/usr/bin/env bash
# Install or update the mars-cv systemd service on the Pi.
# Run on the Pi from the deploy runtime directory:
#   sudo bash install-pi-service.sh
#
# Optional env overrides:
#   RUN_DIR=/home/zpi/mars-cv/run RUN_USER=zpi sudo -E bash install-pi-service.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR}"
RUN_USER="${RUN_USER:-${SUDO_USER:-$USER}}"
SERVICE_NAME="mars-cv.service"
UNIT_PATH="/etc/systemd/system/${SERVICE_NAME}"
ACTIVE_MODEL="teletubby-yolov8n-320.onnx"

if [[ $EUID -ne 0 ]]; then
    echo "Run with sudo: sudo bash $0" >&2
    exit 1
fi

if [[ ! -x "${RUN_DIR}/mars-cv" ]]; then
    echo "Missing executable: ${RUN_DIR}/mars-cv" >&2
    exit 1
fi

if [[ ! -f "${RUN_DIR}/models/${ACTIVE_MODEL}" ]]; then
    echo "Missing model: ${RUN_DIR}/models/${ACTIVE_MODEL}" >&2
    exit 1
fi

TEMPLATE="${RUN_DIR}/mars-cv.service.in"
if [[ ! -f "$TEMPLATE" ]]; then
    echo "Missing template: $TEMPLATE" >&2
    exit 1
fi

# Ensure the service user can access GPIO and camera devices.
usermod -aG gpio,video,render "$RUN_USER" 2>/dev/null || true

sed -e "s|@RUN_DIR@|${RUN_DIR}|g" -e "s|@RUN_USER@|${RUN_USER}|g" "$TEMPLATE" > "$UNIT_PATH"

systemctl daemon-reload
systemctl enable "$SERVICE_NAME"
systemctl restart "$SERVICE_NAME"

echo "Installed ${SERVICE_NAME} for user ${RUN_USER}"
echo "  Runtime: ${RUN_DIR}"
echo "  Model:   ${ACTIVE_MODEL}"
echo "  Idle until ESP START rising edge on BCM GPIO4"
echo ""
systemctl status "$SERVICE_NAME" --no-pager || true
echo ""
echo "Logs: journalctl -u mars-cv -f"
