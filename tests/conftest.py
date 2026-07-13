import os
import socket
import subprocess
import sys
import time
from dataclasses import dataclass

import pytest

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_PROXY_BIN = os.path.join(PROJECT_DIR, "cmake-build-debug", "ReProx")
DEFAULT_BACKEND = os.path.join(SCRIPT_DIR, "dummy_servers", "test_server1.py")

PROXY_PORT = 8700
BACKEND_PORT = 10001


@dataclass(frozen=True)
class ServerHandles:
    """Handles for the running proxy/backend, so tests that need process-level
    introspection (e.g. fd-leak checks) can get at the PIDs without every
    caller needing to know how `servers` starts things."""
    proxy: subprocess.Popen
    backend: subprocess.Popen

    @property
    def proxy_pid(self) -> int:
        return self.proxy.pid

    @property
    def backend_pid(self) -> int:
        return self.backend.pid


def wait_for_port(port: int, host: str = "127.0.0.1", timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"Port {port} did not become available within {timeout}s")


@pytest.fixture(scope="session")
def servers():
    """Starts the backend and proxy servers for the test session."""
    procs = []

    def cleanup():
        for p in procs:
            try:
                p.terminate()
                p.wait(timeout=3)
            except Exception:
                p.kill()

    # Start backend
    backend = subprocess.Popen(
        [sys.executable, DEFAULT_BACKEND, "--port", str(BACKEND_PORT)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    procs.append(backend)
    wait_for_port(BACKEND_PORT)

    # Start proxy
    proxy = subprocess.Popen(
        [DEFAULT_PROXY_BIN],
        cwd=os.path.join(PROJECT_DIR, "cmake-build-debug"),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    procs.append(proxy)
    wait_for_port(PROXY_PORT)

    yield ServerHandles(proxy=proxy, backend=backend)

    cleanup()


def make_connection(port: int = PROXY_PORT) -> socket.socket:
    """Open a TCP connection to the proxy and return the socket."""
    return socket.create_connection(("127.0.0.1", port), timeout=10)


def recv_exact(sock: socket.socket, nbytes: int) -> bytes:
    """Read exactly *nbytes* from *sock*, raising on premature EOF."""
    chunks = []
    remaining = nbytes
    while remaining > 0:
        chunk = sock.recv(min(remaining, 65536))
        if not chunk:
            got = nbytes - remaining
            raise ConnectionError(f"Connection closed after {got}/{nbytes} bytes")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def open_fd_count(pid: int) -> int:
    """Number of open file descriptors for `pid`, via /proc. Linux only —
    matches the rest of this project's platform assumptions (epoll)."""
    return len(os.listdir(f"/proc/{pid}/fd"))
