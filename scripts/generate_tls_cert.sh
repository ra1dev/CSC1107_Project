#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CERT="$ROOT/server/server.crt"
KEY="$ROOT/server/server.key"

mkdir -p "$ROOT/server"

if [[ -f "$CERT" && -f "$KEY" ]]; then
  echo "[cert] Existing certificate and key found:"
  echo "[cert] $CERT"
  echo "[cert] $KEY"
  exit 0
fi

openssl req \
  -x509 \
  -newkey rsa:2048 \
  -nodes \
  -days 30 \
  -keyout "$KEY" \
  -out "$CERT" \
  -subj "/CN=kbmonitor-demo"

echo "[cert] Wrote $CERT"
echo "[cert] Wrote $KEY"

