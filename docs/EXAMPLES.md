# Examples index

Run from the repo root with:

```bash
./shakti examples/<file>.ie
```

## By module

| Module | Example | Description |
|--------|---------|-------------|
| *(core)* | [`matrix.ie`](../examples/matrix.ie) | Matrices (`@`), `dot`, `sum` / `min` / `max` |
| `import sql` | [`sql_demo.ie`](../examples/sql_demo.ie) | Select, insert, update, delete, join |
| `import graph` | [`graph_demo.ie`](../examples/graph_demo.ie) | Knowledge graph triples, query, path |
| `import input` | [`input_demo.ie`](../examples/input_demo.ie) | `readline` + timed event poll |
| `import input` + `synth` | [`synth_input.ie`](../examples/synth_input.ie) | QWERTY jam with synth window |
| `import ipc` | [`ipc_echo.ie`](../examples/ipc_echo.ie) | UDS echo server |
| `import ipc` | [`ipc_echo_client.ie`](../examples/ipc_echo_client.ie) | Client for `ipc_echo.ie` |
| `import ipc` | [`ipc_rdma.ie`](../examples/ipc_rdma.ie) | RDMA/RoCE server (Linux + NIC) |
| `import ipc` | [`ipc_rdma_client.ie`](../examples/ipc_rdma_client.ie) | Client for `ipc_rdma.ie` |

## Other

| File | Description |
|------|-------------|
| [`bridge.ie`](../examples/bridge.ie) | Bridge hand dealer / HCP filter (stdlib only) |

## Module docs

| Module | Doc |
|--------|-----|
| `sql` | [SQL.md](SQL.md) |
| `graph` | [GRAPH.md](GRAPH.md) |
| `input` | [INPUT.md](INPUT.md) |
| `ipc` | [IPC.md](IPC.md) |
| Language & builtins | [RUNTIME_API.md](RUNTIME_API.md) |
