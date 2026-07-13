"""
Echo backend for ReProx integration tests.

Accepts TCP connections on a configurable port (default 10001) and echoes
back every byte it receives, preserving order. Each client is handled in
its own thread so the server can service many concurrent proxy connections.
"""

import argparse
import socket
import threading


def handle_client(conn: socket.socket, addr: tuple) -> None:
    """Read from *conn* and echo everything back until EOF."""
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                break
            # sendall guarantees the entire buffer is written
            conn.sendall(data)
    except OSError:
        pass
    finally:
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="TCP echo server for tests")
    parser.add_argument("--port", type=int, default=10001)
    args = parser.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.port))
    srv.listen(128)
    print(f"Echo backend listening on 127.0.0.1:{args.port}", flush=True)

    while True:
        conn, addr = srv.accept()
        t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
        t.start()


if __name__ == "__main__":
    main()
