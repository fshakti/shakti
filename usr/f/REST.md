# REST (`import rest`)

HTTP **client** (via `curl` on `PATH`) and a minimal in-process **HTTP/1.1 server** (TCP).

## Requirements

- **Client:** `curl` on `PATH`
- **Server:** available on Linux and macOS standalone builds (not WASM)

Optional bearer token for client requests:

```bash
export SHAKTI_REST_TOKEN=...
```

## Client

```ie
import rest

resp : rest.get("https://api.example.com/items")
if rest.ok(resp):
    print(rest.status(resp))
    print(rest.json(resp))
else:
    print("HTTP", rest.status(resp), rest.text(resp))

resp2 : rest.post_json("https://api.example.com/items", {"name": "alpha"})
```

| Function | Description |
|----------|-------------|
| `rest.get(url)` | GET request |
| `rest.post(url[, body, content_type])` | POST request |
| `rest.put(url[, body, content_type])` | PUT request |
| `rest.delete(url)` | DELETE request |
| `rest.post_json(url, obj)` | POST with `application/json` body |
| `rest.put_json(url, obj)` | PUT with `application/json` body |
| `rest.request(method, url[, body, content_type, headers])` | Generic request; `headers` is a dict |
| `rest.status(resp)` | HTTP status code |
| `rest.ok(resp)` | `True` when status is 2xx |
| `rest.json(resp)` | Parsed body (JSON object/list or string) |
| `rest.text(resp)` | Raw response body string |

Response dict shape:

```json
{"status": 200, "body": ..., "raw": "...", "headers": {"Content-Type": "..."}}
```

## Server

Single-threaded, one request per accepted connection. Supports `Content-Length` request bodies only (no chunked transfer).

```ie
import rest

srv : rest.listen(8080)
conn : rest.accept(srv)
req : rest.read(conn)
print(req["method"], req["path"])
rest.respond_json(conn, 200, {"ok": 1})
rest.close(conn)
rest.close(srv)
```

| Function | Description |
|----------|-------------|
| `rest.listen(port[, host])` | Listen on TCP (default host `127.0.0.1`) |
| `rest.accept(listen_h)` | Accept connection handle |
| `rest.read(conn)` | Read request → `{method, path, body, headers}` |
| `rest.write(conn, status[, body, content_type])` | Send HTTP/1.1 response |
| `rest.respond_json(conn, status, obj)` | JSON response helper |
| `rest.close(h)` | Close listen or connection handle |

## Example

[`examples/rest_demo.ie`](../examples/rest_demo.ie) — local server spawn + GET/POST client calls.

## Limitations

- Client shells out to `curl` per request (latency dominated by process spawn, like `import lissen`).
- Server is not production-grade: no TLS, no HTTP/2, no chunked encoding, no keep-alive.
- Not available in WASM builds.

## See also

- [LISSEN.md](LISSEN.md) — Lissen-specific tRPC client
- [IPC.md](IPC.md) — length-prefixed TCP/UDS messaging (not HTTP)
- [THIRD_PARTY.md](THIRD_PARTY.md) — `curl` dependency
