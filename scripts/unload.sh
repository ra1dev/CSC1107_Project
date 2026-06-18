#!/usr/bin/env bash
set -euo pipefail

if lsmod | grep -q '^kbmonitor'; then
  echo "[unload] Removing kbmonitor module"
  sudo rmmod kbmonitor
else
  echo "[unload] kbmonitor is not loaded"
fi

echo "[unload] Recent kernel log:"
sudo dmesg | tail -n 20

