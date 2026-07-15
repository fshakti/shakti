# shakti

Small interpreted language (0.10.0) with decorators, each (`f@` / `xs f@ ys`),
native vectors/matrices, tables, and optional SQL, graph, IPC, REST, synth,
and input modules.

## build

Linux (X11 + ALSA for synth UI):

```bash
sudo apt-get install -y libx11-dev libasound2-dev libexpat1-dev
make prod          # or: make prod-speed
export SHAKTI_LIB=$PWD/lib
```

macOS (Cocoa + Core Audio for synth; talk module on by default):

```bash
brew install libomp expat
make prod          # or: make prod-speed
export SHAKTI_LIB=$PWD/lib
```

| Target | Purpose |
|--------|---------|
| `make prod` | Default optimized build (`-O2`); OpenMP for large vectors |
| `make prod-speed` | `-O3` with `-march=native` (x86) or `-mcpu=native` (arm64); AVX-512/NEON for `mmul`, element-wise ops, and vector `dot`/`sum` |
| `make prod-size` | Size-optimized build |
| `make check-deps` | macOS: verify Homebrew `libomp` and `expat` |

Portable CPU build (no native arch tuning): `SHAKTI_PORTABLE_CPU=1 make prod-speed`

The standalone binary auto-detects `lib/` next to the executable when `SHAKTI_LIB` is unset.

### Local workspace targets

These require gitignored `tests/` and `scripts/` trees in your working copy (not in the published repo):

| Target | Purpose |
|--------|---------|
| `make test` | Run `tests/*.ie` |
| `make test-memory-stress` | Opt-in adaptive large-allocation and OOM tests |
| `make test-parse` | Golden parser tests (`scripts/parse_golden.sh`) |
| `make test-mac` | macOS: `test` + `test-parse` |
| `make bench` | Compare against local benchmark baselines (`benchmarks/`) |
| `make bench-update` | Refresh `benchmarks/baselines/local.json` |
| `make bench-report` | Print benchmark table (no fail on regression) |
| `make bench-transpile` | Measure Python-subset transpiler throughput |
| `make bench-python` | Compare identical `.py` workloads in CPython and Shakti |

Benchmark baselines are machine- and OpenMP-thread-count-specific. Use the same
fixed thread count when recording and comparing a baseline, especially on
many-core hosts:

```bash
OMP_NUM_THREADS=4 make bench-update
OMP_NUM_THREADS=4 make bench
```

The RAM stress suite is intentionally excluded from `make test`. By default it
uses up to 25% of currently available RAM, capped at 16 GiB, then verifies OOM
handling under a 512 MiB process limit. Override the safe cap explicitly:

```bash
MEMORY_STRESS_MAX_GIB=32 MEMORY_STRESS_FRACTION=0.3 make test-memory-stress
```

## run

```bash
./shakti file.ie
./shakti          # REPL
```

## examples

See [doc.md](doc.md#examples-index) for the full index. All example code lives in [`example.ie`](example.ie).

| Module | section | what it does |
|--------|---------|----------------|
| *(core)* | `decorators.ie` | function, class, stacked, and factory decorators |
| *(core)* | `each.ie` | `f@ xs` / `xs f@ ys` each |
| *(core)* | `matrix.ie` | matrices, `mmul`, `dot`, reducers |
| *(core)* | `table_csv.ie` | CSV/TSV `save` / `load` (numeric + string columns) |
| `sql` | `sql_demo.ie` | in-memory table SQL |
| `graph` | `graph_demo.ie` | knowledge graph triples |
| `input` | `input_demo.ie` | readline + event poll |
| `synth` | `synth_demo.ie` | synth window + event loop |
| `synth` | `synth_song.ie` | Twinkle + drum loop with live UI |
| `synth` | `synth_input.ie` | jam keys via `input(2)` + synth |
| `talk` | `talk_demo.ie` | speech-to-text (macOS) |
| `ipc` | `ipc_echo.ie` + `ipc_echo_client.ie` | local UDS echo |
| `ipc` | `ipc_rdma.ie` + `ipc_rdma_client.ie` | RDMA/RoCE IPC (Linux + NIC) |
| `rest` | `rest_demo.ie` | HTTP client + local server |

## tools

Python 3 → Shakti (strict subset):

```bash
./shakti file.py          # transpile + run in-process
./shakti python.py
./shakti s2p.ie input.py -o out.ie   # emit .ie only
SHAKTI_LIB=$PWD/lib ./shakti out.ie
```

`./shakti file.py` runs the supported Python subset through the embedded
transpiler, then evaluates the generated Shakti. This is not CPython.

Compare correctness and end-to-end performance with the host CPython:

```bash
python3 scripts/test_python_vs_shakti.py
make bench-python
```

The benchmark validates matching output first. Timings include process startup
and, for Shakti, in-process transpilation.

Maps `=`→`:`, `==`→`=`, defaults/keywords to `:`, and matrix `@`→`mmul`.
Common NumPy arrays/reducers and pandas DataFrames lower to native vectors,
reducers, and tables. Rejects unsupported Python with line/column diagnostics.
See [doc.md](doc.md#python-3--shakti-converter).

## docs

All documentation is in [doc.md](doc.md):

- [Examples index](doc.md#examples-index)
- [Syntax and builtins](doc.md#syntax-and-builtins)
- [Decorators](doc.md#decorators)
- [Each (`@`)](doc.md#each-)
- [Python 3 → Shakti converter](doc.md#python-3--shakti-converter)
- [`sql` module](doc.md#sql-module)
- [graph module](doc.md#graph-module)
- [`input` module](doc.md#input-module)
- [IPC module](doc.md#ipc-module)
- [REST module](doc.md#rest-module)
- [`synth` module](doc.md#synth-module)
- [`talk` module](doc.md#talk-module-macos)
- [Third-party dependencies](doc.md#third-party-dependencies-and-optional-assets)

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
