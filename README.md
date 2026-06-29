# shakti

Small interpreted language (0.8.1).

## build

Linux (X11 + ALSA for synth UI):

```bash
sudo apt-get install -y libx11-dev libasound2-dev libexpat1-dev
make prod          # or: make prod-speed
export SHAKTI_LIB=$PWD/src/lib
```

macOS (Cocoa + Core Audio for synth; talk module on by default):

```bash
brew install libomp expat
make prod          # or: make prod-speed
export SHAKTI_LIB=$PWD/src/lib
```

| Target | Purpose |
|--------|---------|
| `make prod` | Default optimized build (`-O2`) |
| `make prod-speed` | `-O3` with `-march=native` (x86) or `-mcpu=native` (arm64); enables AVX-512 or NEON matrix paths |
| `make prod-size` | Size-optimized build |
| `make check-deps` | macOS: verify Homebrew `libomp` and `expat` |

Portable CPU build (no native arch tuning): `SHAKTI_PORTABLE_CPU=1 make prod-speed`

The standalone binary auto-detects `src/lib` next to the executable when `SHAKTI_LIB` is unset.

### Local workspace targets

These require gitignored `tests/` and `scripts/` trees in your working copy (not in the published repo):

| Target | Purpose |
|--------|---------|
| `make test` | Run `tests/*.ie` |
| `make test-parse` | Golden parser tests (`scripts/parse_golden.sh`) |
| `make test-mac` | macOS: `test` + `test-parse` |

## run

```bash
./shakti file.ie
./shakti          # REPL
```

## examples

See [docs/EXAMPLES.md](docs/EXAMPLES.md) for the full index.

| Module | file | what it does |
|--------|------|----------------|
| *(core)* | [`examples/matrix.ie`](examples/matrix.ie) | native matrix types |
| `sql` | [`examples/sql_demo.ie`](examples/sql_demo.ie) | in-memory table SQL |
| `input` | [`examples/input_demo.ie`](examples/input_demo.ie) | readline + event poll |
| `synth` | [`examples/synth_demo.ie`](examples/synth_demo.ie) | synth window + event loop |
| `synth` | [`examples/synth_song.ie`](examples/synth_song.ie) | Twinkle + drum loop with live UI |
| `synth` | [`examples/synth_input.ie`](examples/synth_input.ie) | jam keys via `input(2)` + synth |
| `synth` | [`examples/synth_bsr_sample.ie`](examples/synth_bsr_sample.ie) | BSR sample on SAMP row (local `samples/`) |
| `talk` | [`examples/talk_demo.ie`](examples/talk_demo.ie) | speech-to-text (macOS) |
| `ipc` | [`examples/ipc_echo.ie`](examples/ipc_echo.ie) + [`ipc_echo_client.ie`](examples/ipc_echo_client.ie) | local UDS echo |
| `ipc` | [`examples/ipc_rdma.ie`](examples/ipc_rdma.ie) + [`ipc_rdma_client.ie`](examples/ipc_rdma_client.ie) | RDMA/RoCE IPC (Linux + NIC) |
| `lissen` | [`examples/lissen_demo.ie`](examples/lissen_demo.ie) | Lissen fan platform API |
| *(stdlib)* | [`examples/bridge.ie`](examples/bridge.ie) | bridge hand dealer / HCP filter |

## docs

- [docs/EXAMPLES.md](docs/EXAMPLES.md) — examples by module
- [docs/RUNTIME_API.md](docs/RUNTIME_API.md) — syntax, builtins, matrices, I/O
- [docs/SQL.md](docs/SQL.md) — `import sql`
- [docs/INPUT.md](docs/INPUT.md) — `import input`
- [docs/IPC.md](docs/IPC.md) — `import ipc` (TCP, UDS, RDMA)
- [docs/LISSEN.md](docs/LISSEN.md) — `import lissen` ([Lissen](https://www.lissen.com/))
- [docs/SYNTH.md](docs/SYNTH.md) — `import synth`
- [docs/SAMPLES.md](docs/SAMPLES.md) — optional local sample packs
- [docs/TALK.md](docs/TALK.md) — `import talk` (macOS)
- [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) — linked libraries and optional assets

## acknowledgements

Thank-yous coming soon.

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
