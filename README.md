# shakti

Small interpreted language (0.9.1).

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
| `make prod` | Default optimized build (`-O2`) |
| `make prod-speed` | `-O3` with `-march=native` (x86) or `-mcpu=native` (arm64); AVX-512/NEON for matrix `mmul` and vector `dot` |
| `make prod-size` | Size-optimized build |
| `make check-deps` | macOS: verify Homebrew `libomp` and `expat` |

Portable CPU build (no native arch tuning): `SHAKTI_PORTABLE_CPU=1 make prod-speed`

The standalone binary auto-detects `lib/` next to the executable when `SHAKTI_LIB` is unset.

### Local workspace targets

These require gitignored `tests/` and `scripts/` trees in your working copy (not in the published repo):

| Target | Purpose |
|--------|---------|
| `make test` | Run `tests/*.ie` |
| `make test-parse` | Golden parser tests (`scripts/parse_golden.sh`) |
| `make test-mac` | macOS: `test` + `test-parse` |
| `make bench` | Compare against local benchmark baselines (`benchmarks/`) |
| `make bench-update` | Refresh `benchmarks/baselines/local.json` |
| `make bench-report` | Print benchmark table (no fail on regression) |

## run

```bash
./shakti file.ie
./shakti          # REPL
```

## examples

See [doc.md](doc.md#examples-index) for the full index. All example code lives in [`example.ie`](example.ie).

| Module | section | what it does |
|--------|---------|----------------|
| *(core)* | `matrix.ie` | matrices, `mmul`, `dot`, reducers |
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
| *(stdlib)* | `bridge.ie` | bridge hand dealer / HCP filter |

## docs

All documentation is in [doc.md](doc.md):

- [Examples index](doc.md#examples-index)
- [Syntax and builtins](doc.md#syntax-and-builtins)
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
