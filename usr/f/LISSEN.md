# Lissen (`import lissen`)

Client for the [Lissen](https://www.lissen.com/) fan music platform API.

Lissen combines streaming with artist–fan engagement (tickets, merch, exclusive drops). The Shakti module talks to Lissen’s tRPC backend and provides helpers for app/web URLs.

## Requirements

- `curl` on `PATH` (HTTPS)
- Optional: bearer token for authenticated endpoints (`SHAKTI_LISSEN_TOKEN`)

## Quick start

```bash
export SHAKTI_LIB=$PWD/src/lib
./shakti examples/lissen_demo.ie
```

With a session token from the Lissen app:

```bash
export SHAKTI_LISSEN_TOKEN=your_token_here
./shakti examples/lissen_demo.ie
```

## API

| Function | Description |
|----------|-------------|
| `lissen.query(path, input_json)` | tRPC query (GET); `input_json` is a JSON string (default `"{}"`) |
| `lissen.query_dict(path, dict)` | tRPC query, encoding a dict as JSON |
| `lissen.mutation(path, input_json)` | tRPC mutation (POST) |
| `lissen.mutation_dict(path, dict)` | tRPC mutation, encoding a dict as JSON |
| `lissen.me()` | Shorthand for `query("user.me")` |
| `lissen.set_token(tok)` / `lissen.token()` | Set / get bearer token |
| `lissen.set_api_base(url)` / `lissen.api_base()` | Override / read API host |
| `lissen.app_url(kind, id)` / `lissen.home_url()` | Build `https://app.lissen.com/...` URL |
| `lissen.web_url(path)` | Build `https://www.lissen.com/...` URL |
| `lissen.open(url)` | Open URL in the system browser |
| `lissen.unwrap(resp)` | Unwrap tRPC `result.data` (and `.json`) from a response dict |
| `lissen.api_error(resp)` | Return the tRPC `error` dict, or `0` if none |

## Environment

| Variable | Purpose |
|----------|---------|
| `SHAKTI_LISSEN_TOKEN` | Bearer token for authenticated API calls |
| `SHAKTI_LISSEN_API` | API base URL (default `https://api.lissenprod.lissen.live`) |

## Example

```ie
import lissen

resp : lissen.me()
if lissen.api_error(resp):
    print(lissen.api_error(resp)["message"])
else:
    print(lissen.unwrap(resp))
```

## Notes

- Lissen does not publish a public developer SDK; this module uses the same tRPC HTTP transport as the Lissen app.
- Procedure names beyond `user.me` may require authentication or change without notice.
- Playlist import and streaming remain in the Lissen mobile/web apps — use `lissen.open()` to send users there.

See also [examples/lissen_demo.ie](../examples/lissen_demo.ie).
