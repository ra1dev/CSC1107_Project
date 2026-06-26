#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  echo "Usage: $0 <PI_IP> [PI_USER] [PI_PATH]"
  echo ""
  echo "  PI_IP    IP address of the Raspberry Pi"
  echo "  PI_USER  SSH username on the Pi (default: pi)"
  echo "  PI_PATH  Destination directory on the Pi (default: ~/osproject/server)"
  echo ""
  echo "Example:"
  echo "  $0 192.168.1.20"
  echo "  $0 192.168.1.20 pi ~/myproject/server"
  exit 1
}

if [[ $# -lt 1 ]]; then
  usage
fi

PI_IP="$1"
PI_USER="${2:-pi}"
PI_PATH="${3:-~/osproject/server}"

SERVER_CERT="$ROOT/server/server.crt"
CLIENT_CERT="$ROOT/server/client.crt"
CLIENT_KEY="$ROOT/server/client.key"

# Check all required files exist before attempting transfer
missing=0
for f in "$SERVER_CERT" "$CLIENT_CERT" "$CLIENT_KEY"; do
  if [[ ! -f "$f" ]]; then
    echo "[deploy] Missing: $f"
    missing=1
  fi
done

if [[ $missing -eq 1 ]]; then
  echo "[deploy] Run this first: bash scripts/generate_tls_cert.sh <SERVER_IP>"
  exit 1
fi

echo "[deploy] Copying certificates to $PI_USER@$PI_IP:$PI_PATH"

# Single SSH connection: create directory and transfer all files in one step.
# LC_ALL=C prevents locale mismatch warnings from the remote shell.
tar -czf - -C "$ROOT/server" server.crt client.crt client.key | \
  LC_ALL=C ssh "$PI_USER@$PI_IP" "mkdir -p $PI_PATH && tar -xzf - -C $PI_PATH"

echo "[deploy] Done. Files on Pi:"
echo "[deploy]   $PI_PATH/server.crt"
echo "[deploy]   $PI_PATH/client.crt"
echo "[deploy]   $PI_PATH/client.key"
echo ""
echo "[deploy] On the Pi, run:"
echo "[deploy]   ./user/kbmon_tls $PI_IP 8443"
