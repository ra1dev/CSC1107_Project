#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-kbmonitor-codes.zip}"

# Package source files for submission while excluding generated build products,
# certificates, validation logs and other local-only artifacts.
cd "$ROOT"
rm -f "$OUT"

zip -r "$OUT" \
  .gitignore Makefile README.md \
  kernel user scripts docs server \
  -x "*/.git/*" \
  -x "teammate_version/*" \
  -x "validation_logs/*" \
  -x "*/__pycache__/*" \
  -x "*.ko" \
  -x "*.o" \
  -x "*.cmd" \
  -x "*.mod" \
  -x "*.mod.c" \
  -x "Module.symvers" \
  -x "modules.order" \
  -x "user/kbmon" \
  -x "user/kbmon_tls" \
  -x "server/*.crt" \
  -x "server/*.key" \
  -x "*.zip"

echo "[package] Wrote $ROOT/$OUT"
