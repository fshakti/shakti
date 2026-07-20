# shakti

[Discord](https://discord.gg/PkKwUk9Tf)

Small interpreted language (0.10.0) with vectors, matrices, tables, decorators,
each (`f@`), load-time time-series indexes (VWAB / windowed avg / stats / asof),
and optional modules for SQL, graphics, audio, MIDI, PDF, and durable storage.

## grammar

- **Bind** with `:` — `x : 1`, `def f(n:2):`
- **Compare** with `=` — `if x = 1:`
- `==` is not supported
- Leading `@` decorates; expression `@` is each; matrix multiply is `mmul(a, b)`
- Prefix indexes: `shakti_vwbid_index` / `shakti_vwbid`, `shakti_hibid_*`, `shakti_nbbo_*`, `shakti_winavg_*`, `shakti_stats_*`, `shakti_theopl`, `asof_sort` / `asof_bin` — see [doc.md](doc.md#time-series-indexes)

```ie
values : [1, -2, 3]

def square(x):
    return x * x

for value in abs@ values:
    if value > 1:
        print(square(value))
```

## build

```bash
# Linux: sudo apt-get install -y libx11-dev libasound2-dev libexpat1-dev
# macOS: brew install libomp expat
make prod
export SHAKTI_LIB=$PWD/lib
```

`make prod-speed` enables native CPU tuning; `make prod-size` optimizes for size.
`make check-deps` verifies Homebrew packages on macOS.

Optional build flags (default on unless noted): `SHAKTI_GFX`, `SHAKTI_SYNTH`, `SHAKTI_DSP`, `SHAKTI_SONICPI`, `SHAKTI_PDF`, `SHAKTI_MIDI`, `SHAKTI_IEFS`, `SHAKTI_IPC`, `SHAKTI_TALK` (macOS default on).

## run

```bash
./shakti file.ie
./shakti          # REPL
./shakti file.py  # supported Python subset → Shakti, then run
```

More detail: [doc.md](doc.md). Examples: [example.ie](example.ie).

## modules

| `import` | Role | Doc |
|----------|------|-----|
| `sql` | Tables / select | [doc](doc.md#sql-module) |
| `graph` | Knowledge graph | [doc](doc.md#graph-module) |
| `gfx` | Pixel window | [doc](doc.md#gfx-module) |
| `synth` | Softsynth UI | [doc](doc.md#synth-module) |
| `dsp` | Just intonation | [doc](doc.md#dsp-module) |
| `sonicpi` | Sonic Pi OSC | [doc](doc.md#sonicpi-module) |
| `pdf` | PDF 1.4 R/W | [doc](doc.md#pdf-module) |
| `midi` | ALSA / CoreMIDI | [doc](doc.md#midi-module) |
| `iefs` | Durable `.iefs` | [doc](doc.md#iefs-module) |
| `input` / `ipc` / `rest` / `talk` | IO / network / STT | see [doc.md](doc.md) |

Local demos (when `examples/` is present): `examples/gfx_demo.ie`, `examples/pdf_smoke.ie`, `examples/midi_demo.ie`, `examples/sonicpi_demo.ie`, `examples/infinibattle/`.

## test & bench (local tree)

When `tests/`, `benchmarks/`, and `Makefile.local` are present:

```bash
make -f Makefile.local test
make -f Makefile.local test-modules   # dsp, pdf, midi, iefs, sonicpi
make -f Makefile.local bench-modules  # focused module benches
make -f Makefile.local bench          # full suite vs baselines
```

Time-series index correctness tests live under local `tests/` when that tree is present (gitignored).

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
