# LinkedIn integration

Analytics dashboard and MCP bridge for personal LinkedIn metrics and company pages you manage.

## Quick start (mock data)

```bash
cd ~/workspace/shakti
make prod
export SHAKTI_LIB=$PWD/src/lib
export LINKEDIN_MOCK=true
chmod +x integrations/linkedin/run.sh
./integrations/linkedin/run.sh
```

Or: `./shakti integrations/linkedin/dashboard_server.ie` from repo root.

Open http://127.0.0.1:3847 — with mock mode, `/api/dashboard` returns sample data (no OAuth required if you pass a dummy session; use browser flow for full UI).

## Live API

1. Configure Google OAuth and LinkedIn Developer app — see [docs/LINKEDIN.md](../../docs/LINKEDIN.md).
2. Copy `.env.example` to `.env` and fill in credentials.
3. Set `LINKEDIN_MOCK=false`.
4. Run `./integrations/linkedin/run.sh`.

## MCP (Cursor)

1. Keep the Shakti server running.
2. Build the bridge:

```bash
cd integrations/linkedin/mcp
npm install
npm run build
```

3. Add to Cursor `mcp.json`:

```json
{
  "mcpServers": {
    "linkedin": {
      "command": "node",
      "args": ["/home/f/workspace/shakti/integrations/linkedin/mcp/dist/index.js"],
      "env": {
        "SHAKTI_LINKEDIN_API": "http://127.0.0.1:3847",
        "LINKEDIN_DEFAULT_SESSION": "your-session-token-from-browser-localStorage"
      }
    }
  }
}
```

## Layout

| Path | Role |
|------|------|
| `*.ie` (flat modules) | OAuth, API client, snapshot builder |
| `dashboard_server.ie` | HTTP server on port 3847 |
| `server/dashboard_server.ie` | Entry shim (see `run.sh`) |
| `dashboard/` | Static UI |
| `mcp/` | TypeScript MCP bridge |
