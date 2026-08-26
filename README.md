# shakti

[Discord](https://discord.gg/PkKwUk9Tf) · [tree-sitter grammar](https://github.com/avillega/tree-sitter-shakti) (unofficial)

Small interpreted language (0.13.1) with vectors, matrices, tables, decorators,
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
make prod           # strip release binary under .build/; refresh ./shakti symlink
make install        # optional: ~/.local/bin/shakti → this tree's .build/shakti
export SHAKTI_LIB=$PWD/lib
```

`make prod-speed` enables `-O3` plus host CPU tuning (`-mcpu=native` on
arm64 / Apple Silicon, including M5; `-march=x86-64-v2` on x86-64).
For a redistributable arm64 binary, use `SHAKTI_PORTABLE_CPU=1` (`-mcpu=apple-m4`;
current Xcode clang does not yet accept `-mcpu=apple-m5`).
`make prod-size` optimizes for size.
Default `make` / `make prod` enables link-time optimization (`-flto` /
`-flto=auto`) so the split language units (`src/lex.c`, `src/parse.c`,
`src/eval.c`, …) still inline across translation units.
`make clean` removes `.build/` and the `./shakti` symlink.
The linked binary lives at `.build/shakti`; `make build` also creates `./shakti` →
`.build/shakti` so a workspace directory on `PATH` finds `shakti`.

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

Bare REPL meta-commands (banner also lists them): `\d` grammar card, `\v` vars, `\w` names,
`\q` / `\q N` (process exit with optional status). Soft leave: `quit` or `exit`.

```bash
./shakti file.ie
./shakti          # REPL
printf '\\q\n' | ./shakti -q          # quit with status 0
printf '\\q 7\n' | ./shakti -q        # quit with status 7
./shakti --command '1+1'
```

More detail: [doc.md](doc.md). Examples: [examples/example.ie](examples/example.ie),
[examples/sh_demo.ie](examples/sh_demo.ie).
CSV/TSV `load` buffers normal files; set `SHAKTI_CSV_MAX_BYTES` to stream larger inputs.

Fast numeric work belongs on large vectors / `dot` / `mmul` / `make prod-speed` —
scalar `for`/`while` micros stay in the AST interpreter (`src/eval.c`).

## security

Shakti trusts the `.ie` programs it runs. A script can spawn `subprocess()`,
run `sh(cmd)`, read/write files, and use network builtins — treat untrusted
input like an untrusted shell script.

- Set `SHAKTI_SAFE=1` (or `SHAKTI_ALLOW_EXEC=0`) to disable `subprocess()` and
  `sh()`.
- Parser nesting and interpreter recursion are capped (defaults 40 / 3000;
  override call depth with `SHAKTI_CALL_MAX_DEPTH`).
- REST temps use `$XDG_RUNTIME_DIR` / `$TMPDIR` when set, mode `0600`.
- Non-loopback `rest.listen` / `ipc.listen` require `SHAKTI_REST_ALLOW_PUBLIC=1` /
  `SHAKTI_IPC_ALLOW_PUBLIC=1`.

**`examples/example.ie` is a merged copy-paste catalog** — do **not** run the whole file as one program.
It concatenates interactive demos (`gfx`, `synth`, `input`, …). Running it end-to-end
hits an open-window busy loop (`while gfx.alive(): gfx.tick()` with no sleep) and will
spin until the window is closed. Copy one section into its own `.ie`.
For automation, set `SHAKTI_GFX_SKIP=1` where demos support it.

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

Demo games (gfx; need `SHAKTI_GFX=1`): [`import pong`](lib/pong.ie) then `pong.run()` (terminal: `pong.run_terminal()`); [`import chess`](lib/chess.ie) then `chess.run()`. Launchers: [`examples/pong_demo.ie`](examples/pong_demo.ie), [`examples/chess_demo.ie`](examples/chess_demo.ie). Timed gfx tour (movie / stem / pyplot / pong; auto-exits): [`examples/showcase.ie`](examples/showcase.ie). Record and index in `.iefs` with `make record-showcase` (artifacts under `.build/`).

```bash
./shakti examples/pong_test.ie
./shakti examples/pong_spell_test.ie
./shakti examples/pong_bench.ie
./shakti examples/chess_test.ie
```

Merged copy-paste sections also live in [`examples/example.ie`](examples/example.ie) (`pyplot_demo.ie`, `jupyter_demo.ie`, …) — copy a section out; do not run the whole file. Use `SHAKTI_GFX_SKIP=1` to skip gfx windows in pyplot/jupyter demos.

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
