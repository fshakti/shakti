# shakti

Small interpreted language (0.9.0).

## build

```bash
sudo apt-get install -y libexpat1-dev
make prod          # or: make prod-speed
```

macOS (Cocoa + Core Audio for synth; talk module on by default):

```bash
brew install libomp expat
make prod-speed
```

| Target | Purpose |
|--------|---------|
| `make prod` | Balanced build (`-Os -Ofast -O2`) |
| `make prod-speed` | `-O3` with `-march=native` (x86) or `-mcpu=native` (arm64); AVX-512/NEON for matrix `@` and vector `dot` |
| `make prod-size` | Size-optimized build |
| `make check-deps` | macOS: verify Homebrew `libomp` and `expat` |

Portable CPU build (no native arch tuning): `SHAKTI_PORTABLE_CPU=1 make prod-speed`

The standalone binary auto-detects `src/lib` next to the executable when `SHAKTI_LIB` is unset.

## run

```bash
./shakti file.ie
./shakti          # REPL
```

## examples

See [docs/EXAMPLES.md](docs/EXAMPLES.md) for the full index.

## acknowledgements

Thank-yous coming soon.

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
