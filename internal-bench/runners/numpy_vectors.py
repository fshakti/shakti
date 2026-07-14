#!/usr/bin/env python3
"""NumPy vector workloads — mirrors shakti_vectors.ie."""

from __future__ import annotations

import time

import numpy as np


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\t{name}\t{seconds}\t{iterations}\t{ops}")


def main() -> None:
    a = np.arange(1_000_000, dtype=np.int64)
    b = np.arange(1_000_000, dtype=np.int64)

    # warmup
    _ = a + b

    n = 10
    t0 = time.perf_counter()
    for _ in range(n):
        _ = a + b
    bench_line("vec_add_1m", time.perf_counter() - t0, n)

    n = 50
    t0 = time.perf_counter()
    for _ in range(n):
        _ = a * 2
    bench_line("vec_mul_1m", time.perf_counter() - t0, n)

    n = 20
    t0 = time.perf_counter()
    for _ in range(n):
        _ = a > 500_000
    bench_line("vec_compare_1m", time.perf_counter() - t0, n)

    n = 20
    t0 = time.perf_counter()
    for _ in range(n):
        _ = a[a > 500_000]
    bench_line("vec_filter_mask_1m", time.perf_counter() - t0, n)

    _ = a.sum()
    _ = np.dot(a, b)

    n = 20
    t0 = time.perf_counter()
    for _ in range(n):
        _ = a.sum()
    bench_line("vec_sum_1m", time.perf_counter() - t0, n)

    n = 20
    t0 = time.perf_counter()
    for _ in range(n):
        _ = np.dot(a, b)
    bench_line("vec_dot_1m", time.perf_counter() - t0, n)


if __name__ == "__main__":
    main()
