import json
import os
import subprocess

from lib import ProcessGroup, wait_for_port, save_results

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_PROXY_BIN = os.path.join(PROJECT_DIR, "cmake-build-release", "ReProx")
LOADGEN_BIN = os.path.join(SCRIPT_DIR, "bin", "latency_loadgen")
ECHO_BACKEND_BIN = os.path.join(SCRIPT_DIR, "bin", "echo_backend")

PROXY_PORT = 8700
BACKEND_PORT = 10001

DEFAULT_CONNECTION_COUNTS = [1, 8, 32, 128]


def _run_loadgen(port: int, connections: int, duration: int, payload_size: int) -> dict:
    proc = subprocess.run(
        [
            LOADGEN_BIN,
            "--host", "127.0.0.1", "--port", str(port),
            "--connections", str(connections), "--duration", str(duration),
            "--payload-size", str(payload_size),
        ],
        capture_output=True, text=True,
    )
    return json.loads(proc.stdout)


def run(
        proxy_bin: str = DEFAULT_PROXY_BIN,
        test_duration: int = 10,
        connection_counts: list[int] | None = None,
        payload_size: int = 64,
) -> list[dict]:
    if connection_counts is None:
        connection_counts = DEFAULT_CONNECTION_COUNTS

    results = []
    with ProcessGroup() as procs:
        procs.start("echo-backend", [ECHO_BACKEND_BIN, "--port", str(BACKEND_PORT)])
        wait_for_port(BACKEND_PORT)

        procs.start("proxy", [proxy_bin], cwd=os.path.dirname(proxy_bin))
        wait_for_port(PROXY_PORT)

        for connections in connection_counts:
            print(f"  [latency] testing {connections} concurrent connection(s)")

            baseline = _run_loadgen(BACKEND_PORT, connections, test_duration, payload_size)
            baseline["test"] = "latency"
            baseline["path"] = "direct"
            results.append(baseline)

            through_proxy = _run_loadgen(PROXY_PORT, connections, test_duration, payload_size)
            through_proxy["test"] = "latency"
            through_proxy["path"] = "proxy"
            results.append(through_proxy)

    return results


if __name__ == "__main__":
    results = run()
    path = save_results(results, prefix="results_latency")
    print(f"saved {len(results)} result(s) to {path}")
