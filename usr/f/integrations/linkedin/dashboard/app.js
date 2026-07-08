const SESSION_KEY = "linkedin_dashboard_session";
let refreshTimer = null;
const charts = [];

function getSession() {
  const hash = new URLSearchParams(location.hash.replace(/^#/, ""));
  const fromHash = hash.get("session");
  if (fromHash) {
    localStorage.setItem(SESSION_KEY, fromHash);
    history.replaceState(null, "", location.pathname);
  }
  return localStorage.getItem(SESSION_KEY) || "";
}

function authHeaders() {
  const token = getSession();
  if (!token) return {};
  return { Authorization: `Bearer ${token}` };
}

function setStatus(msg) {
  document.getElementById("status").textContent = msg || "";
}

async function api(path) {
  const res = await fetch(path, { headers: { ...authHeaders() } });
  return res.json();
}

function show(id) {
  for (const el of document.querySelectorAll("#login-panel,#connect-panel,#dashboard")) {
    el.classList.add("hidden");
  }
  document.getElementById(id).classList.remove("hidden");
}

function linkedinStatusLabel(auth) {
  if (auth.mock) return "simulated";
  return auth.linkedin === "connected" ? "connected" : "not connected";
}

function renderAuthBar(auth) {
  const bar = document.getElementById("auth-bar");
  const parts = [];
  if (auth.mock) {
    parts.push("Mock mode");
  } else if (auth.google?.signed_in) {
    parts.push(`Signed in: ${auth.google.email || auth.google.name || "Google user"}`);
  }
  parts.push(`LinkedIn: ${linkedinStatusLabel(auth)}`);
  parts.push(`<button class="btn secondary" id="refresh-btn">Refresh</button>`);
  bar.innerHTML = parts.join(" · ");
  document.getElementById("refresh-btn")?.addEventListener("click", loadDashboard);
}

function metricCard(label, value) {
  return `<div class="metric-card"><div class="label">${label}</div><div class="value">${value}</div></div>`;
}

function destroyCharts() {
  while (charts.length) charts.pop().destroy();
}

function lineChart(canvasId, label, points, key) {
  const ctx = document.getElementById(canvasId);
  if (!ctx) return;
  const labels = points.map((p, i) => p.date || `Day ${i + 1}`);
  const data = points.map((p) => p[key] ?? 0);
  charts.push(new Chart(ctx, {
    type: "line",
    data: {
      labels,
      datasets: [{ label, data, borderColor: "#0a66c2", tension: 0.2, fill: false }],
    },
    options: {
      responsive: true,
      plugins: { legend: { display: true } },
      scales: {
        x: { title: { display: true, text: "Date" } },
        y: { title: { display: true, text: label }, beginAtZero: true },
      },
    },
  }));
}

function renderPersonal(personal) {
  const el = document.getElementById("personal-metrics");
  el.innerHTML = [
    metricCard("Connections", personal.connections ?? "—"),
    metricCard("Followers", personal.followers ?? "—"),
    metricCard("Profile views (aggregate)", personal.profile_views_aggregate?.total ?? "—"),
  ].join("");
}

function renderOrgs(orgs) {
  const root = document.getElementById("orgs");
  root.innerHTML = "";
  orgs.forEach((org, idx) => {
    const card = document.createElement("div");
    card.className = "org-card";
    const viewsId = `views-${idx}`;
    const impId = `imps-${idx}`;
    card.innerHTML = `
      <h3>${org.name}</h3>
      <div class="metrics">${metricCard("Followers", org.followers ?? "—")}</div>
      <div class="charts">
        <div class="chart-wrap"><canvas id="${viewsId}"></canvas></div>
        <div class="chart-wrap"><canvas id="${impId}"></canvas></div>
      </div>
      <h4>Recent posts</h4>
      <table>
        <thead><tr><th>Date</th><th>Post</th><th>Impressions</th><th>Reactions</th><th>Comments</th></tr></thead>
        <tbody>
          ${(org.posts || []).map((p) => `<tr><td>${p.created_at || ""}</td><td>${escapeHtml(p.text || "")}</td><td>${p.impressions ?? 0}</td><td>${p.reactions ?? 0}</td><td>${p.comments ?? 0}</td></tr>`).join("")}
        </tbody>
      </table>`;
    root.appendChild(card);
    lineChart(viewsId, "Page views", org.page_views || [], "views");
    lineChart(impId, "Impressions", org.impressions || [], "impressions");
  });
}

function escapeHtml(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

async function loadDashboard() {
  setStatus("Loading…");
  destroyCharts();
  const auth = await api("/api/auth/status");
  renderAuthBar(auth);

  if (!auth.mock && !auth.google?.signed_in) {
    show("login-panel");
    setStatus(auth.google_configured === 0 ? "Add GOOGLE_CLIENT_ID to integrations/linkedin/.env" : "");
    return;
  }

  const data = await api("/api/dashboard");
  if (!auth.mock && data.error === "linkedin_not_connected") {
    show("connect-panel");
    setStatus("Connect LinkedIn to view analytics.");
    return;
  }
  if (data.error) {
    setStatus(data.error);
    return;
  }

  show("dashboard");
  const mockBanner = document.getElementById("mock-banner");
  if (mockBanner) mockBanner.classList.toggle("hidden", !data.mock);
  renderPersonal(data.personal || {});
  renderOrgs(data.organizations || []);
  const mock = data.mock ? " (mock data)" : "";
  setStatus(`Updated ${new Date().toLocaleString()}${mock}`);
}

document.getElementById("connect-linkedin")?.addEventListener("click", async () => {
  const token = getSession();
  const res = await fetch("/auth/linkedin/connect", { headers: { Authorization: `Bearer ${token}` }, redirect: "manual" });
  if (res.type === "opaqueredirect" || res.status === 0) {
    window.location.href = "/auth/linkedin/connect";
    return;
  }
  window.location.href = "/auth/linkedin/connect";
});

loadDashboard();
refreshTimer = setInterval(loadDashboard, 15 * 60 * 1000);
