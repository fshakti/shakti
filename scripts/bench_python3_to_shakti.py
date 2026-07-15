#!/usr/bin/env python3
"""Benchmark examples/s2p.ie convert throughput via CLI invocations."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHAKTI = ROOT / "shakti"
S2P = ROOT / "examples" / "s2p.ie"
LIB = ROOT / "lib"

NUMPY_PANDAS = """\
import numpy as np
import pandas as pd

data = np.array([1, 2, 3, 4])
scaled = data * 2
frame = pd.DataFrame({"value": data, "scaled": scaled})

print(frame)
print(np.sum(frame["scaled"]))
"""

CONTROL_FLOW = """\
def trace(f):
    def wrap(x):
        return f(x)
    return wrap

@trace
def classify(n=0):
    if n < 0:
        label = "neg"
    elif n == 0:
        label = "zero"
    else:
        label = "pos"
    return label

def accumulate(xs):
    total = 0
    for i in range(len(xs)):
        total += i * xs[i]
    return total

i = 0
while i < 10:
    i += 1

result = accumulate([classify(0), classify(1)])
"""


def make_large(unit: str, repeat: int) -> str:
    blocks = []
    for k in range(repeat):
        blocks.append(
            unit.replace("classify", f"classify_{k}")
            .replace("accumulate", f"accumulate_{k}")
            .replace("trace", f"trace_{k}")
            .replace("result", f"result_{k}")
        )
    return "\n".join(blocks)


def convert_once(src: Path, out: Path) -> None:
    env = os.environ.copy()
    env["SHAKTI_LIB"] = str(LIB)
    proc = subprocess.run(
        [str(SHAKTI), str(S2P), str(src), "-o", str(out)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(proc.stderr or proc.stdout or f"s2p failed ({proc.returncode})")


def bench_one(name: str, source: str, seconds_budget: float) -> tuple[float, int, float]:
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / f"{name}.py"
        out = Path(td) / f"{name}.ie"
        src.write_text(source, encoding="utf-8")
        for _ in range(3):
            convert_once(src, out)
        iterations = 0
        t0 = time.perf_counter()
        while True:
            convert_once(src, out)
            iterations += 1
            elapsed = time.perf_counter() - t0
            if elapsed >= seconds_budget and iterations >= 5:
                break
        ops = iterations / elapsed if elapsed > 0 else 0.0
        return elapsed, iterations, ops


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--budget", type=float, default=0.5, help="seconds per case")
    args = ap.parse_args()
    if not SHAKTI.is_file():
        raise SystemExit(f"missing binary: {SHAKTI}")
    if not S2P.is_file():
        raise SystemExit(f"missing converter: {S2P}")

    large = make_large(CONTROL_FLOW, 40)
    cases = [
        ("transpile_numpy_pandas", NUMPY_PANDAS),
        ("transpile_control_flow", CONTROL_FLOW),
        ("transpile_large_400_lines", large),
    ]

    print(f"{'case':<28} {'seconds':>10} {'iters':>8} {'ops/s':>12} {'lines/s':>12}")
    print("-" * 74)
    lines = []
    for name, source in cases:
        elapsed, iterations, ops = bench_one(name, source, args.budget)
        nlines = source.count("\n") + 1
        lines_per_sec = ops * nlines
        print(f"{name:<28} {elapsed:10.4f} {iterations:8d} {ops:12.1f} {lines_per_sec:12.1f}")
        lines.append(f"BENCH\t{name}\t{elapsed}\t{iterations}\t{ops}")

    print()
    for line in lines:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
