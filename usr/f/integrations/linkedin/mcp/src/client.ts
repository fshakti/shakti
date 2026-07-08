const base = process.env.SHAKTI_LINKEDIN_API ?? "http://127.0.0.1:3847";

function sessionHeaders(): Record<string, string> {
  const session = process.env.LINKEDIN_DEFAULT_SESSION;
  if (!session) return {};
  return { Authorization: `Bearer ${session}` };
}

export async function apiGet(path: string): Promise<unknown> {
  const res = await fetch(`${base}${path}`, { headers: sessionHeaders() });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`HTTP ${res.status}: ${text}`);
  }
  return res.json();
}

export function apiBase(): string {
  return base;
}
