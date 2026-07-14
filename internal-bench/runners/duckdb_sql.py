#!/usr/bin/env python3
"""In-process DuckDB workloads — SQL parity with shakti_sql.ie."""

from __future__ import annotations

import time

import duckdb


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\t{name}\t{seconds}\t{iterations}\t{ops}")


def seed_main_table(con: duckdb.DuckDBPyConnection, n: int) -> None:
    con.execute("DROP TABLE IF EXISTS t")
    con.execute("CREATE TABLE t (id INTEGER, dept INTEGER, amount INTEGER)")
    con.executemany("INSERT INTO t VALUES (?, ?, ?)", [(i, i, i) for i in range(n)])


def main() -> None:
    n = 10_000
    con = duckdb.connect(":memory:")
    seed_main_table(con, n)

    runs = 15
    threshold = n // 2
    t0 = time.perf_counter()
    for _ in range(runs):
        con.execute(
            """
            SELECT dept, SUM(amount) AS amount
            FROM t
            WHERE amount > ?
            GROUP BY dept
            """,
            [threshold],
        ).fetchall()
    bench_line("sql_select_group_filter", time.perf_counter() - t0, runs)

    con.execute("DROP TABLE IF EXISTS t2")
    con.execute("CREATE TABLE t2 (id INTEGER, dept INTEGER, amount INTEGER)")
    con.executemany("INSERT INTO t2 VALUES (?, ?, ?)", [(i, i, i) for i in range(n)])

    runs = 15
    t0 = time.perf_counter()
    for _ in range(runs):
        con.execute(
            """
            SELECT dept, SUM(amount) AS amount
            FROM t2
            WHERE amount > ?
            GROUP BY dept
            """,
            [5000],
        ).fetchall()
    bench_line("sql_select_group_filter_2", time.perf_counter() - t0, runs)

    con.execute("CREATE TABLE tu (id INTEGER, name VARCHAR, score INTEGER)")
    con.executemany(
        "INSERT INTO tu VALUES (?, ?, ?)",
        [(1, "a", 0), (2, "b", 1), (3, "c", 2)],
    )

    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        con.execute("UPDATE tu SET score = 99 WHERE id = 2")
    bench_line("sql_update", time.perf_counter() - t0, n)

    con.execute("CREATE TABLE td (id INTEGER, v INTEGER)")
    con.executemany("INSERT INTO td VALUES (?, ?)", [(i, i) for i in range(1000)])

    n = 1000
    t0 = time.perf_counter()
    for _ in range(n):
        con.execute("DELETE FROM td WHERE id > 900")
        if con.execute("SELECT COUNT(*) FROM td").fetchone()[0] < 100:
            con.execute("DELETE FROM td")
            con.executemany("INSERT INTO td VALUES (?, ?)", [(i, i) for i in range(1000)])
    bench_line("sql_delete", time.perf_counter() - t0, n)

    n = 2000
    t0 = time.perf_counter()
    for i in range(n):
        con.execute(f"DROP TABLE IF EXISTS bench_u_{i}")
        con.execute(f"CREATE TABLE bench_u_{i} (id INTEGER, name VARCHAR)")
    bench_line("sql_create", time.perf_counter() - t0, n)

    con.execute("CREATE TABLE ins (id INTEGER, name VARCHAR)")

    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        con.execute("INSERT INTO ins VALUES (1, 'ada')")
    bench_line("sql_insert", time.perf_counter() - t0, n)

    con.close()


if __name__ == "__main__":
    main()
