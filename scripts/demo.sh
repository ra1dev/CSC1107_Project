#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "[demo] Building project"
bash "$ROOT/scripts/build.sh"

echo "[demo] Starting from a clean module state"
bash "$ROOT/scripts/unload.sh" || true

echo "[demo] Loading module"
bash "$ROOT/scripts/load.sh"

echo
echo "[demo] Initial stats"
"$ROOT/user/kbmon" summary

echo
echo "[demo] Press several keys on the USB keyboard, then press Enter here."
read -r _

echo
echo "[demo] Stats after keyboard activity"
"$ROOT/user/kbmon" summary

echo
echo "[demo] Level 2 key-frequency analytics"
"$ROOT/user/kbmon" keys

echo
echo "[demo] Heatmap with per-key counts"
"$ROOT/user/kbmon" heatmap

echo
echo "[demo] Demonstrating write() system call - sending view keys command"
echo "view keys" | sudo tee /dev/kbmonitor > /dev/null
"$ROOT/user/kbmon" raw-keys

echo
echo "[demo] Demonstrating write() system call - resetting counters"
"$ROOT/user/kbmon" reset


echo
echo "[demo] Recent kernel log"
sudo dmesg | tail -n 40

echo
echo "[demo] Unloading module"
bash "$ROOT/scripts/unload.sh"

echo "[demo] Complete"
