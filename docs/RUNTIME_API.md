# Syntax and builtins

## Core syntax

- **Bind** with `:` — `x: 1`, `a, b: (1, 2)`, `def f(n:2):`, `table(a:[1])`
- **Compare** with `=` — `if x = 1:`, `while i < len(s):`
- Dict literals and slices keep `:` — `{k: v}`, `a[1:3]`
- `==` is not supported

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

## Methods

| Method | Types |
|--------|-------|
| `append(x)` | `list` |
| `pop()` | `list` |
| `keys()`, `values()` | `dict`, `table` |
| `len()` | `list`, `str`, `ivec`, `fvec`, `bvec`, `matrix[int]`, `matrix[float]`, `matrix[bool]` |

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
| `@` | matrix multiply (`matrix[bool]` not supported) |
| `+`, `-`, `*`, `/`, `//`, `%`, `**` | element-wise on numeric matrices |
| unary `-` | element-wise negation (int/float matrices) |
| `=`, `!=`, `<`, `>`, `<=`, `>=` | element-wise compare → `matrix[bool]` |

`matrix[bool]` does not support arithmetic or `@`.

Mixed int/float operands promote to `matrix[float]` where needed.

### Builtins

Same reducers as vectors, applied over all elements: `sum`, `min`, `max`, `avg`, `abs`.

### Printing

- `print(m)` — column-aligned rows
- `repr(m)` — compact `[[1, 2], [3, 4]]`

### Tables

A matrix column stores one matrix per table row (table height = matrix row count). SQL `where` filters copy matrix rows like other column types.

CSV/XML load and CSV save do **not** round-trip matrix columns as typed matrices; use in-memory tables or nested lists.

### Performance

On x86-64, `make prod-speed` enables AVX-512 paths for large numeric matrix `@`, element-wise ops, comparisons, and table filters when the CPU supports them. Smaller matrices use scalar code. There is no GPU backend.

Example: [`examples/matrix.ie`](../examples/matrix.ie).

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

## Tables from files

```ie
t : load("file.csv")
t : load("file.xml")
save(t, "out.csv")
```

Supported formats: **CSV** and **XML** only. `save` writes **CSV** only.

## Input

Module: `import input` ([`src/lib/input.ie`](../src/lib/input.ie)). Example: [`examples/synth_input.ie`](../examples/synth_input.ie).

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
| `input_set_own_gui(1)` | Synth window keys go to hub only (see [SYNTH.md](SYNTH.md)) |

## SQL

Run `import sql` first — enables statement syntax:

```ie
import sql

r : select amount by dept from t where amount > 15
r : select sum amount by dept from t
create table u (id: 0, name: "")
insert into u (id, name) values (1, "ada")
update name : "ADA" from u where id = 1
delete from u where id = 2
```

`by col1, col2` groups and sorts ascending. No separate `group by` / `order by`.

## Modules

| Module | Doc | Example |
|--------|-----|---------|
| `synth` | [SYNTH.md](SYNTH.md) | [`examples/synth_demo.ie`](../examples/synth_demo.ie) |
| `talk` | [TALK.md](TALK.md) | [`examples/talk_demo.ie`](../examples/talk_demo.ie) |
| `input` | above | [`examples/synth_input.ie`](../examples/synth_input.ie) |
| `ipc` | [IPC.md](IPC.md) | [`examples/ipc_echo.ie`](../examples/ipc_echo.ie) |
| `sql` | above | — |
