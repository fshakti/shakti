#!/usr/bin/env python3
"""Benchmark the Python 3 -> Shakti converter (in-process, no subprocess noise).

Measures transpile throughput on representative fixtures and prints both a
human-readable table and machine-parseable BENCH lines (same schema as the
Shakti benchmark harness: BENCH<TAB>name<TAB>seconds<TAB>iterations<TAB>ops).
"""

from __future__ import annotations

import argparse
import importlib.util
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONV_PATH = ROOT / "examples" / "python3_to_shakti.py"


def load_converter():
    spec = importlib.util.spec_from_file_location("python3_to_shakti", CONV_PATH)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


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
    # Rename the top-level symbols per copy so each block is independently valid.
    blocks = []
    for k in range(repeat):
        blocks.append(
            unit.replace("classify", f"classify_{k}")
            .replace("accumulate", f"accumulate_{k}")
            .replace("trace", f"trace_{k}")
            .replace("result", f"result_{k}")
        )
    return "\n".join(blocks)


def bench_one(mod, name: str, source: str, seconds_budget: float) -> tuple[float, int, float]:
    # Warmup (parse caches, import of ast machinery, first-call effects).
    for _ in range(5):
        mod.transpile(source, filename=f"{name}.py")
    iterations = 0
    t0 = time.perf_counter()
    while True:
        mod.transpile(source, filename=f"{name}.py")
        iterations += 1
        elapsed = time.perf_counter() - t0
        if elapsed >= seconds_budget and iterations >= 10:
            break
    ops = iterations / elapsed if elapsed > 0 else 0.0
    return elapsed, iterations, ops


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--budget", type=float, default=0.5, help="seconds per case")
    args = ap.parse_args()

    mod = load_converter()
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
        elapsed, iterations, ops = bench_one(mod, name, source, args.budget)
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
