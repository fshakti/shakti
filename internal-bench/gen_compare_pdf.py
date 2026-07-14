#!/usr/bin/env python3
"""Generate a PDF cross-tech benchmark report from internal-bench/results/compare_*.json."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import letter, landscape
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

BENCH_DIR = Path(__file__).resolve().parent
RESULTS = BENCH_DIR / "results"
DEFAULT_OUT = Path(os.environ.get("HOME", "/tmp")) / "Downloads"


def git_commit(root: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=root, text=True
        ).strip()
    except Exception:
        return "unknown"


def host_line() -> str:
    try:
        cpu = subprocess.check_output(
            ["bash", "-c", "grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2"],
            text=True,
        ).strip()
        threads = subprocess.check_output(["nproc"], text=True).strip()
    except Exception:
        cpu = "unknown CPU"
        threads = "?"
    return f"Linux x86_64 · {cpu} · {threads} OpenMP threads"


def latest_json() -> Path:
    files = sorted(RESULTS.glob("compare_*.json"))
    if not files:
        raise SystemExit(f"no compare_*.json in {RESULTS} — run internal-bench/run_compare.py first")
    return files[-1]


def styled_table(data: list[list[str]]) -> Table:
    t = Table(data, repeatRows=1)
    t.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1a365d")),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
                ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                ("FONTSIZE", (0, 0), (-1, -1), 7),
                ("ALIGN", (1, 1), (-1, -1), "RIGHT"),
                ("ALIGN", (0, 0), (0, -1), "LEFT"),
                ("GRID", (0, 0), (-1, -1), 0.25, colors.grey),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f7fafc")]),
                ("TOPPADDING", (0, 0), (-1, -1), 3),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
            ]
        )
    )
    return t


def ratio_cell(fast: float | None, slow: float | None) -> str:
    if fast is None or slow is None or fast <= 0 or slow <= 0:
        return "—"
    return f"{slow / fast:.2f}×"


def build_pdf(json_path: Path, out_path: Path) -> None:
    root = BENCH_DIR.parent
    payload = json.loads(json_path.read_text(encoding="utf-8"))
    stamp = payload.get("timestamp", json_path.stem.removeprefix("compare_"))
    medians = payload.get("medians", 3)
    table: dict[str, dict[str, float]] = payload.get("results", {})

    labels = [k for k in table if table[k]]
    workloads: list[str] = []
    seen: set[str] = set()
    for rows in table.values():
        for name in rows:
            if name not in seen:
                seen.add(name)
                workloads.append(name)

    styles = getSampleStyleSheet()
    title = ParagraphStyle(
        "title",
        parent=styles["Title"],
        fontSize=18,
        spaceAfter=10,
        textColor=colors.HexColor("#1a365d"),
    )
    h2 = ParagraphStyle(
        "h2",
        parent=styles["Heading2"],
        fontSize=11,
        spaceBefore=10,
        spaceAfter=6,
        textColor=colors.HexColor("#2c5282"),
    )
    body = ParagraphStyle("body", parent=styles["Normal"], fontSize=8, leading=11)

    doc = SimpleDocTemplate(
        str(out_path),
        pagesize=landscape(letter),
        leftMargin=0.5 * inch,
        rightMargin=0.5 * inch,
        topMargin=0.5 * inch,
        bottomMargin=0.5 * inch,
    )
    story: list = []

    story.append(Paragraph("Shakti External Benchmark — Cross-Tech Comparison", title))
    story.append(
        Paragraph(
            f"Generated {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')} · "
            f"run {stamp} · commit <b>{git_commit(root)}</b><br/>"
            f"{host_line()}<br/>"
            f"Median of {medians} timed runs per runner. "
            f"Shakti: <font face='Courier'>make prod-speed</font>. "
            f"Python: NumPy / pandas / sqlite3 / DuckDB.",
            body,
        )
    )
    story.append(Spacer(1, 0.15 * inch))

    header = ["workload (seconds)"] + labels + ["best", "Shakti vs NumPy"]
    rows = [header]
    shakti_keys = [k for k in labels if k.startswith("shakti_")]
    numpy_key = "numpy" if "numpy" in table else None

    for case in workloads:
        cells: list[str] = [case]
        secs: dict[str, float] = {}
        for label in labels:
            sec = table.get(label, {}).get(case)
            secs[label] = sec
            cells.append(f"{sec:.4f}" if sec is not None else "—")
        valid = {k: v for k, v in secs.items() if v is not None and v > 0}
        if valid:
            best_label = min(valid, key=valid.get)
            cells.append(best_label.replace("_", " "))
        else:
            cells.append("—")
        shakti_sec = next((secs[k] for k in shakti_keys if secs.get(k)), None)
        np_sec = secs.get(numpy_key) if numpy_key else None
        cells.append(ratio_cell(shakti_sec, np_sec) if shakti_sec and np_sec else "—")
        rows.append(cells)

    story.append(Paragraph("All workloads", h2))
    story.append(styled_table(rows))

    sql_cases = [c for c in workloads if c.startswith("sql_")]
    vec_cases = [c for c in workloads if c.startswith("vec_")]
    for section, cases, title_text in (
        ("sql", sql_cases, "SQL workloads"),
        ("vec", vec_cases, "Vector workloads"),
    ):
        if not cases:
            continue
        story.append(Spacer(1, 0.1 * inch))
        story.append(Paragraph(title_text, h2))
        sub = [header]
        for case in cases:
            cells = [case]
            secs = {}
            for label in labels:
                sec = table.get(label, {}).get(case)
                secs[label] = sec
                cells.append(f"{sec:.4f}" if sec is not None else "—")
            valid = {k: v for k, v in secs.items() if v is not None and v > 0}
            cells.append(min(valid, key=valid.get).replace("_", " ") if valid else "—")
            shakti_sec = next((secs[k] for k in shakti_keys if secs.get(k)), None)
            np_sec = secs.get(numpy_key) if numpy_key else None
            cells.append(ratio_cell(shakti_sec, np_sec) if shakti_sec and np_sec else "—")
            sub.append(cells)
        story.append(styled_table(sub))

    story.append(Spacer(1, 0.15 * inch))
    story.append(
        Paragraph(
            "<b>Notes</b><br/>"
            "• Lower seconds is better. Shakti SQL uses in-memory <font face='Courier'>table</font> + "
            "<font face='Courier'>import sql</font>.<br/>"
            "• pandas/SQLite/DuckDB runners use equivalent Python in-memory workloads.<br/>"
            "• Source: <font face='Courier'>internal-bench/run_compare.py</font> → "
            f"<font face='Courier'>{json_path.name}</font>",
            body,
        )
    )

    doc.build(story)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=Path, help="compare_*.json (default: latest in results/)")
    ap.add_argument(
        "--output",
        type=Path,
        help=f"PDF path (default: ~/Downloads/shakti-external-bench-<stamp>.pdf)",
    )
    args = ap.parse_args()

    json_path = args.json or latest_json()
    stamp = json.loads(json_path.read_text(encoding="utf-8")).get(
        "timestamp", json_path.stem.removeprefix("compare_")
    )
    out_path = args.output or (DEFAULT_OUT / f"shakti-external-bench-{stamp}.pdf")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    build_pdf(json_path, out_path)
    print(f"Wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
