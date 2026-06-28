# Benchmark results — shakti v0.7.0

Published notes only. Full regression baselines and logs live in gitignored local trees (`benchmarks/`, `scripts/`, `internal-bench/`).

## Linux (x86_64, 2026-06-26)

Host: 24 OpenMP threads, `make prod` (`-O2 -g`).

| Step | Result |
|------|--------|
| `make prod` | Pass |
| `make test` | Pass (21 `.ie` tests, incl. IPC) |
| `make test-parse` | Pass (5 golden parses) |
| `make bench-parse` | Pass (~62k parses/sec, min 50,000) |

### Regression suite (`make bench`)

105 cases, median of 5 runs each. Notable timings (seconds, stripped prod binary):

| Case | Seconds | ops/s |
|------|---------|-------|
| `sum_1m` | 0.0020 | 4,990 |
| `vec_add_1m` | 0.0107 | 938 |
| `sql_select_100k` | 0.0269 | 558 |
| `sorted_10k` | 1.3927 | 14 |
| `repr_list` | 0.3199 | 31,258 |
| `json_loads_bench` | 0.0241 | 830 |

Full report: `make bench-report` (local).

## macOS (Apple Silicon, 2026-06-28)

Host: MacBook Pro arm64, 15 cores, `make prod-speed` (`-O3 -mcpu=native`, Homebrew `libomp`).

| Step | Result |
|------|--------|
| `make prod-speed` | Pass (NEON + OpenMP) |
| `make test-mac` | Pass (5 `.ie` tests + 5 golden parses) |
| `make bench-mac` | Pass |

| Benchmark | Result |
|-----------|--------|
| Parser throughput | ~68k parses/sec (min 50,000) |
| Matrix `@` 128×128 | ~0.00026 s/iter (NEON + OpenMP) |

## Cross-tech compare (`make bench-compare`)

Median of 3 runs. Requires local `internal-bench/` tree.

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

## Reproduce (local workspace)

```bash
make clean && make prod-speed
export SHAKTI_LIB=$PWD/src/lib
make test-mac && make bench-mac    # macOS
make test && make bench            # when tests/ and scripts/ present
make bench-update && make bench    # refresh regression baseline
make bench-compare                 # cross-tech (internal-bench/)
```
