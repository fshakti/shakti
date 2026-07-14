#!/usr/bin/env python3
"""Measure shakti binary size and compare against a local gitignored baseline."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "benchmarks" / "baselines" / "size.json"
SHAKTI = Path(os.environ.get("SHAKTI", str(ROOT / "shakti")))


def measure_binary(path: Path) -> dict[str, int]:
    if not path.is_file():
        raise SystemExit(f"missing binary: {path} (run make prod first)")
    st = path.stat()
    out: dict[str, int] = {"bytes": st.st_size}
    proc = subprocess.run(
        ["size", str(path)],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode == 0:
        for line in proc.stdout.splitlines():
            m = re.match(r"\s*\d+\s+(\d+)\s+(\d+)\s+(\d+)", line)
            if m:
                out["text"] = int(m.group(1))
                out["data"] = int(m.group(2))
                out["bss"] = int(m.group(3))
                break
    return out


def load_baseline() -> dict | None:
    if not BASELINE.exists():
        return None
    return json.loads(BASELINE.read_text(encoding="utf-8"))


def save_baseline(metrics: dict[str, int]) -> None:
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    payload = {"version": 1, **metrics}
    BASELINE.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def print_report(cur: dict[str, int], baseline: dict | None, tolerance: float) -> int:
    keys = ["bytes", "text", "data", "bss"]
    print(f"{'metric':<10} {'current':>12} {'baseline':>12} {'delta':>10}")
    print("-" * 48)
    fails = 0
    for key in keys:
        if key not in cur:
            continue
        val = cur[key]
        if baseline and key in baseline:
            base = baseline[key]
            delta = (val / base - 1.0) if base > 0 else 0.0
            mark = " FAIL" if delta > tolerance else ""
            if delta > tolerance:
                fails += 1
            print(f"{key:<10} {val:12d} {base:12d} {delta*100:9.1f}%{mark}")
        else:
            print(f"{key:<10} {val:12d} {'—':>12} {'new':>10}")
    return fails


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true", help="write local size baseline")
    ap.add_argument("--check", action="store_true", help="compare against baseline (default)")
    ap.add_argument("--report", action="store_true", help="print table only; never fail")
    args = ap.parse_args()

    metrics = measure_binary(SHAKTI)
    tolerance = float(os.environ.get("SIZE_TOLERANCE", "0.05"))
    baseline = load_baseline()

    if args.update:
        save_baseline(metrics)
        print(f"updated baseline: {BASELINE}")
        print_report(metrics, None, tolerance)
        return 0

    if args.report:
        print_report(metrics, baseline, tolerance)
        return 0

    if baseline is None:
        print(f"no baseline at {BASELINE}")
        print("run: make size-update")
        print_report(metrics, None, tolerance)
        return 0

    fails = print_report(metrics, baseline, tolerance)
    if fails:
        print(f"\n{ fails } metric(s) exceeded {tolerance*100:.0f}% tolerance", file=sys.stderr)
        return 1
    print(f"\nall metrics within {tolerance*100:.0f}% tolerance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
