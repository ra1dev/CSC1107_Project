#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE="$ROOT/kernel/kbmonitor.ko"
NEEDS_BUILD=0

# Rebuild automatically if the module is missing or older than the source.
if [[ ! -f "$MODULE" ]]; then
  echo "[load] $MODULE not found; building first"
  NEEDS_BUILD=1
elif [[ -n "$(find "$ROOT/kernel" "$ROOT/user" -type f \( -name '*.c' -o -name '*.h' -o -name 'Makefile' \) -newer "$MODULE" -print -quit)" ]]; then
  echo "[load] Source files are newer than $MODULE; rebuilding"
  NEEDS_BUILD=1
fi

if [[ "$NEEDS_BUILD" -eq 1 ]]; then
  make -C "$ROOT" all
fi

if lsmod | grep -q '^kbmonitor'; then
  echo "[load] kbmonitor is already loaded"
else
  echo "[load] Loading kbmonitor module"
  sudo insmod "$MODULE" "$@"
fi

sleep 1

# The module creates both devices; relaxed permissions make the demo usable
# without running every user-space command through sudo.
if [[ -e /dev/kbmonitor ]]; then
  sudo chmod 666 /dev/kbmonitor
  echo "[load] /dev/kbmonitor is ready"
else
  echo "[load] ERROR: /dev/kbmonitor was not created" >&2
  exit 1
fi

if [[ -e /dev/kbmonitor_log ]]; then
  sudo chmod 666 /dev/kbmonitor_log
  echo "[load] /dev/kbmonitor_log is ready"
else
  echo "[load] ERROR: /dev/kbmonitor_log was not created" >&2
  exit 1
fi

echo "[load] Recent kernel log:"
sudo dmesg | tail -n 20
