#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The text demo is intentionally local and requires a TEXT_MODE=1 kernel build.
if [[ ! -e /dev/kbmonitor ]]; then
  echo "[text-demo] /dev/kbmonitor does not exist. Load the module first."
  echo "[text-demo] Build/load with: make clean && make TEXT_MODE=1 && sudo insmod kernel/kbmonitor.ko"
  exit 1
fi

echo "[text-demo] Enabling local text demo mode"
"$ROOT/user/kbmon" text >/dev/null

echo "[text-demo] Clearing previous text demo buffer"
"$ROOT/user/kbmon" clear-text

echo
echo "[text-demo] Type a short demo phrase, then press Enter."
echo "[text-demo] The phrase is read by this script so it is not executed as a shell command."
# Reading through the shell gives the keyboard driver real keypresses to observe
# while preventing the typed phrase from being interpreted as a command.
read -r -p "> " _

echo
echo "[text-demo] Captured local demo text:"
"$ROOT/user/kbmon" text

echo
echo "[text-demo] Disabling local text demo mode"
"$ROOT/user/kbmon" disable-text
