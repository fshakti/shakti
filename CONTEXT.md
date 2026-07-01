# Shakti — project context

Interpreted language (v0.8.2): standalone C CLI, GNU Make build, Apache-2.0.

- **Remote:** https://github.com/quillquant/shakti.git (`master`)
- **Published tree:** `src/`, `examples/`, `docs/`, `README.md`, `Makefile`, `LICENSE`, `NOTICE`, `CONTEXT.md`
- **Local-only (gitignored):** `tests/`, `scripts/`, `benchmarks/`, `android/`, `cmake/`, `.github/`, `docs/results/`

## Platform notes

| Area | Linux | macOS |
|------|-------|-------|
| Matrix fast path | AVX-512 (`make prod-speed`, x86) | NEON + OpenMP (`brew install libomp`, `make prod-speed`) |
| Vector `dot` / large `sum` | AVX-512 + OpenMP (`vec_kernels.c`) | NEON + OpenMP |
| Optional faster dot/sum | `.so` via `` | same |
| Synth UI | X11 + ALSA | Cocoa + Core Audio (`src/synth_mac.m`) |
| Talk (STT) | off by default | on by default (`SHAKTI_TALK=1`) |
| IPC sockets | UDS + TCP | UDS + TCP |
| RDMA IPC | optional (`libibverbs`, `librdmacm`) | not linked |

`make prod-speed` uses `-march=native` on x86 and `-mcpu=native` on arm64. Set `SHAKTI_PORTABLE_CPU=1` for a portable build (`x86-64-v2` or `apple-m1`).

## Modules

| Module | Doc | Example |
|--------|-----|---------|
| `import sql` | [docs/SQL.md](docs/SQL.md) | [examples/sql_demo.ie](examples/sql_demo.ie) |
| `import input` | [docs/INPUT.md](docs/INPUT.md) | [examples/input_demo.ie](examples/input_demo.ie) |
| `import synth` | [docs/SYNTH.md](docs/SYNTH.md) | [examples/synth_demo.ie](examples/synth_demo.ie) |
| `import talk` | [docs/TALK.md](docs/TALK.md) | [examples/talk_demo.ie](examples/talk_demo.ie) |
| `import ipc` | [docs/IPC.md](docs/IPC.md) | [examples/ipc_echo.ie](examples/ipc_echo.ie) |
| `import lissen` | [docs/LISSEN.md](docs/LISSEN.md) | [examples/lissen_demo.ie](examples/lissen_demo.ie) |
| `import rest` | [docs/REST.md](docs/REST.md) | [examples/rest_demo.ie](examples/rest_demo.ie) |
| `import sonicpi` | [docs/SONICPI.md](docs/SONICPI.md) | [examples/sonicpi_demo.ie](examples/sonicpi_demo.ie) |

Full list: [docs/EXAMPLES.md](docs/EXAMPLES.md).

## Verify (published build)

```bash
export SHAKTI_LIB=$PWD/src/lib
make prod
./shakti examples/matrix.ie
```

## Verify (local workspace)

Requires gitignored `tests/`, `scripts/`, and `benchmarks/` trees:

```bash
make prod-speed
make test
make bench
make bench-report
make test-mac      # Darwin only
```

## History

- Rebranded from ****; `src/host_bridge.c` dlopens `.so` when present (`host_dot`, `host_sum`, …).
- Fused `dot()` builtin with SIMD vector kernels (`src/vec_kernels.c`); prefer `dot(a, b)` over `sum(a * b)` on large vectors.
- IPC: localhost defaults to Unix domain sockets; optional RDMA on Linux.
- macOS build parity (2026-06): OpenMP via Homebrew `libomp`, arm64 NEON, `-mcpu=native` in `make prod-speed`.
