# shakti

[Website](https://shakti.com) · [Try shakti in your browser](https://shakti.com/?try)
[Discord](https://discord.gg/csWTFekpN) · [tree-sitter grammar](https://github.com/avillega/tree-sitter-shakti) (unofficial)

Small interpreted language (0.15.0) with vectors, matrices, tables, decorators,
each (`f@`), table joins (`,` / `union` / `outer`), asof helpers, and optional SQL,
graph, IPC, REST, gfx, pyplot, jupyter, synth, input, MIDI, PDF, DSP, Sonic Pi,
and IEFS modules.

## binaries

Prebuilt stripped binaries (refreshed on releases; refresh the slot with
`make dist` to ship your own):

- [Linux / WSL (x86_64)](https://raw.githubusercontent.com/fshakti/shakti/main/build/linux-x86_64/shakti)
- [macOS (Apple Silicon, arm64)](https://raw.githubusercontent.com/fshakti/shakti/main/build/darwin-arm64/shakti)

Download, make executable (`chmod +x shakti`), then `SHAKTI_LIB=$PWD/lib ./shakti`.
The WSL binary is the Linux x86_64 build.

## syntax card

Quick overview of the grammar below. The full one-page reference is built
into the binary (`\d` / `\help` / `help` in the REPL); `.ie` vs Python
differences are noted in `doc.md`.

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
| | `--` | End options |

Unknown flags exit with status 2. Script files must end in `.ie`.

Bare REPL meta-commands (banner also lists them): `\d` grammar card, `\v` user vars, `\w` names,
`\q` / `\q N` (process exit with optional status). Soft leave: `quit` or `exit`.
Line editing: `^D` deletes the character under the cursor (empty line: EOF) —
[llelf](https://github.com/llelf).
In a UTF-8 terminal the line is UTF-8 text: Backspace, `^D`, Delete, Left and Right take a whole character.

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

Demo games (gfx; need `SHAKTI_GFX=1`): [`import pong`](lib/pong.ie) then `pong.run()` (terminal: `pong.run_terminal()`); [`import chess`](lib/chess.ie) then `chess.run()`; [`import draughts`](lib/draughts.ie) then `draughts.run()`. Launchers: [`examples/pong_demo.ie`](examples/pong_demo.ie), [`examples/chess_demo.ie`](examples/chess_demo.ie), [`examples/draughts_demo.ie`](examples/draughts_demo.ie). Timed gfx tour (movie / stem / pyplot / pong; auto-exits): [`examples/showcase.ie`](examples/showcase.ie). Record and index in `.iefs` with `make record-showcase` (artifacts under `.build/`).

```bash
./shakti examples/pong_test.ie
./shakti examples/pong_spell_test.ie
./shakti examples/pong_bench.ie
./shakti examples/chess_test.ie
./shakti examples/draughts_test.ie
```

Merged copy-paste sections also live in [`examples/example.ie`](examples/example.ie) (`pyplot_demo.ie`, `jupyter_demo.ie`, …) — copy a section out; do not run the whole file. Use `SHAKTI_GFX_SKIP=1` to skip gfx windows in pyplot/jupyter demos.

## games / demos

Launch with gfx (`SHAKTI_GFX=1`):
- `chess`: `import chess; chess.run()` · terminal `chess.run_terminal()`
- `draughts`: `import draughts; draughts.run()`
- `pong`: `import pong; pong.run()` · terminal `pong.run_terminal()`
- Timed showcase (movie/stem/pyplot/pong): `examples/showcase.ie`

See `examples/pong_bench.ie` for throughput.

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
