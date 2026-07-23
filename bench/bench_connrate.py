import json
import os
import subprocess

from lib import ProcessGroup, wait_for_port, save_results

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_PROXY_BIN = os.path.join(PROJECT_DIR, "cmake-build-release", "ReProx")
LOADGEN_BIN = os.path.join(SCRIPT_DIR, "bin", "connrate_loadgen")
ECHO_BACKEND_BIN = os.path.join(SCRIPT_DIR, "bin", "echo_backend")

PROXY_PORT = 8700
BACKEND_PORT = 10001

DEFAULT_WORKER_COUNTS = [1, 4, 16, 64]


def _run_loadgen(port: int, workers: int, duration: int) -> dict:
    proc = subprocess.run(
        [
            LOADGEN_BIN,
            "--host", "127.0.0.1", "--port", str(port),
            "--workers", str(workers), "--duration", str(duration),
        ],
        capture_output=True, text=True,
    )
    return json.loads(proc.stdout)


def run(
        proxy_bin: str = DEFAULT_PROXY_BIN,
        test_duration: int = 10,
        worker_counts: list[int] | None = None,
) -> list[dict]:
    if worker_counts is None:
        worker_counts = DEFAULT_WORKER_COUNTS

    results = []
    with ProcessGroup() as procs:
        procs.start("echo-backend", [ECHO_BACKEND_BIN, "--port", str(BACKEND_PORT)])
        wait_for_port(BACKEND_PORT)

        procs.start("proxy", [proxy_bin], cwd=os.path.dirname(proxy_bin))
        wait_for_port(PROXY_PORT)

        for workers in worker_counts:
            print(f"  [connrate] testing {workers} worker(s)")

            baseline = _run_loadgen(BACKEND_PORT, workers, test_duration)
            baseline["test"] = "connrate"
            baseline["path"] = "direct"
            results.append(baseline)

            through_proxy = _run_loadgen(PROXY_PORT, workers, test_duration)
            through_proxy["test"] = "connrate"
            through_proxy["path"] = "proxy"
            results.append(through_proxy)

    return results


if __name__ == "__main__":
    results = run()
    path = save_results(results, prefix="results_connrate")
    print(f"saved {len(results)} result(s) to {path}")
