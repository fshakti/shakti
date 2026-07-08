# Sonic Pi (`import sonicpi`)

Drive a running [Sonic Pi](https://sonic-pi.net/) instance by sending OSC messages over UDP. Shakti does not implement UDP in the `ipc` module; instead `sonicpi` shells out to `oscsend` or `python3` + [`osc_send.py`](../src/lib/osc_send.py).

## Requirements

- Sonic Pi running (for live audio demos)
- `oscsend` (liblo-tools) **or** `python3` on `PATH`
- Optional env: `SHAKTI_SONICPI_HOST` (default `127.0.0.1`), `SHAKTI_SONICPI_PORT` (default `4560`)
- Remote machines: enable **Allow OSC from other computers** in Sonic Pi Preferences → IO → Networked OSC

## Quick start

Sonic Pi buffer:

```ruby
live_loop :shakti do
  use_real_time
  n = sync "/osc*/trigger/prophet"
  synth :prophet, note: n[0], cutoff: n[1], sustain: n[2]
end
```

Shakti:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/sonicpi_demo.ie
```

## API

| Function | Description |
|----------|-------------|
| `sonicpi.set_host(h)` / `sonicpi.host()` | Target host (env `SHAKTI_SONICPI_HOST` overrides) |
| `sonicpi.set_port(p)` / `sonicpi.port()` | UDP port (env `SHAKTI_SONICPI_PORT` overrides) |
| `sonicpi.set_sender(cmd)` / `sonicpi.sender()` | Force `oscsend` or `python3`; empty = auto |
| `sonicpi.available()` | `1` if a sender backend is on `PATH` |
| `sonicpi.osc(path, types, args)` | Send with explicit OSC typetag string (`"iff"`, etc.) |
| `sonicpi.send(path, args)` | Send; typetags inferred from arg types |
| `sonicpi.trigger(path, args)` | Shorthand for `send` |
| `sonicpi.cue(name, args)` | Send to `/<name>` |
| `sonicpi.play(note, vel, dur)` | Send `/play` cue (`vel` default 100, `dur` default 1) |

All send functions return the `sh()` exit code (`0` = success).

## Receive side in Sonic Pi

Sonic Pi prefixes incoming paths with `/osc` and the sender address. Use a wildcard sync:

```ruby
sync "/osc*/my_path"
```

Not `/osc/my_path` unless you know the sender IP and port.

## Why not `ipc`?

Sonic Pi listens for OSC on **UDP** port 4560. Shakti `ipc` is TCP, Unix domain sockets, and optional RDMA — not UDP. The `sonicpi` module keeps networking out of the C runtime and reuses `sh()` like `lissen` uses `curl`.

## Example

[`examples/sonicpi_demo.ie`](../examples/sonicpi_demo.ie)

## Tests

Local workspace: `tests/sonicpi_smoke.ie`, `tests/sonicpi_send.ie` (UDP sink fixture; no Sonic Pi required).
