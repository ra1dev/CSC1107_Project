#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KDIR="${KDIR:-/lib/modules/$(uname -r)/build}"

# Build both parts of the project using the running kernel's header directory
# unless KDIR is supplied explicitly by the user.
echo "[build] Building kernel module and user-space program"
echo "[build] Using KDIR=$KDIR"
make -C "$ROOT" all KDIR="$KDIR"

echo "[build] Done"
