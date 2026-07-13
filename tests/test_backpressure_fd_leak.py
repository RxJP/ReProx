"""
Stress test that exercises the proxy under sustained write-side backpressure,
verifying lossless large-payload transfers across repeated connections and
checking for file descriptor leaks after all sessions complete.
"""
import os
import socket
import sys
import threading
import time

import pytest

from conftest import make_connection, open_fd_count

PAYLOAD_SIZE = 4 * 1024 * 1024
READ_CHUNK = 4096
READ_DELAY_S = 0.0015
CYCLES = 15
PER_CYCLE_TIMEOUT_S = 20


def _slow_round_trip(payload: bytes, timeout: float = PER_CYCLE_TIMEOUT_S) -> bytes:
    """Send `payload` through the proxy and read the echo back slowly."""
    conn = make_connection()
    conn.settimeout(timeout)
    received = bytearray()
    try:
        def sender():
            conn.sendall(payload)
            conn.shutdown(socket.SHUT_WR)

        t = threading.Thread(target=sender)
        t.start()

        deadline = time.monotonic() + timeout
        while len(received) < len(payload) and time.monotonic() < deadline:
            try:
                chunk = conn.recv(READ_CHUNK)
            except (socket.timeout, TimeoutError):
                # No more data arrived before the deadline — this is exactly
                # the observable symptom of the EPOLLHUP data-loss bug (the
                # proxy tore the session down mid-transfer and nothing more
                # will ever arrive). Stop here and let the caller's
                # length/content assertions report a clear mismatch instead
                # of propagating a bare timeout.
                break
            if not chunk:
                break
            received.extend(chunk)
            time.sleep(READ_DELAY_S)

        t.join(timeout=5)
    finally:
        conn.close()
    return bytes(received)


@pytest.mark.skipif(sys.platform != "linux", reason="fd accounting relies on /proc")
def test_backpressure_no_data_loss_and_no_fd_leak(servers):
    proxy_pid = servers.proxy_pid

    # Small warm-up cycle so the baseline fd count reflects steady-state
    # (listening socket, epoll fd, etc.) rather than mid-startup state.
    warm_up = os.urandom(1024)
    result = _slow_round_trip(warm_up, timeout=5)
    assert result == warm_up, "warm-up round trip failed before the real test even started"
    time.sleep(0.2)  # let the proxy finish tearing the warm-up session down

    baseline_fds = open_fd_count(proxy_pid)

    for i in range(CYCLES):
        payload = os.urandom(PAYLOAD_SIZE)
        received = _slow_round_trip(payload)

        assert len(received) == len(payload), (
            f"cycle {i}: length mismatch — sent {len(payload)}, got {len(received)} "
            f"the session was torn down while data was still buffered/unread)"
        )
        assert received == payload, f"cycle {i}: byte mismatch despite matching length"

    # Give the proxy a moment to finish closing out the last session before
    # judging the fd count — teardown happens on the next epoll iteration
    # after the final shutdown, not synchronously with the client's close().
    time.sleep(0.5)

    final_fds = open_fd_count(proxy_pid)

    if final_fds != baseline_fds:
        try:
            fd_targets = {
                fd: os.readlink(f"/proc/{proxy_pid}/fd/{fd}")
                for fd in os.listdir(f"/proc/{proxy_pid}/fd")
            }
        except OSError:
            fd_targets = {}
        pytest.fail(
            f"fd leak detected: proxy had {baseline_fds} open fds before {CYCLES} "
            f"connect/transfer/disconnect cycles, {final_fds} after "
            f"(leaked {final_fds - baseline_fds}, "
            f"~{(final_fds - baseline_fds) / CYCLES:.1f} per cycle).\n"
            f"This is the failure signature of the remove_session close() bug.\n"
            f"Currently open fds: {fd_targets}"
        )
