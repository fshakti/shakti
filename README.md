# shakti

Small interpreted language (0.7.0).

## build

Linux (X11 + ALSA for synth UI):

```bash
sudo apt-get install -y libx11-dev libasound2-dev libexpat1-dev
make prod
export SHAKTI_LIB=$PWD/src/lib
```

macOS: `make prod` (Cocoa + Core Audio for synth; talk module optional).

| Target | Purpose |
|--------|---------|
| `make prod` | Default optimized build (`-O2`) |
| `make prod-speed` | `-O3 -march=native` (x86 AVX-512 matrix paths when available) |
| `make prod-size` | Size-optimized build |
| `make test` | Run `tests/*.ie` |
| `make bench` | Compare against benchmark baselines |

Portable CPU build (no `-march=native`): `SHAKTI_PORTABLE_CPU=1 make prod-speed`

The standalone binary auto-detects `src/lib` next to the executable when `SHAKTI_LIB` is unset.

## run

```bash
./shakti file.ie
./shakti          # REPL
```

## examples

| file | what it does |
|------|----------------|
| [`examples/matrix.ie`](examples/matrix.ie) | native matrix types |
| [`examples/synth_demo.ie`](examples/synth_demo.ie) | synth window + event loop |
| [`examples/synth_song.ie`](examples/synth_song.ie) | Twinkle + drum loop with live UI |
| [`examples/synth_input.ie`](examples/synth_input.ie) | jam keys via `input(2)` + synth |
| [`examples/talk_demo.ie`](examples/talk_demo.ie) | speech-to-text (macOS, `import talk`) |
| [`examples/bridge.ie`](examples/bridge.ie) | bridge hand dealer / HCP filter |
| [`examples/ipc_echo.ie`](examples/ipc_echo.ie) | local IPC echo (UDS) |
| [`examples/ipc_rdma.ie`](examples/ipc_rdma.ie) | RDMA/RoCE IPC server |

## docs

- [docs/RUNTIME_API.md](docs/RUNTIME_API.md) — syntax, builtins, matrices, SQL, I/O
- [docs/IPC.md](docs/IPC.md) — `import ipc` (TCP, UDS, RDMA)
- [docs/BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md) — regression suite (`make bench`)
- [docs/SYNTH.md](docs/SYNTH.md) — `import synth`
- [docs/TALK.md](docs/TALK.md) — `import talk` (macOS)
- [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) — linked system libraries

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
