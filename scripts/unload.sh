#!/usr/bin/env bash
set -euo pipefail

# Remove the module if present.  This script is safe to run before loading so
# demos can start from a known clean state.
if lsmod | grep -q '^kbmonitor'; then
  echo "[unload] Removing kbmonitor module"
  sudo rmmod kbmonitor
else
  echo "[unload] kbmonitor is not loaded"
fi

echo "[unload] Recent kernel log:"
sudo dmesg | tail -n 20
