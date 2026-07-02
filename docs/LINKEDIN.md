# LinkedIn integration (`integrations/linkedin`)

Shakti-based dashboard and MCP bridge for LinkedIn personal and company-page analytics.

## Requirements

- Built `./shakti` binary (`make prod` from repo root)
- `curl` on `PATH` (for `import rest` client)
- `python3` and `openssl` (OAuth helpers)
- LinkedIn **Community Management API** approval for live data
- Google Cloud OAuth client for dashboard sign-in

## Google Cloud setup

1. [Google Cloud Console](https://console.cloud.google.com/) → APIs & Services → Credentials.
2. Create **OAuth 2.0 Client ID** (Web application).
3. Authorized redirect URI: `http://localhost:3847/auth/google/callback`
4. Set `GOOGLE_CLIENT_ID` and `GOOGLE_CLIENT_SECRET` in `integrations/linkedin/.env`.
5. Optional: `ALLOWED_GOOGLE_EMAIL=you@gmail.com` to restrict access.

## LinkedIn Developer Portal setup

1. Create or claim an **Organization LinkedIn Page**.
2. Create an app at [linkedin.com/developers](https://www.linkedin.com/developers/).
3. Verify the app with a page administrator.
4. Request **Community Management API** (Development Tier).
5. OAuth redirect: `http://localhost:3847/auth/linkedin/callback`
6. Scopes: `r_1st_connections_size`, `r_member_profileAnalytics`, `rw_organization_admin`, `r_organization_social`
7. Set `LINKEDIN_CLIENT_ID` and `LINKEDIN_CLIENT_SECRET` in `.env`.

## Run

```bash
export SHAKTI_LIB=$PWD/src/lib
cp integrations/linkedin/.env.example integrations/linkedin/.env
# edit .env — set LINKEDIN_MOCK=true for development
./integrations/linkedin/run.sh
```

Visit http://127.0.0.1:3847

1. **Sign in with Google**
2. **Connect LinkedIn**
3. View dashboard metrics

## API routes

| Route | Description |
|-------|-------------|
| `GET /` | Dashboard UI |
| `GET /static/*` | CSS/JS assets |
| `GET /auth/google` | Start Google OAuth |
| `GET /auth/google/callback` | Google OAuth callback |
| `GET /auth/linkedin/connect` | Start LinkedIn OAuth (requires `Authorization: Bearer <session>`) |
| `GET /auth/linkedin/callback` | LinkedIn OAuth callback |
| `GET /api/auth/status` | Auth state JSON |
| `GET /api/dashboard` | Full analytics snapshot |

Sessions are bearer tokens stored in browser `localStorage` after OAuth redirect (`/#session=...`).

LinkedIn tokens: `~/.shakti/linkedin/tokens/{googleSub}.json`

## API limitations

| Metric | API support |
|--------|-------------|
| Connection count | Yes |
| Profile viewer identities | **No** — aggregate profile-view metrics only |
| Company followers, views, impressions, posts | Yes (with Community Management API) |

## MCP bridge

See [integrations/linkedin/README.md](../integrations/linkedin/README.md). The Shakti server must be running; MCP tools proxy to `http://127.0.0.1:3847/api/*`.

## Mock mode

`LINKEDIN_MOCK=true` returns fixture data from `mock_data.ie` without calling LinkedIn. `/api/dashboard` is available without authentication in mock mode.

## Module files

Shakti modules live as flat `.ie` files in `integrations/linkedin/` (e.g. `http_helpers.ie`, `token_store.ie`, `snapshot.ie`). Import them from `dashboard_server.ie` with `import http_helpers`, etc.

## See also

- [REST.md](REST.md) — Shakti HTTP client/server module
- [integrations/linkedin/README.md](../integrations/linkedin/README.md) — quick start
