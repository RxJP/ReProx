from conftest import make_connection, recv_exact


def test_basic_echo(servers):
    """Send a small payload, verify we get back exactly the same bytes."""
    payload = b"Hello, ReProx! \x00\xff\xfe"
    conn = make_connection()
    try:
        conn.sendall(payload)
        result = recv_exact(conn, len(payload))
        assert result == payload, f"Mismatch!\n  sent: {payload!r}\n  recv: {result!r}"
    finally:
        conn.close()
