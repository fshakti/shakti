# `talk` module (macOS)

Speech-to-text from the microphone. Built by default on macOS (`SHAKTI_TALK=1`).

Grant **Microphone** and **Speech Recognition** to your terminal in System Settings.

Build from the repo root (`make prod`; see [README](../README.md)), then:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/talk_demo.ie
```

## Example

[`examples/talk_demo.ie`](../examples/talk_demo.ie):

```ie
import talk

talk.set_locale("en-US")
text : talk.listen(2)    # stop after 2 s silence, Enter, or click
print(text)
```

Or `talk(2)` as shorthand for `talk.listen(2)`.

Default locale: `en-US`. Override with `export SHAKTI_TALK_LOCALE=en-GB` or `talk.set_locale("en-GB")`.

## API

Module [`src/lib/talk.ie`](../src/lib/talk.ie): `listen(silence_sec)`, `set_locale(locale)`.

| Builtin | Returns |
|---------|---------|
| `talk_listen(silence_sec)` | Transcript string |
| `talk_set_locale(locale)` | Sets BCP-47 locale |

Disable at build: `SHAKTI_TALK=0 make prod`.
