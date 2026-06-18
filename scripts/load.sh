#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE="$ROOT/kernel/kbmonitor.ko"

if [[ ! -f "$MODULE" ]]; then
  echo "[load] $MODULE not found; building first"
  make -C "$ROOT" all
fi

if lsmod | grep -q '^kbmonitor'; then
  echo "[load] kbmonitor is already loaded"
else
  echo "[load] Loading kbmonitor module"
  sudo insmod "$MODULE" "$@"
fi

sleep 1

if [[ -e /dev/kbmonitor ]]; then
  sudo chmod 666 /dev/kbmonitor
  echo "[load] /dev/kbmonitor is ready"
else
  echo "[load] ERROR: /dev/kbmonitor was not created" >&2
  exit 1
fi

echo "[load] Recent kernel log:"
sudo dmesg | tail -n 20

