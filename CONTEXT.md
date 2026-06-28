# Shakti — project context

Interpreted language (v0.7.0): standalone C CLI, GNU Make build, Apache-2.0.

- **Remote:** https://github.com/quillquant/shakti.git (`master`)
- **Published tree:** `src/`, `examples/`, `docs/`, `README.md`, `Makefile`, `LICENSE`, `NOTICE`
- **Local-only (gitignored):** `tests/`, `benchmarks/`, `scripts/`, `internal-bench/`, `android/`, `cmake/`

## Modules

| Module | Doc |
|--------|-----|
| `import synth` | [docs/SYNTH.md](docs/SYNTH.md) |
| `import talk` | [docs/TALK.md](docs/TALK.md) (macOS) |
| `import input` | [docs/RUNTIME_API.md](docs/RUNTIME_API.md) |
| `import ipc` | [docs/IPC.md](docs/IPC.md) — UDS/TCP/RDMA sync + poll-based async |
| `import sql` | [docs/RUNTIME_API.md](docs/RUNTIME_API.md) |

## Verify

```bash
export SHAKTI_LIB=$PWD/src/lib
make shakti
make test          # tests/*.ie (local)
make bench         # benchmarks/ regression (local)
```

## Notes

- Rebranded from ****; `src/host_bridge.c` dlopens `.so` when present.
- IPC: localhost defaults to Unix domain sockets; TCP uses `TCP_NODELAY` + `writev` framing.
- RDMA (`src/ipc_rdma.c`) links when `libibverbs` + `librdmacm` headers exist on Linux.
