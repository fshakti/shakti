#!/usr/bin/env python3
"""Scaffold gitignored internal-bench/ for cross-tech performance comparisons."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "internal-bench"

WORKLOADS_JSON = """{
  "version": 1,
  "medians": 3,
  "warmup": 1,
  "suites": {
    "sql": [
      "sql_select_group_filter",
      "sql_select_group_filter_2",
      "sql_update",
      "sql_delete",
      "sql_create",
      "sql_insert"
    ],
    "vectors": [
      "vec_add_1m",
      "vec_mul_1m",
      "vec_compare_1m",
      "vec_filter_mask_1m"
    ]
  }
}
"""

REQUIREMENTS_TXT = """numpy>=1.24
pandas>=2.0
duckdb>=0.10
"""

README_MD = """# internal-bench (local only — gitignored)

Cross-tech comparisons: Shakti vs Python (numpy, pandas) vs in-memory SQLite and DuckDB.

## Setup

```bash
# from repo root
make shakti
make internal-bench-scaffold   # skip if this folder already exists
cd internal-bench
python3 -m venv .venv
source .venv/bin/activate      # Windows: .venv\\\\Scripts\\\\activate
pip install -r requirements.txt
cd ..
make bench-compare
```

If `pip` fails against a private index, use: `pip install --index-url https://pypi.org/simple -r requirements.txt`

Results land in `results/` as timestamped TSV and JSON.

See [`docs/INTERNAL_BENCH.md`](../docs/INTERNAL_BENCH.md) for workload definitions and fairness notes.
"""

RUN_COMPARE_PY = '''#!/usr/bin/env python3
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
        if not line.startswith("BENCH\\t"):
            continue
        parts = line.split("\\t")
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
    json_path.write_text(json.dumps(payload, indent=2) + "\\n", encoding="utf-8")

    labels = [label for label, _ in specs]
    header = ["workload"] + labels
    lines = ["\\t".join(header)]
    for case in all_cases:
        row = [case]
        for label in labels:
            sec = table.get(label, {}).get(case)
            row.append(f"{sec:.6f}" if sec is not None else "")
        lines.append("\\t".join(row))
    tsv_path.write_text("\\n".join(lines) + "\\n", encoding="utf-8")

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
'''

SHAKTI_SQL_IE = """# Shakti SQL workloads — mirrors benchmarks/suites/sql.ie
import sql

def bench_line(name, seconds, iterations):
    ops = 0.0
    if seconds > 0:
        ops = float(iterations) / float(seconds)
    print("BENCH\\t" + name + "\\t" + str(seconds) + "\\t" + str(iterations) + "\\t" + str(ops))

n = 10000
dept = range(n)
amount = range(n)
id_col = range(n)
t = table(id=id_col, dept=dept, amount=amount)

runs = 15
t0 = clock()
for i in range(runs):
    select amount by dept from t where amount > n / 2
bench_line("sql_select_group_filter", clock() - t0, runs)

t2 = table(id=id_col, dept=dept, amount=amount)
runs = 15
t0 = clock()
for i in range(runs):
    select amount by dept from t2 where amount > 5000
bench_line("sql_select_group_filter_2", clock() - t0, runs)

tu = table(id=[1, 2, 3], name=["a", "b", "c"], score=range(3))
n = 2000
t0 = clock()
for i in range(n):
    update score = 99 from tu where id == 2
bench_line("sql_update", clock() - t0, n)

td = table(id=range(1000), v=range(1000))
n = 1000
t0 = clock()
for i in range(n):
    delete from td where id > 900
bench_line("sql_delete", clock() - t0, n)

n = 2000
t0 = clock()
for i in range(n):
    create table bench_u (id: 0, name: "")
bench_line("sql_create", clock() - t0, n)

create table ins (id: 0, name: "")
n = 2000
t0 = clock()
for i in range(n):
    insert into ins (id, name) values (1, "ada")
bench_line("sql_insert", clock() - t0, n)
"""

SHAKTI_VECTORS_IE = """# Shakti vector workloads — subset of benchmarks/suites/vectors.ie

def bench_line(name, seconds, iterations):
    ops = 0.0
    if seconds > 0:
        ops = float(iterations) / float(seconds)
    print("BENCH\\t" + name + "\\t" + str(seconds) + "\\t" + str(iterations) + "\\t" + str(ops))

a = range(1000000)
b = range(1000000)
n = 10
t0 = clock()
for i in range(n):
    a + b
bench_line("vec_add_1m", clock() - t0, n)

n = 50
t0 = clock()
for i in range(n):
    a * 2
bench_line("vec_mul_1m", clock() - t0, n)

n = 20
t0 = clock()
for i in range(n):
    a > 500000
bench_line("vec_compare_1m", clock() - t0, n)

n = 20
t0 = clock()
for i in range(n):
    a[a > 500000]
bench_line("vec_filter_mask_1m", clock() - t0, n)
"""

NUMPY_VECTORS_PY = '''#!/usr/bin/env python3
"""NumPy vector workloads — mirrors shakti_vectors.ie."""

from __future__ import annotations

import time

import numpy as np


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\\t{name}\\t{seconds}\\t{iterations}\\t{ops}")


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


if __name__ == "__main__":
    main()
'''

PANDAS_SQL_PY = '''#!/usr/bin/env python3
"""pandas SQL-equivalent workloads — semantic parity with shakti_sql.ie."""

from __future__ import annotations

import time

import pandas as pd


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\\t{name}\\t{seconds}\\t{iterations}\\t{ops}")


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
'''

SQLITE_SQL_PY = '''#!/usr/bin/env python3
"""In-memory SQLite workloads — SQL parity with shakti_sql.ie."""

from __future__ import annotations

import sqlite3
import time


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\\t{name}\\t{seconds}\\t{iterations}\\t{ops}")


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
'''

DUCKDB_SQL_PY = '''#!/usr/bin/env python3
"""In-process DuckDB workloads — SQL parity with shakti_sql.ie."""

from __future__ import annotations

import time

import duckdb


def bench_line(name: str, seconds: float, iterations: int) -> None:
    ops = float(iterations) / seconds if seconds > 0 else 0.0
    print(f"BENCH\\t{name}\\t{seconds}\\t{iterations}\\t{ops}")


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
'''

FILES: dict[str, str | bytes] = {
    "README.md": README_MD,
    "requirements.txt": REQUIREMENTS_TXT,
    "workloads.json": WORKLOADS_JSON,
    "run_compare.py": RUN_COMPARE_PY,
    "runners/shakti_sql.ie": SHAKTI_SQL_IE,
    "runners/shakti_vectors.ie": SHAKTI_VECTORS_IE,
    "runners/numpy_vectors.py": NUMPY_VECTORS_PY,
    "runners/pandas_sql.py": PANDAS_SQL_PY,
    "runners/sqlite_sql.py": SQLITE_SQL_PY,
    "runners/duckdb_sql.py": DUCKDB_SQL_PY,
}


def scaffold(*, force: bool = False) -> None:
    if TARGET.exists() and any(TARGET.iterdir()) and not force:
        raise SystemExit(
            f"{TARGET} already exists — remove it or pass --force to overwrite"
        )

    TARGET.mkdir(parents=True, exist_ok=True)
    (TARGET / "results").mkdir(parents=True, exist_ok=True)
    (TARGET / "runners").mkdir(parents=True, exist_ok=True)

    for rel, content in FILES.items():
        path = TARGET / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, str):
            path.write_text(content, encoding="utf-8")
        else:
            path.write_bytes(content)
        if rel.endswith(".py"):
            path.chmod(0o755)

    (TARGET / "run_compare.py").chmod(0o755)
    print(f"scaffolded {TARGET}")
    print("next: cd internal-bench && python3 -m venv .venv && pip install -r requirements.txt")
    print("then: make bench-compare")


def main() -> int:
    ap = argparse.ArgumentParser(description="Scaffold gitignored internal-bench/")
    ap.add_argument("--force", action="store_true", help="overwrite existing files")
    args = ap.parse_args()
    scaffold(force=args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
