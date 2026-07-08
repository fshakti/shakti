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

Module [`src/lib/synth.ie`](../src/lib/synth.ie): `open`, `close`, `alive`, `tick`, `set_steps`, `steps`, `set_metro`, `metro_on`, `set_metro_sound`, `metro_sound`, `set_mute`, `note_on`, `note_off`, `set_tuning`, `tuning`.

## Tuning

Default pitch uses **12-TET** (equal temperament). Switch to **just intonation** for pure frequency ratios (3:2 fifth, 5:4 major third) so chord partials align:

```ie
synth.set_tuning("just")   # or "ji"
synth.set_tuning("12tet")  # default; aliases: "equal"
```

Example: [`examples/synth_just_intonation.ie`](../examples/synth_just_intonation.ie) plays a just-intonation C major chord.

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

Local sample packs live under [`samples/`](../samples/) (gitignored). See [SAMPLES.md](SAMPLES.md).

| API | Purpose |
|-----|---------|
| `load_sample(path)` | Load a `.wav` (16- or 24-bit PCM) into row 6 (`SAMP`) |
| `sample_loaded()` | `1` if a sample is loaded |
| `sample_name()` | Basename of the loaded file |

Example: [`examples/synth_bsr_sample.ie`](../examples/synth_bsr_sample.ie) loads a BSR Yellow kick drum.

**Limits:** stereo is downmixed to mono; sample rate is resampled to 48 kHz; playback buffer holds at most **8 seconds**.

## Examples

| File | Description |
|------|-------------|
| [`synth_demo.ie`](../examples/synth_demo.ie) | Window + event loop |
| [`synth_song.ie`](../examples/synth_song.ie) | Twinkle + drum sequencer |
| [`synth_input.ie`](../examples/synth_input.ie) | Keyboard jam with `input(2)` |
| [`synth_bsr_sample.ie`](../examples/synth_bsr_sample.ie) | Load sample into SAMP row |

Disable at build: `SHAKTI_SYNTH=0 make prod`.
