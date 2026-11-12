# Shakti documentation

Version **0.15.0**.

Value type tags are layout 1: `char` is 4, `str` is 5, then vectors (`list[int]` … `list`), dict/table/function/datetime/time/input/subprocess, then matrices through `matrix[bool]`. IEFS writes that layout in header word 20 and rejects layout 0 (tag 4 was `str`); re-save those files.

## Contents

- [Examples index](#examples-index)
- [Command-line interface](#command-line-interface)
- [Language pipeline](#language-pipeline)
- [Syntax and builtins](#syntax-and-builtins)
- [Table joins](#table-joins)
- [Decorators](#decorators)
- [Each (`@`)](#each)
- [`sql` module](#sql-module)
- [graph module](#graph-module)
- [`gfx` module](#gfx-module)
- [`pyplot` module](#pyplot-module)
- [`mplsoccer` module](#mplsoccer-module)
- [`jupyter` module](#jupyter-module)
- [`input` module](#input-module)
- [IPC module](#ipc-module)
- [REST module](#rest-module)
- [WebSocket module](#websocket-module)
- [JSON-RPC daemon](#json-rpc-daemon)
- [`synth` module](#synth-module)
- [`dsp` module](#dsp-module)
- [`stem` module](#stem-module)
- [`sonicpi` module](#sonicpi-module)
- [`pdf` module](#pdf-module)
- [`midi` module](#midi-module)
- [`iefs` module](#iefs-module)
- [`hdb` module](#hdb-module)
- [`hld` module](#hld-module)
- [`talk` module](#talk-module-macos)
- [Security](#security)
- [Third-party dependencies](#third-party-dependencies-and-optional-assets)

---

# Examples index

Most demo sections live in [`examples/example.ie`](examples/example.ie) (labels like `sql_demo.ie` are section banners, not separate files).

**Do not run `examples/example.ie` as a single program.** It concatenates interactive demos. After the early non-GUI sections it reaches `gfx_demo` / `synth_*` / `input` event loops (`while gfx.alive(): gfx.tick()` and similar). Interactive loops stay open until the window is closed. Copy one section into its own file, or run a standalone under `examples/`.

```bash
export SHAKTI_LIB=$PWD/lib
# Paste a single section from examples/example.ie into /tmp/demo.ie and run that
./.build/shakti /tmp/demo.ie
```

Copy a section into its own file if you need to run it alone (for example IPC server + client).

## By module

| Module | Section | Description |
|--------|---------|-------------|
| *(core)* | `decorators.ie` | Function, class, stacked, factory, and assignment decorators |
| *(core)* | `each.ie` | `f@ xs` / `xs f@ ys` each |
| *(core)* | `lists.ie` | Typed `list[…]` / `matrix[…]` constructors ([`examples/lists.ie`](examples/lists.ie)) |
| *(core)* | `oop.ie` | Classes / inheritance ([`examples/oop.ie`](examples/oop.ie)) |
| *(core)* | `matrix.ie` | Matrices (`mmul`), `dot`, `sum` / `min` / `max` |
| *(core)* | `table_csv.ie` | CSV/TSV `save` / `load` |
| `import json` | `json_demo.ie` | JSON file load / table / save |
| *(core)* | `sh_demo.ie` | `sh(cmd)` wait status (`true` / `false`) |
| `import subprocess` | `subprocess_demo.ie` | `subprocess.Popen` ([`examples/subprocess_demo.ie`](examples/subprocess_demo.ie)) |
| `import sql` | `sql_demo.ie` | Select, insert, update, delete, join |
| `import graph` | `graph_demo.ie` | Knowledge graph triples, query, path |
| `import gfx` | `gfx_demo.ie` | Pixel window + click drawing |
| `import pong` | `pong_demo.ie` | Pong gfx launcher ([`examples/pong_demo.ie`](examples/pong_demo.ie)) |
| `import chess` | `chess_demo.ie` | Chess gfx launcher ([`examples/chess_demo.ie`](examples/chess_demo.ie)) |
| `import draughts` | `draughts_demo.ie` | Draughts gfx launcher ([`examples/draughts_demo.ie`](examples/draughts_demo.ie)) |
| `import pyplot` | `pyplot_demo.ie` | Line / scatter / bar charts on gfx |
| `import mplsoccer` | `mplsoccer_demo.ie` | Pitch, heatmap, arrows ([`examples/mplsoccer_demo.ie`](examples/mplsoccer_demo.ie)) |
| `import jupyter` | `jupyter_demo.ie` | Notebook cells, `eval`, `.ipynb` R/W, gfx view |
| `import input` | `input_demo.ie` | `readline` + timed event poll |
| `import input` + `synth` | `synth_input.ie` | QWERTY jam with synth window |
| `import synth` | `synth_demo.ie` | Synth window + event loop |
| `import synth` | `synth_song.ie` | Twinkle + drum sequencer |
| `import synth` | `synth_just_intonation.ie` | Just-intonation major chord |
| `import dsp` | `dsp_demo.ie` | Just-intonation ratio helpers (`degree_freq`, `ratio_cents`, `et_delta`) |
| `import stem` | `stem_demo.ie` | Streaming 4-stem separator ([`examples/stem_demo.ie`](examples/stem_demo.ie)) |
| `import sonicpi` | `sonicpi_demo.ie` | Drive Sonic Pi over OSC |
| `import pdf` | `pdf_demo.ie` | PDF 1.4 write/read |
| `import midi` | `midi_demo.ie` | ALSA / CoreMIDI I/O |
| `import iefs` | `iefs_demo.ie` | Durable `.iefs` save / load / map |
| `import talk` | `talk_demo.ie` | Speech-to-text (macOS) |
| `import ipc` | `ipc_echo.ie` | UDS echo server |
| `import ipc` | `ipc_echo_client.ie` | Client for `ipc_echo.ie` |
| `import ipc` | `ipc_sync.ie` | UDS req/rep server (`recv_req` + `reply`) |
| `import ipc` | `ipc_sync_client.ie` | Client for `ipc_sync.ie` (`send_sync`) |
| `import ipc` | `ipc_rdma.ie` | RDMA/RoCE server (Linux + NIC) |
| `import ipc` | `ipc_rdma_client.ie` | Client for `ipc_rdma.ie` |
| `import rest` | `rest_demo.ie` | HTTP GET/POST client + local server |
| `import discord` | `discord_demo.ie` | Discord REST (`guilds()`; skip unless token set) |
| *(core)* | `core_demo.ie` | Defaults, decorators, slices, tables, f-strings |

## Module docs

| Module | Doc |
|--------|-----|
| `sql` | [sql module](#sql-module) |
| `graph` | [graph module](#graph-module) |
| `gfx` | [gfx module](#gfx-module) |
| `pyplot` | [pyplot module](#pyplot-module) |
| `mplsoccer` | [mplsoccer module](#mplsoccer-module) |
| `jupyter` | [jupyter module](#jupyter-module) |
| `input` | [input module](#input-module) |
| `synth` | [synth module](#synth-module) |
| `dsp` | [dsp module](#dsp-module) |
| `stem` | [stem module](#stem-module) |
| `sonicpi` | [sonicpi module](#sonicpi-module) |
| `pdf` | [pdf module](#pdf-module) |
| `midi` | [midi module](#midi-module) |
| `iefs` | [iefs module](#iefs-module) |
| `talk` | [talk module](#talk-module-macos) |
| `ipc` | [IPC module](#ipc-module) |
| `rest` | [REST module](#rest-module) |
| `ws` | [WebSocket module](#websocket-module) |
| Language & builtins | [syntax and builtins](#syntax-and-builtins) |
| CLI | [command-line interface](#command-line-interface) |
| Pipeline | [language pipeline](#language-pipeline) |
| REPL | [REPL](#repl) |
| Table joins | [table joins](#table-joins) |

## Command-line interface

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

Unknown flags exit with status 2. A silent leading `run` argument is accepted for Android launchers only.

A script or `-c` program that doesn't parse is refused whole: its first parse
error is printed, nothing runs, and the exit status is 1 (with `-i`, `-c` reports
it and enters the REPL). The REPL reports a line that doesn't parse and goes on.

Script files must end in `.ie`.

An uncaught error while a script or `-c` program runs (a `raise`, or an error the
program ends on) ends it with status 1; with `-i`, `-c` reports it and enters the
REPL. A failed `assert` ends the process with status 1, `-i` or not.

### REPL

Start with `./.build/shakti` (or `-i` after `--command`). Banner line:

`\d docs  \v vars  \w names  \q [N]  quit|exit`

| Input | Effect |
|-------|--------|
| `\d` / `\help` / `help` | Print the fixed-width grammar card (built into the binary; same text as the removed `IE.txt`); full prose remains in this doc. |
| `\v` | User-defined names and values (not imports, `argv`, or `__` internals) |
| `\w` | User-defined names only |
| `\q` | Process exit status `0` |
| `\q N` | Process exit status `N` (integer; invalid args print `usage: \q [N]` and stay in the REPL) |
| `quit` / `exit` | Soft leave the REPL loop (process status `0`) |

Unknown `\` commands print an error and stay in the REPL.

## Language pipeline

The standalone binary is a **tree-walk interpreter** (no bytecode VM). Source is
tokenized in `src/lex.c`, parsed to an AST in `src/parse.c` / `src/ast.c`, and
evaluated in `src/eval.c`. `src/shakti_lang.c` is the CLI/REPL driver. Cross-file
inlining is recovered with LTO on the default link (see [README build](README.md#build)).

---

# Syntax and builtins

## Core syntax

- **Bind** with `:` — `x: 1`, `a, b: (1, 2)`, `def f(n:2):`, `table(a:[1])`
- `for a, b in xs:` takes each item of `xs` apart, whether a list, a vector or
  a matrix's row (`for a, b in [[1, 2], [3, 4]]:`)
- **Compare** with `=` — `if x = 1:`, `while i < len(s):`
- Dict literals and slices keep `:` — `{k: v}`, `a[1:3]`
- `==` is not supported
- `_` separates digits in a decimal number, as in Python: `1_000`, `0.000_1`, `1e1_0`; it must sit
  between two digits (`1_`, `1__0`, `1_.5` are errors)
- `None`, `False`, zero (including `char(0)` and the zero date, time and datetime), `""`
  and empty containers (list, matrix, dict, table) are false in `if`, `while`,
  `not`, `and`/`or`, `bool()`, `any()`, `all()` and `filter()`
- An error value used as a condition, or as a statement whose result is not kept, raises,
  so `try`/`except` can catch it. Storing one (`x : abs("nope")`) keeps the value.
  `assert` raises `AssertionError`, which `try` catches. `bool` of an error is false
- `=` compares a value with a value. `!=` is the opposite of `=`. Ordering two lists with `<` is an error
- A missing dict key reads as `None`. `len` of a string is its length in bytes
- SQL words (`select`, `from`, `where`, `update`, `delete`, and the rest of that list) are reserved
- An integer literal that does not fit in a signed 64-bit int is a float. `abs` of the minimum int is that positive float
- `b"..."` is `list[char]`. `//=`, `%=` and `**=` (and `//:`, `%:`, `**:`) update in place
- Integer bitwise helpers: `band(a,b)`, `bor(a,b)`, `bxor(a,b)`, `bnot(a)`,
  `shl(a,b)`, `shr(a,b)` — `shl`/`shr` are logical shifts on the low 64 bits;
  shift counts must be in `0..63`
- `0xHH` (exactly two hex digits) is `char`; `0x1234` (even length except 6/8) is `list[char]`; `0c…` is always `list[char]`; 6/8-digit or odd `0x` is `int`
- `char(n)` builds a `char` from an int (distinct from `chr`, which returns a
  one-character `str`)
- `int(x)` of a float truncates toward zero; a NaN, an infinity or a float outside the int range
  (`int(1e19)`) is an error value
- `parse_check(src)` returns `dict(ok, error)` — syntax only, no eval
- `assert e` and `assert e, msg` are statements: the whole expression is asserted
  (`assert [1] = [2]` fails); `assert(e, msg)` works too
- `break` and `continue` act on the innermost `while` or `for` around them,
  within their own `def` or `class` body or at top level; one with no loop
  there (even in a `def` written inside a loop) is a parse error (`'break'
  outside a loop`); an f-string's `{…}` and `eval`'s text are parsed on
  their own, so a `break` there is an error even inside a loop

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

for r in table(a:[1, 2], b:[3, 4]):
    print(r)    # each row as a dict, as list(t) gives them: {"a": 1, "b": 3}, then {"a": 2, "b": 4}

import sql
r : select id, amount by dept from t where amount > 15
```

**Note:** After a name, write `n(-1)` to pass a negative argument. `x -1`
subtracts; `abs -1.2` is juxtaposition (`abs(-1.2)`).

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
| `len()` | `list`, `str`, `ivec`, `fvec`, `bvec`, `list[char]`, `matrix[int]`, `matrix[float]`, `matrix[bool]`, `matrix[char]` |
| `find(substr)` | `str` — byte index of first match, or `-1` |

## Lists

`len(x)` counts items in a list, vector, or string, and rows in a matrix.

| Call | Meaning |
|------|---------|
| `any(xs)`, `all(xs)` | Of one list or vector: whether some item, or every item, is true (`any([0, 0])` is `False`, `all([])` is `True`). With several arguments, `any(a, b, …)` and `all(a, b, …)` test the arguments. One str, matrix, dict or number is tested whole (`all("")` is `False`). |
| `sublist(n, xs)` | First `n` items. A negative `n` takes from the end. |
| `sublist([i, n], xs)` | `n` items starting at index `i` (`i` may be negative). |
| `drop(n, xs)` | Drop the first `n` items. A negative `n` drops from the end. |
| `catenate(x, y)` | Join lists, vectors, or strings. `+` on `list[int]` adds element-wise; `catenate` joins. A matrix joins by rows: with a matrix of its kind and width, or a vector of its row kind and width (one row), the result is a matrix; otherwise its rows are items of a `list`. |
| `x in xs` | `True` when some item of a list or vector, or row of a matrix, is `= x` (`1 in [1.0, 2.0]`, `[1, 2] in [[1, 2], [3, 4]]`); in a str, a substring or a char; in a dict, a key. A `list[int]` on the left of a `list[int]`, or of a list whose items are all ints, gives a `list[bool]`, one per item (`range(3) in [1, 2]` is `[False, True, True]`); on the left of a list with an item that isn't an int it is an error (`[1, 2] in [[1, 2], [3]]`). A `list[bool]` is never found, nor is a row of a `matrix[bool]` (`=` doesn't compare a `list[bool]`). `not in` is the negation. |

```ie
xs : 1 2 3 4 5
print(sublist(2, xs))         # [1, 2]
print(sublist(-2, xs))        # [4, 5]
print(sublist([1, 3], xs))    # [2, 3, 4]
print(drop(2, xs))            # [3, 4, 5]
print(drop(-1, xs))           # [1, 2, 3, 4]
print(catenate(1 2, 3 4))     # [1, 2, 3, 4]
print(catenate("ab", "cd"))   # abcd
m : catenate([[1, 2]], [[3, 4]])
print(repr(m))                # [[1, 2], [3, 4]], a matrix[int]
print(len(xs))                # 5
```

`xs[i:j]` is a sublist. `xs[n:]` drops the first `n` items. `xs[i:j:k]` takes every
`k`th item. A bound or step is an int (a char counts), or left out (or None); anything else
is an error value (`xs[1.5:3]` is "slice bound must be an int").

## Vectors

`range(n)` builds an `ivec` `[0, 1, …, n - 1]`. Element-wise `+`, `-`, `*`, `/`, `//`, `%` work on matching-length `ivec` / `fvec` pairs (and scalar broadcast).
`**` of ints is an int, as `2 ** 3` is `8`, for vectors and matrices too (`[2, 3] ** 2` is `[4, 9]`);
if an exponent is negative or a power overflows, the whole result is float (`[2, 4] ** -1` is
`[0.5, 0.25]`). An int result is exact, as `5 ** 27` is: `5 ** [27]` is
`[7450580596923828125]`.

A zero divisor in `/`, `//` or `%` on a vector or matrix, whether an item or the number beside it, is the same error value as for scalars (`division by zero`, `modulo by zero`).

| Builtin | Meaning |
|---------|---------|
| `dot(a, b)` | Inner product (fused; no intermediate vector) |
| `sum(v)` | Sum of elements (of an empty `list[float]` or `matrix[float]`: `0.0`; of any other empty vector or matrix: `0`); of a `list[char]` or `matrix[char]`, the sum of the codes; a bool counts as 0 or 1, so of a `list[bool]` or `matrix[bool]` it is the number of `True`s; of a `list`, its items added in order: ints exactly (a char as its code, a bool as 0 or 1), giving an int if the total fits, else a float, and in float from the first float item on; `sum(a, b, ...)` adds its arguments the same way, a vector, matrix or `list` argument summed first, so one whose own sum overflows is a float before it is added; an error operand passes through; of anything else but a number, bool, char, vector or matrix (`sum("x")`), an error |
| `min(v)`, `max(v)` | Least and greatest element (`None` of an empty vector or matrix); of a `list[char]` or `matrix[char]`, `min`/`max` are chars, ordered by code; of a `list[bool]` or `matrix[bool]`, `min`/`max` are bools; of a `str`, its least and greatest character (a `char`); a scalar is its own `min` and `max` (`max(5)` is `5`) |
| `min(a, b, ...)`, `max(a, b, ...)` | Compare the arguments with `<` and `>`, as in Python: `max(3, 9, 4)` is `9`; arguments, or items of a list, that don't compare (`1` and `"a"`, or a `None`) are an error |
| `avg(v)` | Mean of elements (`nan` of an empty vector or matrix); of a `list[char]` or `matrix[char]`, the mean of the codes; of a `list[bool]` or `matrix[bool]`, the fraction of `True`s; of anything but a vector or matrix (`avg(5)`), an error |
| `abs(v)` | Element-wise absolute value |
| `sqrt(v)`, `exp`, `log`, `sin`, `cos`, `tan`, `floor`, `ceil` | Element-wise on int, float and char vectors and matrices, giving floats; an int, float or char (its code) gives a float; an error is passed through; anything else (a bool, str, list, dict, table or None) is an error |

```ie
a : range(1000000)
b : range(1000000)
print(dot(a, b))       # prefer this on large vectors
print(sum(a * b))      # same math; allocates a * b first
```

`.` selects attributes or columns. `dot(a, b)` is inner product.
`mmul(a, b)` is matrix multiply. Leading `@` decorates; expression `@` is each.

Large vector operations use OpenMP. `make prod-speed` enables `-O3` and
native CPU tuning (`-mcpu=native` on arm64, including Apple Silicon M5;
`-march=x86-64-v2` on x86-64). `SHAKTI_PORTABLE_CPU=1` uses `-mcpu=apple-m4` on
arm64 (clang does not yet expose `-mcpu=apple-m5` on current Xcode).

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

for row in m:                 # each row as a vector, as m[i] gives it
    print(row)
for i, row in enumerate(m):   # each row with its index
    print(i, row)
map(sum, m)                   # [16, 7]: map, filter and zip take the rows too
reverse(m)                    # [[3, 4], [9, 7]]: the rows in reverse order
```

### Operators

| Operator | Meaning |
|----------|---------|
| `mmul(a, b)` | matrix multiply (`matrix[bool]` not supported) |
| `+`, `-`, `*`, `/`, `//`, `%`, `**` | element-wise on numeric matrices; a zero divisor in `/`, `//` or `%` is an error, as for scalars |
| unary `-` | element-wise negation (int/float matrices) |
| `=`, `!=`, `<`, `>`, `<=`, `>=` | element-wise compare → `matrix[bool]` |

Expression `@` is each. Leading `@` decorates. `mmul` multiplies matrices.

`matrix[bool]` does not support arithmetic or `mmul`.

Mixed int/float operands promote to `matrix[float]` where needed.

### Builtins

Reducers over all elements: `sum`, `min`, `max`, `avg`. Element-wise: `abs`, and the math builtins
(`sqrt`, `exp`, `log`, `sin`, `cos`, `tan`, `floor`, `ceil`), which give `matrix[float]`. `dot` applies to vectors only, not matrices. `mmul(a, b)` is matrix multiply.

### Performance

On x86-64, `make prod-speed` uses `-march=x86-64-v2` with scalar C (+ OpenMP) for large numeric matrix `mmul`, element-wise ops, comparisons, and table filters. On arm64 (Apple Silicon), the same matrix operations use NEON; `prod-speed` passes `-mcpu=native` (M5 and other hosts) or `-mcpu=apple-m4` with `SHAKTI_PORTABLE_CPU=1` (install `libomp` for OpenMP row parallelism). Smaller matrices use scalar code.

The default `make prod` build parallelizes large `ivec` `+` / `-` / `*` with OpenMP. Vector **`dot`** and large **`sum`** on `fvec` use the SIMD/OpenMP stack in `src/vec_kernels.c` (NEON on arm64; scalar+OpenMP on x86; on Darwin with Accelerate, large `sum`/`dot` use `vDSP` and large float `mmul` uses `cblas_dgemm`). `prod-speed` also retunes C and ObjC units with `-O3` and the arch flags above. Default `make` / `make prod` also enables LTO (`-flto` / `-flto=auto`) so lexer, parser, and eval still inline across `src/*.c` units. There is no GPU backend in the standalone binary.

OpenMP thread count affects short vector timings. Keep `OMP_NUM_THREADS`
fixed when comparing local runs.

Example: `matrix.ie`.

### Printing

- `print(m)` — column-aligned rows
- `repr(m)` — compact `[[1, 2], [3, 4]]`

### Tables

A matrix column stores one matrix per table row (table height = matrix row count). SQL `where` filters copy matrix rows like other column types.

`t.c : v` replaces or adds column `c`. `v` must be a vector, list or matrix with the table's height; a scalar, a string or a column of another height is an error value.

CSV/TSV/XML load and CSV/TSV save do **not** round-trip matrix columns as typed matrices; use in-memory tables or nested lists. See [Tables from files](#tables-from-files) for string-column and short-row behavior.

## Data

`d[k]` reads key `k`, `d[k]: v` sets it (a new key goes at the end) and `d[k] += v` (or `-=`,
`*=`, `/=`) updates it (with a missing key it does nothing), for keys of every kind: a string key
matches by text, any other by `=` (`d: {1: "a"}; d[1]: "b"` changes key 1), and a key that `=`
finds equal to no key (NaN, a function, a table) is never found, so each `d[k]: v` adds it again.

```ie
d : dict(a:1, b:2)
t : table(a:[1, 2], b:[3, 4])
u : t[t.a > 1]    # rows where the bool mask is true; a mask whose length is not the table's row count is an error value
t : table(a:1 2, b:3 4)
t : table(a:1 2; b:3 4)
k : ktable(a:1, b:2)
```

Each column of `table(...)` (named arguments or a dict) is a vector, list or matrix, and all have the same length, the table's row count. Anything else (a scalar, a string, a dict, a table, None) or columns of different lengths is an error value.

`for k in d` gives each key of a dict in order, and `for k, v in d` each key
with its value. `enumerate(d)` pairs each key with its index: `[[0, "a"], [1, "b"]]`;
`map`, `filter` and `zip` take the keys too (`zip(d, [7, 8])` is `[["a", 7], ["b", 8]]`),
and `reverse(d)` is the dict with its entries in reverse order.

## I/O and JSON

- `read(path)`, `write(path, text)`, `readlines(path)`
- `listdir(path)`, `walk(path)`
- `getcwd()`, `mkdir(path)`, `getenv(name)` — `getenv` returns `None` if unset
- `path_join`, `path_exists`, `path_isdir`, `path_isfile`, `path_basename`,
  `path_dirname`, `path_splitext`
- `json_loads(s)`, `json_dumps(x)` — JSON subset (no comments or trailing commas)
- `re_match`, `re_findall`, `re_sub`, `re_split` — POSIX regex on Unix/macOS
- `argv` — script path plus remaining CLI args (set when running a script file)
- `parse_check(src)` — syntax-only check; returns `dict(ok, error)`
- `eval(src)` — parse and evaluate a Shakti source string in the **root** environment (returns the value, or an error value). Bindings persist across calls, including when `eval` is invoked from nested functions.
- `sh(cmd)` — run `cmd` via `/bin/sh -c` (POSIX). Returns the wait status
  integer (`0` = success; any non-zero value means failure). This is the
  `waitpid` status, not a bash 0–255 exit code — test with
  `rc = 0` / `rc != 0`. Disabled when `SHAKTI_SAFE=1` or `SHAKTI_ALLOW_EXEC=0`.
  Not available on Windows or WASM builds.
- `subprocess()` / `load(dir_with_run, …, exec:True)` — spawn a directory `run` helper (PTY). `load` on a directory runs nothing unless `exec:True` is passed; `__dir__:` is passed only to the child's environment. Disabled when `SHAKTI_SAFE=1` or `SHAKTI_ALLOW_EXEC=0`. Untrusted `.ie` input is otherwise equivalent to an untrusted shell script; see [Security](#security).
- Parser nesting is capped at 40; interpreter call depth defaults to 3000 (`SHAKTI_CALL_MAX_DEPTH`), and a call made when less than 128 KB plus a sixteenth of the C stack (at most 1 MB) is left is refused too ("recursion limit exceeded", on macOS and Linux, but not on stacks under 256 KB or, on Linux, with no stack limit); an expression too deep for the stack that is left raises the same error.

## Process / REPL

- REPL: `\q` or `\q N` — terminate the process with integer status (default `0`). Invalid `\q` args print `usage: \q [N]` and stay in the REPL.
- Soft leave: type `quit` or `exit` to leave the REPL loop without forcing a non-zero status.
- Other meta-commands: `\d` / `\help` / `help` (built-in grammar card), `\v` (vars), `\w` (names). See [REPL](#repl) under the CLI section.

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
- A float is written rounded to 15 significant digits, or to 16 or 17 when fewer don't read back as
  the same float (from one digit below the smallest normal float: `5e-324`), as `123456789.125`,
  `0.1`, `1e+300`, always with a `.` or an exponent, so a float column loads back as floats, value
  for value. A whole float below 10^15 is written with all its digits and `.0` (`2.0`,
  `100000000000000.0`); infinities and NaN as `inf`, `-inf` and `nan`.
- String columns: list-of-string columns write each cell as text; `None` / unsupported cell types emit an empty field.
- Matrix columns are not supported (save fails).
- Delimiter is `,` for `.csv` and tab for `.tsv`. A field holding the delimiter, a `"` or a line break is
  written in double quotes, with each `"` in it doubled.

### CSV / TSV load

- Header required; data rows load as numeric columns (`ivec` or `fvec`). Non-numeric text parses as `0.0`
  and makes that column an `fvec`.
  A column loads as an `ivec` when every field is digits with an optional sign, once
  the spaces around it (and tabs, in CSV, and a `\r` at its end) are taken off, inside quotes too:
  `1, 2` and `1," 2 "` are ints; `2.0`, `1e3`, or any text such as `x` make the column float.
- Empty fields (e.g. CSV `1,` or TSV `1\t`) pad as `0`. Leading/trailing empty fields are preserved.
- String columns written by `save` do **not** round-trip as strings — reload yields numerics (often `0` for labels).
- A field in double quotes may hold the delimiter, line breaks (anywhere in it, at its end too) and `""`
  (one `"`), as `save` writes them; a quoted field still open at the end of the file is an error. A
  field is quoted only when `"` is its first character (for a record's first field, its first after
  padding; `1, "a"` is text), and a `"` that opens a field is closed by the next lone `"`, even lines
  later: the lines between are part of the field.
- Normal-sized files load via a single buffered read (fast path). Files larger than
  `SHAKTI_CSV_MAX_BYTES` (default 1 GiB; override with that env var) stream with
  `getline` so the whole file need not reside in memory; a record is held whole, though, so a quoted
  field never closed reads the rest of the file into memory before the error.

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
| `input(1)` | Stream of typed keys: each key down's text (`""` for a key without one, such as an arrow); in a terminal, one str per byte, so a UTF-8 character gives several |
| `input(2)` | Stream of `{code, modifiers, utf8, kind}` dicts: a `"down"` and an `"up"` for each key |
| `input("prompt")` | Stream of lines |
| `input_set_own_gui(1)` | Synth window keys go to hub only (see [synth module](#synth-module)) |

In a terminal, `readline`, `input()` and `input("prompt")` read a line with a small line editor.
In a UTF-8 terminal the line is UTF-8 text (a typed `é` is two bytes of the `str`), and Backspace
deletes a whole character, as in the REPL.

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

A bool mask in `where` has one item per row of the table; a mask of another length is an error value.
An error value in `where` is the statement's result (a `from` that isn't a table, or an error value
on the right of an `update`, comes first), and a raise in it stops the statement: either way
`update` and `delete` change nothing (a `.iefs` file is not rewritten).

`by col1, col2` groups and sorts ascending. No separate `group by` / `order by`. Prefer core `,` / `union` / `outer` table joins (see [Table joins](#table-joins----union--outer)); SQL `t1 join t2 on col` is not yet implemented.

## Modules

| Module | Doc | Example |
|--------|-----|---------|
| `sql` | [sql module](#sql-module) | `sql_demo.ie` |
| `graph` | [graph module](#graph-module) | `graph_demo.ie` |
| `gfx` | [gfx module](#gfx-module) | `gfx_demo.ie` |
| `pyplot` | [pyplot module](#pyplot-module) | `pyplot_demo.ie` |
| `mplsoccer` | [mplsoccer module](#mplsoccer-module) | `mplsoccer_demo.ie` |
| `jupyter` | [jupyter module](#jupyter-module) | `jupyter_demo.ie` |
| `input` | [input module](#input-module) | `input_demo.ie` |
| `synth` | [synth module](#synth-module) | `synth_demo.ie` |
| `talk` | [talk module](#talk-module-macos) | `talk_demo.ie` |
| `ipc` | [IPC module](#ipc-module) | `ipc_echo.ie` |
| `rest` | [REST module](#rest-module) | `rest_demo.ie` |
| `ws` | [WebSocket module](#websocket-module) | |

A module that imports itself, directly or through other modules, is an error: the import that
began the cycle raises `import: circular import of 'm'`.

Index: [examples index](#examples-index).

---

# Table joins

Dyadic `,` joins two tables on the **first column** when names and types match:

| Shape | Behavior |
|-------|----------|
| First cols are matching sorted `time` / `time_ns` `ivec` | Asof (left-preserving; miss → `None`) |
| Right first-column values are unique | Left equi-join |
| Right first-column has duplicates | Inner equi-join (1:N expands) |

Explicit forms (same first-column key; no `on` clause):

| Form | Behavior |
|------|----------|
| `a outer b` | Full outer equi-join (tables only) |
| `a union b` | Keyed union-join on tables; order-preserving unique concat on lists/`ivec`/`fvec` |

A join's result (`,`, `outer`, `union`) is a new table with its own columns: replacing a cell of it (`j.a[0] : 9`) doesn't change either source table, nor the other way round. The items of a list column are still shared.

SQL `t1 join t2 on col` remains unimplemented.

## Asof join helpers

| Builtin | Role |
|---------|------|
| `asof_sort(eq, time)` | Sort paired `ivec`s by `(eq, time)` → `[eq_sorted, time_sorted]` |
| `asof_bin(eq, time, query_eq, query_time)` | Per query row, last index with matching `eq` and `time ≤ query_time` (`-1` if none) |

## See also

- [SQL](#sql) / [`sql` module](#sql-module) — general `select` / `where`

---

# `sql` module

Enables in-memory **table SQL** statement syntax after `import sql`.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy sql_demo.ie section from examples/example.ie into its own file, then run it
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
| Delete | `delete from t where expr` (no column list; `delete from t` deletes every row) |
| Create | `create table name (col: default, ...)` |
| Insert | `insert into name (cols...) values (...)` |
| Join | `t1 join t2 on col` *(not yet implemented)* |

`insert` names each column at most once: `insert into u (id, id) values (1, 2)` is
`insert: column 'id' named more than once`, and inserts nothing.

The result of `select` is a new table with its own columns: replacing a cell of it (`r.a[0] : 9`) doesn't change the source table, nor the other way round. `update` and `delete` store their result in the variable they name (after `r : update … from t`, `r` and `t` are the same table), but another name that held the old table (`u : t` beforehand) keeps the old columns: a cell write through `u` doesn't reach `t`, nor the other way round. The items of a list column are still shared, as for a filtered select, so a write inside one (`r.l[2][0] : 9`) reaches the source. A column of a table built in the statement itself stays shared with the value it was built from: after `r : select from table(a: x)`, `r.a[0] : 9` changes `x`. Joins give new tables too ([Table joins](#table-joins)).

`by col1, col2` groups and sorts ascending. There is no separate `group by` / `order by` clause.

`insert` puts each value in its column as it is, or is an error and inserts nothing: a `list[int]`
column takes an int or a char, a `list[float]` a float, an int or a char (an int becomes a float,
and a char its code), a `list[bool]` a bool, a `list[char]` a char, a matrix a row of its width
whose items fit, and a `list` anything. `create table u (id: 0, name: "")` makes `id` a `list[int]`
and `name` a `list`, so `insert into u (id, name) values ("ada", 1)` is
`insert: column 'id' holds int, not str`.

`update` puts values in a column by the same rule (though `insert` puts in the value itself, so a
dict it inserts is shared with its variable), or is an error and changes nothing:
`update id : "x" from u` is `update: column 'id' holds int, not str`, whichever rows `where` picks.
In `col : expr`, a vector, list or matrix is a column, with a value for each row of the table
(`update amount : amount * 2 from t`), except that a vector is one row for a matrix column
(`update m : [0, 0] from t where id = 1` for a `matrix[int]` column `m`); anything else is one
value for every row `where` picks, each row with its own copy of it (a dict put in a `list`
column is not shared by the rows, or with the variable it came from). A column of another
height is `update: column length mismatch`, and an error value is the update's result (a raise in
`where` still stops it).

A group's `sum` of an int column is an int, added as `sum` adds a `list[int]`: exact past 2^53,
and, if any group's int sum in the select doesn't fit in an int, every int sum in it is a
float column. Of a float or `list[bool]` column it is a float. `avg` of an int, float or
`list[bool]` column is a float, and `count` an int. Of a `list` column or a `list[char]` column, a
group's `sum` and `avg` are what the builtins `sum` and `avg` give for its items (an error value
for a group with an item they don't add, as `sum([1, None])` is). A group's `min` and `max` of a
float column are floats; of any other column they are what the builtins `min` and `max` give for
the group's items: an int for an int column, a bool for a `list[bool]` column, a char for a
`list[char]` column, and for a `list` column the group's least and greatest item (an error value
for a group whose items don't compare, as `min([1, None])` is). Of a matrix column, a group's
`sum`, `avg`, `min` and `max` are those of the matrix of the group's rows, taken over all its
items: rows `[1, 10]` and `[2, 20]` have `sum` 33 and `avg` 8.25.

The aggregates are `count`, `sum`, `avg`, `min` and `max`; any other word in a select is a column's
name. A column the table doesn't have is an error value, with or without `by`:
`select sum nosuch by k from t` and `select last n by k from t` are `select: unknown column 'nosuch'`
and `select: unknown column 'last'`, and `update nosuch : 1 from t` is `update: unknown column 'nosuch'`.

`update` and `delete` return the changed table. From a table variable they also rebind it; from a str naming a `.iefs` file (`update … from path`) they write the file and leave the variable as it was (see [iefs module](#iefs-module)).

See also [syntax and builtins](#syntax-and-builtins) for tables, `load("file.csv")`, and matrix types.

---

# graph module

In-memory knowledge graph (subject–predicate–object triples).

Run from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy graph_demo.ie section from examples/example.ie
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

Module `lib/graph.ie`. The functions below act on the current graph, which `create()` and `use(g)` set.

| Function | Purpose |
|----------|---------|
| `graph.create()` | Create a graph and make it the current one (returns handle id) |
| `graph.use(g)` | Make graph `g` (a handle from `create()`) the current one |
| `graph.add(s, p, o)` | Insert a triple |
| `graph.query(s, p, o)` | Pattern match; returns table |
| `graph.neighbors(node, direction)` | Edges touching `node` (`"out"`, `"in"`, `"both"`) |
| `graph.path(from, to, max_depth)` | Shortest path as list of node names |
| `graph.from_table(t, subj_col, pred, obj_col)` | Bulk load from a Shakti table |
| `graph.to_table(s, p, o)` | Export query matches as table |
| `graph.count()` | Number of triples |
| `graph.clear()` | Remove all triples |
| `graph.add_many(triples)` | Insert many `[s, p, o]` lists; returns count added |
| `graph.related(node[, max_depth])` | Nodes within `max_depth` hops (default 2), including `node` |

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

Standalone **pixel window** with a fixed 960×540 design buffer, presented at 1920×1080 with letterboxed scaling (X11 on Linux, Cocoa on macOS). Built by default (`SHAKTI_GFX=1`).

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy the gfx_demo.ie section from examples/example.ie into its own file, then:
./.build/shakti /tmp/gfx_demo.ie
```

Linux needs `libx11-dev`. Disable at build time with `SHAKTI_GFX=0 make prod`.

`gfx.tick()` polls events and presents if dirty, then sleeps ~16 ms (~60 Hz),
matching `synth.tick`. Set `SHAKTI_GFX_NO_SLEEP=1` to restore a busy-wait loop.
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
| `gfx_demo.ie` | Minimal open/draw/click loop ([`examples/example.ie`](examples/example.ie) section) |

## API

Module `lib/gfx.ie`.

| Form | Meaning |
|------|---------|
| `gfx.open([title])` | Open the window; returns an error value on failure |
| `gfx.close()` | Close the window |
| `gfx.alive()` | `1` while the window is open |
| `gfx.available()` | `1` when built with a GUI backend |
| `gfx.tick()` | Poll events, present if dirty, sleep ~16 ms (`SHAKTI_GFX_NO_SLEEP=1` disables) |
| `gfx.sync_keys()` | Refresh GUI-owned key state into the input hub |
| `gfx.clear(color)` | Fill the full design buffer with `0xRRGGBB` |
| `gfx.fill_rect(x, y, w, h, color)` | Filled rectangle |
| `gfx.line(x0, y0, x1, y1, color)` | Bresenham line |
| `gfx.fill_circle(cx, cy, r, color)` | Filled circle |
| `gfx.text(x, y, s, color[, scale])` | Draw monospace 5×7 ASCII text (`scale` default 1); A–Z and a–z have distinct glyphs |
| `gfx.text_width(s[, scale])` | Pixel width of one line of text (6×scale per char) |
| `gfx.copy_rect(sx, sy, w, h, dx, dy)` | Copy a rectangle within the design buffer |
| `gfx.click_pending()` | `1` when a click is waiting |
| `gfx.click_x()` | Last click X in design coordinates |
| `gfx.click_y()` | Last click Y in design coordinates |
| `gfx.consume_click()` | Clear the pending click |
| `gfx.mouse_x()` | Current mouse X in design coordinates |
| `gfx.mouse_y()` | Current mouse Y in design coordinates |
| `gfx.mouse_down()` | `1` while the primary mouse button is held |

Colors are packed as `0xRRGGBB`. Clicks and mouse positions are reported in design-buffer coordinates (not raw window pixels). `gfx.text` covers digits, A–Z, a–z (distinct lowercase), and common punctuation; unknown glyphs draw as a hollow box.

Demo games: [`import pong`](lib/pong.ie) / `pong.run()`, [`import chess`](lib/chess.ie) / `chess.run()`, [`import draughts`](lib/draughts.ie) / `draughts.run()` (needs `SHAKTI_GFX=1`).

---

# `pyplot` module

matplotlib.pyplot-shaped **charting** on top of [`gfx`](#gfx-module) (no separate C backend). Stateful figure/axes dicts mirror pyplot’s implicit current axes. Not a full matplotlib port — no Artist OO, savefig, or GUI backends beyond gfx.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy the pyplot_demo.ie section from examples/example.ie into its own file
SHAKTI_GFX_SKIP=1 ./.build/shakti /tmp/pyplot_demo.ie   # skip gfx window
```

Or copy the `pyplot_demo.ie` section from [`examples/example.ie`](examples/example.ie).

## Example

```ie
import pyplot

pyplot.plot([1, 2, 3, 4], [1, 4, 9, 16], color:"C0", label:"sq")
pyplot.title("demo")
pyplot.xlabel("x")
pyplot.ylabel("y")
pyplot.legend()
pyplot.grid(1)
pyplot.show()
```

Call style is `pyplot.plot(...)` (no `import … as plt` alias).

## Examples

| File | Description |
|------|-------------|
| `examples/example.ie` (`pyplot_demo.ie` section) | Line + scatter + bar with legend/grid |

## API

Module `lib/pyplot.ie` (imports `gfx`).

| Form | Meaning |
|------|---------|
| `pyplot.plot(y)` / `pyplot.plot(x, y)` | Line series; optional `color`, `label` |
| `pyplot.scatter(x, y)` | Points; optional `color`, `label` |
| `pyplot.bar(x, height)` | Bars; optional `color`, `label` |
| `pyplot.hist(x[, bins])` | Histogram (rendered as bars); optional `color`, `label` |
| `pyplot.title` / `xlabel` / `ylabel` | Axis chrome text |
| `pyplot.grid([flag])` | Toggle grid (`1` on, `0` off) |
| `pyplot.legend()` | Show series labels |
| `pyplot.xlim(lo, hi)` / `pyplot.ylim(lo, hi)` | Data limits (auto from series when unset) |
| `pyplot.clf()` / `pyplot.cla()` / `pyplot.figure()` | Clear figure / clear axes / new figure state |
| `pyplot.gcf()` / `pyplot.gca()` | Current figure dict (same object in v1) |
| `pyplot.draw()` | Render once into the open gfx buffer |
| `pyplot.show()` | Open gfx, draw, block until the window closes |
| `pyplot.close()` | Close the gfx window if open |

Colors: short names (`"b"`, `"r"`, …), cycle ids (`"C0"`…`"C9"`), `#RRGGBB` strings, or `0xRRGGBB` ints. Auto color cycles when `color` is omitted. Headless demos: set `SHAKTI_GFX_SKIP=1` to skip `show()`.

---

# `mplsoccer` module

Football **pitch charts** on [`gfx`](#gfx-module). Same shape as the common mplsoccer pitch calls: a statsbomb or uefa frame, scatter, lines, arrows, a rectangular heatmap, and a StatsBomb event table. Not a Matplotlib port. Arcs are polylines. There is no alpha, image blit, rotated text, or framebuffer save. `gfx.text` is ASCII.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
SHAKTI_GFX_SKIP=1 ./.build/shakti examples/mplsoccer_demo.ie
```

## Example

```ie
import mplsoccer

mplsoccer.pitch(pitch_type:"statsbomb", corner_arcs:1)
stats : mplsoccer.bin_statistic([20, 40, 40], [30, 30, 50], bins:[6, 5])
mplsoccer.heatmap(stats)
mplsoccer.scatter([20, 40], [30, 50], color:"white")
mplsoccer.arrows(20, 30, 70, 50, color:"yellow")
mplsoccer.show()
```

`custom` is the same 105×68 frame as `uefa`. Any other pitch type (`opta`, `wyscout`, `tracab`, `metricasports`, `skillcorner`, `impect`, `secondspectrum`) raises.

## Examples

| File | Description |
|------|-------------|
| `examples/mplsoccer_demo.ie` | Coordinate checks, a local event file, pitch, heatmap, arrow |

## API

Module `lib/mplsoccer.ie` (imports `gfx` and `rest`).

| Form | Meaning |
|------|---------|
| `mplsoccer.pitch(...)` | Horizontal pitch. `pitch_type` is `"statsbomb"` (120×80, y down) or `"uefa"` / `"custom"` (105×68, y up). Options: `half`, `pitch_color`, `line_color`, `stripe`, `stripe_color`, `pad_left` / `pad_right` / `pad_bottom` / `pad_top` (data units, default 4), `goal_type` (`"line"` or `"box"`), `corner_arcs`, `positional`, `shade_middle` |
| `mplsoccer.vertical_pitch(...)` | Same options. Length runs up the screen |
| `mplsoccer.standardize(x, y, pitch_from, pitch_to)` | Piecewise map through the pitch markings. `x` and `y` in the result are lists. `pitch_to` defaults to the current pitch |
| `mplsoccer.flip_side(x, y, flip)` | Reflect points to the other half when `flip` is on. One flag, or one per point |
| `mplsoccer.calculate_angle_and_distance(x0, y0, x1, y1)` | Radians counter-clockwise in **data** coordinates, and Euclidean distance. On a statsbomb pitch, y grows down the screen |
| `mplsoccer.scatter` / `plot` / `lines` / `arrows` / `text` | Queue marks in pitch coordinates. `text` is one string; `dy` is a pixel offset |
| `mplsoccer.flow(x0, y0, x1, y1[, bins])` | One arrow per cell: the mean displacement of segments whose start falls in that cell. Default bins are 5 by 4 |
| `mplsoccer.bin_statistic(x, y[, values, statistic, bins, normalize])` | `count` (default), `sum`, or `mean` on a rectangle grid. Points outside the pitch are dropped. Returns `statistic`, `x_edge`, `y_edge`, `cx`, `cy`, `nx`, `ny`. `normalize` divides by the total |
| `mplsoccer.heatmap(stats[, vmin, vmax])` | Paint that grid white to blue |
| `mplsoccer.formations()` / `formation(name)` / `draw_formation(name)` | Nine formations (`4-4-2`, `4-3-3`, `4-2-3-1`, `4-1-4-1`, `4-4-1-1`, `3-5-2`, `3-4-3`, `5-3-2`, `5-4-1`) stored in uefa meters and mapped onto the current pitch |
| `mplsoccer.draw()` / `show()` / `clf()` / `close()` | Paint, open the gfx window, clear overlays, close. `show` no-ops when `SHAKTI_GFX_SKIP=1` |
| `mplsoccer.sblocal(path)` | Flatten a StatsBomb event JSON file to a table |
| `mplsoccer.sbopen(match_id)` | GET that match from the public open-data events URL. Digits only. The dataset is not shipped in this tree |

`sblocal` keeps events that have a location. Columns: `x`, `y`, `end_x`, `end_y`, `minute`, `angle`, `length`, `has_end`, `has_pass`, `type`, `player`, `team`. End location comes from a pass, shot, or carry. An event with no end keeps `has_end` 0 and copies `x`, `y`. Pass angle and length are 0 when the event is not a pass (`has_pass` 0).

## Not in this module

`kdeplot`, `hexbin`, `voronoi`, `convexhull`, `triplot`, sonar, radar, pizza, and bumpy charts, inset images, zone editors, multi-axes grids, and the authenticated StatsBomb API.

Colors: names (`"white"`, `"yellow"`, `"blue"`, `"green"`, `"red"`, `"black"`, …), `#RRGGBB`, or `0xRRGGBB` ints.

---

# `jupyter` module

IPython / Jupyter **Notebook**-shaped interactive cells on top of `eval` + `json_loads` / `json_dump` (optional [`gfx`](#gfx-module) viewer). Not JupyterLab and not a Python kernel — Shakti source in cells, nbformat-ish `.ipynb` round-trip.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy the jupyter_demo.ie section from examples/example.ie into its own file
SHAKTI_GFX_SKIP=1 ./.build/shakti /tmp/jupyter_demo.ie   # skip gfx window
```

Or copy the `jupyter_demo.ie` section from [`examples/example.ie`](examples/example.ie).

## Example

```ie
import jupyter

jupyter.new()
jupyter.markdown("# demo")
jupyter.code("x : 1 + 2")
jupyter.code("x * 10")
jupyter.run_all()
print(jupyter.get_out()[1], jupyter.get_out()[2])
jupyter.save("/tmp/demo.ipynb")
jupyter.show()
```

IPython-shaped one-shot: `jupyter.run("3 * 4")` appends a code cell and executes it.

## Examples

| File | Description |
|------|-------------|
| `examples/example.ie` (`jupyter_demo.ie` section) | Cells, errors, run/run_all, save/load, optional gfx view |

## API

Module `lib/jupyter.ie` (imports `gfx` for `draw` / `show` only).

| Form | Meaning |
|------|---------|
| `jupyter.new()` / `jupyter.clear()` | Reset notebook + `In`/`Out` history |
| `jupyter.code(src)` / `jupyter.markdown(src)` | Append a cell; returns index |
| `jupyter.run_cell(i)` / `jupyter.run_all()` | `eval` code cells; fills outputs + history |
| `jupyter.run(src)` | Append code cell and execute (IPython-shaped) |
| `jupyter.get_in()` / `jupyter.get_out()` | History lists (`[0]` placeholder; first result at `[1]`) |
| `jupyter.execution_count()` | Last execution count |
| `jupyter.cell(i)` / `jupyter.ncells()` / `jupyter.gcn()` / `jupyter.get_notebook()` | Inspect notebook (`gcn` ≡ `get_notebook`) |
| `jupyter.clear_outputs()` / `jupyter.clear_history()` | Clear cell outputs or In/Out (history only; cells kept) |
| `jupyter.dumps()` / `jupyter.loads(s)` | nbformat-ish JSON string |
| `jupyter.save(path)` / `jupyter.load(path)` | `.ipynb` via `json_dump` / `json_load` |
| `jupyter.draw()` / `jupyter.show()` / `jupyter.close()` | Render notebook in gfx (like pyplot) |

Notes:

- Cell `source` may be a string or a list of strings (joined on load). Unknown / non-dict cells in a file are skipped.
- `loads` / `load` require a JSON **object**; non-objects return `None` and leave the current notebook unchanged. Parse errors return an `error` value.
- Code outputs use `execute_result` (`text/plain`) or `error`. A `None` result (empty cell / no value) records history but omits `execute_result`.
- Bindings persist across `eval` calls (root environment), including after `load` + `run_all`.
- Saved notebooks carry `kernelspec` metadata (`name: shakti`, `language: ie`).
- `jupyter.show()` no-ops when `SHAKTI_GFX_SKIP=1` (same headless convention as benches).

---

# `input` module

Unified **terminal and GUI event hub** — keyboard, mouse, wheel, and line input in one poll API. Used by the synth UI and REPL.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy input_demo.ie section from examples/example.ie
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
| `input(1)` | Stream of typed keys: each key down's text (`""` for a key without one, such as an arrow); in a terminal, one str per byte, so a UTF-8 character gives several |
| `input(2)` | Stream of `{code, modifiers, utf8, kind}` dicts: a `"down"` and an `"up"` for each key |
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

Sync and poll-based async message passing between shakti processes (`import ipc`). The wire is a 4-byte big-endian length plus payload (max 1 MiB). `ipc.send` / `ipc.recv` stay envelope-free (string or `list[char]`). Envelope RPC APIs put an 8-byte header inside the payload: magic `0x49`, kind (`async` / `req` / `rep`), `corr_id` (u32 big-endian), two reserved zeros.

## Transport selection

| Target | Default | Override |
|--------|---------|----------|
| `127.0.0.1`, `localhost`, `::1` | Unix domain socket `shakti-<port>.sock` in the socket directory (`SHAKTI_IPC_DIR`, below) | `transport="tcp"` |
| Remote host | RDMA (RoCE v2 via `librdmacm`) when a device exists, else TCP | `transport="tcp"` or `transport="rdma"` |

A transport is `auto`, `tcp`, `uds` (or `unix`) or `rdma`; any other name is an error (`ipc_listen: unknown transport "tpc" (auto, tcp, uds, unix, rdma)`), and so is a transport that is not a `str`. With no transport (or `""`), `ipc.listen`, `ipc.connect`, `ipc_listen` and `ipc_connect` take `SHAKTI_IPC_TRANSPORT`'s, else `auto`, and an unknown name there is the same error (`... "tpc" in SHAKTI_IPC_TRANSPORT ...`).

Environment:

- `SHAKTI_IPC_DIR` — the socket directory, for Unix sockets and their `.lock` files (default `$XDG_RUNTIME_DIR`, else `/tmp/shakti-<uid>`, made 0700). It must be a directory of the user's own that others can't write to (so not `/tmp` itself); otherwise `ipc.listen` and `ipc.connect` over a Unix socket are an error (`ipc: uds path too long or socket dir unsafe`). Shared memory doesn't use it (see Shared memory).
- `SHAKTI_IPC_TRANSPORT` — default transport: `auto`, `tcp`, `uds` (or `unix`), `rdma`; any other name is an error when it is used (above)
- `SHAKTI_IPC_ALLOW_PUBLIC=1` — allow `ipc.listen` on a non-loopback address (default: loopback/UDS only)

A second `ipc.listen` on a port whose Unix listener is alive is an error (`ipc: uds bind: Address already in use`), as over TCP. A Unix listener keeps `shakti-<port>.lock` beside its socket; `ipc.close` removes both while that `.lock` is still its own; a process that ends without `ipc.close` leaves both, and the next listen reuses the lock file and replaces the socket. A non-socket at the socket path is an error (`ipc: <path> exists and is not a socket`). A socket whose listener holds no `.lock` (another program's, an older shakti's, or one whose `.lock` a cleaner of the socket directory removed) is connected to once: refused means it is replaced; otherwise the port is in use, and that listener sees one closed connection; a listener that sends on it may be ended by SIGPIPE. On macOS a full queue is refused too, and each of these connections stays queued until the listener accepts, so one that is not accepting is taken over once they fill its queue (16 for a shakti listener).

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

A send on a connection whose other end has closed gives an error value (`ipc: send: Broken pipe`, or the system's reason) and the process goes on; so do `send_async`, `send_sync`, `reply` and `send_n`. Over TCP the first send after the close can still succeed: the close is seen when the other end answers it.

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

`ipc.recv_nowait(h)` returns `""` when no full message is available. `ipc.recv_nowait_bin(h)` / `ipc.recv_bin_nowait(h)` return an empty `list[char]`.

## Envelope RPC

```ie
import ipc

srv : ipc.listen(9001)
conn : ipc.accept(srv)
req : ipc.recv_req(conn)
ipc.reply(conn, req[0], "pong")

c : ipc.connect("127.0.0.1", 9001)
rep : ipc.send_sync(c, "ping")
```

| Function | Role |
|----------|------|
| `ipc.send_async(h, msg)` | Fire-and-forget envelope |
| `ipc.send_sync(h, msg[, timeout_ms])` | Request; wait for matching `rep` (default 5000 ms) |
| `ipc.reply(h, corr_id, msg)` | Reply to a request |
| `ipc.recv_msg(h)` | `dict(kind, corr_id, data)` |
| `ipc.recv_req(h)` | `list[corr_id, data]` (skips async) |
| `ipc.send_n(h, msg, n)` | Repeat raw send on a stream connection |

`recv_*_nowait` variants return `None` (or empty payload types as above) when nothing is ready. `send_sync` is not supported on SHM broadcast rings.

## Shared memory (local bulk)

```ie
tok : ipc.shm_open("buf", 1048576)
ipc.shm_close(tok)

a : ipc.shm_create("chan", 65536)
b : ipc.shm_attach("chan")
pub : ipc.shm_broadcast_create("news", 65536)
r : ipc.shm_broadcast_attach("news")
blob : ipc.shm_view(a, 256, 64)
```

`shm_open` is a POSIX `mmap` token. `shm_create` / `shm_attach` are SPSC duplex rings (256-byte header, futex wait on Linux). `shm_broadcast_*` is SPMC and lossy for slow readers. `shm_view` copies ring bytes (`offset` must be ≥ 256).

A name is in use while the process that made it with `shm_open`, `shm_create` or `shm_broadcast_create` has it open (a ring and a broadcast ring share names). That process holds a lock on `<name>.lock` in `/tmp/shakti-<uid>`, for example `shakti_ipc_<uid>_chan.lock` on Linux and `shakti_ipc_chan.lock` on macOS, from the create until `ipc.close` (`shm_close` for a token) or the end of the process, so each name made holds one file descriptor until it is closed. The lock files are always there, whatever `SHAKTI_IPC_DIR` and `XDG_RUNTIME_DIR` say, since a name is the user's across the machine and every process of the user has to find its lock. The create makes `/tmp/shakti-<uid>` (0700) if it is missing; if it can't, or it is not a real directory of the user's own, open to no one else, the create is an error saying why (`ipc: shm lock directory /tmp/shakti-<uid>: open to other users`). Creating a name in use is an error (`ipc: shm name already in use (/shakti_ipc_<uid>_chan)`), and the ring is left alone. The close removes the name and the `.lock` while that `.lock` is still its own. A name left by a process that ended without closing it, or whose maker holds no lock (an older shakti, or a `.lock` removed by a cleaner of the directory), is replaced.

A process that exits (at the end of its script, by `\q` in the REPL, or by a failed `assert` or an error that ends it) closes the rings and tokens it still has open, as `ipc.close` and `ipc.shm_close` would: the names it made go with it, with their `.lock`, and a ring it attached to gets its side (or broadcast reader slot) back. A process ended by a signal, Ctrl-C (SIGINT) and SIGTERM among them (shakti doesn't catch signals), or by a crash leaves them behind; the next create of such a name replaces it.

## Multicast

```ie
h : ipc.join("239.255.42.1", 19000)
ipc.publish(h, "hello")
```

IPv4 join is required. IPv6 join may error if the host has no IPv6 multicast interface. `send_sync` is not supported on multicast handles (the reply cookie and correlation id would be visible on the group).

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
| `ipc_send(h, str\|list[char])` | Send message (no envelope) |
| `ipc_recv(h)` | Blocking receive (`str`) |
| `ipc_recv_nowait(h)` | Non-blocking receive |
| `ipc_recv_bin(h)` | Blocking receive (`list[char]`) |
| `ipc_recv_nowait_bin(h)` | Non-blocking binary receive (`ipc.recv_nowait_bin` / `ipc.recv_bin_nowait`) |
| `ipc_send_async(h, msg)` | Envelope async send |
| `ipc_send_sync(h, msg[, timeout_ms])` | Envelope request/reply |
| `ipc_reply(h, corr_id, msg)` | Envelope reply |
| `ipc_recv_msg(h)` / `_nowait` | Envelope message dict |
| `ipc_recv_req(h)` / `_nowait` | Next request `[corr_id, data]` |
| `ipc_send_n(h, msg, n)` | Repeat raw send |
| `ipc_set_nonblock(h, on)` | Toggle non-blocking mode |
| `ipc_poll(handles, timeout_ms)` | Ready handle list |
| `ipc_close(h)` | Close handle |
| `ipc_shm_open(name, size)` | Shared memory token |
| `ipc_shm_close(token)` | Unmap and unlink |
| `ipc_shm_create(name, size)` | Duplex ring (side A) |
| `ipc_shm_attach(name)` | Duplex ring (side B) |
| `ipc_shm_broadcast_create(name, size[, max_readers])` | SPMC publisher |
| `ipc_shm_broadcast_attach(name)` | SPMC reader |
| `ipc_shm_view(h, offset, len)` | Copy ring bytes |
| `ipc_join(group, port[, iface[, source]])` | UDP multicast membership |
| `ipc_publish(h, msg)` | Multicast send |
| `ipc_rdma_available()` | `1` if RDMA device present |

## Examples

See [examples index](#examples-index). IPC-specific:

| File | Description |
|------|-------------|
| `ipc_echo.ie` | UDS echo server (raw send/recv) |
| `ipc_echo_client.ie` | UDS client |
| `ipc_sync.ie` | UDS req/rep server |
| `ipc_sync_client.ie` | UDS `send_sync` client |
| `ipc_rdma.ie` | RDMA server (raw send/recv) |
| `ipc_rdma_client.ie` | RDMA client |

---

# REST module

HTTP **client** (via `curl`) and a minimal in-process **HTTP/1.1 server** (TCP) after `import rest`.

## Requirements

- **Client:** `curl`, discovered at `SHAKTI_CURL` (absolute path) or `/usr/bin`, `/usr/local/bin`, `/opt/homebrew/bin`, `/bin` (not via `PATH`), run with `-q` and a minimal environment.
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
| `rest.post_iefs(url, obj[, codec, level])` | POST `application/iefs` (`list[char]` IEFS frame) |
| `rest.put_iefs(url, obj[, codec, level])` | PUT `application/iefs` |
| `rest.post_hld(url, obj[, codec, level])` | POST `application/x-hld` |
| `rest.put_hld(url, obj[, codec, level])` | PUT `application/x-hld` |
| `rest.request(method, url[, body, content_type, headers])` | Generic request; `headers` is a dict; body may be `str` or `list[char]` |
| `rest.status(resp)` | HTTP status code |
| `rest.ok(resp)` | `True` when status is 2xx |
| `rest.json(resp)` | Parsed body (JSON object/list or string) |
| `rest.iefs(resp)` | Decoded IEFS body |
| `rest.hld(resp)` | Decoded HLD body |
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
| `rest.listen(port[, host])` | Listen on TCP (default host `127.0.0.1`; non-loopback needs `SHAKTI_REST_ALLOW_PUBLIC=1`) |
| `rest.listen_tls(port, cert, key[, host])` | TLS listen (PEM cert + key); handshake runs in `rest.accept` |
| `rest.accept(listen_h)` | Accept connection handle |
| `rest.read(conn)` | Read request → `{method, path, body, headers}` |
| `rest.write(conn, status[, body, content_type])` | Send HTTP/1.1 response |
| `rest.respond_json(conn, status, obj)` | JSON response helper |
| `rest.respond_iefs(conn, status, obj[, codec, level])` | IEFS binary response |
| `rest.respond_hld(conn, status, obj[, codec, level])` | HLD binary response |
| `rest.set_nonblock(h, enabled)` | Non-blocking fd |
| `rest.close(h)` | Close listen or connection handle |

A `rest.write` (or `respond_json`, `respond_iefs`, `respond_hld`) to a client that has hung up gives an error value such as `rest_write: write failed: Broken pipe` (the reason can also be `Connection reset by peer`, or, over `rest.listen_tls` once a write has already failed, `Input/output error`) and the process goes on, with `rest.listen` and `rest.listen_tls` alike; `rest.close` of such a connection closes it as usual. The first write after the hang-up can still succeed: the client's system answers it with a reset, and later writes fail.

`rest.listen_tls` is for long-lived sockets (HTTPS mini-server, `wss` upgrade). The HTTP **client** still uses `curl`.

```ie
srv : rest.listen_tls(9443, "server.crt", "server.key")
conn : rest.accept(srv)
req : rest.read(conn)
```

## Example

`rest_demo.ie` — local server spawn + GET/POST client calls.

## Limitations

- Client shells out to `curl` per request (latency dominated by process spawn).
- Client allows loopback (`127/8`, `::1`) so `post_iefs` / `post_hld` can reach the in-process mini-server; other private addresses stay blocked.
- Server is not production-grade: no HTTP/2, no chunked encoding, no keep-alive.
- TLS listen requires OpenSSL at build time (`SHAKTI_TLS=1`, default on native).
- Not available in WASM builds.

## See also

- [WebSocket module](#websocket-module) — RFC6455 upgrade from `rest.read`
- [JSON-RPC daemon](#json-rpc-daemon) — `server_serve`
- [IPC module](#ipc-module) — length-prefixed TCP/UDS messaging (not HTTP)
- [third-party](#third-party-dependencies-and-optional-assets) — `curl` / OpenSSL

---

# WebSocket module

RFC6455 client and server after `import ws`. Build requires OpenSSL (`-lssl -lcrypto`; `SHAKTI_WS=1` implies `SHAKTI_TLS=1`). Caps: 128 handles, 1 MiB payload, FIN required (no fragments). Binary frames are `list[char]`.

## Client

```ie
import ws

c : ws.connect("ws://127.0.0.1:9000/echo")
ws.send(c, "hello")
msg : ws.recv(c)          # dict(type: "text"|"bin"|"close", data: ...)
print(msg["type"], msg["data"])
ws.close(c)

c : ws.connect("wss://example.com/chat")
c : ws.connect("wss://127.0.0.1:9443/echo", insecure:1)  # skip cert verify
```

| Function | Description |
|----------|-------------|
| `ws.connect(url[, insecure])` | `ws://` or `wss://` |
| `ws.send(h, text)` | Text frame |
| `ws.send_bin(h, data)` | Binary; `str` or `list[char]` |
| `ws.recv(h)` | Blocks; auto-answers ping |
| `ws.poll(handles, timeout_ms)` | Readable / closed handles |
| `ws.close(h)` | Sends close (1000) then frees |
| `ws.set_nonblock(h, enabled)` | Non-blocking fd |

`ws.connect` asks for no extension or subprotocol. The server's answer must be `HTTP/1.1 101` with the right `Sec-WebSocket-Accept`, an `Upgrade` header that includes `websocket` and a `Connection` header that includes `Upgrade` (header names and these two values in any case), and must name no extension or subprotocol (no `Sec-WebSocket-Extensions` or `Sec-WebSocket-Protocol` header); otherwise the result is `ws_connect: websocket handshake failed`.

## Server

Upgrade a connection from `rest.listen` / `rest.listen_tls` after `rest.read`:

```ie
import rest
import ws

srv : rest.listen(9000)
conn : rest.accept(srv)
req : rest.read(conn)
w : ws.accept(conn)       # writes 101; consumes conn
msg : ws.recv(w)
ws.send(w, msg["data"])
ws.close(w)
rest.close(srv)
```

Not available in WASM builds.

---

# JSON-RPC daemon

Blocking HTTP daemon: `server_serve(port)` (default 8080). Loopback bind unless `SHAKTI_SERVE_ALLOW_PUBLIC=1`. Non-loopback bind requires `SHAKTI_SERVE_TOKEN`. Optional HTTPS: `SHAKTI_TLS_CERT` and `SHAKTI_TLS_KEY` (PEM).

Routes:

- `POST /rpc` and `POST /jsonrpc` — JSON-RPC 2.0 (`Content-Type: application/iefs` and `application/x-hld` return **415**)
  - `handle_rpc(method, params, id)` if defined in the calling environment
  - else built-in `ping` → `"pong"`
- other methods/paths — `handle_request(method, path, body, content_type)` if defined (must return a dict with `status` / `content_type` / `body`)

CORS: localhost Origin only; mutating cross-origin requests get 403. Optional bearer: `Authorization: Bearer …` matching `SHAKTI_SERVE_TOKEN`.

```ie
def handle_rpc(method, params, id):
    if method = "echo":
        return params
    if method = "ping":
        return "pong"
    return None

server_serve(8080)
```

```bash
curl -s -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"ping","id":1}' \
  http://127.0.0.1:8080/rpc
```

A handler that raises, or stops on an error, is answered as one that returns an error value: `handle_rpc` with JSON-RPC error `-32603`, `handle_request` with **500**, both `internal error` (`SHAKTI_SERVE_DEBUG=1` logs the message to stderr). The daemon goes on to the next request.

Not available in WASM builds.

---

# `synth` module

Desktop synth UI (Linux: X11 + ALSA; macOS: Cocoa + Core Audio).

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy synth_demo.ie section from examples/example.ie (interactive; busy-waits while alive)
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

Leave an interactive synth session with the window close button, or from a bare REPL with `\q` (see [Process / REPL](#process--repl)).


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

USB MIDI controller (iRig Keys 2): `examples/synth_midi.ie`

```bash
export SHAKTI_LIB=$PWD/lib
./.build/shakti examples/synth_midi.ie
```

Maps notes, pitch bend, Mod (CC1 → vibrato), and knobs CC12–19 (level, reverb,
cutoff, resonance, ADSR). Select **iRig Keys 2** as the macOS sound output for
the analog Volume knob.

## API

Module `lib/synth.ie`: `open`, `close`, `alive`, `tick`, `set_steps`, `steps`, `set_metro`, `metro_on`, `set_metro_sound`, `metro_sound`, `set_mute`, `note_on`, `note_off`, `set_tuning`, `tuning`, `set_skin`, `skin`.

`synth.set_skin(name)` selects a UI palette (`"default"` or `"irish"`). `synth.skin()` returns the active name. `synth.set_preset(0..3)` selects GRAND PIANO / CLASSIC SYNTH / ELECTRIC PIANO / WARM PAD.

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

# `dsp` module

Just-intonation / k-scale ratio primitives. Built by default (`SHAKTI_DSP=1`).

```bash
export SHAKTI_LIB=$PWD/lib
# copy dsp_demo.ie section from examples/example.ie
```

## Perfect 7 ratio set

| Degree | Ratio | × root (A=440) |
|--------|-------|----------------|
| 1 | 1/1 | 440 Hz |
| 2 | 9/8 | 495 Hz |
| 3 | 5/4 | 550 Hz |
| 4 | 3/2 | 660 Hz |
| 5 | 15/8 | 825 Hz |
| 6 | 3/1 | 1320 Hz |
| 7 | 3/5 | 264 Hz |

## Example

`dsp_demo.ie`:

```ie
import dsp

root : 440.0
print(dsp.degree_freq(root, 4))   # 660 — perfect fifth
for row in dsp.perfect7(root):
    print(row["degree"], row["num"], row["den"], row["hz"])
```

## API

Module `lib/dsp.ie`:

| Function | Description |
|----------|-------------|
| `dsp.ratio_freq(root_hz, num, den)` | Frequency from ratio |
| `dsp.ratio_cents(num, den)` | Cents from unison (1200×log₂) |
| `dsp.ratio_reduce(num, den)` | GCD-reduced `{num, den}` dict |
| `dsp.perfect7([root_hz])` | List of 7 dicts (`degree`, `num`, `den`, `cents`, optional `hz`) |
| `dsp.degree_freq(root_hz, degree)` | Degree 1–7 from the perfect table |
| `dsp.et_cents(semitone)` | Equal-temperament cents for semitone count |
| `dsp.et_delta(num, den)` | Cents deviation from nearest ET semitone |

Disable at build: `SHAKTI_DSP=0 make prod`.

---

# `stem` module

Streaming 4-stem separator (drums / bass / vocals / other) via STFT + HPSS + band soft-masks. Built by default (`SHAKTI_STEM=1`). Algorithmic look-ahead is about **64–100 ms** (e.g. ~63.9 ms at 44.1 kHz with `n_fft=1024`, `hop=256`, HPSS window 17).

Also includes an **offline CPU spectrogram MLP** path (`SHAKST01` checkpoint format): magnitude frames → two ReLU layers → sigmoid stem masks → ISTFT. Generate a synthetic checkpoint with `stem.write_ml_synth` for tests; no GPU backend.

WAV I/O is the file edge only (engine stays f32). Read: PCM16 / PCM24 / PCM32 integer / IEEE float32. Write: `pcm16` (default) / `pcm24` / `f32`. Live STFT/HPSS is scalar C (NEON hypot on arm64).

```bash
export SHAKTI_LIB=$PWD/lib
```

## API

| Call | Role |
|------|------|
| `stem.open(sr, block)` | Init streaming engine |
| `stem.process(samples)` | Feed fvec/list → dict of stem fvecs |
| `stem.set_gains(d, b, v, o)` | Mute/solo gains (applied on emit) |
| `stem.gains()` | Current gains dict |
| `stem.mix(stems)` | Sum stem dict → fvec |
| `stem.latency_ms()` | Algorithmic latency |
| `stem.info()` | `n_fft` / `hop` / `hpss_len` / latency |
| `stem.separate_file(path, outdir, wavfmt)` | Offline file split (classical engine); `wavfmt` is `pcm16` / `pcm24` / `f32` |
| `stem.write_ml_synth(path, nhidden)` | Write synthetic `SHAKST01` checkpoint |
| `stem.load_ml(path)` / `stem.unload_ml()` | Load/unload MLP weights |
| `stem.ml_info()` | Checkpoint dims / flags (CPU-only) |
| `stem.ml_spike(batch, iters)` | Time one MVM layer |
| `stem.separate_file_ml(path, outdir, wavfmt)` | Offline file split via MLP masks (**44100 Hz** WAV required) |
| `stem.si_sdr(est, ref)` | Scale-invariant SDR (dB) |
| `stem.write_wav(path, samples, sr, fmt)` | Write mono WAV (`pcm16` / `pcm24` / `f32`) |
| `stem.read_wav(path)` | Read WAV → `{"samples","sr","n"}` |
| `stem.close()` / `stem.alive()` | Lifecycle |

## Example

```ie
import stem

stem.open(44100, 256)
print(stem.latency_ms())
out : stem.process(block)
stem.set_gains(1, 1, 0, 1)   # mute vocals
mix : stem.mix(out)
stem.close()

stem.write_ml_synth("/tmp/demo.stemw", 32)
stem.load_ml("/tmp/demo.stemw")
ml : stem.separate_file_ml("/tmp/tone.wav")
stem.unload_ml()
```

Disable at build: `SHAKTI_STEM=0 make prod`.

---

# `sonicpi` module

OSC bridge to [Sonic Pi](https://sonic-pi.net/) for live-coded music from Shakti scripts. Built by default (`SHAKTI_SONICPI=1`).

```bash
export SHAKTI_LIB=$PWD/lib
# copy sonicpi_demo.ie section from examples/example.ie into its own file, then run it
```

## Prerequisites

1. Install **Sonic Pi** separately from [sonic-pi.net](https://sonic-pi.net/) (not bundled with Shakti).
2. Start Sonic Pi and enable its OSC listener.
3. By default Sonic Pi listens for OSC on **`127.0.0.1:4560`**. For remote hosts, enable **Preferences → IO → Networked OSC → Allow OSC from other computers**.

## Example

`sonicpi_demo.ie`:

```ie
import sonicpi

sonicpi.configure("127.0.0.1", 4560)
sonicpi.bpm(120)
sonicpi.play(60, 0.8, 0.25)
sonicpi.synth("prophet", 70, 0.9, 1.0)
```

## API

Module `lib/sonicpi.ie`:

| Function | Description |
|----------|-------------|
| `configure(host, port)` | Target Sonic Pi OSC listener (default `127.0.0.1`, `4560`) |
| `play(note, amp, sustain)` | Play note via `/shakti/play` |
| `synth(name, note, amp, sustain)` | Named synth via `/shakti/synth` |
| `bpm(tempo)` | Set tempo via `/shakti/bpm` |
| `stop()` | Stop via `/shakti/stop` |

For arbitrary OSC paths, call the builtin `sonicpi_send(path, args...)` directly.

Environment overrides: `SONICPI_HOST`, `SONICPI_PORT`.

Disable at build: `SHAKTI_SONICPI=0 make prod`.

Shakti communicates with Sonic Pi over the public **OSC** protocol. Shakti does **not** ship Sonic Pi binaries, samples, or synthdefs. Sonic Pi is external software by Samuel Aaron and contributors.

---

# `pdf` module

From-scratch PDF 1.4 reader/writer (`src/pdf.c`) — no MuPDF/Poppler/PDFium. Built by default (`SHAKTI_PDF=1`).

```bash
export SHAKTI_LIB=$PWD/lib
# copy pdf_demo.ie section from examples/example.ie into its own file, then run it
```

## Example

```ie
import pdf

w : pdf.create()
pdf.add_page(w)
pdf.text_at(w, 72, 720, "Hello Shakti", size:12)
pdf.save(w, "/tmp/hello.pdf")
pdf.close(w)

d : pdf.open("/tmp/hello.pdf")
print(pdf.page_count(d))
print(pdf.text(d))
pdf.close(d)
```

## API

| Function | Description |
|----------|-------------|
| `pdf.create()` | New write document; returns handle |
| `pdf.add_page(h)` | Append letter page (612×792 pt) |
| `pdf.text_at(h, x, y, text, size:12)` | Draw text (origin bottom-left) |
| `pdf.save(h, path)` | Emit PDF 1.4 file |
| `pdf.open(path)` | Open for read |
| `pdf.page_count(h)` | Number of pages |
| `pdf.info(h)` | Info dict (may be empty) |
| `pdf.text(h, page:0)` | Extract text; `page` 1-based, `0` = all |
| `pdf.close(h)` | Release handle |

Disable at build: `SHAKTI_PDF=0 make prod`.

---

# `midi` module

MIDI I/O via ALSA sequencer (Linux) or CoreMIDI (macOS). Built by default (`SHAKTI_MIDI=1`).

```bash
export SHAKTI_LIB=$PWD/lib
# copy midi_demo.ie section from examples/example.ie into its own file, then run it
# optional: MIDI_PORT='Scarlett' ./.build/shakti /tmp/midi_demo.ie
```

## vs `synth` / `sonicpi`

| Module | Role |
|--------|------|
| `midi` | Wire protocol to hardware / DAW / virtual ports |
| `synth` | Built-in softsynth + UI |
| `sonicpi` | OSC bridge to Sonic Pi |

## API

| Function | Description |
|----------|-------------|
| `open()` / `close()` / `alive()` | Lifecycle |
| `backend()` | `"alsa"`, `"coremidi"`, or `"none"` |
| `list()` | Ports as dicts |
| `connect(id_or_name)` / `disconnect()` | Subscribe output (and input when matched) |
| `note_on` / `note_off` / `cc` / `program` / `raw` | Send |
| `poll()` | Next inbound event or `nil` |

Disable at build: `SHAKTI_MIDI=0 make prod`.

---

# `iefs` module

Portable durable save/load for Shakti values (`.iefs`). Built by default (`SHAKTI_IEFS=1`). Default **write version is 2** (layout CRC over `hdr[0:16]+hdr[20:24]+payload`). Version 1 files still load (payload-only CRC). Version 3 is TOC + 2 MiB extents.

```ie
import iefs
iefs.save(x, "data.iefs")
iefs.save(x, "data.iefs", codec:"zstd")
iefs.save(x, "data.iefs", format:3)
iefs.save(x, "data.iefs", sync:"none")   # "full" (default), "data", or "none"
iefs.save(x, "data.iefs", format:3, codecs:{"t":"datetime+zstd", "px":"gorilla_f64"})
x2 : iefs.load("data.iefs")
x3 : iefs.map("data.iefs", pages:"thp")  # or "2m" / "1g"
blob : iefs.dumps(x, codec:"snappy")    # list[char]
y : iefs.loads(blob)
print(iefs.direct_available(), iefs.uring_available(), iefs.libaio_available())
```

- `iefs.load` / global `load("….iefs")` — full read + CRC + malloc copy.
- `iefs.map` — `mmap` the file, skip CRC, alias contiguous vector/matrix payloads. Compressed files require `load` / `loads`. When the file of a format-3 dict whose values are all vectors or matrices has a key name missing, not NUL-terminated, or repeated, its header, table of contents and the padding before its first value are checked against its CRC (each value counted by the CRC the table stores for it, so no value is read), so a damaged name is `iefs: checksum mismatch`, as from `load`; damage to a value, or to a name that leaves every name present and distinct, is not checked. Looking for a repeat sorts a dict's key names on each map (about 60 µs for 1000 keys), and a dict that repeats a key is checked on each map (about 0.11 ms, 0.36 ms for 1000 keys). `iefs.map(path, columns: ["b", "a"])` gives only those columns of a table, in the order named, and a name given twice gives the column twice; a format-3 dict whose values are all vectors or matrices takes `columns:` too and keeps each key once. A name not in the file is an error. A `select` from a mapped table held in a variable (`m : iefs.map("….iefs")`, then `select from m`) copies the columns it selects into memory, as a select from any table variable does (see [`sql` module](#sql-module)); `select … from "….iefs"` reads them from the mapping when it keeps every row.
- `iefs.dumps` / `iefs.loads` — in-memory frames as `list[char]`.
- Host codecs: `zstd`, `snappy`, plus residual `delta_i64` / `fire_i64` / `gorilla_f64` / `date` / `time` / `datetime` (v3 extents; compounds like `fire_i64+zstd`). Also interned as `compress` / `decompress`.
- SQL `select … from "….iefs"` opens via **map**. `update … from` and `delete from` a `.iefs` path write their result back to the file; a variable holding the path keeps the path.
- Whole-file zstd/snappy on v1/v2; v3 per-extent codec + optional outer LZ.
- `codecs:` (on `save` and `dumps`) is a dict from column name to codec, and a dict (even `{}`) makes format 3 the default. `""` (the default), `[]` or an empty vector (`list[int]`, `list[float]`, `list[bool]` or `list[char]`) means no codecs. An error value given as `codecs:` is returned as it is. Anything else (`codecs:"zstd"`, a table, a matrix) is an error value, and `save` writes nothing. `codec:` is the codec for the whole file (format 2) or for each column (format 3); with `codecs:`, for every column it doesn't name.
- Each key of a `codecs:` dict (on `save`, `dumps` and [`hdb.write`](#hdb-module)) is the name of a column of the value: a table's column, or a dict's key. A key that names none, such as `"pz"` in `codecs:{"pz": "zstd"}` for a table with a column `px`, is an error value (`iefs: codecs names no column 'pz'`, or `hdb_write: …` from `hdb.write`) and nothing is written. A value with no columns, such as a vector, takes only `{}`.

Type layout 1 (`char`=4, `str`=5) is stored at header offset 20. Env: `SHAKTI_IEFS_DIRECT=0|1`, `SHAKTI_IEFS_DIRECT_MIN=<bytes>`, `SHAKTI_IEFS_VERSION=3`, `SHAKTI_IEFS_LIBAIO=0|1`.
On Darwin, `direct_available()` is 0; large AUTO reads/writes still set `F_NOCACHE` above the same size threshold.

Disable at build: `SHAKTI_IEFS=0 make prod`.

---

# `hdb` module

Partitioned historical store over IEFS v3 (`SHAKTI_HDB=1`, requires IEFS). Layout: `<root>/meta.iefs` + `<root>/<table>/<part>/data.iefs`.

```ie
import hdb
h : hdb.open("/data/quotes")
hdb.create(h, "trade", schema:dict(sym:"", time:0, px:0.0), part:"date")
hdb.write(h, "trade", "2026.07.31", t)
ps : hdb.parts(h, "trade", lo:"2026.07.01", hi:"2026.07.31")
p  : hdb.map(h, "trade", "2026.07.31")
t  : hdb.load(h, "trade", lo:"2026.07.01", hi:"2026.07.31")
c  : hdb.scan(h, "trade", lo:"2026.07.01", hi:"2026.07.31")
while hdb.next(c):
    part : hdb.current(c)
```

Range prune uses inclusive `lo`/`hi` (`strcmp`); `eq` selects one partition. Vertical concat on `list[int]` / `list[float]` / `list[char]` / string lists / matrices.

`codecs:` (on `write`) is a dict from column name to [IEFS](#iefs-module) codec, as in `codecs:{"px": "zstd"}`; each key names a column of the table (a key that names none is `hdb_write: codecs names no column '…'`); `""` (the default), `[]` or an empty vector (`list[int]`, `list[float]`, `list[bool]` or `list[char]`) means no codecs; an error value given as `codecs:` is returned as it is; anything else (`codecs:"zstd"`, a table, a matrix) is an error value. Each error value writes nothing.

---

# `hld` module

HLD1 HTTP wrapper around an IEFS frame (`SHAKTI_HLD=1`, requires IEFS). Wire (little-endian): `HLD1` + `codec:u8` + `pad:u8` + `raw_len:u32` + `wire_len:u32` + payload. Codec 0/1/2 = none/zstd/snappy.

```ie
import hld
blob : hld.encode(x)                          # list[char]
blob : hld.encode(x, codec:"zstd", level:3)
y : hld.decode(blob)
```

HTTP Content-Type: `application/x-hld` (see `import rest` helpers).

---

# `talk` module (macOS)

Speech-to-text from the microphone. Built by default on macOS (`SHAKTI_TALK=1`).

Grant **Microphone** and **Speech Recognition** to your terminal in System Settings.

Build from the repo root (`make prod`; see [README](README.md)), then:

```bash
export SHAKTI_LIB=$PWD/lib
# copy talk_demo.ie section from examples/example.ie
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

# Security

Shakti trusts the `.ie` programs it runs: a script can read and write files, use the network, and (unless disabled) run processes. Treat an untrusted script like an untrusted shell script, and use OS isolation (containers, bwrap, landlock) for hostile code.

## Safe mode

| Setting | Effect |
|---------|--------|
| `SHAKTI_SAFE=1` | No process execution (`sh`, `subprocess`, `load(dir, exec:True)`); no network (`rest`, `ws`, `server_serve`, TCP/RDMA/multicast `ipc`, `sonicpi`); file access confined to `SHAKTI_FS_ROOT`. Unix-socket and shared-memory IPC still work. |
| `SHAKTI_FS_ROOT=<dir>` | Root for safe-mode file access (default: working directory at first use). Paths are resolved with `realpath`, so symlinks pointing outside are rejected. `path_exists` / `path_isdir` / `path_isfile` return `False` outside the root. |
| `SHAKTI_ALLOW_NET=1` | Re-enable network in safe mode. |
| `SHAKTI_ALLOW_EXEC=0` | Disable process execution only (files and network unaffected). |

Confined builtins: `read`, `write`, `readlines`, `listdir`, `walk`, `stat`, `mkdir`, `json_load`, `json_dump`, `load`, `save`, `iefs` save/load/map, `hdb.open`, `pdf_open`, `pdf_save`, `save_context`, `load_context`, `stem_*` file functions, `synth_load_sample`. The WASM host runs in safe mode; it must set `SHAKTI_ALLOW_NET=1` if a page needs `rest` / `ws`.

## Module search path

`import NAME` loads only `NAME.ie` (or `a/b.ie` for `import a.b`), searching in order:

1. the running script's directory (absolute, symlinks resolved);
2. the stdlib found next to the executable — first of `<exe>/lib`, `<exe>/../lib`, `<exe>/../../lib`, `<exe>/../lib/shakti`, `<exe>/../share/shakti/lib` that contains `json.ie`;
3. `$SHAKTI_LIB`;
4. the current directory — only when there is no script (REPL / `-c`), after the stdlib.

## Resource limits

| Variable | Default | Meaning |
|----------|---------|---------|
| `SHAKTI_MAX_DECODE` | `1g` | Max decompressed size for in-memory decodes: `decompress`, `iefs.loads` (without `max_plain`), `hld.decode`. Accepts `k`/`m`/`g`. Clamped to the 16 GiB codec hard cap. `iefs.load` / `iefs.map` of local files keep the 64 GiB cap. |
| `SHAKTI_REST_MAX_DECODE` | `268435456` | Max decoded size (bytes) of IEFS/HLD REST bodies. |
| `SHAKTI_CALL_MAX_DEPTH` | `3000` | Interpreter call depth. A call made when less than 128 KB plus a sixteenth of the C stack (at most 1 MB) is left is also refused (macOS and Linux; not on stacks under 256 KB or, on Linux, with no stack limit), so a deeper limit needs a larger stack (`ulimit -s`). |

Also: parser nesting is capped at 40 (deeper input is a parse error, "nesting too deep", and on macOS and Linux so is source nested, in brackets or blocks, deeper than the C stack that is left allows); expressions deeper than 4096 levels are rejected; f-string width and precision are capped at 100; `load_context` validates every length against the file size before allocating.

## REST and WebSocket

| Variable | Default | Meaning |
|----------|---------|---------|
| `SHAKTI_REST_SERVER_TOKEN` | unset | Bearer token required by `rest.read`; required for non-loopback `rest.listen`. Compared in constant time; failures get `401`. |
| `SHAKTI_REST_CLIENT_TOKEN` | unset | Bearer token the client sends, only to `https://` hosts in `SHAKTI_REST_TOKEN_HOSTS`. |
| `SHAKTI_REST_TOKEN` | — | Deprecated fallback for both of the above (one-time warning). |
| `SHAKTI_REST_ALLOWED_HOSTS` | unset | Extra `Host` names accepted by loopback listeners (e.g. behind a reverse proxy); `*` disables the check. |
| `SHAKTI_REST_ALLOWED_ORIGINS` | unset | Exact Origins accepted by loopback listeners for state-changing requests and by `ws.accept`; `*` allows any. |
| `SHAKTI_REST_READ_TIMEOUT_MS` | `10000` | Total time for `rest.read` (request line, headers and body). |
| `SHAKTI_REST_TLS_TIMEOUT_MS` | `5000` | TLS handshake budget in `rest.accept`. |
| `SHAKTI_NET_ALLOW_PRIVATE` | unset | `1` lets the REST client and `ws.connect` reach private / link-local / CGNAT addresses (loopback is always allowed). |
| `SHAKTI_CURL` | unset | Absolute path to `curl`. |

- Loopback `rest.listen` servers reject non-local `Host` headers (400, blocks DNS rebinding) and non-local `Origin` on non-GET/HEAD/OPTIONS requests (403, blocks CSRF). Requests without `Origin` (curl, scripts) are unaffected.
- `ws.accept(conn, req, origins:[...])` rejects non-local Origins on loopback listeners unless allowed (`origins:["*"]` allows any). This blocks cross-site WebSocket hijacking.
- IEFS/HLD bodies are decoded only when `Content-Type` is `application/iefs`, `application/x-iefs`, `application/hld` or `application/x-hld`. REST bodies are limited to 1 MB, which excludes IEFS format 3 (padded to 4 MB); use format 2.
- The client runs curl with `-q` (ignores `~/.curlrc`) and a minimal environment (`LANG`/`LC_*`, `TZ`, `SSL_CERT_FILE`, `SSL_CERT_DIR`, `CURL_CA_BUNDLE`, `LD_LIBRARY_PATH`). Proxies are not supported.
- `ws.connect` applies the client address policy, requires a real `HTTP/1.1 101` response with a matching `Sec-WebSocket-Accept`, and verifies certificates (IP SANs for IP literals).

## JSON-RPC daemon

- The TLS handshake and headers must arrive within 5 s; the whole request within 10 s; response writes are bounded to 10 s. The server is single-threaded, so one idle client can still delay others by up to 5 s.
- Non-loopback binds (`SHAKTI_SERVE_ALLOW_PUBLIC=1`) need TLS (`SHAKTI_TLS_CERT` / `SHAKTI_TLS_KEY`) unless `SHAKTI_SERVE_ALLOW_PLAINTEXT=1`.
- Handler errors reach clients as `internal error`; set `SHAKTI_SERVE_DEBUG=1` to log details to stderr. A handler `content_type` with control characters, or a status outside 100–999, produces a 500.

## IPC

| Variable | Default | Meaning |
|----------|---------|---------|
| `SHAKTI_IPC_TOKEN` | unset | Shared secret for TCP/RDMA IPC: mutual HMAC-SHA256 challenge-response on connect (token never sent; 5 s timeout). Required (with `SHAKTI_IPC_ALLOW_PUBLIC=1`) for non-loopback `ipc.listen`. Needs a TLS build. |
| `SHAKTI_IPC_MCAST_KEY` | unset | Authenticates multicast datagrams (HMAC-SHA256 trailer with timestamp ±60 s and replay cache; payload limit shrinks by 48 bytes). All group members must share it. Needs a TLS build. |
| `SHAKTI_IPC_MCAST_REPLY_RATE` | `100` | Max `ipc.reply` per second to one reply-to address (global cap 10×); `0` disables. |

- `ipc.join` accepts only datagrams addressed to the joined group (on Linux the socket binds to the group); unicast to the port is dropped. A full reply-to table evicts the oldest entry; `ipc.reply` refuses multicast, broadcast, unspecified and port-0 destinations.
- Shared memory: on Linux segments are named `shakti_ipc_<euid>_<name>`; attach refuses segments owned by another user, group/world-accessible, or smaller than the header. Sharing a channel between users is not supported.
- `/tmp/shakti-<uid>` (the Unix-socket directory when neither `SHAKTI_IPC_DIR` nor `XDG_RUNTIME_DIR` is set, and always the directory of shared-memory lock files) must be a real directory owned by the user with no group/other permissions. A socket directory named by `SHAKTI_IPC_DIR` or `XDG_RUNTIME_DIR` must be the user's and not writable by group or others.

## Build hardening

`SHAKTI_HARDEN=1` (default) adds `-fstack-protector-strong -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2`, and on Linux `-fPIE -pie -Wl,-z,relro,-z,now,-z,noexecstack`. Append extra flags with `EXTRA_CFLAGS` / `EXTRA_LDFLAGS` instead of overriding `CFLAGS`. The Docker image (`alpine:3.22.6`) builds a static PIE without gfx/synth and sets `SHAKTI_LIB=/lib/shakti`. `lib/subspace/run` runs its container with `--network none` (opt in with `SHAKTI_SUBSPACE_NETWORK=1`), `--cap-drop ALL`, `no-new-privileges`, a read-only root, and pid/memory limits.

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
| libssl, libcrypto | TLS (`rest.listen_tls`, `wss://`, `server_serve` HTTPS) | Linux, macOS (`SHAKTI_TLS=1`) |
| libzstd | IEFS/HLD zstd codec (`SHAKTI_WITH_ZSTD=1`) | Linux, macOS |
| libsnappy | IEFS/HLD snappy codec (`SHAKTI_WITH_SNAPPY=1`) | Linux, macOS |
| liburing | Optional IEFS batched reads | Linux |
| libaio | Optional IEFS batched reads | Linux |
| libgomp | OpenMP (matrix `mmul`, large `ivec` `+`/`-`/`*`, vector `dot` / large `sum`) | Linux (default with GCC) |
| libomp | OpenMP (`brew install libomp`) | macOS |
| libpthread, libm, librt, libdl | Runtime | Linux |

`import rest` runs `curl` for HTTP client requests (not linked at build time). It is looked up at `SHAKTI_CURL` (absolute path) or `/usr/bin`, `/usr/local/bin`, `/opt/homebrew/bin`, `/bin` — never via `PATH` — and run with `-q` and a minimal environment. The in-process HTTP server uses BSD sockets; TLS uses OpenSSL when `SHAKTI_TLS=1`.

Disable optional components at build time: `SHAKTI_GFX=0`, `SHAKTI_SYNTH=0`, `SHAKTI_DSP=0`, `SHAKTI_STEM=0`, `SHAKTI_SONICPI=0`, `SHAKTI_PDF=0`, `SHAKTI_MIDI=0`, `SHAKTI_IEFS=0`, `SHAKTI_TALK=0`, `SHAKTI_IPC=0`, `SHAKTI_RDMA=0`, `SHAKTI_TLS=0`, `SHAKTI_WS=0`, `SHAKTI_SERVER=0`.


## Platform SDKs

macOS builds use system frameworks (Cocoa, Core Audio, Speech, etc.) under their respective platform licenses. Windows builds use the host toolchain under its terms.

---
