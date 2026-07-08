#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { apiBase, apiGet } from "./client.js";

type Snapshot = {
  personal?: { connections?: number; followers?: number; profile_views_aggregate?: { total?: number } };
  organizations?: Array<{
    id?: string;
    name?: string;
    followers?: number;
    page_views?: unknown[];
    impressions?: unknown[];
    posts?: unknown[];
  }>;
  error?: string;
};

const server = new McpServer({ name: "linkedin", version: "0.1.0" });

server.tool("linkedin_auth_status", "Google + LinkedIn connection status", {}, async () => {
  const data = await apiGet("/api/auth/status");
  return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
});

server.tool("linkedin_connect", "Open the dashboard to sign in (server must be running)", {}, async () => {
  return {
    content: [
      {
        type: "text",
        text: `Open ${apiBase()}/ in your browser. Sign in with Google, then connect LinkedIn. Set LINKEDIN_DEFAULT_SESSION to the session token from localStorage for MCP API calls.`,
      },
    ],
  };
});

server.tool("get_dashboard_snapshot", "Full dashboard JSON snapshot", {}, async () => {
  const data = (await apiGet("/api/dashboard")) as Snapshot;
  return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
});

server.tool("get_connections_count", "Personal network connection count", {}, async () => {
  const data = (await apiGet("/api/dashboard")) as Snapshot;
  const n = data.personal?.connections ?? null;
  return { content: [{ type: "text", text: JSON.stringify({ connections: n }) }] };
});

server.tool("get_personal_analytics", "Personal followers and profile view aggregates", {}, async () => {
  const data = (await apiGet("/api/dashboard")) as Snapshot;
  return { content: [{ type: "text", text: JSON.stringify(data.personal ?? {}, null, 2) }] };
});

server.tool(
  "list_managed_organizations",
  "List company pages you administer",
  {},
  async () => {
    const data = (await apiGet("/api/dashboard")) as Snapshot;
    const orgs = (data.organizations ?? []).map((o) => ({ id: o.id, name: o.name, followers: o.followers }));
    return { content: [{ type: "text", text: JSON.stringify(orgs, null, 2) }] };
  },
);

server.tool(
  "get_organization_analytics",
  "Followers, views, impressions, and posts for one org by name or id",
  { org: z.string().describe("Organization name or id") },
  async ({ org }) => {
    const data = (await apiGet("/api/dashboard")) as Snapshot;
    const match = (data.organizations ?? []).find(
      (o) => o.id === org || o.name?.toLowerCase() === org.toLowerCase(),
    );
    if (!match) {
      return { content: [{ type: "text", text: JSON.stringify({ error: "organization not found", org }) }] };
    }
    return { content: [{ type: "text", text: JSON.stringify(match, null, 2) }] };
  },
);

const transport = new StdioServerTransport();
await server.connect(transport);
