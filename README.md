# shakti

[Discord](https://discord.gg/PkKwUk9Tf)

Small interpreted language (0.10.0) with vectors, matrices, tables, decorators,
each (`f@`), and optional SQL, graph, IPC, REST, synth, and input modules.

## grammar

- **Bind** with `:` — `x : 1`, `def f(n:2):`
- **Compare** with `=` — `if x = 1:`
- `==` is not supported
- Leading `@` decorates; expression `@` is each; matrix multiply is `mmul(a, b)`

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

## run

```bash
./shakti file.ie
./shakti          # REPL
./shakti file.py  # supported Python subset → Shakti, then run
```

More detail: [doc.md](doc.md). Examples: [example.ie](example.ie).

## license

Apache License 2.0 — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
