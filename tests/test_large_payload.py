import hashlib
import os
import socket

from conftest import make_connection


def test_large_payload(servers):
    """Send several MB of random-ish data and verify integrity via SHA-256."""
    size_mb = 512.0
    size = int(size_mb * 1024 * 1024)
    payload = os.urandom(size)
    expected_hash = hashlib.sha256(payload).hexdigest()

    import threading

    conn = make_connection()
    try:
        def send_data():
            conn.sendall(payload)
            conn.shutdown(socket.SHUT_WR)

        sender = threading.Thread(target=send_data)
        sender.start()

        result = bytearray()
        while True:
            chunk = conn.recv(65536)
            if not chunk:
                break
            result.extend(chunk)

        sender.join()
    finally:
        conn.close()

    actual_hash = hashlib.sha256(bytes(result)).hexdigest()
    assert len(result) == size, f"Length mismatch: sent {size}, received {len(result)}"
    assert actual_hash == expected_hash, f"SHA-256 mismatch!\n  expected: {expected_hash}\n  actual:   {actual_hash}"
