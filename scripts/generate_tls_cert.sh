#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_CERT="$ROOT/server/server.crt"
SERVER_KEY="$ROOT/server/server.key"
CLIENT_CERT="$ROOT/server/client.crt"
CLIENT_KEY="$ROOT/server/client.key"

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <SERVER_IP>"
  echo "Example: $0 192.168.1.20"
  exit 1
fi

SERVER_IP="$1"
mkdir -p "$ROOT/server"

# Server certificate — bound to SERVER_IP so the client can verify it
if [[ -f "$SERVER_CERT" && -f "$SERVER_KEY" ]]; then
  echo "[cert] Server certificate already exists: $SERVER_CERT"
else
  openssl req \
    -x509 -newkey rsa:2048 -nodes -days 30 \
    -keyout "$SERVER_KEY" \
    -out    "$SERVER_CERT" \
    -subj   "/CN=$SERVER_IP" \
    -addext "subjectAltName=IP:$SERVER_IP"
  echo "[cert] Wrote $SERVER_CERT"
  echo "[cert] Wrote $SERVER_KEY"
fi

# Client certificate — identifies the Pi to the server (mutual TLS)
if [[ -f "$CLIENT_CERT" && -f "$CLIENT_KEY" ]]; then
  echo "[cert] Client certificate already exists: $CLIENT_CERT"
else
  openssl req \
    -x509 -newkey rsa:2048 -nodes -days 30 \
    -keyout "$CLIENT_KEY" \
    -out    "$CLIENT_CERT" \
    -subj   "/CN=kbmonitor-client"
  echo "[cert] Wrote $CLIENT_CERT"
  echo "[cert] Wrote $CLIENT_KEY"
fi

echo ""
echo "[cert] Mutual TLS setup:"
echo "[cert]   Server machine needs: server.crt  server.key  client.crt"
echo "[cert]   Pi (client) needs:    server.crt  client.crt  client.key"
echo ""
echo "[cert] Server command:"
echo "[cert]   python3 server/tls_receiver.py --cert server/server.crt --key server/server.key --client-cert server/client.crt"
echo ""
echo "[cert] Client command (Pi):"
echo "[cert]   ./user/kbmon_tls $SERVER_IP 8443"
