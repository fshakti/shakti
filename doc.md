# Shakti documentation

Version **0.12.0**.

## Contents

- [Examples index](#examples-index)
- [Command-line interface](#command-line-interface)
- [Syntax and builtins](#syntax-and-builtins)
- [Time-series indexes](#time-series-indexes)
- [Decorators](#decorators)
- [Each (`@`)](#each)
- [Python 3 → Shakti converter](#python-3-shakti-converter)
- [C → Shakti converter](#c-shakti-converter)
- [C# → Shakti converter](#c-shakti-converter-1)
- [Java → Shakti converter](#java-shakti-converter)
- [`sql` module](#sql-module)
- [graph module](#graph-module)
- [`gfx` module](#gfx-module)
- [`pyplot` module](#pyplot-module)
- [`jupyter` module](#jupyter-module)
- [`input` module](#input-module)
- [IPC module](#ipc-module)
- [REST module](#rest-module)
- [`synth` module](#synth-module)
- [`dsp` module](#dsp-module)
- [`stem` module](#stem-module)
- [`sonicpi` module](#sonicpi-module)
- [`pdf` module](#pdf-module)
- [`midi` module](#midi-module)
- [`iefs` module](#iefs-module)
- [`talk` module](#talk-module-macos)
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
| *(core)* | `matrix.ie` | Matrices (`mmul`), `dot`, `sum` / `min` / `max` |
| *(core)* | `table_csv.ie` | CSV/TSV `save` / `load` (numeric + string columns) |
| `import sql` | `sql_demo.ie` | Select, insert, update, delete, join |
| `import graph` | `graph_demo.ie` | Knowledge graph triples, query, path |
| `import gfx` | `gfx_demo.ie` | Pixel window + click drawing |
| `import pyplot` | `pyplot_demo.ie` | Line / scatter / bar charts on gfx |
| `import jupyter` | `jupyter_demo.ie` | Notebook cells, `eval`, `.ipynb` R/W, gfx view |
| `import input` | `input_demo.ie` | `readline` + timed event poll |
| `import input` + `synth` | `synth_input.ie` | QWERTY jam with synth window |
| `import synth` | `synth_demo.ie` | Synth window + event loop |
| `import synth` | `synth_song.ie` | Twinkle + drum sequencer |
| `import synth` | `synth_just_intonation.ie` | Just-intonation major chord |
| `import dsp` | `dsp_demo.ie` | Just-intonation ratio helpers |
| `import sonicpi` | `sonicpi_demo.ie` | Drive Sonic Pi over OSC |
| `import pdf` | `pdf_demo.ie` | PDF 1.4 write/read |
| `import midi` | `midi_demo.ie` | ALSA / CoreMIDI I/O |
| `import iefs` | `iefs_demo.ie` | Durable `.iefs` save/load |
| `import talk` | `talk_demo.ie` | Speech-to-text (macOS) |
| `import ipc` | `ipc_echo.ie` | UDS echo server |
| `import ipc` | `ipc_echo_client.ie` | Client for `ipc_echo.ie` |
| `import ipc` | `ipc_rdma.ie` | RDMA/RoCE server (Linux + NIC) |
| `import ipc` | `ipc_rdma_client.ie` | Client for `ipc_rdma.ie` |
| `import rest` | `rest_demo.ie` | HTTP GET/POST client + local server |

## Tools

| Tool | Description |
|------|-------------|
| `converters/p2s.ie` | Strict Python 3 → Shakti converter ([docs](#python-3-shakti-converter)) |
| `converters/c2s.ie` | Strict C → Shakti converter ([docs](#c-shakti-converter)) |
| `converters/cs2s.ie` | Strict C# → Shakti converter ([docs](#c-shakti-converter-1)) |
| `converters/j2s.ie` | Strict Java → Shakti converter ([docs](#java-shakti-converter)) |
| `examples/python.py` / `examples/c.c` / `examples/csharp.cs` / `examples/java.java` | Tiny demos for each converter |
| `example.py` / `example.c` / `example.cs` / `example.java` | Broader subset demos (`./.build/shakti examples/example.py` / `example.c` / `example.cs` / `example.java`) |

## Module docs

| Module | Doc |
|--------|-----|
| `sql` | [sql module](#sql-module) |
| `graph` | [graph module](#graph-module) |
| `gfx` | [gfx module](#gfx-module) |
| `pyplot` | [pyplot module](#pyplot-module) |
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
| Language & builtins | [syntax and builtins](#syntax-and-builtins) |
| CLI | [command-line interface](#command-line-interface) |
| REPL | [REPL](#repl) |
| Time-series indexes | [time-series indexes](#time-series-indexes) |

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

### REPL

Start with `./.build/shakti` (or `-i` after `--command`). Banner line:

`\d docs  \v vars  \w names  \q [N]  quit|exit`

| Input | Effect |
|-------|--------|
| `\d` / `\help` / `help` | Print the fixed-width grammar card from [`IE.txt`](IE.txt); full prose remains in this doc |
| `\v` | Bound names and values |
| `\w` | Bound names only |
| `\q` | Process exit status `0` |
| `\q N` | Process exit status `N` (integer; invalid args print `usage: \q [N]` and stay in the REPL) |
| `quit` / `exit` | Soft leave the REPL loop (process status `0`) |

Unknown `\` commands print an error and stay in the REPL.

Build note: the direct-run converters (`./.build/shakti file.py`, `./.build/shakti file.c`,
`./.build/shakti file.cs`, `./.build/shakti file.java`) use embedded copies generated into
`gen/` from `converters/p2s.ie`, `converters/c2s.ie`, `converters/cs2s.ie`, and `converters/j2s.ie`.
Repeated source runs use a disk transpile cache; set `SHAKTI_NO_TRANSPILE_CACHE=1`
to force a fresh convert each process.

---

# Python 3 → Shakti converter

Strict subset converter written in Shakti. Embedded in the executable from
generated headers under `gen/` for direct runs, and still available as
`converters/p2s.ie` for emit-only conversion.

```bash
./.build/shakti file.py                 # transpile + run (supported subset)
./.build/shakti examples/python.py
./.build/shakti converters/p2s.ie input.py -o out.ie
./.build/shakti converters/p2s.ie examples/python.py -o python.ie
SHAKTI_LIB=$PWD/lib ./.build/shakti out.ie
```

`./.build/shakti file.py` is not CPython: it lowers the supported subset to Shakti
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
| `+x` | `x` |
| `class C:` (no bases) | body flattened (`pass` / nested `def` → top-level) |

Also converts functions, one-argument lambdas, `if`/`elif`/`else`, `while`, `for`, break/continue/pass, lists/tuples/dicts, indexing/slices, attributes, calls, decorators, f-strings (without format specs), augmented `+= -= *= /=`, and `import name`.

`examples/python.py` demonstrates NumPy and pandas lowering. `numpy.array`/`asarray`
become native vectors or matrices; common reducers map to Shakti builtins;
`pandas.Series` becomes a vector and `pandas.DataFrame({...})` becomes
`table(...)`. Other NumPy/pandas calls fail with a source location.

## Rejected

Chained assignment/comparisons, annotations, class inheritance/bases, comprehensions/generators, sets/bytes/complex, `is`/`is not`, bitwise ops, `*args`/`**kwargs`, keyword-only/positional-only args, multi-argument lambdas, loop `else`, from-import/aliases, nested/starred unpacking, exceptions/`with`/`yield`/`async`, and `del`/`global`/`nonlocal`.

Docstrings become `#` comments.

---

# C → Shakti converter

Strict subset converter written in Shakti. Embedded in the executable from
generated headers under `gen/` for direct runs, and still available as
`converters/c2s.ie` for emit-only conversion.

If `main` is present, the converter appends a call so the program runs.
`int main(void)` / `int main()` become `def main():` plus `main()`.
`int main(int argc, char **argv)` becomes `def main(argc, argv):` plus
`main(len(argv[1:]), argv[1:])` (CLI args without the program name).

```bash
./.build/shakti file.c                  # transpile + run (supported subset)
./.build/shakti examples/example.c
./.build/shakti converters/c2s.ie input.c -o out.ie
./.build/shakti converters/c2s.ie examples/c.c -o c.ie
SHAKTI_LIB=$PWD/lib ./.build/shakti out.ie
```

`./.build/shakti file.c` is not a C compiler or libc runtime: it lowers the supported
subset to Shakti and evaluates that. Unsupported syntax exits nonzero with
`file:line:col` diagnostics (original `.c` path preserved).

### Performance vs `cc -O2` (methodology)

Compare **already-built** `cc -O2` binaries (compile untimed) to Shakti:

| Path | What is timed | How to read it |
|------|---------------|----------------|
| `./.build/shakti file.c` | process start + transpile + parse + eval | Historical headline; mixes converter cost |
| `./.build/shakti file.ie` | process start + parse + eval | Fairer interpreter-vs-binary gap |
| `eval~` | batch `.ie` minus single `.ie`, per rep | Approximates loop body cost |

Fair peer for “language VM” cost is **CPython** (~parity on the same harness).
Expect **gcc -O2** to win scalar micros by a wide margin: Shakti is an AST
interpreter with bulk NEON/OpenMP only on large vectors (`make prod-speed`).
Tips: emit `.ie` once for hot runs; repeated `./.build/shakti file.c` uses a disk
transpile cache (disable with `SHAKTI_NO_TRANSPILE_CACHE=1`). Counting
`while (i < N): …; i += 1` loops lowered from C are specialized in the evaluator.

## Rewrites

| C | Shakti |
|---|--------|
| `int x = 1;` | `x : 1` |
| `int a=1, b, c=3;` | `a : 1` / `b : 0` / `c : 3` |
| `x == 1` | `x = 1` |
| `int f(int n) { ... }` | `def f(n): ...` |
| K&R `f(a,b) int a; int b; {…}` | `def f(a, b): …` |
| `printf(...)` / `puts(...)` | `print(...)` |
| `true` / `false` / `NULL` | `True` / `False` / `None` |
| `&&` / `\|\|` / `!` | `and` / `or` / `not` |
| `&` / `\|` / `^` / `~` / `<<` / `>>` | `band` / `bor` / `bxor` / `bnot` / `shl` / `shr` |
| `&=` `\|=` `^=` `<<=` `>>=` | `x : band(x, y)` (etc.) |
| `for (int i = 0; i < n; i++)` | `i : 0` + `while (i < n): ...; i += 1` |
| `do { … } while (c);` | `while True:` body + `if not c: break` |
| `switch` / `case` / `default` | temp + `if`/`elif`/`else` (no fall-through) |
| `enum E { A, B=3, C };` | `A : 0` / `B : 3` / `C : 4` |
| `typedef int MyInt;` / `typedef struct …` | recorded aliases (no emit) |
| `struct S { int x; };` / `struct S s = {1};` | field map + `s : {"x": 1}`; `s.x` → `s["x"]` |
| `(int)x` / `(T)x` | erased (operand only) |
| `+x` | `x` |
| prefix `++x;` / `--x;` as stmt | `x += 1` / `x -= 1` |
| `int nums[] = {2, 4}` | `nums : [2, 4]` |
| `#include ...` | erased |
| `#define NAME rest` (object-like) | expand `NAME` during lex |
| `main` | `def main(...):` + trailing call (`main()` or `main(len(argv[1:]), argv[1:])`) |

Also converts `if`/`else if`/`else`, `while`, break/continue, indexing,
calls, augmented `+= -= *= /= %=`, postfix `++`/`--` (as `+= 1` / `-= 1`),
and same-function `label:` / `goto label;` via a `__c_goto` string state
(`while True` + `continue` jumps). Storage-class keywords
`static`/`extern`/`register`/`auto` are ignored when parsing types.

## Rejected

Pointers/`*` deref/`&`/`->` (except `main` argv sugar), unions, function-like
`#define NAME(`, conditional preprocessor (`#if`/`#ifdef`/`#ifndef`/`#endif`/
`#undef`), switch fall-through, `malloc`/`free`/`sizeof`, prefix `++`/`--`
inside larger expressions, and arbitrary libc APIs outside the mapped
`printf`/`puts` surface.

`//` comments become `#` comments; closed `/* ... */` block comments are
discarded. An unterminated block comment exits nonzero with the opening
`line:col` and does not emit or execute a partial program.

---

# C# → Shakti converter

Strict subset converter written in Shakti. Embedded in the executable from
generated headers under `gen/` for direct runs, and still available as
`converters/cs2s.ie` for emit-only conversion.

```bash
./.build/shakti file.cs                 # transpile + run (supported subset)
./.build/shakti examples/csharp.cs
./.build/shakti converters/cs2s.ie input.cs -o out.ie
./.build/shakti converters/cs2s.ie examples/csharp.cs -o csharp.ie
SHAKTI_LIB=$PWD/lib ./.build/shakti out.ie
```

`./.build/shakti file.cs` is not the .NET runtime: it lowers the supported subset to
Shakti and evaluates that. Unsupported syntax exits nonzero with
`file:line:col` diagnostics (original `.cs` path preserved).

## Rewrites

| C# | Shakti |
|----|--------|
| `int x = 1;` | `x : 1` |
| `x == 1` | `x = 1` |
| `int f(int n = 2) { ... }` | `def f(n:2): ...` |
| `f(k: 1)` | `f(k:1)` |
| `Console.WriteLine(...)` | `print(...)` |
| `xs.Length` / `xs.Count` | `len(xs)` |
| `xs[1..3]` | `xs[1:3]` |
| `true` / `false` / `null` | `True` / `False` / `None` |
| `&&` / `\|\|` / `!` | `and` / `or` / `not` |
| `x => x + 1` | `lambda x: (x + 1)` |
| `for (int i = 0; i < n; i++)` | `i : 0` + `while (i < n): ...; i += 1` |
| `switch (x) { case ... }` | temp + `if`/`elif`/`else` (no fall-through) |
| `(T)x` | `x` (cast erased) |
| `+x` | `x` |
| `++x;` / `--x;` | `x += 1` / `x -= 1` (statements only) |
| `class C { ... }` | flattened fields/methods (no inheritance) |
| `new[] { 1, 2 }` | `[1, 2]` |
| `new Dictionary<...> { {k, v} }` | `{k: v}` |
| `new { a = x, b = y }` | `{ "a": x, "b": y }` (via `table` for DataFrames) |
| `$"hi {x}"` | `f"hi {x}"` |
| `using ...;` | erased |

Also converts expression-bodied methods (`=>`), `if`/`else if`/`else`, `while`,
`foreach`, break/continue, indexing/slices, attributes, calls, keyword
arguments, augmented `+= -= *= /=`, postfix `++`/`--` (as `+= 1` / `-= 1`),
and one-argument lambdas.

`examples/csharp.cs` demonstrates NumPy/pandas-style lowering via `Np.*` / `Pd.*`
(paralleling Python's `numpy`/`pandas` aliases). `Np.Array` becomes a native
vector; common reducers map to Shakti builtins; `Pd.DataFrame(new { ... })`
becomes `table(...)`. Other `Np`/`Pd` calls fail with a source location.

## Rejected

Structs/interfaces/enums/records/namespaces, inheritance (`class B : A`),
`try`/`catch`/`do`/`throw`/`lock`/`yield`/`async`/`await`, switch fall-through,
prefix `++`/`--` inside larger expressions, multi-argument/block lambdas,
format specs in interpolations, bitwise ops, generics beyond ignored type
arguments on declarations/calls, and arbitrary .NET library APIs outside the
mapped `Console`/`Np`/`Pd` surface.

`//` comments become `#` comments; closed `/* ... */` block comments are
discarded. An unterminated block comment exits nonzero with the opening
`line:col` and does not emit or execute a partial program.

---

# Java → Shakti converter

Strict subset converter written in Shakti. Embedded in the executable from
generated headers under `gen/` for direct runs, and still available as
`converters/j2s.ie` for emit-only conversion.

One top-level class is accepted as a non-runtime shell for `static` fields and
methods. If `main` is present, the converter appends `main(argv[1:])` so CLI
arguments match Java (no program name).

```bash
./.build/shakti file.java               # transpile + run (supported subset)
./.build/shakti examples/java.java
./.build/shakti examples/example.java
./.build/shakti converters/j2s.ie input.java -o out.ie
./.build/shakti converters/j2s.ie examples/java.java -o java.ie
SHAKTI_LIB=$PWD/lib ./.build/shakti out.ie
```

`./.build/shakti file.java` is not the JVM: it lowers the supported subset to Shakti
and evaluates that. Unsupported syntax exits nonzero with `file:line:col`
diagnostics (original `.java` path preserved).

## Rewrites

| Java | Shakti |
|------|--------|
| `int x = 1;` | `x : 1` |
| `x == 1` | `x = 1` |
| `static int f(int n) { ... }` | `def f(n): ...` |
| `System.out.println(...)` | `print(...)` |
| `xs.length` / `xs.size()` | `len(xs)` |
| `true` / `false` / `null` | `True` / `False` / `None` |
| `&&` / `\|\|` / `!` | `and` / `or` / `not` |
| `x -> x + 1` | `lambda x: (x + 1)` |
| `for (int i = 0; i < n; i++)` | `i : 0` + `while (i < n): ...; i += 1` |
| `for (int x : xs)` | `for x in xs:` |
| `do { ... } while (c)` | `while True: ...; if not c: break` |
| `switch (x) { case ... }` | temp + `if`/`elif`/`else` (no fall-through) |
| `(T)x` | `x` (cast erased) |
| `+x` | `x` |
| `++x;` / `--x;` | `x += 1` / `x -= 1` (statements only) |
| `new int[]{1, 2}` | `[1, 2]` |
| `List.of(1, 2)` | `[1, 2]` |
| `Map.of("k", v)` | `{"k": v}` |
| `m.get(k)` | `m[k]` |
| `"hi " + n` | `"hi " + str(n)` |
| `Math.max(a, b)` | `max(a, b)` |
| `package ...;` / `import ...;` | erased |
| class shell + `main` | static members + trailing `main(argv[1:])` |

Also converts `if`/`else if`/`else`, `while`, break/continue, indexing,
attributes, calls, augmented `+= -= *= /=`, postfix `++`/`--` (as `+= 1` /
`-= 1`), and one-argument expression lambdas.

## Rejected

Inheritance/`implements`, constructors, instance fields/methods, nested types,
overloading, `try`/`catch`/`throw`/`synchronized`/`assert`, switch fall-through,
prefix `++`/`--` inside larger expressions, multi-argument/block lambdas, sized
arrays `new T[n]`, bitwise ops, generics beyond ignored type arguments, and
arbitrary JDK APIs outside the mapped `System.out` / `Math` / `List.of` /
`Map.of` surface.

`//` comments become `#` comments; closed `/* ... */` block comments are
discarded. An unterminated block comment exits nonzero with the opening
`line:col` and does not emit or execute a partial program.

---

# Syntax and builtins

## Core syntax

- **Bind** with `:` — `x: 1`, `a, b: (1, 2)`, `def f(n:2):`, `table(a:[1])`
- **Compare** with `=` — `if x = 1:`, `while i < len(s):`
- Dict literals and slices keep `:` — `{k: v}`, `a[1:3]`
- `==` is not supported
- Integer bitwise helpers (used by the C converter): `band(a,b)`, `bor(a,b)`,
  `bxor(a,b)`, `bnot(a)`, `shl(a,b)`, `shr(a,b)` — `shl`/`shr` are logical
  shifts on the low 64 bits; shift counts must be in `0..63`

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

Large vector operations use OpenMP. `make prod-speed` enables `-O3` and
native CPU tuning (`-mcpu=native` on arm64, including Apple Silicon M5;
`-march=x86-64-v2` on x86-64). `SHAKTI_PORTABLE_CPU=1` uses `-mcpu=apple-m4` on
arm64 (clang does not yet expose `-mcpu=apple-m5` on current Xcode).
With `ISOLDE_LIB`, reducers may use `isolde_*` kernels.
For windowed weighted averages over many keys, see [time-series indexes](#time-series-indexes).

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

On x86-64, `make prod-speed` uses `-march=x86-64-v2` with scalar C (+ OpenMP) for large numeric matrix `mmul`, element-wise ops, comparisons, and table filters. On arm64 (Apple Silicon), the same matrix operations use NEON; `prod-speed` passes `-mcpu=native` (M5 and other hosts) or `-mcpu=apple-m4` with `SHAKTI_PORTABLE_CPU=1` (install `libomp` for OpenMP row parallelism). Smaller matrices use scalar code.

The default `make prod` build parallelizes large `ivec` `+` / `-` / `*` with OpenMP. Vector **`dot`** and large **`sum`** on `fvec` use the SIMD/OpenMP stack in `src/vec_kernels.c` (NEON on arm64; scalar+OpenMP on x86; on Darwin with Accelerate, large `sum`/`dot` use `vDSP` and large float `mmul` uses `cblas_dgemm`). `prod-speed` also retunes C and ObjC units with `-O3` and the arch flags above. There is no GPU backend in the standalone binary.

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
- `eval(src)` — parse and evaluate a Shakti source string in the **root** environment (returns the value, or an error value). Bindings persist across calls, including when `eval` is invoked from nested functions.
- `sh(cmd)` — runs `cmd` via the host shell (`system`). Disabled when `SHAKTI_SAFE=1` or `SHAKTI_ALLOW_EXEC=0`. Untrusted `.ie` input is otherwise equivalent to an untrusted shell script; see README security notes.
- Parser nesting is capped at 40; interpreter call depth defaults to 3000 (`SHAKTI_CALL_MAX_DEPTH`).

## Process / REPL

- REPL: `\q` or `\q N` — terminate the process with integer status (default `0`), q-style (Isolde parity). Invalid `\q` args print `usage: \q [N]` and stay in the REPL.
- Soft leave: type `quit` or `exit` to leave the REPL loop without forcing a non-zero status.
- Other meta-commands: `\d` / `\help` / `help` (grammar card [`IE.txt`](IE.txt)), `\v` (vars), `\w` (names). See [REPL](#repl) under the CLI section.

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
- Normal-sized files load via a single buffered read (fast path). Files larger than
  `SHAKTI_CSV_MAX_BYTES` (default 1 GiB; override with that env var) stream with
  `getline` so the whole file need not reside in memory.

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

`by col1, col2` groups and sorts ascending. No separate `group by` / `order by`. Prefer core `,` / `union` / `outer` table joins (see [Table joins](#table-joins----union--outer)); SQL `t1 join t2 on col` is not yet implemented.

For high-throughput windowed averages / range aggregates over dense keys, prefer the [time-series indexes](#time-series-indexes) instead of a full SQL scan.

## Modules

| Module | Doc | Example |
|--------|-----|---------|
| `sql` | [sql module](#sql-module) | `sql_demo.ie` |
| `graph` | [graph module](#graph-module) | `graph_demo.ie` |
| `gfx` | [gfx module](#gfx-module) | `gfx_demo.ie` |
| `pyplot` | [pyplot module](#pyplot-module) | `pyplot_demo.ie` |
| `jupyter` | [jupyter module](#jupyter-module) | `jupyter_demo.ie` |
| `input` | [input module](#input-module) | `input_demo.ie` |
| `synth` | [synth module](#synth-module) | `synth_demo.ie` |
| `talk` | [talk module](#talk-module-macos) | `talk_demo.ie` |
| `ipc` | [IPC module](#ipc-module) | `ipc_echo.ie` |
| `rest` | [REST module](#rest-module) | `rest_demo.ie` |

Index: [examples index](#examples-index).

---

# Time-series indexes

Core builtins (no `import`) for load-time prefix / sorted indexes over columnar `ivec` / `fvec` data. Build the index **once** outside the timed query path; query with binary search over half-open `[t0, t1)` windows.

Keys and group ids must be dense nonnegative integers (`max_id ≤ row count`). Windows are half-open: include `t0`, exclude `t1`.

## Weighted average (`wavg*`)

Σ(value·weight) / Σ(weight) for a key set over a time window:

```ie
idx : wavg_index(rows.key, rows.time, rows.value, rows.weight)
avg : wavg(idx, keys, t0, t1)   # 0 if weight sum is 0
```

| Builtin | Role |
|---------|------|
| `wavg_index(key, time, value, weight)` | Sort by `(key, time)`; return `[time, product_prefix, weight_prefix, bounds]` |
| `wavg(index, keys, t0, t1)` | Scalar weighted average for keys in `keys` over `[t0, t1)` |

`value` / `weight` may be `fvec` or other numeric columns. The query does not reorder the caller’s key list. Prefer a pre-sorted key list to skip an internal sort.

## Windowed average (`winavg*`)

Per-key mean of a numeric column over one or more windows (returns the **last** window’s grouped table):

| Builtin | Role |
|---------|------|
| `winavg_index(key, time, value)` | Dense key starts + prefix sums/counts |
| `winavg(index, keys, starts, window)` | For each start in `starts`, mean over `[start, start+window)` |

Result columns: `key`, `avg`.

## Range maximum (`range_max*`)

Per-key `max(value)` over a key set and time window:

| Builtin | Role |
|---------|------|
| `range_max_index(key, time, value)` | Sort by `(key, time)`; return `[time, value, bounds]` |
| `range_max(index, keys, t0, t1)` | `max(value)` by `key` for keys in `keys` over `[t0, t1)` |

Result columns: `key`, `value`.

## Key max/min (`key_maxmin*`)

Per-key `max(col_hi)` and `min(col_lo)`:

| Builtin | Role |
|---------|------|
| `key_maxmin_index(key, col_hi, col_lo)` | Build the grouped table once at load |
| `key_maxmin(index)` | Return the precomputed table (or pass raw columns for a one-shot scan) |

Result columns: `key`, `col_hi`, `col_lo`.

## Time-series stats (`ts_stats*`)

Aggregates by key for one group id over a time window:

| Builtin | Role |
|---------|------|
| `ts_stats_index(group, time, key, value)` | Sort by `(group, time)` |
| `ts_stats_agg(index, group_id, t0, t1)` | `count` / `sum` / `min` / `max` / `avg` by `key` |
| `ts_stats_bucket(index, group_id, t0, t1, bucket)` | Fixed-width time buckets; returns the last bucket’s table (no sum column) |

## Cumulative multiplier hits (`cum_mult_hits`)

For the first `n_rows` rows with positive `value`, scan forward within `horizon` on the same key and count hits when the cumulative same-key sum reaches 2×, 4×, and 20× the initial value.

| Builtin | Role |
|---------|------|
| `cum_mult_hits(key, time, value, n_rows, horizon)` | Return hit count (int) |

## Table joins (`,` / `union` / `outer`)

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

SQL `t1 join t2 on col` remains unimplemented.

## Asof join helpers

| Builtin | Role |
|---------|------|
| `asof_sort(eq, time)` | Sort paired `ivec`s by `(eq, time)` → `[eq_sorted, time_sorted]` |
| `asof_bin(eq, time, query_eq, query_time)` | Per query row, last index with matching `eq` and `time ≤ query_time` (`-1` if none) |

## See also

- [SQL](#sql) / [`sql` module](#sql-module) — general `select` / `where` (slower for full-table weighted-average scans)

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

Demo games: [`import pong`](lib/pong.ie) / `pong.run()`, [`import chess`](lib/chess.ie) / `chess.run()` (needs `SHAKTI_GFX=1`).

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
- `SHAKTI_IPC_ALLOW_PUBLIC=1` — allow `ipc.listen` on a non-loopback address (default: loopback/UDS only)

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
| `rest.listen(port[, host])` | Listen on TCP (default host `127.0.0.1`; non-loopback needs `SHAKTI_REST_ALLOW_PUBLIC=1`) |
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
| `stem.separate_file(path, outdir)` | Offline file split (classical engine) |
| `stem.write_ml_synth(path, nhidden)` | Write synthetic `SHAKST01` checkpoint |
| `stem.load_ml(path)` / `stem.unload_ml()` | Load/unload MLP weights |
| `stem.ml_info()` | Checkpoint dims / flags (CPU-only) |
| `stem.ml_spike(batch, iters)` | Time one MVM layer |
| `stem.separate_file_ml(path, outdir)` | Offline file split via MLP masks (**44100 Hz** WAV required) |
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

Portable durable save/load for Shakti values (`.iefs`). Built by default (`SHAKTI_IEFS=1`).

```bash
export SHAKTI_LIB=$PWD/lib
# copy iefs_demo.ie section from examples/example.ie
```

```ie
import iefs
iefs.save(x, "data.iefs")
x2 : iefs.load("data.iefs")          # CRC-checked owned copy
x3 : iefs.map("data.iefs")           # mmap; skip CRC; alias payloads
x3 : iefs.map("data.iefs", pages:"thp")  # or "2m" / "1g" (HugePages must be reserved)
iefs.save(x, "big.iefs", 1)          # force O_DIRECT when available (Linux);
                                     # on Darwin large AUTO I/O uses F_NOCACHE
print(iefs.direct_available())
```

- `iefs.load` / global `load("….iefs")` — full read + CRC + malloc copy (unchanged).
- `iefs.map` — `mmap` the file, skip CRC, alias contiguous vector/matrix payloads. Mutating an aliased value materializes a private copy first.
- SQL `select … from "….iefs"` opens via **map**. `update` / `delete` from a `.iefs` path string open via map and write the result back with `iefs.save` semantics.
- CSV / XML / TSV `load` paths are unchanged.

Global `save`/`load` also recognize the `.iefs` extension. Supported: scalars, vectors, matrices, lists, dicts, tables. Functions, errors, and input streams are rejected.

Env: `SHAKTI_IEFS_DIRECT=0|1`, `SHAKTI_IEFS_DIRECT_MIN=<bytes>` (`ISOLDE_IEFS_*` aliases still accepted).
On Darwin, `direct_available()` is 0; large AUTO reads/writes still set `F_NOCACHE` above the same size threshold.

Disable at build: `SHAKTI_IEFS=0 make prod`.

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

Disable optional components at build time: `SHAKTI_GFX=0`, `SHAKTI_SYNTH=0`, `SHAKTI_DSP=0`, `SHAKTI_STEM=0`, `SHAKTI_SONICPI=0`, `SHAKTI_PDF=0`, `SHAKTI_MIDI=0`, `SHAKTI_IEFS=0`, `SHAKTI_TALK=0`, `SHAKTI_IPC=0`, `SHAKTI_RDMA=0`.


## Platform SDKs

macOS builds use system frameworks (Cocoa, Core Audio, Speech, etc.) under their respective platform licenses. Windows builds use the host toolchain under its terms.

---
