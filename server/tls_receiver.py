#!/usr/bin/env python3
"""Minimal live TLS receiver for kbmonitor keylog JSON lines."""

import argparse
import socket
import ssl
from datetime import datetime, timezone


def parse_args():
    parser = argparse.ArgumentParser(description="Receive kbmonitor TLS JSON lines")
    parser.add_argument("--host", default="0.0.0.0", help="listen address")
    parser.add_argument("--port", type=int, default=8443, help="listen port")
    parser.add_argument("--cert", required=True, help="TLS certificate file")
    parser.add_argument("--key", required=True, help="TLS private key file")
    return parser.parse_args()


def main():
    args = parse_args()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certfile=args.cert, keyfile=args.key)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((args.host, args.port))
        sock.listen(5)
        print(f"[tls-receiver] listening on {args.host}:{args.port}")

        while True:
            client, addr = sock.accept()
            with client:
                try:
                    with context.wrap_socket(client, server_side=True) as tls:
                        print(f"\n[tls-receiver] stream from {addr[0]}:{addr[1]}", flush=True)
                        pending = ""
                        while True:
                            data = tls.recv(4096)
                            if not data:
                                break
                            pending += data.decode("utf-8", errors="replace")
                            while "\n" in pending:
                                line, pending = pending.split("\n", 1)
                                if line:
                                    stamp = datetime.now(timezone.utc).isoformat()
                                    print(f"[tls-receiver] {stamp} {line}", flush=True)
                        if pending:
                            stamp = datetime.now(timezone.utc).isoformat()
                            print(f"[tls-receiver] {stamp} {pending}", flush=True)
                except ssl.SSLError as exc:
                    print(f"[tls-receiver] TLS error from {addr}: {exc}")
                    continue


if __name__ == "__main__":
    main()
