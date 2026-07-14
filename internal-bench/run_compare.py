#!/usr/bin/env python3
"""Run all internal-bench runners and emit a cross-tech comparison report."""

from __future__ import annotations

import json
import os
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BENCH_DIR = Path(__file__).resolve().parent
RESULTS = BENCH_DIR / "results"
SHAKTI = Path(os.environ.get("SHAKTI", str(ROOT / "shakti")))
VENV_PY = BENCH_DIR / ".venv" / "bin" / "python"
if sys.platform == "win32":
    VENV_PY = BENCH_DIR / ".venv" / "Scripts" / "python.exe"
SYSTEM_PY = Path(sys.executable)

MEDIANS = 3


def parse_bench_lines(text: str) -> dict[str, float]:
    out: dict[str, float] = {}
    for line in text.splitlines():
        if not line.startswith("BENCH\t"):
            continue
        parts = line.split("\t")
        if len(parts) != 5:
            continue
        _, name, seconds, _iters, _ops = parts
        out[name] = float(seconds)
    return out


def median_cases(runs: list[dict[str, float]]) -> dict[str, float]:
    names = sorted({name for run in runs for name in run})
    out: dict[str, float] = {}
    for name in names:
        samples = [run[name] for run in runs if name in run]
        if samples:
            out[name] = statistics.median(samples)
    return out


def run_cmd(cmd: list[str], *, cwd: Path | None = None, env: dict | None = None) -> dict[str, float]:
    proc = subprocess.run(
        cmd,
        cwd=cwd or BENCH_DIR,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"runner failed ({cmd[0]}): exit {proc.returncode}")
    if proc.stderr.strip():
        sys.stderr.write(proc.stderr)
    parsed = parse_bench_lines(proc.stdout)
    if not parsed:
        raise SystemExit(f"no BENCH lines from {cmd[0]}")
    return parsed


def python_cmd(script: Path) -> list[str]:
    py = VENV_PY if VENV_PY.exists() else SYSTEM_PY
    return [str(py), str(script)]


def run_shakti(script: Path) -> dict[str, float]:
    if not SHAKTI.exists():
        raise SystemExit(f"missing shakti binary: {SHAKTI} (run make shakti)")
    env = os.environ.copy()
    env["SHAKTI_LIB"] = str(ROOT / "lib")
    return run_cmd([str(SHAKTI), str(script)], cwd=ROOT, env=env)


def collect_runner(label: str, fn) -> dict[str, float]:
    runs: list[dict[str, float]] = []
    for i in range(MEDIANS):
        runs.append(fn())
        if i == 0:
            print(f"  {label}: {len(runs[-1])} cases", file=sys.stderr)
    med = median_cases(runs)
    print(f"  {label}: median of {MEDIANS} runs", file=sys.stderr)
    return med


def main() -> int:
    workloads_path = BENCH_DIR / "workloads.json"
    if not workloads_path.exists():
        raise SystemExit("missing workloads.json — run make internal-bench-scaffold")

    workloads = json.loads(workloads_path.read_text(encoding="utf-8"))
    suite_order = workloads.get("suites", {})
    all_cases = [c for cases in suite_order.values() for c in cases]

    runners_dir = BENCH_DIR / "runners"
    specs: list[tuple[str, callable]] = [
        ("shakti_sql", lambda: run_shakti(runners_dir / "shakti_sql.ie")),
        ("shakti_vectors", lambda: run_shakti(runners_dir / "shakti_vectors.ie")),
        ("numpy", lambda: run_cmd(python_cmd(runners_dir / "numpy_vectors.py"))),
        ("pandas", lambda: run_cmd(python_cmd(runners_dir / "pandas_sql.py"))),
        ("sqlite", lambda: run_cmd(python_cmd(runners_dir / "sqlite_sql.py"))),
        ("duckdb", lambda: run_cmd(python_cmd(runners_dir / "duckdb_sql.py"))),
    ]

    print("Running cross-tech benchmarks (median of 3)...", file=sys.stderr)
    table: dict[str, dict[str, float]] = {}
    for label, fn in specs:
        try:
            table[label] = collect_runner(label, fn)
        except SystemExit as exc:
            print(f"  {label}: SKIPPED ({exc})", file=sys.stderr)
            table[label] = {}

    RESULTS.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    json_path = RESULTS / f"compare_{stamp}.json"
    tsv_path = RESULTS / f"compare_{stamp}.tsv"

    payload = {"timestamp": stamp, "medians": MEDIANS, "results": table}
    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    labels = [label for label, _ in specs]
    header = ["workload"] + labels
    lines = ["\t".join(header)]
    for case in all_cases:
        row = [case]
        for label in labels:
            sec = table.get(label, {}).get(case)
            row.append(f"{sec:.6f}" if sec is not None else "")
        lines.append("\t".join(row))
    tsv_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    col_w = max(len(h) for h in header)
    print("")
    print(f"{'workload'.ljust(col_w)}  " + "  ".join(f"{h:>14}" for h in labels))
    print("-" * (col_w + 16 * len(labels)))
    for case in all_cases:
        cells = []
        for label in labels:
            sec = table.get(label, {}).get(case)
            cells.append(f"{sec:14.4f}" if sec is not None else f"{'—':>14}")
        print(f"{case.ljust(col_w)}  " + "  ".join(cells))

    print("")
    print(f"wrote {tsv_path}")
    print(f"wrote {json_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
