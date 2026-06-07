#!/usr/bin/env python3
"""Plot the compact extrapolated PD GPU sweep from parsed summary CSV."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from html import escape
from pathlib import Path
from typing import Any


FAMILY_LABELS = {
    "calibrated_reference": "Calibrated reference",
    "balanced_worker_scaling": "Balanced workers",
    "balanced_tp_scaling": "Balanced TP",
    "prefill_worker_scaling": "Prefill workers",
    "decode_worker_scaling": "Decode workers",
    "prefill_tp_scaling": "Prefill TP",
    "decode_tp_scaling": "Decode TP",
    "mixed_balanced_scaling": "Mixed balanced",
}

FAMILY_COLORS = {
    "calibrated_reference": "#2b2b2b",
    "balanced_worker_scaling": "#1f77b4",
    "balanced_tp_scaling": "#8c564b",
    "prefill_worker_scaling": "#2ca02c",
    "decode_worker_scaling": "#d62728",
    "prefill_tp_scaling": "#17becf",
    "decode_tp_scaling": "#9467bd",
    "mixed_balanced_scaling": "#ff7f0e",
}

FAMILY_ORDER = (
    "balanced_worker_scaling",
    "mixed_balanced_scaling",
    "prefill_worker_scaling",
    "decode_worker_scaling",
    "balanced_tp_scaling",
    "prefill_tp_scaling",
    "decode_tp_scaling",
)

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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        default="build/pd_extrapolated_sweep_compact/summary.csv",
        help="Parsed sweep summary CSV from tools/parse_serving_outputs.py.",
    )
    parser.add_argument(
        "--output-dir",
        default="build/pd_extrapolated_sweep_compact/plots",
        help="Directory for generated SVG plots.",
    )
    return parser.parse_args()


def to_float(row: dict[str, Any], column: str) -> float:
    value = row.get(column, "")
    return float(value) if value not in {"", None} else 0.0


def to_int(row: dict[str, Any], column: str) -> int:
    value = row.get(column, "")
    return int(float(value)) if value not in {"", None} else 0


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def family_label(family: str) -> str:
    return FAMILY_LABELS.get(family, family.replace("_", " ").title())


def family_color(family: str) -> str:
    return FAMILY_COLORS.get(family, "#555555")


def format_number(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    if abs(value) >= 100:
        return f"{value:.1f}"
    if abs(value) >= 10:
        return f"{value:.2f}".rstrip("0").rstrip(".")
    return f"{value:.3f}".rstrip("0").rstrip(".")


def case_short_label(row: dict[str, str]) -> str:
    return (
        f"P {to_int(row, 'pd_prefill_workers')}xTP{to_int(row, 'pd_prefill_tp_degree')} / "
        f"D {to_int(row, 'pd_decode_workers')}xTP{to_int(row, 'pd_decode_tp_degree')}"
    )


def metric_improvement_pct(
    row: dict[str, str],
    baseline: dict[str, str],
    column: str,
    higher_is_better: bool,
) -> float:
    baseline_value = to_float(baseline, column)
    value = to_float(row, column)
    if baseline_value == 0.0:
        return 0.0
    if higher_is_better:
        return (value - baseline_value) / baseline_value * 100.0
    return (baseline_value - value) / baseline_value * 100.0


def percent_y_domain(values: list[float]) -> tuple[float, float]:
    low = min(0.0, min(values))
    high = max(0.0, max(values))
    if low == high:
        return -1.0, 1.0
    padding = max((high - low) * 0.10, 0.08)
    return (low - padding if low < 0.0 else 0.0, high + padding)


def format_percent(value: float) -> str:
    if abs(value) >= 10.0:
        return f"{value:.1f}%"
    return f"{value:.2f}%"


def write_overview_svg(path: Path, rows: list[dict[str, str]]) -> None:
    width = 1220
    panel_height = 250
    top_margin = 108
    left = 86
    right = 310
    bottom_margin = 78
    gap = 36
    height = (
        top_margin
        + len(METRICS) * panel_height
        + (len(METRICS) - 1) * gap
        + bottom_margin
    )
    plot_width = width - left - right
    x_values = sorted({to_int(row, "total_logical_gpus") for row in rows})
    x_min = min(x_values)
    x_max = max(x_values)
    if x_min == x_max:
        x_min -= 1
        x_max += 1

    baseline = next(
        row for row in rows if row.get("calibration_scope") == "calibrated_reference"
    )
    by_family: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        family = row.get("scale_family", "")
        if family == "calibrated_reference":
            continue
        by_family[family].append(row)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="{width}" height="{height}" fill="#ffffff" />',
        (
            "<style>text{font-family:Arial,Helvetica,sans-serif;} "
            ".muted{fill:#666;} .axis{stroke:#333;} .grid{stroke:#e6e6e6;}"
            "</style>"
        ),
        (
            f'<text x="{width / 2}" y="34" text-anchor="middle" '
            'font-size="24" font-weight="700">Extrapolated PD GPU Sweep</text>'
        ),
        (
            f'<text x="{width / 2}" y="58" text-anchor="middle" '
            'font-size="13" class="muted">Only P 1xTP1 / D 1xTP1 is '
            "calibrated; every other point is extrapolated until matching "
            "measurements exist.</text>"
        ),
        (
            f'<text x="{width / 2}" y="77" text-anchor="middle" '
            'font-size="13" class="muted">Vertical axes are baseline-relative '
            "percent improvement, so 0% is the calibrated reference. "
            "TP-only cases are flat under the "
            "current serving_disagg_colocated scalar cost model; worker scaling "
            "changes queueing behavior.</text>"
        ),
    ]

    family_offsets = {
        family: (index - (len(FAMILY_ORDER) - 1) / 2.0) * 0.09
        for index, family in enumerate(FAMILY_ORDER)
    }

    def sx(value: float) -> float:
        return left + (value - x_min) / (x_max - x_min) * plot_width

    for panel_index, (
        column,
        title,
        unit,
        scale,
        higher_is_better,
    ) in enumerate(METRICS):
        top = top_margin + panel_index * (panel_height + gap)
        y_values = [
            metric_improvement_pct(row, baseline, column, higher_is_better)
            for row in rows
        ]
        y_min, y_max = percent_y_domain(y_values)

        def sy(value: float) -> float:
            return (
                top
                + panel_height
                - (value - y_min) / (y_max - y_min) * panel_height
            )

        svg.append(
            f'<text x="{left}" y="{top - 13}" font-size="17" '
            f'font-weight="700">{escape(title)} improvement vs baseline (%)</text>'
        )
        svg.append(
            f'<text x="{left + 420}" y="{top - 13}" font-size="12" '
            'class="muted">positive is better</text>'
        )
        svg.append(
            f'<line x1="{left}" y1="{top + panel_height}" x2="{left + plot_width}" y2="{top + panel_height}" class="axis" />'
        )
        svg.append(
            f'<line x1="{left}" y1="{top}" x2="{left}" '
            f'y2="{top + panel_height}" class="axis" />'
        )

        for tick_index in range(5):
            value = y_min + (y_max - y_min) * tick_index / 4.0
            y = sy(value)
            svg.append(
                f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_width}" y2="{y:.1f}" class="grid" />'
            )
            svg.append(
                f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" '
                f'font-size="11" class="muted">{format_percent(value)}</text>'
            )
        for x_value in x_values:
            x = sx(x_value)
            svg.append(
                f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top + panel_height}" class="grid" />'
            )
            if panel_index == len(METRICS) - 1:
                svg.append(
                    f'<text x="{x:.1f}" y="{top + panel_height + 24}" text-anchor="middle" font-size="12">{x_value}</text>'
                )

        baseline_raw_value = to_float(baseline, column) * scale
        baseline_y = sy(0.0)
        svg.append(
            f'<line x1="{left}" y1="{baseline_y:.1f}" '
            f'x2="{left + plot_width}" y2="{baseline_y:.1f}" '
            'stroke="#2b2b2b" stroke-width="1.5" stroke-dasharray="6 5" />'
        )
        svg.append(
            f'<text x="{left + plot_width + 12}" y="{baseline_y + 4:.1f}" '
            f'font-size="11" fill="#2b2b2b">baseline 0% '
            f'({format_number(baseline_raw_value)} {escape(unit)})</text>'
        )

        for family in FAMILY_ORDER:
            family_rows = sorted(
                by_family.get(family, []),
                key=lambda row: (
                    to_int(row, "total_logical_gpus"),
                    to_int(row, "pd_prefill_workers"),
                    to_int(row, "pd_decode_workers"),
                ),
            )
            if not family_rows:
                continue
            color = family_color(family)
            offset = family_offsets.get(family, 0.0)
            points = [
                (
                    sx(to_int(row, "total_logical_gpus") + offset),
                    sy(
                        metric_improvement_pct(
                            row, baseline, column, higher_is_better
                        )
                    ),
                )
                for row in family_rows
            ]
            polyline = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
            dash = ' stroke-dasharray="5 4"' if "tp_scaling" in family else ""
            svg.append(
                f'<polyline points="{polyline}" fill="none" stroke="{color}" stroke-width="2.2"{dash} />'
            )
            for row, (x, y) in zip(family_rows, points):
                improvement = metric_improvement_pct(
                    row, baseline, column, higher_is_better
                )
                tooltip = (
                    f"{row['case_name']}\\n"
                    f"{title}: {format_number(to_float(row, column) * scale)} {unit}\\n"
                    f"Improvement: {format_percent(improvement)}"
                )
                svg.append(
                    f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.4" fill="{color}"><title>{escape(tooltip)}</title></circle>'
                )

        base_x = sx(to_int(baseline, "total_logical_gpus"))
        svg.append(
            f'<path d="M {base_x:.1f} {baseline_y - 8:.1f} '
            f'L {base_x + 8:.1f} {baseline_y:.1f} '
            f'L {base_x:.1f} {baseline_y + 8:.1f} '
            f'L {base_x - 8:.1f} {baseline_y:.1f} Z" '
            f'fill="#2b2b2b"><title>{escape(baseline["case_name"])}</title></path>'
        )

    x_label_y = height - 31
    svg.append(
        f'<text x="{left + plot_width / 2}" y="{x_label_y}" text-anchor="middle" font-size="14">Total logical GPUs = prefill workers x prefill TP + decode workers x decode TP</text>'
    )

    legend_x = left + plot_width + 82
    legend_y = top_margin + 28
    svg.append(
        f'<text x="{legend_x}" y="{legend_y - 22}" font-size="14" '
        'font-weight="700">Scaling family</text>'
    )
    svg.append(
        f'<path d="M {legend_x + 8} {legend_y - 5} L {legend_x + 16} {legend_y + 3} L {legend_x + 8} {legend_y + 11} L {legend_x} {legend_y + 3} Z" fill="#2b2b2b" />'
    )
    svg.append(
        f'<text x="{legend_x + 28}" y="{legend_y + 7}" font-size="12">{family_label("calibrated_reference")}</text>'
    )
    for index, family in enumerate(FAMILY_ORDER, 1):
        y = legend_y + index * 24
        color = family_color(family)
        dash = ' stroke-dasharray="5 4"' if "tp_scaling" in family else ""
        svg.append(
            f'<line x1="{legend_x}" y1="{y}" x2="{legend_x + 22}" y2="{y}" stroke="{color}" stroke-width="2.2"{dash} />'
        )
        svg.append(f'<circle cx="{legend_x + 11}" cy="{y}" r="4" fill="{color}" />')
        svg.append(
            f'<text x="{legend_x + 28}" y="{y + 4}" font-size="12">{family_label(family)}</text>'
        )

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def write_ranked_metric_svg(
    path: Path,
    rows: list[dict[str, str]],
    column: str,
    title: str,
    unit: str,
    scale: float,
    higher_is_better: bool,
) -> None:
    baseline = next(
        row for row in rows if row.get("calibration_scope") == "calibrated_reference"
    )
    ranked = sorted(
        rows,
        key=lambda row: (
            -metric_improvement_pct(row, baseline, column, higher_is_better),
            row["case_name"],
        ),
    )

    width = 1220
    row_height = 26
    top = 88
    left = 360
    right = 70
    bottom = 52
    height = top + row_height * len(ranked) + bottom
    chart_width = width - left - right
    improvements = [
        metric_improvement_pct(row, baseline, column, higher_is_better)
        for row in ranked
    ]
    min_value = min(0.0, min(improvements))
    max_value = max(1.0, max(improvements))
    span = max_value - min_value

    def sx(value: float) -> float:
        return left + (value - min_value) / span * chart_width

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="#ffffff" />',
        '<style>text{font-family:Arial,Helvetica,sans-serif;} .muted{fill:#666;} .grid{stroke:#e6e6e6;}</style>',
        f'<text x="{width / 2}" y="34" text-anchor="middle" font-size="24" font-weight="700">{escape(title)} Improvement vs Calibrated Reference</text>',
        f'<text x="{width / 2}" y="58" text-anchor="middle" font-size="13" class="muted">Positive values are better than P 1xTP1 / D 1xTP1. Values are extrapolated except the zero baseline.</text>',
    ]

    for tick in range(0, int(max_value) + 2):
        x = sx(float(tick))
        svg.append(f'<line x1="{x:.1f}" y1="{top - 18}" x2="{x:.1f}" y2="{height - bottom + 8}" class="grid" />')
        svg.append(f'<text x="{x:.1f}" y="{top - 25}" text-anchor="middle" font-size="11" class="muted">{tick}%</text>')
    zero_x = sx(0.0)
    svg.append(f'<line x1="{zero_x:.1f}" y1="{top - 22}" x2="{zero_x:.1f}" y2="{height - bottom + 10}" stroke="#333" />')

    for index, row in enumerate(ranked):
        y = top + index * row_height
        family = row.get("scale_family", "")
        color = family_color(family)
        improvement = improvements[index]
        x0 = sx(0.0)
        x1 = sx(improvement)
        bar_x = min(x0, x1)
        bar_width = max(1.5, abs(x1 - x0))
        label = case_short_label(row)
        family_text = family_label(family)
        scope = row.get("calibration_scope", "")
        text_weight = "700" if scope == "calibrated_reference" else "400"
        svg.append(
            f'<text x="{left - 12}" y="{y + 17}" text-anchor="end" font-size="12" font-weight="{text_weight}">{escape(label)}</text>'
        )
        svg.append(
            f'<text x="{left - 350}" y="{y + 17}" font-size="11" class="muted">{escape(family_text)}</text>'
        )
        svg.append(
            f'<rect x="{bar_x:.1f}" y="{y + 5}" width="{bar_width:.1f}" height="15" fill="{color}" opacity="0.86"><title>{escape(row["case_name"])}</title></rect>'
        )
        value_x = max(x0, x1) + 6
        svg.append(
            f'<text x="{value_x:.1f}" y="{y + 17}" font-size="11">{improvement:.2f}% ({format_number(to_float(row, column) * scale)} {escape(unit)})</text>'
        )

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    rows = load_rows(Path(args.input))
    if not rows:
        raise SystemExit("No rows found in sweep summary.")
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    write_overview_svg(output_dir / "pd_extrapolated_sweep_overview.svg", rows)
    write_ranked_metric_svg(
        output_dir / "pd_extrapolated_ttft_ranked.svg",
        rows,
        "ttft_mean_ns",
        "Mean TTFT",
        "ms",
        1.0e-6,
        False,
    )
    write_ranked_metric_svg(
        output_dir / "pd_extrapolated_tpot_ranked.svg",
        rows,
        "tpot_mean_ns",
        "Mean TPOT",
        "ms",
        1.0e-6,
        False,
    )
    print(f"Wrote plots to {output_dir}")


if __name__ == "__main__":
    main()
