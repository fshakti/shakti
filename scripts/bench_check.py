#!/usr/bin/env python3
"""Run Shakti benchmarks and compare against a local gitignored baseline."""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BENCHMARKS = ROOT / "benchmarks"
BASELINE = BENCHMARKS / "baselines" / "local.json"
SHAKTI = Path(os.environ.get("SHAKTI", str(ROOT / "shakti")))
RUNS = 5
MIN_BASELINE_SEC = 0.002  # skip regression gate entirely
MICRO_BASELINE_SEC = 0.008  # conversion/call micro-benches (high run-to-run variance)
MICRO_TOLERANCE = 0.30  # 30% for micro-benches; bool_conv showed ~27% spread across runs


def parse_bench_lines(text: str) -> dict[str, dict[str, float]]:
    cases: dict[str, dict[str, float]] = {}
    for line in text.splitlines():
        if not line.startswith("BENCH\t"):
            continue
        parts = line.split("\t")
        if len(parts) != 5:
            continue
        _, name, seconds, iterations, ops = parts
        cases[name] = {
            "seconds": float(seconds),
            "iterations": float(iterations),
            "ops_per_sec": float(ops),
        }
    return cases


def median_cases(runs: list[dict[str, dict[str, float]]]) -> dict[str, dict[str, float]]:
    names = sorted({name for run in runs for name in run})
    out: dict[str, dict[str, float]] = {}
    for name in names:
        samples = [run[name]["seconds"] for run in runs if name in run]
        if not samples:
            continue
        med = statistics.median(samples)
        sample = next(run[name] for run in runs if name in run)
        out[name] = {
            "seconds": med,
            "iterations": sample["iterations"],
            "ops_per_sec": sample["iterations"] / med if med > 0 else 0.0,
        }
    return out


def run_benchmarks() -> dict[str, dict[str, float]]:
    if not SHAKTI.exists():
        raise SystemExit(f"missing binary: {SHAKTI} (run make shakti first)")
    env = os.environ.copy()
    env["SHAKTI_LIB"] = str(ROOT / "lib")
    env.setdefault("SHAKTI_SYNTH_HEADLESS", "1")
    results: list[dict[str, dict[str, float]]] = []
    for _ in range(RUNS):
        proc = subprocess.run(
            [str(SHAKTI), "run.ie"],
            cwd=BENCHMARKS,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr)
            raise SystemExit(f"benchmark run failed (exit {proc.returncode})")
        if proc.stderr.strip():
            sys.stderr.write(proc.stderr)
        parsed = parse_bench_lines(proc.stdout)
        if not parsed:
            raise SystemExit("no BENCH lines captured from benchmarks/run.ie")
        results.append(parsed)
    return median_cases(results)


def load_baseline() -> dict | None:
    if not BASELINE.exists():
        return None
    return json.loads(BASELINE.read_text(encoding="utf-8"))


def save_baseline(cases: dict[str, dict[str, float]]) -> None:
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    payload = {"version": 1, "cases": cases}
    BASELINE.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def case_tolerance(base_sec: float, default: float) -> float:
    if base_sec < MIN_BASELINE_SEC:
        return default
    if base_sec < MICRO_BASELINE_SEC:
        return float(os.environ.get("BENCH_MICRO_TOLERANCE", str(MICRO_TOLERANCE)))
    return default


def print_report(cases: dict[str, dict[str, float]], baseline: dict | None, tolerance: float) -> int:
    print(f"{'case':<28} {'seconds':>10} {'ops/s':>14} {'baseline':>10} {'delta':>8}")
    print("-" * 74)
    fails = 0
    base_cases = (baseline or {}).get("cases", {})
    for name in sorted(cases):
        cur = cases[name]
        sec = cur["seconds"]
        ops = cur["ops_per_sec"]
        if name in base_cases:
            base_sec = base_cases[name]["seconds"]
            delta = (sec / base_sec - 1.0) if base_sec > 0 else 0.0
            if base_sec < MIN_BASELINE_SEC:
                print(f"{name:<28} {sec:10.4f} {ops:14.1f} {base_sec:10.4f} {'(noisy)':>8}")
            else:
                tol = case_tolerance(base_sec, tolerance)
                over = delta > tol
                mark = " FAIL" if over else ""
                if over:
                    fails += 1
                suffix = " micro" if tol > tolerance else ""
                print(f"{name:<28} {sec:10.4f} {ops:14.1f} {base_sec:10.4f} {delta*100:7.1f}%{mark}{suffix}")
        else:
            print(f"{name:<28} {sec:10.4f} {ops:14.1f} {'—':>10} {'new':>8}")
    return fails


def check_manifest() -> None:
    proc = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "gen_benchmark_manifest.py"), "--check"],
        cwd=ROOT,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true", help="write local baseline")
    ap.add_argument("--check", action="store_true", help="compare against baseline (default)")
    ap.add_argument("--report", action="store_true", help="print table only; never fail")
    args = ap.parse_args()

    if os.environ.get("BENCH_REPORT") == "1":
        args.report = True

    check_manifest()
    cases = run_benchmarks()

    tolerance = float(os.environ.get("BENCH_TOLERANCE", "0.15"))
    baseline = load_baseline()

    if args.update:
        save_baseline(cases)
        print(f"updated baseline: {BASELINE} ({len(cases)} cases)")
        print_report(cases, None, tolerance)
        return 0

    if args.report:
        print_report(cases, baseline, tolerance)
        return 0

    if baseline is None:
        print(f"no baseline at {BASELINE}")
        print("run: make bench-update")
        print_report(cases, None, tolerance)
        return 0

    fails = print_report(cases, baseline, tolerance)
    if fails:
        print(f"\n{fails} benchmark(s) exceeded tolerance", file=sys.stderr)
        return 1
    print(f"\nall {len(cases)} benchmarks within tolerance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
