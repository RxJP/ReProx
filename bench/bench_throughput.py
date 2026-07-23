# Throughput benchmark: iperf3 through the proxy, both directions, across a range of parallel-stream counts.

import json
import os
import subprocess
import time

from lib import ProcessGroup, wait_for_port, save_results

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_PROXY_BIN = os.path.join(PROJECT_DIR, "cmake-build-release", "ReProx")

PROXY_PORT = 8700
BACKEND_PORT = 10001

DEFAULT_PARALLEL_STREAMS = [1, 4, 8, 16, 64, 128]


def run(
        proxy_bin: str = DEFAULT_PROXY_BIN,
        test_duration: int = 10,
        parallel_streams: list[int] | None = None,
        connect_timeout_ms: int = 10000,
) -> list[dict]:
    if parallel_streams is None:
        parallel_streams = DEFAULT_PARALLEL_STREAMS

    results = []
    with ProcessGroup() as procs:
        procs.start("iperf3-backend", ["iperf3", "-s", "-p", str(BACKEND_PORT)])
        wait_for_port(BACKEND_PORT)

        procs.start("proxy", [proxy_bin], cwd=os.path.dirname(proxy_bin))
        wait_for_port(PROXY_PORT)

        for num_streams in parallel_streams:
            print(f"  [throughput] testing {num_streams} stream(s)")

            for direction, extra_args in (("c2p", []), ("p2c", ["-R"])):
                proc = subprocess.run(
                    [
                        "iperf3", "-c", "127.0.0.1", "-p", str(PROXY_PORT),
                        "-P", str(num_streams), "-t", str(test_duration),
                        "--connect-timeout", str(connect_timeout_ms),
                        "--json", *extra_args,
                    ],
                    capture_output=True,
                )
                data = json.loads(proc.stdout)
                data["test"] = "throughput"
                data["direction"] = direction
                results.append(data)
                time.sleep(1)  # let the streams from this run fully tear down

    return results


if __name__ == "__main__":
    results = run()
    path = save_results(results, prefix="results_throughput")
    print(f"saved {len(results)} result(s) to {path}")
