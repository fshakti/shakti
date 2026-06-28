# Shakti — project context

Interpreted language (v0.7.0): standalone C CLI, GNU Make build, Apache-2.0.

- **Remote:** https://github.com/quillquant/shakti.git (`master`)
- **Published tree:** `src/`, `examples/`, `docs/`, `README.md`, `Makefile`, `LICENSE`, `NOTICE`, `CONTEXT.md`
- **Local-only (gitignored):** `tests/`, `benchmarks/`, `scripts/`, `internal-bench/`, `android/`, `cmake/`, `.github/`, `docs/results/`

## Platform notes

| Area | Linux | macOS |
|------|-------|-------|
| Matrix fast path | AVX-512 (`make prod-speed`, x86) | NEON + OpenMP (`brew install libomp`, `make prod-speed`) |
| Synth UI | X11 + ALSA | Cocoa + Core Audio (`src/synth_mac.m`) |
| Talk (STT) | off by default | on by default (`SHAKTI_TALK=1`) |
| IPC sockets | UDS + TCP | UDS + TCP |
| RDMA IPC | optional (`libibverbs`, `librdmacm`) | not linked |

`make prod-speed` uses `-march=native` on x86 and `-mcpu=native` on arm64. Set `SHAKTI_PORTABLE_CPU=1` for a portable build (`x86-64-v2` or `apple-m1`).

## Modules

| Module | Doc |
|--------|-----|
| `import synth` | [docs/SYNTH.md](docs/SYNTH.md) |
| `import talk` | [docs/TALK.md](docs/TALK.md) (macOS) |
| `import input` | [docs/RUNTIME_API.md](docs/RUNTIME_API.md) |
| `import ipc` | [docs/IPC.md](docs/IPC.md) — UDS/TCP/RDMA sync + poll-based async |
| `import sql` | [docs/RUNTIME_API.md](docs/RUNTIME_API.md) |

## Verify (published build)

```bash
export SHAKTI_LIB=$PWD/src/lib
make prod
./shakti examples/matrix.ie
```

## Verify (local workspace)

Requires gitignored `tests/` and `scripts/` trees:

```bash
make prod-speed
make test-mac      # Darwin
make bench-mac     # Darwin
make test && make bench   # full local regression when scripts/benchmarks present
```

## History

- Rebranded from ****; `src/host_bridge.c` dlopens `.so` when present.
- IPC: localhost defaults to Unix domain sockets; TCP uses `TCP_NODELAY` + `writev` framing.
- RDMA (`src/ipc_rdma.c`) links when `libibverbs` + `librdmacm` headers exist on Linux.
- macOS build parity (2026-06): Darwin OpenMP via Homebrew `libomp`, arm64 NEON in `src/mat_simd.c`, `-mcpu=native` in `make prod-speed`.
