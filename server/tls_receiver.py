#!/usr/bin/env python3
"""TLS receiver that logs encrypted keyboard statistics from kbmonitor."""

import argparse
import json
import os
import socket
import ssl
from datetime import datetime, timezone


def parse_args():
    parser = argparse.ArgumentParser(
        description="Receive and log encrypted kbmonitor keyboard statistics"
    )
    parser.add_argument("--host", default="0.0.0.0", help="listen address")
    parser.add_argument("--port", type=int, default=8443, help="listen port")
    parser.add_argument("--cert", required=True, help="server TLS certificate file")
    parser.add_argument("--key", required=True, help="server TLS private key file")
    parser.add_argument("--client-cert", default=None,
                        help="client CA cert to enable mutual TLS (omit for one-way TLS)")
    parser.add_argument("--log", default="kbstats.log",
                        help="file to append received statistics to (default: kbstats.log)")
    return parser.parse_args()


def format_snapshot(data):
    s = data.get("summary", {})
    c = data.get("categories", {})
    return (
        f"host={data.get('host', '?')} "
        f"sample={data.get('sample', '?')} "
        f"interval={data.get('interval_sec', '?')}s "
        f"total={s.get('total_presses', 0)} "
        f"rate={s.get('presses_per_minute', 0)}/min "
        f"last10s={s.get('presses_last_10s', 0)} "
        f"letters={c.get('letters', 0)} "
        f"digits={c.get('digits', 0)} "
        f"mods={c.get('modifiers', 0)}"
    )


def handle_line(line, log_file):
    stamp = datetime.now(timezone.utc).isoformat()
    try:
        data = json.loads(line)
        msg_type = data.get("type", "unknown")
        if msg_type == "session_start":
            display = (
                f"SESSION START host={data.get('host', '?')} "
                f"interval={data.get('interval_sec', '?')}s"
            )
        elif msg_type == "stats_snapshot":
            display = format_snapshot(data)
        else:
            display = line
    except json.JSONDecodeError:
        display = line

    print(f"[{stamp}] {display}", flush=True)
    log_file.write(f"{stamp}\t{line}\n")
    log_file.flush()


def serve(args):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=args.cert, keyfile=args.key)

    client_cert = args.client_cert
    if client_cert and os.path.exists(client_cert):
        context.verify_mode = ssl.CERT_REQUIRED
        context.load_verify_locations(cafile=client_cert)
        print(f"[tls-receiver] mutual TLS enabled — requiring client cert from {client_cert}")
    else:
        print(f"[tls-receiver] warning: client cert not found at {client_cert}; "
              f"run scripts/generate_tls_cert.sh <SERVER_IP> to enable mutual TLS")

    log_path = os.path.abspath(args.log)
    print(f"[tls-receiver] logging statistics to {log_path}")

    with open(args.log, "a") as log_file, \
         socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((args.host, args.port))
        sock.listen(5)
        print(f"[tls-receiver] listening on {args.host}:{args.port}", flush=True)

        while True:
            client, addr = sock.accept()
            with client:
                try:
                    with context.wrap_socket(client, server_side=True) as tls:
                        print(f"\n[tls-receiver] connection from "
                              f"{addr[0]}:{addr[1]}", flush=True)
                        pending = ""
                        while True:
                            data = tls.recv(4096)
                            if not data:
                                break
                            pending += data.decode("utf-8", errors="replace")
                            while "\n" in pending:
                                line, pending = pending.split("\n", 1)
                                if line:
                                    handle_line(line, log_file)
                        if pending:
                            handle_line(pending, log_file)
                        print(f"[tls-receiver] connection closed "
                              f"{addr[0]}:{addr[1]}", flush=True)
                except ssl.SSLError as exc:
                    print(f"[tls-receiver] TLS error from {addr}: {exc}",
                          flush=True)


def main():
    serve(parse_args())


if __name__ == "__main__":
    main()
