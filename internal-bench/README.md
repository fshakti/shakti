# internal-bench (local only — gitignored)

Cross-tech comparisons: Shakti vs Python (numpy, pandas) vs in-memory SQLite and DuckDB.

## Setup

```bash
# from repo root
make shakti
make internal-bench-scaffold   # skip if this folder already exists
cd internal-bench
python3 -m venv .venv
source .venv/bin/activate      # Windows: .venv\\Scripts\\activate
pip install -r requirements.txt
cd ..
make bench-compare
```

Results land in `results/` as timestamped TSV and JSON.

See [`docs/INTERNAL_BENCH.md`](../docs/INTERNAL_BENCH.md) for workload definitions and fairness notes.
