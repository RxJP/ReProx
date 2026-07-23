"""Runs every benchmark category in sequence and saves all collected
results, compressed, into a single file.

Each bench_*.py module also has its own main block,
so any one of them can be run standalone.
"""
import argparse
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

import bench_throughput
import bench_latency
import bench_connrate
from lib import save_results

PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DEFAULT_PROXY_BIN = os.path.join(PROJECT_DIR, "cmake-build-release", "ReProx")


def run_all(proxy_bin: str = DEFAULT_PROXY_BIN, test_duration: int = 10) -> list[dict]:
    all_results: list[dict] = []

    print("running throughput benchmarks...")
    all_results.extend(bench_throughput.run(proxy_bin=proxy_bin, test_duration=test_duration))

    print("running latency benchmarks...")
    all_results.extend(bench_latency.run(proxy_bin=proxy_bin, test_duration=test_duration))

    print("running connection-rate benchmarks...")
    all_results.extend(bench_connrate.run(proxy_bin=proxy_bin, test_duration=test_duration))

    return all_results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--proxy-bin", default=DEFAULT_PROXY_BIN,
        help="path to the proxy binary (default: cmake-build-release/ReProx)",
    )
    parser.add_argument(
        "--duration", type=int, default=10,
        help="duration in seconds for each individual benchmark run (default: 10)",
    )
    args = parser.parse_args()

    if not os.path.isfile(args.proxy_bin):
        parser.error(
            f"proxy binary not found at {args.proxy_bin} "
            f"(build it first, or pass --proxy-bin)"
        )

    results = run_all(proxy_bin=args.proxy_bin, test_duration=args.duration)
    path = save_results(results, prefix="results_all")
    print(f"saved {len(results)} result(s) to {path}")


if __name__ == "__main__":
    main()
