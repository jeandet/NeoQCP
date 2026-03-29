#!/usr/bin/env python3
"""
Perf profiling wrapper for NeoQCP multigraph benchmarks.

The binary uses raise(SIGSTOP) after warmup so perf only captures the
hot loop. This script launches the binary, waits for it to stop, then
attaches perf and sends SIGCONT.

    # Record a specific scenario:
    python3 run-perf.py l1_resampling

    # Record all scenarios:
    python3 run-perf.py --all

    # Just perf stat (no recording):
    python3 run-perf.py --stat l1_resampling

    # Custom iterations:
    python3 run-perf.py l1_resampling --iters 10

    # Generate report from existing perf.data:
    python3 run-perf.py --report l1_resampling

Output goes to results/<scenario>.perf.data (and .stat.txt for stat).
"""

import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
BINARY = SCRIPT_DIR / "multigraph_perf"
RESULTS_DIR = SCRIPT_DIR / "results"

SCENARIOS = [
    "l1_resampling",
    "l2_resampling",
    "adaptive",
    "data_setup",
    "full_replot",
    "pan_replot",
]


def ensure_binary():
    if not BINARY.exists():
        sys.exit(
            f"Binary not found: {BINARY}\n"
            f"Build with: meson compile -C <builddir> multigraph_perf"
        )


def wait_for_stop(pid, timeout=120):
    """Wait until process enters stopped state (T) via /proc."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with open(f"/proc/{pid}/status") as f:
                for line in f:
                    if line.startswith("State:"):
                        if "T (stopped)" in line or "t (tracing stop)" in line:
                            return True
                        break
        except FileNotFoundError:
            return False
        time.sleep(0.05)
    return False


def run_stat(scenario, iters):
    """Run perf stat — attach after warmup barrier."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    out_file = RESULTS_DIR / f"{scenario}.stat.txt"

    cmd = [str(BINARY), scenario]
    if iters:
        cmd.append(str(iters))

    print(f"\n{'='*60}")
    print(f"perf stat: {scenario}" + (f" ({iters} iters)" if iters else ""))
    print(f"{'='*60}")

    # Launch benchmark — it will SIGSTOP after warmup
    bench = subprocess.Popen(cmd, stderr=subprocess.PIPE, text=True)
    if not wait_for_stop(bench.pid):
        bench.wait()
        print(bench.stderr.read(), file=sys.stderr)
        sys.exit("Benchmark did not reach barrier")

    # Print warmup output
    # (stderr is still open, read what's buffered won't work with pipe
    #  — we'll get it after the process exits)

    # Attach perf stat, then resume the benchmark
    perf_cmd = [
        "perf", "stat",
        "-e", "cycles,instructions,cache-references,cache-misses,"
              "branches,branch-misses,L1-dcache-load-misses,"
              "LLC-load-misses,task-clock",
        "-p", str(bench.pid),
    ]
    perf = subprocess.Popen(perf_cmd, stderr=subprocess.PIPE, text=True)

    # Give perf a moment to attach
    time.sleep(0.1)
    os.kill(bench.pid, signal.SIGCONT)

    bench.wait()
    bench_output = bench.stderr.read()
    print(bench_output, file=sys.stderr)

    perf.wait()
    perf_output = perf.stderr.read()
    print(perf_output)

    with open(out_file, "w") as f:
        f.write(f"# Scenario: {scenario}\n")
        if iters:
            f.write(f"# Iterations: {iters}\n")
        f.write(bench_output)
        f.write(perf_output)
    print(f"  → saved to {out_file}")


def run_record(scenario, iters, call_graph, frequency):
    """Run perf record — attach after warmup barrier."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    data_file = RESULTS_DIR / f"{scenario}.perf.data"

    cmd = [str(BINARY), scenario]
    if iters:
        cmd.append(str(iters))

    print(f"\n{'='*60}")
    print(f"perf record: {scenario}" + (f" ({iters} iters)" if iters else ""))
    print(f"  call-graph: {call_graph or 'default'}, freq: {frequency} Hz")
    print(f"{'='*60}")

    # Launch benchmark — it will SIGSTOP after warmup
    bench = subprocess.Popen(cmd, stderr=subprocess.PIPE, text=True)
    if not wait_for_stop(bench.pid):
        bench.wait()
        print(bench.stderr.read(), file=sys.stderr)
        sys.exit("Benchmark did not reach barrier")

    # Attach perf record, then resume
    perf_cmd = [
        "perf", "record",
        "-o", str(data_file),
        "-F", str(frequency),
        "-p", str(bench.pid),
    ]
    if call_graph:
        perf_cmd += ["--call-graph", call_graph]
    perf = subprocess.Popen(perf_cmd, stderr=subprocess.PIPE, text=True)

    time.sleep(0.1)
    os.kill(bench.pid, signal.SIGCONT)

    bench.wait()
    bench_output = bench.stderr.read()
    print(bench_output, file=sys.stderr)

    # perf record exits when the target exits
    perf.wait()
    print(perf.stderr.read(), file=sys.stderr)
    print(f"  → saved to {data_file}")
    return data_file


def run_report(scenario):
    """Generate text report from perf.data."""
    data_file = RESULTS_DIR / f"{scenario}.perf.data"
    if not data_file.exists():
        print(f"No perf data found: {data_file}")
        print(f"Run: python3 {__file__} {scenario}")
        return

    report_file = RESULTS_DIR / f"{scenario}.report.txt"

    result = subprocess.run(
        ["perf", "report", "-i", str(data_file),
         "--stdio", "--no-children", "--percent-limit", "0.5"],
        capture_output=True, text=True,
    )
    print(result.stdout[:5000])

    with open(report_file, "w") as f:
        f.write(result.stdout)
    print(f"\n  → full report saved to {report_file}")

    callee_file = RESULTS_DIR / f"{scenario}.callers.txt"
    result2 = subprocess.run(
        ["perf", "report", "-i", str(data_file),
         "--stdio", "--children", "--percent-limit", "1.0"],
        capture_output=True, text=True,
    )
    with open(callee_file, "w") as f:
        f.write(result2.stdout)
    print(f"  → caller/callee report saved to {callee_file}")


def main():
    parser = argparse.ArgumentParser(
        description="Perf profiling wrapper for NeoQCP MultiGraph benchmarks",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("scenario", nargs="?", help="Scenario to profile")
    parser.add_argument("--all", action="store_true", help="Run all scenarios")
    parser.add_argument("--stat", action="store_true",
                        help="Use perf stat instead of perf record")
    parser.add_argument("--report", action="store_true",
                        help="Generate report from existing perf.data")
    parser.add_argument("--iters", type=int, default=None,
                        help="Override iteration count")
    parser.add_argument("--call-graph", default="dwarf",
                        choices=["dwarf", "fp", "lbr"],
                        help="Call-graph recording method (default: dwarf)")
    parser.add_argument("--frequency", type=int, default=4999,
                        help="Sampling frequency in Hz (default: 4999)")
    args = parser.parse_args()

    if not args.scenario and not args.all:
        parser.print_help()
        print(f"\nAvailable scenarios: {', '.join(SCENARIOS)}")
        sys.exit(1)

    targets = SCENARIOS if args.all else [args.scenario]
    for t in targets:
        if t not in SCENARIOS:
            sys.exit(f"Unknown scenario: {t}\nAvailable: {', '.join(SCENARIOS)}")

    if args.report:
        for t in targets:
            run_report(t)
        return

    ensure_binary()

    for t in targets:
        if args.stat:
            run_stat(t, args.iters)
        else:
            run_record(t, args.iters, args.call_graph, args.frequency)
            run_report(t)


if __name__ == "__main__":
    main()
