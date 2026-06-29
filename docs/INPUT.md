# `input` module

Unified **terminal and GUI event hub** — keyboard, mouse, wheel, and line input in one poll API. Used by the synth UI and REPL.

Build from the repo root (`make prod`; see [README](../README.md)), then:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/input_demo.ie
```

Synth + keyboard jam: [`examples/synth_input.ie`](../examples/synth_input.ie) (`input_set_own_gui(1)` routes window keys to the hub).

## Example

[`examples/input_demo.ie`](../examples/input_demo.ie):

```ie
import input

line : readline("line? ")
print(line)

for ev in input(2):
    if ev["kind"] = "down":
        print(ev["code"], ev["utf8"])
```

## API

Module [`src/lib/input.ie`](../src/lib/input.ie).

| Form | Meaning |
|------|---------|
| `input(0)` | Pending events (non-blocking list) |
| `input(ms)` | Events within `ms` milliseconds |
| `input(1)` | Stream of raw characters |
| `input(2)` | Stream of `{code, modifiers, utf8, kind}` dicts |
| `input("prompt")` | Stream of lines |
| `readline("prompt")` | One blocking line |
| `wait(-1)` | Block until first event |
| `input_set_own_gui(1)` | Synth window keys go to hub only ([SYNTH.md](SYNTH.md)) |

Side-channel state on the module: `input.x`, `input.y`, `input.wheel`, `input.qwerty`, `input.hz`.

| Helper | Purpose |
|--------|---------|
| `input.set_hz(n)` | Poll rate hint |
| `input.set_own_gui(on)` | GUI key routing |
| `input.reload_qwerty()` | Reload QWERTY → semitone map |
| `input.refresh_side()` | Refresh `x`/`y`/`wheel`/`qwerty`/`hz` |
