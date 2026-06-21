#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "[setup] Installing Raspberry Pi build/demo dependencies"
sudo apt update
sudo apt install -y \
  build-essential \
  raspberrypi-kernel-headers \
  libssl-dev \
  python3 \
  openssl \
  zip

echo "[setup] Checking kernel header path"
KDIR="${KDIR:-/lib/modules/$(uname -r)/build}"
if [[ ! -d "$KDIR" ]]; then
  echo "[setup] ERROR: kernel headers not found at $KDIR" >&2
  echo "[setup] Reboot the Pi after installing headers, then rerun this script." >&2
  exit 1
fi

echo "[setup] Kernel headers found: $KDIR"
echo "[setup] Project root: $ROOT"
echo "[setup] Next step: bash scripts/build.sh"
