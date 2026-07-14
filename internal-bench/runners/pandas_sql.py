#!/usr/bin/env python3
"""pandas SQL-equivalent workloads — semantic parity with shakti_sql.ie."""

from __future__ import annotations

import time

import pandas as pd


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\t{name}\t{seconds}\t{iterations}\t{ops}")


def main() -> None:
    n = 10_000
    df = pd.DataFrame({"id": range(n), "dept": range(n), "amount": range(n)})

    runs = 15
    threshold = n // 2
    t0 = time.perf_counter()
    for _ in range(runs):
        sub = df[df["amount"] > threshold]
        _ = sub.groupby("dept", sort=False)["amount"].sum()
    bench_line("sql_select_group_filter", time.perf_counter() - t0, runs)

    df2 = pd.DataFrame({"id": range(n), "dept": range(n), "amount": range(n)})
    runs = 15
    t0 = time.perf_counter()
    for _ in range(runs):
        sub = df2[df2["amount"] > 5000]
        _ = sub.groupby("dept", sort=False)["amount"].sum()
    bench_line("sql_select_group_filter_2", time.perf_counter() - t0, runs)

    tu = pd.DataFrame({"id": [1, 2, 3], "name": ["a", "b", "c"], "score": [0, 1, 2]})
    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        tu.loc[tu["id"] == 2, "score"] = 99
    bench_line("sql_update", time.perf_counter() - t0, n)

    td = pd.DataFrame({"id": range(1000), "v": range(1000)})
    n = 1000
    t0 = time.perf_counter()
    for _ in range(n):
        td.drop(td.index[td["id"] > 900], inplace=True)
        if len(td) < 100:
            td = pd.DataFrame({"id": range(1000), "v": range(1000)})
    bench_line("sql_delete", time.perf_counter() - t0, n)

    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        _ = pd.DataFrame({"id": pd.Series(dtype="int64"), "name": pd.Series(dtype="object")})
    bench_line("sql_create", time.perf_counter() - t0, n)

    ins = pd.DataFrame({"id": [0], "name": [""]})
    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        ins = pd.concat(
            [ins, pd.DataFrame({"id": [1], "name": ["ada"]})],
            ignore_index=True,
        )
    bench_line("sql_insert", time.perf_counter() - t0, n)


if __name__ == "__main__":
    main()
