#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Convenience wrapper for the TLS client.  By default it uses --insecure for a
# quick LAN demo; pass --verify to use the generated certificate files.
if [[ $# -lt 1 ]]; then
  echo "Usage: bash scripts/tls_client_demo.sh SERVER_IP [PORT] [--verify]"
  exit 1
fi

SERVER_IP="$1"
PORT="${2:-8443}"
VERIFY_MODE="${3:---insecure}"

if [[ ! -x "$ROOT/user/kbmon_tls" ]]; then
  echo "[tls-client] user/kbmon_tls not found; building first"
  make -C "$ROOT" all
fi

echo "[tls-client] Streaming aggregate keyboard statistics to $SERVER_IP:$PORT"
if [[ "$VERIFY_MODE" == "--verify" ]]; then
  "$ROOT/user/kbmon_tls" "$SERVER_IP" "$PORT" \
    --ca-file "$ROOT/server/server.crt" \
    --server-name kbmonitor-demo
else
  "$ROOT/user/kbmon_tls" "$SERVER_IP" "$PORT" --insecure
fi
