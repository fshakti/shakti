# Examples index

Run from the repo root with:

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/<file>.ie
```

## By module

| Module | Example | Description |
|--------|---------|-------------|
| *(core)* | [`matrix.ie`](../examples/matrix.ie) | Matrices (`@`), `dot`, `sum` / `min` / `max` |
| `import sql` | [`sql_demo.ie`](../examples/sql_demo.ie) | Select, insert, update, delete, join |
| `import input` | [`input_demo.ie`](../examples/input_demo.ie) | `readline` + timed event poll |
| `import input` + `synth` | [`synth_input.ie`](../examples/synth_input.ie) | QWERTY jam with synth window |
| `import synth` | [`synth_demo.ie`](../examples/synth_demo.ie) | Synth window + event loop |
| `import synth` | [`synth_song.ie`](../examples/synth_song.ie) | Twinkle + drum sequencer |
| `import synth` | [`synth_bsr_sample.ie`](../examples/synth_bsr_sample.ie) | Load BSR kick into SAMP row |
| `import talk` | [`talk_demo.ie`](../examples/talk_demo.ie) | Speech-to-text (macOS) |
| `import ipc` | [`ipc_echo.ie`](../examples/ipc_echo.ie) | UDS echo server |
| `import ipc` | [`ipc_echo_client.ie`](../examples/ipc_echo_client.ie) | Client for `ipc_echo.ie` |
| `import ipc` | [`ipc_rdma.ie`](../examples/ipc_rdma.ie) | RDMA/RoCE server (Linux + NIC) |
| `import ipc` | [`ipc_rdma_client.ie`](../examples/ipc_rdma_client.ie) | Client for `ipc_rdma.ie` |
| `import lissen` | [`lissen_demo.ie`](../examples/lissen_demo.ie) | Lissen platform API + app URLs |
| `import rest` | [`rest_demo.ie`](../examples/rest_demo.ie) | HTTP GET/POST client + local server |
| `import sonicpi` | [`sonicpi_demo.ie`](../examples/sonicpi_demo.ie) | OSC cues to Sonic Pi |

## Other

| File | Description |
|------|-------------|
| [`bridge.ie`](../examples/bridge.ie) | Bridge hand dealer / HCP filter (stdlib only) |

## Module docs

| Module | Doc |
|--------|-----|
| `sql` | [SQL.md](SQL.md) |
| `input` | [INPUT.md](INPUT.md) |
| `synth` | [SYNTH.md](SYNTH.md) |
| `talk` | [TALK.md](TALK.md) |
| `ipc` | [IPC.md](IPC.md) |
| `lissen` | [LISSEN.md](LISSEN.md) |
| `rest` | [REST.md](REST.md) |
| `sonicpi` | [SONICPI.md](SONICPI.md) |
| Language & builtins | [RUNTIME_API.md](RUNTIME_API.md) |
