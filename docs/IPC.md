# IPC module (`import ipc`)

Sync and poll-based async message passing between shakti processes. Messages are length-prefixed strings (4-byte big-endian header + payload, max 1 MiB).

## Transport selection

| Target | Default | Override |
|--------|---------|----------|
| `127.0.0.1`, `localhost`, `::1` | Unix domain socket at `$SHAKTI_IPC_DIR/shakti-<port>.sock` (default `/tmp`) | `transport="tcp"` |
| Remote host | RDMA (RoCE v2 via `librdmacm`) when a device exists, else TCP | `transport="tcp"` or `transport="rdma"` |

Environment:

- `SHAKTI_IPC_DIR` — UDS socket directory (default `/tmp`)
- `SHAKTI_IPC_TRANSPORT` — default transport: `auto`, `tcp`, `uds`, `rdma`

Build:

- `SHAKTI_IPC=1` (default) — socket IPC in the binary
- `SHAKTI_RDMA=1` (default on Linux) — links `ipc_rdma.c` when `/usr/include/infiniband/verbs.h` and `rdma/rdma_cma.h` exist; adds `-lrdmacm -libverbs`

RoCE setup (Linux): install `rdma-core`, `libibverbs-dev`, `librdmacm-dev`; configure the NIC (`rdma link`, `ibv_devinfo`). RoCE v2 is negotiated by the kernel/RDMA stack; no extra GID setup in shakti v1.

## Sync API

```ie
import ipc

srv : ipc.listen(9000)
conn : ipc.accept(srv)
ipc.send(conn, "hello")
msg : ipc.recv(conn)
ipc.close(conn)
ipc.close(srv)

c : ipc.connect("127.0.0.1", 9000)
ipc.send(c, "ping")
print(ipc.recv(c))
ipc.close(c)
```

## Async API

Same model as `input(ms)` — poll-based, not coroutines.

```ie
import ipc

c : ipc.connect("127.0.0.1", 9000)
ipc.set_nonblock(c, 1)
ready : ipc.poll([c], 50)
if len(ready) > 0:
    msg : ipc.recv_nowait(c)
```

`ipc.recv_nowait(h)` returns `""` when no full message is available.

## Shared memory (local bulk)

```ie
tok : ipc.shm_open("buf", 1048576)
ipc.shm_close(tok)
```

POSIX `shm_open` + `mmap`; use for large zero-copy regions between co-located processes.

## RDMA

```ie
if ipc.rdma_available():
    srv : ipc.listen(19100, "0.0.0.0", "rdma")
    conn : ipc.accept(srv)
    ...
```

See [`examples/ipc_rdma.ie`](../examples/ipc_rdma.ie).

## Native builtins

| Builtin | Role |
|---------|------|
| `ipc_listen(port[, host, transport])` | Listen handle |
| `ipc_accept(listen_h)` | Connection handle |
| `ipc_connect(host, port[, transport])` | Connection handle |
| `ipc_send(h, str)` | Send message |
| `ipc_recv(h)` | Blocking receive |
| `ipc_recv_nowait(h)` | Non-blocking receive |
| `ipc_set_nonblock(h, on)` | Toggle non-blocking mode |
| `ipc_poll(handles, timeout_ms)` | Ready handle list |
| `ipc_close(h)` | Close handle |
| `ipc_shm_open(name, size)` | Shared memory token |
| `ipc_shm_close(token)` | Unmap and unlink |
| `ipc_rdma_available()` | `1` if RDMA device present |

## Examples

| File | Description |
|------|-------------|
| [`examples/ipc_echo.ie`](../examples/ipc_echo.ie) | UDS echo server |
| [`examples/ipc_echo_client.ie`](../examples/ipc_echo_client.ie) | UDS client |
| [`examples/ipc_rdma.ie`](../examples/ipc_rdma.ie) | RDMA server |
| [`examples/ipc_rdma_client.ie`](../examples/ipc_rdma_client.ie) | RDMA client |

## Benchmarks

Regression cases in `benchmarks/suites/ipc.ie` (included in `make bench`):

| Case | Description |
|------|-------------|
| `ipc_uds_roundtrip_bench` | 200 send/recv round trips over UDS (256 B payload) |
| `ipc_tcp_roundtrip_bench` | 200 send/recv round trips over TCP loopback |
| `ipc_shm_cycle_bench` | 500 `shm_open` / `shm_close` cycles |
| `ipc_rdma_available_bench` | 100k `ipc_rdma_available()` calls |

Refresh baseline after IPC changes: `make bench-update`.

