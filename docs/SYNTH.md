# `synth` module

Desktop synth UI (Linux: X11 + ALSA; macOS: Cocoa + Core Audio).

Build from the repo root (`make prod`; see [README](../README.md)), then:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/synth_demo.ie
```

## Example

[`examples/synth_demo.ie`](../examples/synth_demo.ie):

```ie
import synth

synth.open()
synth.set_steps(16)
synth.set_metro(1)
synth.set_metro_sound(0)   # 0 = click, 1 = drum

while synth.alive():
    synth.tick()
```

Jam with keyboard events: [`examples/synth_input.ie`](../examples/synth_input.ie):

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

Module [`src/lib/synth.ie`](../src/lib/synth.ie): `open`, `close`, `alive`, `tick`, `set_steps`, `steps`, `set_metro`, `metro_on`, `set_metro_sound`, `metro_sound`, `set_mute`, `note_on`, `note_off`.

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

Disable at build: `SHAKTI_SYNTH=0 make prod`.
