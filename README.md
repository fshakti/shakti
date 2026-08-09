# shakti

[Discord](https://discord.gg/PkKwUk9Tf) · [tree-sitter grammar](https://github.com/avillega/tree-sitter-shakti) (unofficial)

Small interpreted language (0.12.0) with vectors, matrices, tables, decorators,
each (`f@`), table joins (`,` / `union` / `outer`), load-time time-series indexes
(weighted avg / range max / windowed avg / keyed stats / asof), and optional SQL,
graph, IPC, REST, gfx, pyplot, jupyter, synth, input, MIDI, PDF, DSP, Sonic Pi,
and IEFS modules.

## grammar

- **Bind** with `:` — `x : 1`, `def f(n:2):`
- **Compare** with `=` — `if x = 1:`
- `==` is not supported
- Leading `@` decorates; expression `@` is each; matrix multiply is `mmul(a, b)`
- Time-series indexes: `wavg_index` / `wavg`, `range_max_*`, `key_maxmin_*`,
  `winavg_*`, `ts_stats_*`, `cum_mult_hits`, `asof_sort` / `asof_bin` — see
  [doc.md](doc.md#time-series-indexes)

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
make build          # default: same as `make` / `make all`
make prod           # strip release binary
export SHAKTI_LIB=$PWD/lib
```

`make prod-speed` enables `-O3` plus host CPU tuning (`-mcpu=native` on
arm64 / Apple Silicon, including M5; `-march=x86-64-v2` on x86-64).
For a redistributable arm64 binary, use `SHAKTI_PORTABLE_CPU=1` (`-mcpu=apple-m4`;
current Xcode clang does not yet accept `-mcpu=apple-m5`).
`make prod-size` optimizes for size.
`make clean` removes the binary and objects under `.build/`.
Embedded converter headers are generated under `gen/` from `converters/p2s.ie`,
`converters/c2s.ie`, `converters/cs2s.ie`, and `converters/j2s.ie`.

Optional build flags (default on unless noted): `SHAKTI_GFX`, `SHAKTI_SYNTH`, `SHAKTI_DSP`, `SHAKTI_STEM`, `SHAKTI_SONICPI`, `SHAKTI_PDF`, `SHAKTI_MIDI`, `SHAKTI_IEFS`, `SHAKTI_IPC`, `SHAKTI_TALK` (macOS default on).

## run

```
shakti [options] [script [args...]]
shakti [options] -c|--command <code> [-i|--interactive]
shakti
```

| Short | Long | Action |
|-------|------|--------|
| `-h` | `--help` | Usage; exit 0 |
| `-V` | `--version` | Version; exit 0 |
| `-q` | `--quiet` | No banner |
| `-b` | `--banner` | Force banner |
| `-c <code>` | `--command <code>` | Eval string |
| `-i` | `--interactive` | REPL after command |
| | `--parse-dump` | AST dump |
| | `--parse-profile` | Parse microbench |
| | `--parse-profile-iters <n>` | Iterations |
| | `--` | End options |

Unknown flags exit with status 2.

```bash
./shakti file.ie
./shakti          # REPL
./shakti --command '1+1'
./shakti file.py    # supported Python subset → Shakti, then run
./shakti file.c     # supported C subset → Shakti, then run
./shakti file.cs    # supported C# subset → Shakti, then run
./shakti file.java  # supported Java subset → Shakti, then run
```

Converters: [`converters/p2s.ie`](converters/p2s.ie) (Python), [`converters/c2s.ie`](converters/c2s.ie) (C), [`converters/cs2s.ie`](converters/cs2s.ie) (C#), [`converters/j2s.ie`](converters/j2s.ie) (Java). Their embedded CLI copies are generated into `gen/`.  
More detail: [doc.md](doc.md). Examples: [examples/example.ie](examples/example.ie), [examples/example.py](examples/example.py), [examples/example.c](examples/example.c), [examples/example.cs](examples/example.cs), [examples/example.java](examples/example.java).  
Tiny converter demos: [examples/python.py](examples/python.py), [examples/c.c](examples/c.c), [examples/csharp.cs](examples/csharp.cs), [examples/java.java](examples/java.java).
C / C# / Java converters reject unterminated strings and `/* ... */` block
comments with source-line diagnostics instead of emitting partial programs.
CSV/TSV `load` buffers normal files; set `SHAKTI_CSV_MAX_BYTES` to stream larger inputs.

`./shakti file.c` is **subset lowering + interpret**, not a C compiler. Repeated
`.c` / `.py` / `.cs` / `.java` runs use a disk transpile cache under
`~/.cache/shakti/transpile/` (keyed by source and converter; set
`SHAKTI_NO_TRANSPILE_CACHE=1` to disable). Prefer emitting `.ie` once for hot loops. Fast numeric work belongs on large vectors /
`dot` / `mmul` / `make prod-speed` — scalar `for`/`while` micros stay in the AST
interpreter. Fair VM peer is CPython; vs `cc -O2` see [doc.md](doc.md#performance-vs-cc--o2-methodology)
and local `make -f Makefile.local compare-langs`.

## security

Shakti trusts the `.ie` programs it runs. A script can call `sh(cmd)` (shell),
read/write files, and use network builtins — treat untrusted input like an
untrusted shell script.

- Set `SHAKTI_SAFE=1` (or `SHAKTI_ALLOW_EXEC=0`) to disable `sh()`.
- Parser nesting and interpreter recursion are capped (defaults 40 / 3000;
  override call depth with `SHAKTI_CALL_MAX_DEPTH`).
- REST temps use `$XDG_RUNTIME_DIR` / `$TMPDIR` when set, mode `0600`.

**`examples/example.ie` is a merged copy-paste catalog** — do **not** run the whole file as one program.
It concatenates interactive demos (`gfx`, `synth`, `input`, …). Running it end-to-end
opens windows and loops until they are closed (`gfx.tick` paces at ~60 Hz). Copy one section into its own `.ie`, or use
`examples/_local/gfx_demo.ie` / other standalone demos under `examples/_local/` when present.
For automation, prefer those standalone files and set `SHAKTI_GFX_SKIP=1` where demos support it.

## modules

| `import` | Role | Doc |
|----------|------|-----|
| `sql` | Tables / select | [doc](doc.md#sql-module) |
| `graph` | Knowledge graph | [doc](doc.md#graph-module) |
| `gfx` | Pixel window | [doc](doc.md#gfx-module) |
| `pyplot` | Charts (pyplot-shaped) | [doc](doc.md#pyplot-module) |
| `jupyter` | Notebook / IPython-shaped | [doc](doc.md#jupyter-module) |
| `synth` | Softsynth UI | [doc](doc.md#synth-module) |
| `dsp` | Just intonation | [doc](doc.md#dsp-module) |
| `stem` | Streaming 4-stem separator | [doc](doc.md#stem-module) |
| `sonicpi` | Sonic Pi OSC | [doc](doc.md#sonicpi-module) |
| `pdf` | PDF 1.4 R/W | [doc](doc.md#pdf-module) |
| `midi` | ALSA / CoreMIDI | [doc](doc.md#midi-module) |
| `iefs` | Durable `.iefs` | [doc](doc.md#iefs-module) |
| `input` / `ipc` / `rest` / `talk` | IO / network / STT | see [doc.md](doc.md) |

Demo games (gfx; need `SHAKTI_GFX=1`): [`import pong`](lib/pong.ie) then `pong.run()` (terminal: `pong.run_terminal()`); [`import chess`](lib/chess.ie) then `chess.run()`. Launchers: [`examples/pong_demo.ie`](examples/pong_demo.ie), [`examples/chess_demo.ie`](examples/chess_demo.ie).

```bash
make -f Makefile.local test-pong    # examples/pong_test.ie + examples/pong_spell_test.ie
make -f Makefile.local bench-pong   # examples/pong_bench.ie (physics / frame / proj / AI)
make -f Makefile.local test-chess   # examples/chess_test.ie (rules + AI smoke)
```

Local demos (when `examples/_local/` is present): `examples/_local/gfx_demo.ie`, `examples/_local/pyplot_demo.ie`, `examples/_local/jupyter_demo.ie`, `examples/_local/pdf_smoke.ie`, `examples/_local/midi_demo.ie`, `examples/_local/sonicpi_demo.ie`.  
Merged copy-paste sections also live in [`examples/example.ie`](examples/example.ie) (`pyplot_demo.ie`, `jupyter_demo.ie`, …) — copy a section out; do not run the whole file. Use `SHAKTI_GFX_SKIP=1` to skip gfx windows in pyplot/jupyter demos.

## test & bench (local tree)

When `tests/`, `benchmarks/`, and `Makefile.local` are present:

```bash
make -f Makefile.local test
make -f Makefile.local test-modules   # dsp, stem, pdf, midi, iefs, sonicpi, pyplot, jupyter
make -f Makefile.local bench-modules  # focused module benches
make -f Makefile.local bench          # full suite vs baselines
make -f Makefile.local test-c         # C converter regression script
make -f Makefile.local test-transpile-matrix  # shared converter feature matrix
make -f Makefile.local bench-transpile-all
make -f Makefile.local compare-langs  # vs host runtimes; C .c/.ie/eval~ split
```

Time-series index correctness tests live under local `tests/` when that tree is present (gitignored).

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
