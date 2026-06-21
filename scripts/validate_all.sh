#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${LOG_DIR:-$ROOT/validation_logs}"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG="$LOG_DIR/validate-$STAMP.log"

mkdir -p "$LOG_DIR"

run() {
  echo
  echo "[validate] $*"
  "$@"
}

run_capture() {
  echo
  echo "[validate] $*"
  "$@"
}

exec > >(tee "$LOG") 2>&1

echo "[validate] kbmonitor validation started at $(date)"
echo "[validate] Log: $LOG"

cd "$ROOT"

echo
echo "[validate] Normal build"
run make clean
run make

echo
echo "[validate] Fresh module load"
bash "$ROOT/scripts/unload.sh" || true
run bash "$ROOT/scripts/load.sh"

echo
echo "[validate] Device checks"
test -e /dev/kbmonitor
ls -l /dev/kbmonitor

echo
echo "[validate] User-space command checks"
run "$ROOT/user/kbmon" status
run "$ROOT/user/kbmon" summary
run "$ROOT/user/kbmon" keys
run "$ROOT/user/kbmon" heatmap
run "$ROOT/user/kbmon" events
run "$ROOT/user/kbmon" export
run "$ROOT/user/kbmon" reset

echo
echo "[validate] Direct write/read checks"
echo "view status" > /dev/kbmonitor
cat /dev/kbmonitor
echo "view events" > /dev/kbmonitor
cat /dev/kbmonitor

echo
echo "[validate] Kernel log sample"
sudo dmesg | tail -n 60

echo
echo "[validate] Clean unload"
run bash "$ROOT/scripts/unload.sh"

echo
echo "[validate] TEXT_MODE build check"
run make clean
run make TEXT_MODE=1

echo
echo "[validate] TLS certificate helper check"
run bash "$ROOT/scripts/generate_tls_cert.sh"

echo
echo "[validate] Final cleanup"
run make clean

echo
echo "[validate] PASS. Full log saved to $LOG"
