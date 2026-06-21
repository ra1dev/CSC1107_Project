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
  echo "[cert] To regenerate with the latest demo SAN, delete those two files and rerun this script."
  echo "[cert] Verified client example:"
  echo "[cert]   ./user/kbmon_tls <SERVER_IP> 8443 --ca-file server/server.crt --server-name kbmonitor-demo"
  exit 0
fi

openssl req \
  -x509 \
  -newkey rsa:2048 \
  -nodes \
  -days 30 \
  -keyout "$KEY" \
  -out "$CERT" \
  -subj "/CN=kbmonitor-demo" \
  -addext "subjectAltName=DNS:kbmonitor-demo,IP:127.0.0.1"

echo "[cert] Wrote $CERT"
echo "[cert] Wrote $KEY"
echo "[cert] Demo TLS name: kbmonitor-demo"
echo "[cert] Verified client example:"
echo "[cert]   ./user/kbmon_tls <SERVER_IP> 8443 --ca-file server/server.crt --server-name kbmonitor-demo"
