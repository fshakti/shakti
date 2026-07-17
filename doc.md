# Shakti documentation

## Contents

- [Examples index](#examples-index)
- [Syntax and builtins](#syntax-and-builtins)
- [Decorators](#decorators)
- [Each (`@`)](#each)
- [Python 3 → Shakti converter](#python-3-shakti-converter)
- [`sql` module](#sql-module)
- [graph module](#graph-module)
- [`gfx` module](#gfx-module)
- [`input` module](#input-module)
- [IPC module](#ipc-module)
- [REST module](#rest-module)
- [`synth` module](#synth-module)
- [`talk` module](#talk-module-macos)
- [Third-party dependencies](#third-party-dependencies-and-optional-assets)

---

# Examples index

Most demo sections live in [`example.ie`](example.ie) (labels like `sql_demo.ie` are section banners, not separate files). Extra gfx demos also live under [`examples/`](examples/) when present locally.

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: <name>.ie
```

Copy a section into its own file if you need to run it alone (for example IPC server + client). Timed/movie gfx demos: `examples/gfx_demo_timed.ie`, `examples/gfx_movie.ie`.

## By module

| Module | Section | Description |
|--------|---------|-------------|
| *(core)* | `decorators.ie` | Function, class, stacked, factory, and assignment decorators |
| *(core)* | `each.ie` | `f@ xs` / `xs f@ ys` each |
| *(core)* | `matrix.ie` | Matrices (`mmul`), `dot`, `sum` / `min` / `max` |
| *(core)* | `table_csv.ie` | CSV/TSV `save` / `load` (numeric + string columns) |
| `import sql` | `sql_demo.ie` | Select, insert, update, delete, join |
| `import graph` | `graph_demo.ie` | Knowledge graph triples, query, path |
| `import gfx` | `gfx_demo.ie` | Pixel window + click drawing (also `examples/gfx_demo.ie`) |
| `import gfx` | `examples/gfx_demo_timed.ie` | Timed open/draw/present (standalone) |
| `import gfx` | `examples/gfx_movie.ie` | Animated redraw demo (standalone) |
| `import input` | `input_demo.ie` | `readline` + timed event poll |
| `import input` + `synth` | `synth_input.ie` | QWERTY jam with synth window |
| `import synth` | `synth_demo.ie` | Synth window + event loop |
| `import synth` | `synth_song.ie` | Twinkle + drum sequencer |
| `import synth` | `synth_just_intonation.ie` | Just-intonation major chord |
| `import talk` | `talk_demo.ie` | Speech-to-text (macOS) |
| `import ipc` | `ipc_echo.ie` | UDS echo server |
| `import ipc` | `ipc_echo_client.ie` | Client for `ipc_echo.ie` |
| `import ipc` | `ipc_rdma.ie` | RDMA/RoCE server (Linux + NIC) |
| `import ipc` | `ipc_rdma_client.ie` | Client for `ipc_rdma.ie` |
| `import rest` | `rest_demo.ie` | HTTP GET/POST client + local server |

## Tools

| Tool | Description |
|------|-------------|
| `s2p.ie` | Strict Python 3 → Shakti converter |

## Module docs

| Module | Doc |
|--------|-----|
| `sql` | [sql module](#sql-module) |
| `graph` | [graph module](#graph-module) |
| `gfx` | [gfx module](#gfx-module) |
| `input` | [input module](#input-module) |
| `synth` | [synth module](#synth-module) |
| `talk` | [talk module](#talk-module-macos) |
| `ipc` | [IPC module](#ipc-module) |
| `rest` | [REST module](#rest-module) |
| Language & builtins | [syntax and builtins](#syntax-and-builtins) |

---

# Python 3 → Shakti converter

Strict subset converter written in Shakti. Embedded in the executable for
direct runs, and still available as `s2p.ie` for emit-only conversion.

```bash
./shakti file.py                 # transpile + run (supported subset)
./shakti python.py
./shakti s2p.ie input.py -o out.ie
./shakti s2p.ie python.py -o python.ie
SHAKTI_LIB=$PWD/lib ./shakti out.ie
```

`./shakti file.py` is not CPython: it lowers the supported subset to Shakti
and evaluates that. Unsupported syntax exits nonzero with `file:line:col`
diagnostics (original `.py` path preserved).

## Rewrites

| Python | Shakti |
|--------|--------|
| `x = 1` | `x : 1` |
| `x == 1` | `x = 1` |
| `def f(n=2):` | `def f(n:2):` |
| `f(k=1)` | `f(k:1)` |
| `a @ b` | `mmul(a, b)` |
| `assert x` | `assert(x)` |

Also converts functions, one-argument lambdas, `if`/`elif`/`else`, `while`, `for`, break/continue/pass, lists/tuples/dicts, indexing/slices, attributes, calls, decorators, f-strings (without format specs), augmented `+= -= *= /=`, and `import name`.

`python.py` demonstrates NumPy and pandas lowering. `numpy.array`/`asarray`
become native vectors or matrices; common reducers map to Shakti builtins;
`pandas.Series` becomes a vector and `pandas.DataFrame({...})` becomes
`table(...)`. Other NumPy/pandas calls fail with a source location.

## Rejected

Chained assignment/comparisons, annotations, classes, comprehensions/generators, sets/bytes/complex, `is`/`is not`, bitwise ops, unary `+`, `*args`/`**kwargs`, keyword-only/positional-only args, multi-argument lambdas, loop `else`, from-import/aliases, nested/starred unpacking, exceptions/`with`/`yield`/`async`, and `del`/`global`/`nonlocal`.

Docstrings become `#` comments.

---

# Syntax and builtins

## Core syntax

- **Bind** with `:` — `x: 1`, `a, b: (1, 2)`, `def f(n:2):`, `table(a:[1])`
- **Compare** with `=` — `if x = 1:`, `while i < len(s):`
- Dict literals and slices keep `:` — `{k: v}`, `a[1:3]`
- `==` is not supported

String and formatted-string token text is limited to 8191 bytes, and source
indentation is limited to 255 nested levels. Inputs beyond either limit fail
with a lexer error instead of being truncated.

```ie
def index_of(s, ch):
    i : 0
    while i < len(s):
        if s[i] = ch:
            return i
        i : i + 1
    return -1

for c in "abc":
    print(c)

import sql
r : select id, amount by dept from t where amount > 15
```

**Note:** `n - 1` after a variable parses as a call (`n(-1)`). Use `n + (-1)` instead.

## Decorators

Leading `@` decorates the following `def`, `class`, or name assignment.

```ie
def trace(f):
    def wrapper(x):
        print("call:", x)
        return f(x)
    return wrapper

@trace
def square(n):
    return n * n

print(square(5))
```

Decorators may be names, dotted lookups, or calls:

```ie
@registry.validate
@repeat(3)
def emit(s):
    return s
```

Stacked decorators apply nearest-first:
`emit : registry.validate(repeat(3)(emit))`.

Assignments may be decorated:

```ie
@trace
double : lambda x: x + x
```

`@d` rebinds the name as `name : d(name)`. It cannot decorate control flow,
expressions, attributes, or unpacking.

Leading `@` decorates. Expression `@` is each. Matrix multiply is `mmul(a, b)`.

## Each (`@`)

`@` applies a callable over finite containers. The callable may be a name,
dotted path, lambda, or call result. `+@` is invalid.

### Unary each

```ie
print(abs@ [1, -2, 3])     # [1, 2, 3]
print(abs@ -7)             # 7
```

`f@ xs` applies `f` to each element. A scalar is applied once.

### Dyadic each

```ie
def add(a, b):
    return a + b

print([1, 2] add@ [10, 20])   # [11, 22]
print([1, 2] add@ 5)          # [6, 7]
print(5 add@ [1, 2])          # [6, 7]
```

`xs f@ ys` applies `f` to pairs. Scalars extend. Containers must conform.

### Containers and results

| Input | Iteration | Result |
|-------|-----------|--------|
| `list` / `ivec` / `fvec` / `bvec` | elements | specialized native vector when homogeneous; otherwise `list` |
| `str` | characters | `str` when every result is a one-character string; otherwise `list` |
| matrix | cells | specialized native matrix when homogeneous; otherwise list-of-rows |
| `dict` | values (keys preserved) | `dict` with the left key order |
| `table` | cells (columns/shape preserved) | `table` with the same columns |
| `input` stream | — | error (not supported) |

Dicts pair by key; keys must match. Tables pair by column; names and row counts
must match. Empty inputs retain shape and type.

### Errors

```ie
range(2) add@ range(3)   # length mismatch
a @ b                    # error unless a is callable
```

Leading `@` decorates. `mmul(a, b)` multiplies matrices.

## Methods

| Method | Types |
|--------|-------|
| `append(x)` | `list` |
| `pop()` | `list` |
| `keys()`, `values()` | `dict`, `table` |
| `len()` | `list`, `str`, `ivec`, `fvec`, `bvec`, `matrix[int]`, `matrix[float]`, `matrix[bool]` |

## Vectors

`range(n)` builds an `ivec` `[0, 1, …, n - 1]`. Element-wise `+`, `-`, `*`, `/`, `//`, `%` work on matching-length `ivec` / `fvec` pairs (and scalar broadcast).

| Builtin | Meaning |
|---------|---------|
| `dot(a, b)` | Inner product (fused; no intermediate vector) |
| `sum(v)` | Sum of elements |
| `min(v)`, `max(v)`, `avg(v)` | Reducers |
| `abs(v)` | Element-wise absolute value |

```ie
a : range(1000000)
b : range(1000000)
print(dot(a, b))       # prefer this on large vectors
print(sum(a * b))      # same math; allocates a * b first
```

`.` selects attributes or columns. `dot(a, b)` is inner product.
`mmul(a, b)` is matrix multiply. Leading `@` decorates; expression `@` is each.

Large vector operations use OpenMP. `make prod-speed` enables native SIMD.
With `ISOLDE_LIB`, reducers may use `isolde_*` kernels.

## Matrices

Rectangular nested literals promote to native matrix types. Ragged nested lists stay as `list`.

| Type | Literal | `type()` name |
|------|---------|---------------|
| int | `[[1, 2], [3, 4]]` | `matrix[int]` |
| float | `[[1.0, 2.0], [3.0, 4.0]]` | `matrix[float]` |
| bool | `[[True, False], [False, True]]` | `matrix[bool]` |

### Indexing and shape

```ie
m : [[1, 2], [3, 4]]
m[0]        # row [1, 2] as ivec
m[0, 1]     # cell 2
m[0:1]      # row slice (submatrix)
m[0] : [9, 8]   # assign row
m[0, 1] : 7     # assign cell

len(m)      # row count
shape(m)    # [rows, cols]
```

### Operators

| Operator | Meaning |
|----------|---------|
| `mmul(a, b)` | matrix multiply (`matrix[bool]` not supported) |
| `+`, `-`, `*`, `/`, `//`, `%`, `**` | element-wise on numeric matrices |
| unary `-` | element-wise negation (int/float matrices) |
| `=`, `!=`, `<`, `>`, `<=`, `>=` | element-wise compare → `matrix[bool]` |

Expression `@` is each. Leading `@` decorates. `mmul` multiplies matrices.

`matrix[bool]` does not support arithmetic or `mmul`.

Mixed int/float operands promote to `matrix[float]` where needed.

### Builtins

Same reducers as vectors, applied over all elements: `sum`, `min`, `max`, `avg`, `abs`. `dot` applies to vectors only, not matrices. `mmul(a, b)` is matrix multiply.

### Performance

On x86-64, `make prod-speed` enables AVX-512 paths for large numeric matrix `mmul`, element-wise ops, comparisons, and table filters when the CPU supports them. On arm64 (Apple Silicon), the same matrix operations use NEON (install `libomp` for OpenMP row parallelism). Smaller matrices use scalar code.

The default `make prod` build parallelizes large `ivec` `+` / `-` / `*` with OpenMP. Vector **`dot`** and large **`sum`** on `fvec` use the SIMD/OpenMP stack in `src/vec_kernels.c` (AVX-512/NEON when `prod-speed` enables those ISAs). There is no GPU backend in the standalone binary.

OpenMP thread count affects short vector timings. Keep `OMP_NUM_THREADS`
fixed when comparing local runs.

Example: `matrix.ie`.

### Printing

- `print(m)` — column-aligned rows
- `repr(m)` — compact `[[1, 2], [3, 4]]`

### Tables

A matrix column stores one matrix per table row (table height = matrix row count). SQL `where` filters copy matrix rows like other column types.

CSV/TSV/XML load and CSV/TSV save do **not** round-trip matrix columns as typed matrices; use in-memory tables or nested lists. See [Tables from files](#tables-from-files) for string-column and short-row behavior.

## Data

```ie
d : dict(a:1, b:2)
t : table(a:[1, 2], b:[3, 4])
k : ktable(a:1, b:2)
```

## I/O and JSON

- `read(path)`, `write(path, text)`, `readlines(path)`
- `listdir(path)`, `walk(path)`
- `json_loads(s)`, `json_dumps(x)` — JSON subset (no comments or trailing commas)
- `re_match`, `re_findall`, `re_sub`, `re_split` — POSIX regex on Unix/macOS
- `argv` — script path plus remaining CLI args (set when running a script file)

## Tables from files

```ie
t : load("file.csv")
t : load("file.tsv")
t : load("file.xml")
save(t, "out.csv")
save(t, "out.tsv")
```

Supported formats: **CSV**, **TSV**, and **XML**. `save` writes **CSV** and **TSV** only.

### CSV / TSV save

- Numeric columns: `ivec` / `fvec` (and int/float cells in list columns).
- String columns: list-of-string columns write each cell as text; `None` / unsupported cell types emit an empty field.
- Matrix columns are not supported (save fails).
- Delimiter is `,` for `.csv` and tab for `.tsv`. Fields are not quoted; values must not contain the delimiter.

### CSV / TSV load

- Header required; data rows load as numeric columns (`ivec` or `fvec`). Non-numeric text parses as `0`.
- Empty fields (e.g. CSV `1,` or TSV `1\t`) pad as `0`. Leading/trailing empty fields are preserved.
- String columns written by `save` do **not** round-trip as strings — reload yields numerics (often `0` for labels).

## Input

Module: `import input` — see [input module](#input-module). Examples: `input_demo.ie`, `synth_input.ie`.

```ie
import input

for line in input("> "):
    print(line)

for ev in input(2):
    if ev["kind"] = "down":
        print(ev["code"], ev["utf8"])

events : input(50)          # poll up to 50 ms
line   : readline("name? ") # one blocking line
wait(-1)                    # block until first event
```

| Form | Meaning |
|------|---------|
| `input(0)` | Pending events (non-blocking list) |
| `input(ms)` | Events within `ms` milliseconds |
| `input(1)` | Stream of raw characters |
| `input(2)` | Stream of `{code, modifiers, utf8, kind}` dicts |
| `input("prompt")` | Stream of lines |
| `input_set_own_gui(1)` | Synth window keys go to hub only (see [synth module](#synth-module)) |

## SQL

Run `import sql` first — see [sql module](#sql-module). Example: `sql_demo.ie`.

```ie
import sql

r : select amount by dept from t where amount > 15
r : select sum amount by dept from t
create table u (id: 0, name: "")
insert into u (id, name) values (1, "ada")
update name : "ADA" from u where id = 1
delete from u where id = 2
```

`by col1, col2` groups and sorts ascending. No separate `group by` / `order by`. Join (`t1 join t2 on col`) is not yet implemented.

## Modules

| Module | Doc | Example |
|--------|-----|---------|
| `sql` | [sql module](#sql-module) | `sql_demo.ie` |
| `graph` | [graph module](#graph-module) | `graph_demo.ie` |
| `gfx` | [gfx module](#gfx-module) | `gfx_demo.ie` / `examples/gfx_demo.ie` |
| `input` | [input module](#input-module) | `input_demo.ie` |
| `synth` | [synth module](#synth-module) | `synth_demo.ie` |
| `talk` | [talk module](#talk-module-macos) | `talk_demo.ie` |
| `ipc` | [IPC module](#ipc-module) | `ipc_echo.ie` |
| `rest` | [REST module](#rest-module) | `rest_demo.ie` |

Index: [examples index](#examples-index).

---

# `sql` module

Enables in-memory **table SQL** statement syntax after `import sql`.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: sql_demo.ie
```

## Example

`sql_demo.ie`:

```ie
import sql

t : table(id:[1, 2, 3], dept:["sales", "eng", "sales"], amount:[10, 20, 30])

r : select amount by dept from t where amount > 15
totals : select sum amount by dept from t

create table u (id: 0, name: "")
insert into u (id, name) values (1, "ada")
update name : "ADA" from u where id = 1
delete from u where id = 2

j : t join u on id
```

## Statements

| Statement | Example |
|-----------|---------|
| Select | `select col by group from t where expr` |
| Aggregate | `select sum amount by dept from t` |
| Update | `update col : expr from t where expr` |
| Delete | `delete from t where expr` |
| Create | `create table name (col: default, ...)` |
| Insert | `insert into name (cols...) values (...)` |
| Join | `t1 join t2 on col` *(not yet implemented)* |

`by col1, col2` groups and sorts ascending. There is no separate `group by` / `order by` clause.

See also [syntax and builtins](#syntax-and-builtins) for tables, `load("file.csv")`, and matrix types.

---

# graph module

In-memory knowledge graph (subject–predicate–object triples).

Run from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: graph_demo.ie
```

## Example

`graph_demo.ie`:

```ie
import graph

graph.create()
graph.add("Ada", "knows", "Bob")
graph.add("Bob", "knows", "Carol")

print(graph.query("Ada", "knows", "*"))
print(graph.path("Ada", "Carol", 4))
```

## Model

Each fact is a **triple** `(subject, predicate, object)` — the same RDF-style pattern used in enterprise knowledge graphs (e.g. Cambridge Semantics Anzo). Triples live in memory with indexes on subject, predicate, and object for fast pattern lookup.

Use `"*"` (or `""`) as a wildcard in queries.

## API

Module `lib/graph.ie`:

| Function | Purpose |
|----------|---------|
| `graph.create()` | Create a graph (returns handle id) |
| `graph.add(s, p, o)` | Insert a triple |
| `graph.query(s, p, o)` | Pattern match; returns table |
| `graph.neighbors(node, direction)` | Edges touching `node` (`"out"`, `"in"`, `"both"`) |
| `graph.path(from, to, max_depth)` | Shortest path as list of node names |
| `graph.from_table(t, subj_col, pred, obj_col)` | Bulk load from a Shakti table |
| `graph.to_table(s, p, o)` | Export query matches as table |
| `graph.count()` | Number of triples |
| `graph.clear()` | Remove all triples |

## Builtins

Lower-level C builtins (handle id as first argument):

| Builtin | Returns |
|---------|---------|
| `graph_create()` | New graph id |
| `graph_add(g, s, p, o)` | Triple count after insert |
| `graph_query(g, s, p, o)` | Table with `subject`, `predicate`, `object` columns |
| `graph_neighbors(g, node, direction)` | Table of matching edges |
| `graph_path(g, from, to[, max_depth])` | List of node names |
| `graph_from_table(g, t, subj_col, pred, obj_col)` | Triple count after load |
| `graph_to_table(g, s, p, o)` | Same as `graph_query` |
| `graph_count(g)` | Triple count |
| `graph_clear(g)` | `0` |

## With SQL tables

`import graph` complements [`import sql`](#sql): use tables for structured rows, then `graph.from_table` to link entities by relationship.

See also [syntax and builtins](#syntax-and-builtins) for the `table()` constructor and [examples index](#examples-index).

---

# `gfx` module

Standalone **pixel window** with a fixed 960×540 design buffer and letterboxed present scaling (X11 on Linux, Cocoa on macOS). Built by default (`SHAKTI_GFX=1`).

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: gfx_demo.ie
# or: ./shakti examples/gfx_demo.ie
```

Linux needs `libx11-dev`. Disable at build time with `SHAKTI_GFX=0 make prod`.

## Example

`gfx_demo.ie`:

```ie
import gfx

if not gfx.available():
    print("gfx not available")
else:
    gfx.open("Shakti GFX")
    gfx.clear(0x0a0a12)
    gfx.fill_rect(80, 60, 800, 40, 0x2244aa)
    gfx.fill_circle(640, 300, 90, 0x44dd88)
    gfx.line(40, 500, 920, 40, 0xffffff)
    while gfx.alive():
        if gfx.click_pending():
            gfx.fill_circle(gfx.click_x(), gfx.click_y(), 12, 0xffcc00)
            gfx.consume_click()
        gfx.tick()
    gfx.close()
```

## Examples

| File | Description |
|------|-------------|
| `gfx_demo.ie` | Minimal open/draw/click loop ([`example.ie`](example.ie) section; also `examples/gfx_demo.ie`) |
| `examples/gfx_demo_timed.ie` | Reports open/draw/tick timing |
| `examples/gfx_movie.ie` | Animated redraw stress demo |
| `tests/gfx_api.ie` | Smoke test for API and reopen/close behavior |

## API

Module `lib/gfx.ie`.

| Form | Meaning |
|------|---------|
| `gfx.open([title])` | Open the window; returns an error value on failure |
| `gfx.close()` | Close the window |
| `gfx.alive()` | `1` while the window is open |
| `gfx.available()` | `1` when built with a GUI backend |
| `gfx.tick()` | Poll events and present the framebuffer if dirty |
| `gfx.sync_keys()` | Refresh GUI-owned key state into the input hub |
| `gfx.clear(color)` | Fill the full design buffer with `0xRRGGBB` |
| `gfx.fill_rect(x, y, w, h, color)` | Filled rectangle |
| `gfx.line(x0, y0, x1, y1, color)` | Bresenham line |
| `gfx.fill_circle(cx, cy, r, color)` | Filled circle |
| `gfx.click_pending()` | `1` when a click is waiting |
| `gfx.click_x()` | Last click X in design coordinates |
| `gfx.click_y()` | Last click Y in design coordinates |
| `gfx.consume_click()` | Clear the pending click |

Colors are packed as `0xRRGGBB`. Clicks are reported in design-buffer coordinates (not raw window pixels).

---

# `input` module

Unified **terminal and GUI event hub** — keyboard, mouse, wheel, and line input in one poll API. Used by the synth UI and REPL.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: input_demo.ie
```

Synth + keyboard jam: `synth_input.ie` (`input_set_own_gui(1)` routes window keys to the hub).

## Example

`input_demo.ie`:

```ie
import input

line : readline("line? ")
print(line)

for ev in input(2):
    if ev["kind"] = "down":
        print(ev["code"], ev["utf8"])
```

## API

Module `lib/input.ie`.

| Form | Meaning |
|------|---------|
| `input(0)` | Pending events (non-blocking list) |
| `input(ms)` | Events within `ms` milliseconds |
| `input(1)` | Stream of raw characters |
| `input(2)` | Stream of `{code, modifiers, utf8, kind}` dicts |
| `input("prompt")` | Stream of lines |
| `readline("prompt")` | One blocking line |
| `wait(-1)` | Block until first event |
| `input_set_own_gui(1)` | Synth window keys go to hub only ([synth module](#synth-module)) |

Side-channel state on the module: `input.x`, `input.y`, `input.wheel`, `input.qwerty`, `input.hz`.

| Helper | Purpose |
|--------|---------|
| `input.set_hz(n)` | Poll rate hint |
| `input.set_own_gui(on)` | GUI key routing |
| `input.reload_qwerty()` | Reload QWERTY → semitone map |
| `input.refresh_side()` | Refresh `x`/`y`/`wheel`/`qwerty`/`hz` |

---

# IPC module

Sync and poll-based async message passing between shakti processes (`import ipc`). Messages are length-prefixed strings (4-byte big-endian header + payload, max 1 MiB).

## Transport selection

| Target | Default | Override |
|--------|---------|----------|
| `127.0.0.1`, `localhost`, `::1` | Unix domain socket at `$SHAKTI_IPC_DIR/shakti-<port>.sock` (default `/tmp`) | `transport="tcp"` |
| Remote host | RDMA (RoCE v2 via `librdmacm`) when a device exists, else TCP | `transport="tcp"` or `transport="rdma"` |

Environment:

- `SHAKTI_IPC_DIR` — UDS socket directory (default `/tmp`)
- `SHAKTI_IPC_TRANSPORT` — default transport: `auto`, `tcp`, `uds`, `rdma`

Build:

- `SHAKTI_IPC=1` (default) — socket IPC in the binary
- `SHAKTI_RDMA=1` (default on Linux) — links `ipc_rdma.c` when `/usr/include/infiniband/verbs.h` and `rdma/rdma_cma.h` exist; adds `-lrdmacm -libverbs`

RoCE setup (Linux): install `rdma-core`, `libibverbs-dev`, `librdmacm-dev`; configure the NIC (`rdma link`, `ibv_devinfo`). RoCE v2 is negotiated by the kernel/RDMA stack; no extra GID setup in shakti v1.

## Sync API

```ie
import ipc

srv : ipc.listen(9000)
conn : ipc.accept(srv)
ipc.send(conn, "hello")
msg : ipc.recv(conn)
ipc.close(conn)
ipc.close(srv)

c : ipc.connect("127.0.0.1", 9000)
ipc.send(c, "ping")
print(ipc.recv(c))
ipc.close(c)
```

## Async API

Same model as `input(ms)` — poll-based, not coroutines.

```ie
import ipc

c : ipc.connect("127.0.0.1", 9000)
ipc.set_nonblock(c, 1)
ready : ipc.poll([c], 50)
if len(ready) > 0:
    msg : ipc.recv_nowait(c)
```

`ipc.recv_nowait(h)` returns `""` when no full message is available.

## Shared memory (local bulk)

```ie
tok : ipc.shm_open("buf", 1048576)
ipc.shm_close(tok)
```

POSIX `shm_open` + `mmap`; use for large zero-copy regions between co-located processes.

## RDMA

```ie
if ipc.rdma_available():
    srv : ipc.listen(19100, "0.0.0.0", "rdma")
    conn : ipc.accept(srv)
    ...
```

See `ipc_rdma.ie`.

## Native builtins

| Builtin | Role |
|---------|------|
| `ipc_listen(port[, host, transport])` | Listen handle |
| `ipc_accept(listen_h)` | Connection handle |
| `ipc_connect(host, port[, transport])` | Connection handle |
| `ipc_send(h, str)` | Send message |
| `ipc_recv(h)` | Blocking receive |
| `ipc_recv_nowait(h)` | Non-blocking receive |
| `ipc_set_nonblock(h, on)` | Toggle non-blocking mode |
| `ipc_poll(handles, timeout_ms)` | Ready handle list |
| `ipc_close(h)` | Close handle |
| `ipc_shm_open(name, size)` | Shared memory token |
| `ipc_shm_close(token)` | Unmap and unlink |
| `ipc_rdma_available()` | `1` if RDMA device present |

## Examples

See [examples index](#examples-index). IPC-specific:

| File | Description |
|------|-------------|
| `ipc_echo.ie` | UDS echo server |
| `ipc_echo_client.ie` | UDS client |
| `ipc_rdma.ie` | RDMA server |
| `ipc_rdma_client.ie` | RDMA client |

---

# REST module

HTTP **client** (via `curl` on `PATH`) and a minimal in-process **HTTP/1.1 server** (TCP) after `import rest`.

## Requirements

- **Client:** `curl` on `PATH`
- **Server:** available on Linux and macOS standalone builds (not WASM)

Optional bearer token for client requests:

```bash
export SHAKTI_REST_TOKEN=...
```

## Client

```ie
import rest

resp : rest.get("https://api.example.com/items")
if rest.ok(resp):
    print(rest.status(resp))
    print(rest.json(resp))
else:
    print("HTTP", rest.status(resp), rest.text(resp))

resp2 : rest.post_json("https://api.example.com/items", {"name": "alpha"})
```

| Function | Description |
|----------|-------------|
| `rest.get(url)` | GET request |
| `rest.post(url[, body, content_type])` | POST request |
| `rest.put(url[, body, content_type])` | PUT request |
| `rest.delete(url)` | DELETE request |
| `rest.post_json(url, obj)` | POST with `application/json` body |
| `rest.put_json(url, obj)` | PUT with `application/json` body |
| `rest.request(method, url[, body, content_type, headers])` | Generic request; `headers` is a dict |
| `rest.status(resp)` | HTTP status code |
| `rest.ok(resp)` | `True` when status is 2xx |
| `rest.json(resp)` | Parsed body (JSON object/list or string) |
| `rest.text(resp)` | Raw response body string |

Response dict shape:

```json
{"status": 200, "body": ..., "raw": "...", "headers": {"Content-Type": "..."}}
```

## Server

Single-threaded, one request per accepted connection. Supports `Content-Length` request bodies only (no chunked transfer).

```ie
import rest

srv : rest.listen(8080)
conn : rest.accept(srv)
req : rest.read(conn)
print(req["method"], req["path"])
rest.respond_json(conn, 200, {"ok": 1})
rest.close(conn)
rest.close(srv)
```

| Function | Description |
|----------|-------------|
| `rest.listen(port[, host])` | Listen on TCP (default host `127.0.0.1`) |
| `rest.accept(listen_h)` | Accept connection handle |
| `rest.read(conn)` | Read request → `{method, path, body, headers}` |
| `rest.write(conn, status[, body, content_type])` | Send HTTP/1.1 response |
| `rest.respond_json(conn, status, obj)` | JSON response helper |
| `rest.close(h)` | Close listen or connection handle |

## Example

`rest_demo.ie` — local server spawn + GET/POST client calls.

## Limitations

- Client shells out to `curl` per request (latency dominated by process spawn).
- Server is not production-grade: no TLS, no HTTP/2, no chunked encoding, no keep-alive.
- Not available in WASM builds.

## See also

- [IPC module](#ipc-module) — length-prefixed TCP/UDS messaging (not HTTP)
- [third-party](#third-party-dependencies-and-optional-assets) — `curl` dependency

---

# `synth` module

Desktop synth UI (Linux: X11 + ALSA; macOS: Cocoa + Core Audio).

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: synth_demo.ie
```

## Example

`synth_demo.ie`:

```ie
import synth

synth.open()
synth.set_steps(16)
synth.set_metro(1)
synth.set_metro_sound(0)   # 0 = click, 1 = drum

while synth.alive():
    synth.tick()
```

Jam with keyboard events: `synth_input.ie`:

```ie
import synth, input

synth.open()
input_set_own_gui(1)

for ev in input(2):
    synth.tick()
    if not synth.alive():
        break
    if ev["kind"] = "down":
        idx : input.qwerty[str(ev["code"])]
        if idx >= 0:
            synth.note_on(60 + idx, 0.88)
```

## API

Module `lib/synth.ie`: `open`, `close`, `alive`, `tick`, `set_steps`, `steps`, `set_metro`, `metro_on`, `set_metro_sound`, `metro_sound`, `set_mute`, `note_on`, `note_off`, `set_tuning`, `tuning`.

## Tuning

Default pitch uses **12-TET** (equal temperament). Switch to **just intonation** for pure frequency ratios (3:2 fifth, 5:4 major third) so chord partials align:

```ie
synth.set_tuning("just")   # or "ji"
synth.set_tuning("12tet")  # default; aliases: "equal"
```

Example: `synth_just_intonation.ie` plays a just-intonation C major chord.

| Builtin | Returns |
|---------|---------|
| `synth_open()` | Opens window + audio |
| `synth_close()` | Closes synth |
| `synth_alive()` | `1` while open |
| `synth_tick()` | Pump UI + audio |
| `synth_set_steps(n)` | Pattern length 1..64 |
| `synth_steps()` | Current length |
| `synth_set_metro(on)` | Metronome on/off |
| `synth_set_metro_sound(0\|1)` | Click vs drum |

## Samples

Load local `.wav` files with `load_sample(path)`.

| API | Purpose |
|-----|---------|
| `load_sample(path)` | Load a `.wav` (16- or 24-bit PCM) into row 6 (`SAMP`) |
| `sample_loaded()` | `1` if a sample is loaded |
| `sample_name()` | Basename of the loaded file |

**Limits:** stereo is downmixed to mono; sample rate is resampled to 48 kHz; playback buffer holds at most **8 seconds**.

## Examples

| File | Description |
|------|-------------|
| `synth_demo.ie` | Window + event loop |
| `synth_song.ie` | Twinkle + drum sequencer |
| `synth_input.ie` | Keyboard jam with `input(2)` |

Disable at build: `SHAKTI_SYNTH=0 make prod`.

---


# `talk` module (macOS)

Speech-to-text from the microphone. Built by default on macOS (`SHAKTI_TALK=1`).

Grant **Microphone** and **Speech Recognition** to your terminal in System Settings.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
./shakti example.ie  # section: talk_demo.ie
```

## Example

`talk_demo.ie`:

```ie
import talk

talk.set_locale("en-US")
text : talk.listen(2)    # stop after 2 s silence, Enter, or click
print(text)
```

Or `talk(2)` as shorthand for `talk.listen(2)`.

Default locale: `en-US`. Override with `export SHAKTI_TALK_LOCALE=en-GB` or `talk.set_locale("en-GB")`.

## API

Module `lib/talk.ie`: `listen(silence_sec)`, `set_locale(locale)`.

| Builtin | Returns |
|---------|---------|
| `talk_listen(silence_sec)` | Transcript string |
| `talk_set_locale(locale)` | Sets BCP-47 locale |

Disable at build: `SHAKTI_TALK=0 make prod`.

---

# Third-party dependencies and optional assets

The standalone `shakti` binary has **no vendored C libraries** in the published tree.

## Linked system libraries

| Library | Purpose | Platform |
|---------|---------|----------|
| libexpat | XML table loading (`load("file.xml")`) | Linux, macOS |
| libX11, libasound | GFX + synth UI | Linux |
| Cocoa, Core Audio, Core Foundation | GFX + synth UI | macOS |
| Speech, AVFoundation | `import talk` | macOS |
| librdmacm, libibverbs | Optional RDMA IPC | Linux (when dev headers present) |
| libgomp | OpenMP (matrix `mmul`, large `ivec` `+`/`-`/`*`, vector `dot` / large `sum`) | Linux (default with GCC) |
| libomp | OpenMP (`brew install libomp`) | macOS |
| libpthread, libm, librt, libdl | Runtime | Linux |

`import rest` uses `curl` on `PATH` for HTTP client requests (not linked at build time). The in-process HTTP server uses BSD sockets.

Optional **`libisolde.so`** (set `ISOLDE_LIB` or place next to the isolde tree): when loaded, `dot` / `sum` / `min` / `max` on vectors may delegate to `isolde_*` builtins for native kernels. The standalone binary works without it.

Disable optional components at build time: `SHAKTI_GFX=0`, `SHAKTI_SYNTH=0`, `SHAKTI_TALK=0`, `SHAKTI_IPC=0`, `SHAKTI_RDMA=0`.


## Platform SDKs

macOS builds use system frameworks (Cocoa, Core Audio, Speech, etc.) under their respective platform licenses. Windows builds use the host toolchain under its terms.

---
