import hashlib
import os
import socket

from conftest import make_connection


def test_many_small_writes(servers):
    """Fire tiny sends in a tight loop, verify nothing dropped."""
    num_writes = 5000
    chunk_size = 13
    chunks = [
        i.to_bytes(4, "big") + os.urandom(chunk_size - 4)
        for i in range(num_writes)
    ]
    full_payload = b"".join(chunks)
    expected_hash = hashlib.sha256(full_payload).hexdigest()

    conn = make_connection()
    try:
        for chunk in chunks:
            conn.sendall(chunk)
        conn.shutdown(socket.SHUT_WR)
        result = b""
        while True:
            data = conn.recv(65536)
            if not data:
                break
            result += data
    finally:
        conn.close()

    actual_hash = hashlib.sha256(result).hexdigest()
    assert len(result) == len(full_payload), f"Length mismatch: sent {len(full_payload)}, received {len(result)}"
    assert actual_hash == expected_hash, f"SHA-256 mismatch!\n  expected: {expected_hash}\n  actual:   {actual_hash}"
