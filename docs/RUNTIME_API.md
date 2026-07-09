# Syntax and builtins

## Core syntax

- **Bind** with `:` — `x: 1`, `a, b: (1, 2)`, `def f(n:2):`, `table(a:[1])`
- **Compare** with `=` — `if x = 1:`, `while i < len(s):`
- Dict literals and slices keep `:` — `{k: v}`, `a[1:3]`
- `==` is not supported; `self` is redundant

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

class thing:
  def __init__(*a,b:"default"):
    self.a:a
    self.b:b
d:thing(*range(69),b:"cool")
```

### Syntax and types

- `list[char]` has dedicated syntax: `0x0000`, but other uniform lists just looks like `[1,2,3]`
- `date`/`datetime` has dedicated syntax: `2026.07.09` or `2026.07.09D00:00:00.000` (iso8601 dates/times ok too)

## Methods

| Method               | Applicable types |
|----------------------|------------------|
| `append(x)`          | `list` |
| `pop()`              | `list` aka `list[]` |
| `keys()`, `values()`   | `dict`, `table` |
| `len()`              | `list`, `str`, `list[int]`, `list[float]`, `list[char]`, `list[bool]`, `matrix[int]`, `matrix[float]`, `matrix[char]`, `matrix[bool]` |

- useful e.g. for when unfortunately-named var/function named `len` etc is in environment

## Vectors

`range(n)` builds an `list[int]` `[0, 1, …, n - 1]`. Element-wise `+`, `-`, `*`, `/`, `//`, `%` work on matching-length `list[int]` / `list[float]` pairs (and scalar broadcast).

| Builtin                      | Meaning |
|------------------------------|---------|
| `dot(a, b)`                  | Inner product (fused; no intermediate vector) |
| `sum(v)`                     | Sum of elements |
| `min(v)`, `max(v)`, `avg(v)`     | Reducers |
| `abs(v)`                     | Element-wise absolute value |

```ie
a : range(1000000)
b : range(1000000)
print(dot(a, b))       # prefer this on large vectors
print(sum(a * b))      # same math; allocates a * b first
```

**`.` is not dot product** — it is attribute / column access (`table.col`, `dict.key`). Matrix multiply is **`@`**. For a vector inner product use **`dot(a, b)`**.

On large `list[float]`, `sum` uses a SIMD path when built with `make prod-speed`.

## Matrices

Rectangular nested literals promote to native matrix types. Ragged nested lists stay as `list`.

| Type  | Literal                          | `type()` name |
|-------|----------------------------------|---------------|
| int   | `[[1, 2], [3, 4]]`               | `matrix[int]` |
| float | `[[1.0, 2.0], [3.0, 4.0]]`       | `matrix[float]` |
| bool  | `[[True, False], [False, True]]` | `matrix[bool]` |

### Indexing and shape

```ie
m : [[1, 2], [3, 4]]
m[0]        # row [1, 2] as list[int]
m[0, 1]     # cell 2
m[0:1]      # row slice (submatrix)
m[0] : [9, 8]   # assign row
m[0, 1] : 7     # assign cell

len(m)      # row count
shape(m)    # [rows, cols]
```

### Operators

| Operator  | Meaning |
|-----------|---------|
| `@`         | matrix multiply (`matrix[bool]` not supported) |
| `+`, `-`, `*`   | type-preserving addition, subtraction, multiplication; element-wise on numeric matrices |
| `//`, `%`     | integer-divison, modulo, produce integers element-wise |
| `/`, `*``*`     | float-divison and power; element-wise on numeric matrices |
| unary `-`   | element-wise negation (int/float matrices) |
| `=`, `!=`, `<>` | element-wise compare (equal, not-equal, not-equal)     → `matrix[bool]` |
| `<`, `<=`, `=<` | element-wise compare (less, less/eq, less/eq)          → `matrix[bool]` |
| `>`, `>=`, `>=` | element-wise compare (greater, greater/eq, greater/eq) → `matrix[bool]` |

- `matrix[bool]` does not support arithmetic or `@`.
- `matrix[char]` is unsigned

Mixed int/float operands promote to `matrix[float]` where needed.

### Builtins

Same reducers as vectors, applied over all elements: `sum`, `min`, `max`, `avg`, `abs`. `dot` applies to vectors only, not matrices.

### Performance

On x86-64, `make prod-speed` enables AVX-512 paths for large numeric matrix `@`, element-wise ops, comparisons, and table filters when the CPU supports them. On arm64 (Apple Silicon), the same matrix operations use NEON (install `libomp` for OpenMP row parallelism). Smaller matrices use scalar code.

Vector **`dot`** and large **`sum`** on `list[float]` use the same SIMD/OpenMP stack (`src/vec_kernels.c`). There is no GPU backend in the standalone binary.

Example: [`examples/matrix.ie`](../examples/matrix.ie).

### Printing

- `print(m)` — column-aligned rows
- `repr(m)`  — compact `[[1, 2], [3, 4]]`

### Tables

A matrix column stores one matrix per table row (table height = matrix row count). SQL `where` filters copy matrix rows like other column types.

## Data

```ie
d : dict(a:1, b:2)
t : table(a:[1, 2], b:[3, 4])
k : ktable(a:1, b:2)
```

## I/O and JSON

- `load(file)` and `save(obj, file)` read/write "anything"
- `read(path)`, `write(path, text)`, `readlines(path)` work on bytes, lines
- `listdir(path)`, `walk(path)` scan directories
- `json_loads(s)`, `json_dumps(x)` - limit to JSON subset (no comments or trailing commas)
- `re_match`, `re_findall`, `re_sub`, `re_split` - POSIX regex on Unix/macOS

### Tables from files

```ie
t : load("file.csv")
t : load("file.xml")
save(t, "out.csv")
```

- Extensible: **csv** files are handled by [`csv.ie`](../lib/shakti/csv.ie),
  json files are handled by [`json.ie`](../lib/shakti/json.ie) and so on
- Directories have binary representation for faster load/save (directory must exist)


### Interactive Input

Module: `import input` — see [INPUT.md](INPUT.md). Examples: [`input_demo.ie`](../examples/input_demo.ie), [`synth_input.ie`](../examples/synth_input.ie).

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

| Form              | Meaning |
|-------------------|---------|
| `input(0)`        | Pending events (non-blocking list) |
| `input(ms)`       | Events within `ms` milliseconds |
| `input(1)`        | Stream of raw characters |
| `input(2)`        | Stream of `{code, modifiers, utf8, kind}` dicts |
| `input("prompt")` | Stream of lines |

## SQL

Run `import sql` first — see [SQL.md](SQL.md). Example: [`sql_demo.ie`](../examples/sql_demo.ie).

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

| Module  | Doc                  | Example |
|---------|----------------------|---------|
| `sql`   | [SQL.md](SQL.md)     | [`examples/sql_demo.ie`](../examples/sql_demo.ie) |
| `input` | [INPUT.md](INPUT.md) | [`examples/input_demo.ie`](../examples/input_demo.ie) |
| `ipc`   | [IPC.md](IPC.md)     | [`examples/ipc_echo.ie`](../examples/ipc_echo.ie) |

Index: [EXAMPLES.md](EXAMPLES.md).

## Debugging

Debugging is enabled during **unhandled exceptions**, **restartable exceptions**, and **interrupts**,
the former occur ultimately because of `raise` and the latter because the operator pressed **Control-C** (stylised `^C`)

    \abort

During any debugging sesssion returns `None` which your program may be able to tolerate.

    \continue
    \c

During an **interrupt** continue processing normally. An unhandled exception cannot continue.

    \fail

Unwind the stack completely, return to the toplevel environment and stop all processing.

    \retry

If an exception is raised implements `__retry__`, this function will be called instead,
and its value will be returned.

