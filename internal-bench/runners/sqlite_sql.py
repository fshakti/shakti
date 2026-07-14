#!/usr/bin/env python3
"""In-memory SQLite workloads — SQL parity with shakti_sql.ie."""

from __future__ import annotations

import sqlite3
import time


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\t{name}\t{seconds}\t{iterations}\t{ops}")


def seed_main_table(conn: sqlite3.Connection, n: int) -> None:
    conn.execute("DROP TABLE IF EXISTS t")
    conn.execute("CREATE TABLE t (id INTEGER, dept INTEGER, amount INTEGER)")
    conn.executemany(
        "INSERT INTO t VALUES (?, ?, ?)",
        [(i, i, i) for i in range(n)],
    )
    conn.commit()


def main() -> None:
    n = 10_000
    conn = sqlite3.connect(":memory:")
    seed_main_table(conn, n)

    runs = 15
    threshold = n // 2
    t0 = time.perf_counter()
    for _ in range(runs):
        conn.execute(
            """
            SELECT dept, SUM(amount) AS amount
            FROM t
            WHERE amount > ?
            GROUP BY dept
            """,
            (threshold,),
        ).fetchall()
    bench_line("sql_select_group_filter", time.perf_counter() - t0, runs)

    conn.execute("DROP TABLE IF EXISTS t2")
    conn.execute("CREATE TABLE t2 (id INTEGER, dept INTEGER, amount INTEGER)")
    conn.executemany(
        "INSERT INTO t2 VALUES (?, ?, ?)",
        [(i, i, i) for i in range(n)],
    )
    conn.commit()

    runs = 15
    t0 = time.perf_counter()
    for _ in range(runs):
        conn.execute(
            """
            SELECT dept, SUM(amount) AS amount
            FROM t2
            WHERE amount > ?
            GROUP BY dept
            """,
            (5000,),
        ).fetchall()
    bench_line("sql_select_group_filter_2", time.perf_counter() - t0, runs)

    conn.execute("CREATE TABLE tu (id INTEGER, name TEXT, score INTEGER)")
    conn.executemany(
        "INSERT INTO tu VALUES (?, ?, ?)",
        [(1, "a", 0), (2, "b", 1), (3, "c", 2)],
    )
    conn.commit()

    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        conn.execute("UPDATE tu SET score = 99 WHERE id = 2")
        conn.commit()
    bench_line("sql_update", time.perf_counter() - t0, n)

    conn.execute("CREATE TABLE td (id INTEGER, v INTEGER)")
    conn.executemany("INSERT INTO td VALUES (?, ?)", [(i, i) for i in range(1000)])
    conn.commit()

    n = 1000
    t0 = time.perf_counter()
    for _ in range(n):
        conn.execute("DELETE FROM td WHERE id > 900")
        conn.commit()
        if conn.execute("SELECT COUNT(*) FROM td").fetchone()[0] < 100:
            conn.execute("DELETE FROM td")
            conn.executemany("INSERT INTO td VALUES (?, ?)", [(i, i) for i in range(1000)])
            conn.commit()
    bench_line("sql_delete", time.perf_counter() - t0, n)

    n = 2000
    t0 = time.perf_counter()
    for i in range(n):
        conn.execute(f"DROP TABLE IF EXISTS bench_u_{i}")
        conn.execute(f"CREATE TABLE bench_u_{i} (id INTEGER, name TEXT)")
        conn.commit()
    bench_line("sql_create", time.perf_counter() - t0, n)

    conn.execute("CREATE TABLE ins (id INTEGER, name TEXT)")
    conn.commit()

    n = 2000
    t0 = time.perf_counter()
    for _ in range(n):
        conn.execute("INSERT INTO ins (id, name) VALUES (1, 'ada')")
        conn.commit()
    bench_line("sql_insert", time.perf_counter() - t0, n)

    conn.close()


if __name__ == "__main__":
    main()
