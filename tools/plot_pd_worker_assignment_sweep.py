#!/usr/bin/env python3
"""Plot a small PD prefill/decode worker assignment sweep."""

from __future__ import annotations

import argparse
import csv
from html import escape
from pathlib import Path
from typing import Any


METRICS = (
    ("ttft_mean_ns", "Mean TTFT", "ms", 1.0e-6, False),
    ("tpot_mean_ns", "Mean TPOT", "ms", 1.0e-6, False),
    ("e2e_mean_ns", "Mean E2E", "ms", 1.0e-6, False),
    (
        "output_token_throughput_tokens_per_sec",
        "Output Token Throughput",
        "tokens/s",
        1.0,
        True,
    ),
)

FAMILY_COLORS = {
    "calibrated_reference": "#303030",
    "balanced_worker_scaling": "#1f77b4",
    "prefill_worker_scaling": "#2ca02c",
    "decode_worker_scaling": "#d62728",
}

FAMILY_LABELS = {
    "calibrated_reference": "calibrated",
    "balanced_worker_scaling": "balanced",
    "prefill_worker_scaling": "prefill-heavy",
    "decode_worker_scaling": "decode-heavy",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        default="build/pd_4gpu_worker_assignment_sweep/summary.csv",
        help="Parsed sweep summary CSV from tools/parse_serving_outputs.py.",
    )
    parser.add_argument(
        "--output-dir",
        default="build/pd_4gpu_worker_assignment_sweep/plots",
        help="Directory for generated SVG plots.",
    )
    return parser.parse_args()


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{path} has no rows")
    return rows


def to_float(row: dict[str, Any], column: str) -> float:
    value = row.get(column, "")
    return float(value) if value not in {"", None} else 0.0


def to_int(row: dict[str, Any], column: str) -> int:
    value = row.get(column, "")
    return int(float(value)) if value not in {"", None} else 0


def assignment_label(row: dict[str, str]) -> str:
    return f"P{to_int(row, 'pd_prefill_workers')} / D{to_int(row, 'pd_decode_workers')}"


def case_sort_key(row: dict[str, str]) -> tuple[int, int, int]:
    total = to_int(row, "total_logical_gpus")
    decode = to_int(row, "pd_decode_workers")
    prefill = to_int(row, "pd_prefill_workers")
    return (total, decode, prefill)


def metric_value(row: dict[str, str], column: str, scale: float) -> float:
    return to_float(row, column) * scale


def metric_improvement_pct(
    row: dict[str, str],
    baseline: dict[str, str],
    column: str,
    higher_is_better: bool,
) -> float:
    base = to_float(baseline, column)
    value = to_float(row, column)
    if base == 0.0:
        return 0.0
    if higher_is_better:
        return (value - base) / base * 100.0
    return (base - value) / base * 100.0


def format_metric(value: float, unit: str) -> str:
    if unit == "tokens/s":
        return f"{value:.2f}"
    return f"{value:.2f}"


def format_pct(value: float) -> str:
    if abs(value) >= 10.0:
        return f"{value:.1f}%"
    return f"{value:.2f}%"


def family_color(row: dict[str, str]) -> str:
    return FAMILY_COLORS.get(row.get("scale_family", ""), "#666666")


def family_label(row: dict[str, str]) -> str:
    family = row.get("scale_family", "")
    return FAMILY_LABELS.get(family, family.replace("_", " "))


def percent_domain(values: list[float]) -> tuple[float, float]:
    low = min(0.0, min(values))
    high = max(0.0, max(values))
    if low == high:
        return -1.0, 1.0
    padding = max((high - low) * 0.12, 0.10)
    return low - padding, high + padding


def write_assignment_svg(path: Path, rows: list[dict[str, str]]) -> None:
    rows = sorted(rows, key=case_sort_key)
    baseline = next(
        (
            row
            for row in rows
            if row.get("calibration_scope") == "calibrated_reference"
        ),
        rows[0],
    )

    width = 1180
    left = 88
    right = 54
    top = 116
    panel_gap = 90
    panel_height = 162
    height = top + len(METRICS) * panel_height + (len(METRICS) - 1) * panel_gap + 96
    bar_gap = 16
    plot_width = width - left - right
    bar_width = (plot_width - bar_gap * (len(rows) - 1)) / len(rows)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="{width}" height="{height}" fill="#ffffff" />',
        (
            "<style>text{font-family:Arial,Helvetica,sans-serif;}"
            ".muted{fill:#626262}.grid{stroke:#e9e9e9}.axis{stroke:#333}"
            "</style>"
        ),
        (
            f'<text x="{width / 2}" y="34" text-anchor="middle" '
            'font-size="24" font-weight="700">PD Worker Assignment Sweep '
            '(<= 4 GPUs)</text>'
        ),
        (
            f'<text x="{width / 2}" y="58" text-anchor="middle" '
            'font-size="13" class="muted">TP is fixed at 1, so each worker '
            "uses one logical GPU. Only P1 / D1 is calibrated; all other "
            "assignments are extrapolated.</text>"
        ),
        (
            f'<text x="{width / 2}" y="77" text-anchor="middle" '
            'font-size="13" class="muted">Bars show improvement relative to '
            "the calibrated reference; positive values are better. Raw values "
            "are printed above each bar.</text>"
        ),
    ]

    for panel_index, (column, title, unit, scale, higher_is_better) in enumerate(
        METRICS
    ):
        panel_top = top + panel_index * (panel_height + panel_gap)
        improvements = [
            metric_improvement_pct(row, baseline, column, higher_is_better)
            for row in rows
        ]
        y_min, y_max = percent_domain(improvements)

        def sy(value: float) -> float:
            return (
                panel_top
                + panel_height
                - (value - y_min) / (y_max - y_min) * panel_height
            )

        baseline_y = sy(0.0)
        svg.append(
            f'<text x="{left}" y="{panel_top - 18}" font-size="17" '
            f'font-weight="700">{escape(title)} improvement</text>'
        )
        svg.append(
            f'<text x="{left + plot_width}" y="{panel_top - 18}" '
            'text-anchor="end" font-size="12" class="muted">positive is '
            "better</text>"
        )

        tick_count = 4
        for tick in range(tick_count + 1):
            value = y_min + (y_max - y_min) * tick / tick_count
            y = sy(value)
            svg.append(
                f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_width}" '
                f'y2="{y:.1f}" class="grid" />'
            )
            svg.append(
                f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" '
                f'font-size="11" class="muted">{format_pct(value)}</text>'
            )

        svg.append(
            f'<line x1="{left}" y1="{baseline_y:.1f}" '
            f'x2="{left + plot_width}" y2="{baseline_y:.1f}" '
            'stroke="#222" stroke-width="1.5" stroke-dasharray="6 5" />'
        )
        svg.append(
            f'<text x="{left + plot_width + 8}" y="{baseline_y + 4:.1f}" '
            'font-size="11" class="muted">baseline</text>'
        )

        for index, row in enumerate(rows):
            improvement = improvements[index]
            x = left + index * (bar_width + bar_gap)
            y = sy(max(0.0, improvement))
            h = abs(sy(improvement) - baseline_y)
            if improvement >= 0:
                y = sy(improvement)
            else:
                y = baseline_y
            svg.append(
                f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_width:.1f}" '
                f'height="{max(h, 1.0):.1f}" fill="{family_color(row)}" '
                'rx="3" />'
            )
            value = metric_value(row, column, scale)
            label_y = y - 8 if improvement >= 0 else y + h + 16
            svg.append(
                f'<text x="{x + bar_width / 2:.1f}" y="{label_y:.1f}" '
                'text-anchor="middle" font-size="11" fill="#222">'
                f'{format_metric(value, unit)}</text>'
            )
            svg.append(
                f'<text x="{x + bar_width / 2:.1f}" '
                f'y="{panel_top + panel_height + 22}" text-anchor="middle" '
                f'font-size="12">{escape(assignment_label(row))}</text>'
            )
            svg.append(
                f'<text x="{x + bar_width / 2:.1f}" '
                f'y="{panel_top + panel_height + 38}" text-anchor="middle" '
                'font-size="11" class="muted">'
                f'{to_int(row, "total_logical_gpus")} GPUs, '
                f'{escape(family_label(row))}</text>'
            )

    legend_x = left
    legend_y = height - 46
    legend_items = (
        ("calibrated_reference", "Calibrated reference"),
        ("balanced_worker_scaling", "Balanced"),
        ("prefill_worker_scaling", "Prefill-heavy"),
        ("decode_worker_scaling", "Decode-heavy"),
    )
    for index, (family, label) in enumerate(legend_items):
        x = legend_x + index * 210
        svg.append(
            f'<rect x="{x}" y="{legend_y}" width="12" height="12" '
            f'fill="{FAMILY_COLORS[family]}" rx="2" />'
        )
        svg.append(
            f'<text x="{x + 18}" y="{legend_y + 11}" font-size="12">'
            f'{escape(label)}</text>'
        )

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def write_ranked_svg(path: Path, rows: list[dict[str, str]]) -> None:
    width = 900
    row_height = 54
    top = 86
    left = 245
    right = 120
    bottom = 42
    rows = sorted(rows, key=lambda row: to_float(row, "e2e_mean_ns"))
    height = top + len(rows) * row_height + bottom
    plot_width = width - left - right
    values = [to_float(row, "e2e_mean_ns") * 1.0e-6 for row in rows]
    v_min = min(values)
    v_max = max(values)
    if v_min == v_max:
        v_min -= 1.0
        v_max += 1.0
    pad = max((v_max - v_min) * 0.10, 0.50)
    v_min -= pad
    v_max += pad

    def sx(value: float) -> float:
        return left + (value - v_min) / (v_max - v_min) * plot_width

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="{width}" height="{height}" fill="#ffffff" />',
        (
            "<style>text{font-family:Arial,Helvetica,sans-serif;}"
            ".muted{fill:#626262}.grid{stroke:#e9e9e9}</style>"
        ),
        (
            f'<text x="{width / 2}" y="34" text-anchor="middle" '
            'font-size="22" font-weight="700">Ranked by Mean E2E Latency</text>'
        ),
        (
            f'<text x="{width / 2}" y="57" text-anchor="middle" '
            'font-size="13" class="muted">Lower is better; all non-P1/D1 '
            "rows are extrapolated.</text>"
        ),
    ]

    tick_count = 4
    for tick in range(tick_count + 1):
        value = v_min + (v_max - v_min) * tick / tick_count
        x = sx(value)
        svg.append(
            f'<line x1="{x:.1f}" y1="{top - 8}" x2="{x:.1f}" '
            f'y2="{height - bottom}" class="grid" />'
        )
        svg.append(
            f'<text x="{x:.1f}" y="{height - bottom + 22}" '
            'text-anchor="middle" font-size="11" class="muted">'
            f'{value:.1f}</text>'
        )

    for index, row in enumerate(rows):
        y = top + index * row_height
        value = to_float(row, "e2e_mean_ns") * 1.0e-6
        x = sx(value)
        svg.append(
            f'<text x="{left - 14}" y="{y + 24}" text-anchor="end" '
            'font-size="13" font-weight="700">'
            f'{escape(assignment_label(row))}</text>'
        )
        svg.append(
            f'<text x="{left - 14}" y="{y + 41}" text-anchor="end" '
            'font-size="11" class="muted">'
            f'{to_int(row, "total_logical_gpus")} GPUs, '
            f'{escape(family_label(row))}</text>'
        )
        svg.append(
            f'<line x1="{left}" y1="{y + 26}" x2="{x:.1f}" '
            f'y2="{y + 26}" stroke="{family_color(row)}" stroke-width="7" '
            'stroke-linecap="round" />'
        )
        svg.append(
            f'<circle cx="{x:.1f}" cy="{y + 26}" r="6" '
            f'fill="{family_color(row)}" />'
        )
        svg.append(
            f'<text x="{x + 11:.1f}" y="{y + 30}" font-size="12">'
            f'{value:.2f} ms</text>'
        )

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    rows = load_rows(Path(args.input))
    write_assignment_svg(output_dir / "pd_4gpu_worker_assignment.svg", rows)
    write_ranked_svg(output_dir / "pd_4gpu_e2e_ranked.svg", rows)
    print(f"Wrote plots to {output_dir}")


if __name__ == "__main__":
    main()
