# Shared helpers for the benchmark scripts under bench.
import gzip
import json
import os
import socket
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))


def wait_for_port(port: int, host: str = "127.0.0.1", timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"Port {port} did not become available within {timeout}s")


@dataclass
class _TrackedProc:
    name: str
    popen: subprocess.Popen

    def terminate(self, timeout: float = 3.0) -> None:
        try:
            self.popen.terminate()
            self.popen.wait(timeout=timeout)
        except Exception:
            self.popen.kill()


class ProcessGroup:
    """Tracks subprocesses started during a benchmark run and guarantees
    cleanup via a context manager, so a failed or interrupted run doesn't
    leave an orphaned proxy/backend process behind holding the port open
    for the next run.
    """

    def __init__(self):
        self._procs: list[_TrackedProc] = []

    def start(self, name: str, args: list[str], **popen_kwargs) -> subprocess.Popen:
        popen_kwargs.setdefault("stdout", subprocess.DEVNULL)
        popen_kwargs.setdefault("stderr", subprocess.STDOUT)
        p = subprocess.Popen(args, **popen_kwargs)
        self._procs.append(_TrackedProc(name, p))
        return p

    def __enter__(self) -> "ProcessGroup":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        for proc in reversed(self._procs):
            proc.terminate()


def save_results(results: list, prefix: str = "results") -> str:
    """compresses the results (as JSON) and writes it to a file"""
    out_dir = os.path.join(BENCH_DIR, "bench_results")
    os.makedirs(out_dir, exist_ok=True)
    data = json.dumps(results)
    compressed = gzip.compress(data.encode("utf-8"))
    now = datetime.now().strftime("%d-%m-%Y_%H:%M")
    path = os.path.join(out_dir, f"{prefix}_{now}.gzip")
    with open(path, "wb") as f:
        f.write(compressed)
    return path


def load_results(path: str) -> list:
    """Inverse of save_results, for reading a saved run back for analysis."""
    with open(path, "rb") as f:
        data = gzip.decompress(f.read())
    return json.loads(data.decode("utf-8"))
