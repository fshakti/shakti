# Benchmark Results — shakti v0.7.0

Run date: **2026-06-26** (UTC)  
Host: Linux x86_64, 24 OpenMP threads  
Binary: `./shakti` (debug build, `-O2 -g`)

## Build & test

| Step | Result |
|------|--------|
| `make shakti` | Pass |
| `./shakti tests/smoke.ie` | Pass |
| `./shakti examples/matrix.ie` | Pass |
| `make test` | Pass (21 `.ie` tests, incl. IPC) |
| `make test-parse` | Pass (5 golden parses) |
| `make bench-parse` | Pass (62,344 parses/sec, min 50,000) |

## Regression suite (`make bench`)

105 cases, median of 5 runs each.

Notable timings (seconds, stripped prod binary):

| Case | Seconds | ops/s |
|------|---------|-------|
| `sum_1m` | 0.0020 | 4,990 |
| `vec_add_1m` | 0.0107 | 938 |
| `sql_select_100k` | 0.0269 | 558 |
| `sorted_10k` | 1.3927 | 14 |
| `repr_list` | 0.3199 | 31,258 |
| `json_loads_bench` | 0.0241 | 830 |

Full report: `make bench-report` (local, gitignored under `benchmarks/`).

## Cross-tech compare (`make bench-compare`)

Median of 3 runs. Results file: `internal-bench/results/compare_20260626T011651Z.json`

### SQL workloads (seconds)

| Workload | shakti | pandas | sqlite | duckdb |
|----------|--------|--------|--------|--------|
| sql_select_group_filter | 0.0191 | 0.0046 | 0.0334 | 0.0462 |
| sql_select_group_filter_2 | 0.0104 | 0.0039 | 0.0307 | 0.0435 |
| sql_update | 0.0012 | 0.1761 | 0.0030 | 0.4182 |
| sql_delete | 0.0045 | 0.0641 | 0.0172 | 0.3391 |
| sql_create | 0.0019 | 0.1941 | 0.1351 | 0.4280 |
| sql_insert | 0.0007 | 0.2055 | 0.0036 | 0.4445 |

### Vector workloads (seconds)

| Workload | shakti | numpy | kore |
|----------|--------|-------|------|
| vec_add_1m | 0.0127 | 0.0028 | — |
| vec_mul_1m | 0.0179 | 0.0070 | — |
| vec_compare_1m | 0.0064 | 0.0016 | — |
| vec_filter_mask_1m | 0.0063 | 0.0062 | — |
| kore_sum_1m | 0.0039 | 0.0029 | 0.000005 |
| kore_dot_1m | 0.0150 | 0.0400 | 0.000007 |

## Reproduce

```bash
make clean && make shakti
make test
make bench-update   # refresh local baseline (gitignored)
make bench          # regression gate
make bench-report   # human-readable table
make bench-compare  # cross-tech comparison
make test-parse && make bench-parse
```

Regenerate local bench logs with `make bench-report` (output is not committed; see `benchmarks/baselines/local.json` locally).
