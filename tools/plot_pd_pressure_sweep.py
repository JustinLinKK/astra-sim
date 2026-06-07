#!/usr/bin/env python3
"""Plot PD pressure sweeps by request rate and prompt length."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from html import escape
from pathlib import Path
from typing import Any


ASSIGNMENT_COLORS = {
    "P3/D1": "#2ca02c",
    "P2/D2": "#1f77b4",
    "P1/D3": "#d62728",
    "P2/D1": "#66bb6a",
    "P1/D2": "#ef5350",
    "P1/D1": "#303030",
}

METRICS = (
    ("e2e_mean_ns", "Mean E2E Latency", "ms", 1.0e-6, False, "pd_4gpu_pressure_e2e.svg"),
    ("ttft_mean_ns", "Mean TTFT", "ms", 1.0e-6, False, "pd_4gpu_pressure_ttft.svg"),
    ("tpot_mean_ns", "Mean TPOT", "ms", 1.0e-6, False, "pd_4gpu_pressure_tpot.svg"),
    (
        "output_token_throughput_tokens_per_sec",
        "Output Token Throughput",
        "tokens/s",
        1.0,
        True,
        "pd_4gpu_pressure_throughput.svg",
    ),
    (
        "slo_attainment_fraction",
        "SLO Attainment",
        "%",
        100.0,
        True,
        "pd_4gpu_pressure_slo.svg",
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        default="build/pd_4gpu_pressure_sweep/summary.csv",
        help="Parsed pressure sweep summary CSV.",
    )
    parser.add_argument(
        "--output-dir",
        default="build/pd_4gpu_pressure_sweep/plots",
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
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{path} has no rows")
    return rows


def assignment_label(row: dict[str, str]) -> str:
    return f"P{to_int(row, 'pd_prefill_workers')}/D{to_int(row, 'pd_decode_workers')}"


def prompt_mean(row: dict[str, str]) -> float:
    return to_float(row, "prompt_tokens_mean")


def request_rate(row: dict[str, str]) -> float:
    return to_float(row, "trace_request_rate_per_second")


def format_tick(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    return f"{value:.1f}".rstrip("0").rstrip(".")


def metric_domain(values: list[float]) -> tuple[float, float]:
    low = min(values)
    high = max(values)
    if low == high:
        padding = max(abs(low) * 0.05, 1.0)
        return low - padding, high + padding
    padding = max((high - low) * 0.08, 0.5)
    return low - padding, high + padding


def assignment_color(label: str) -> str:
    return ASSIGNMENT_COLORS.get(label, "#666666")


def write_metric_svg(
    path: Path,
    rows: list[dict[str, str]],
    column: str,
    title: str,
    unit: str,
    scale: float,
    higher_is_better: bool,
) -> None:
    prompt_values = sorted({prompt_mean(row) for row in rows})
    rate_values = sorted({request_rate(row) for row in rows})
    assignments = sorted({assignment_label(row) for row in rows})

    width = 1180
    left = 88
    right = 190
    top = 106
    panel_height = 170
    panel_gap = 44
    bottom = 78
    height = top + len(prompt_values) * panel_height + (len(prompt_values) - 1) * panel_gap + bottom
    plot_width = width - left - right

    all_metric_values = [to_float(row, column) * scale for row in rows]
    y_min, y_max = metric_domain(all_metric_values)
    x_min = min(rate_values)
    x_max = max(rate_values)
    if x_min == x_max:
        x_min -= 1.0
        x_max += 1.0

    def sx(value: float) -> float:
        return left + (value - x_min) / (x_max - x_min) * plot_width

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="{width}" height="{height}" fill="#ffffff" />',
        (
            "<style>text{font-family:Arial,Helvetica,sans-serif;}"
            ".muted{fill:#626262}.grid{stroke:#e8e8e8}.axis{stroke:#333}</style>"
        ),
        (
            f'<text x="{width / 2}" y="34" text-anchor="middle" '
            f'font-size="24" font-weight="700">{escape(title)} Under Pressure</text>'
        ),
        (
            f'<text x="{width / 2}" y="58" text-anchor="middle" '
            'font-size="13" class="muted">Each panel fixes average input tokens; '
            "x-axis sweeps request rate. TP=1 and every row uses 4 logical GPUs.</text>"
        ),
        (
            f'<text x="{width / 2}" y="77" text-anchor="middle" '
            f'font-size="13" class="muted">Raw {escape(unit)} values; '
            f'{"higher" if higher_is_better else "lower"} is better. '
            "All rows are extrapolated until matching measurements exist.</text>"
        ),
    ]

    for panel_index, prompt in enumerate(prompt_values):
        panel_top = top + panel_index * (panel_height + panel_gap)

        def sy(value: float) -> float:
            return panel_top + panel_height - (value - y_min) / (y_max - y_min) * panel_height

        svg.append(
            f'<text x="{left}" y="{panel_top - 14}" font-size="16" '
            f'font-weight="700">avg input {format_tick(prompt)} tokens</text>'
        )

        for tick in range(5):
            y_value = y_min + (y_max - y_min) * tick / 4
            y = sy(y_value)
            svg.append(
                f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_width}" '
                f'y2="{y:.1f}" class="grid" />'
            )
            svg.append(
                f'<text x="{left - 10}" y="{y + 4:.1f}" text-anchor="end" '
                f'font-size="11" class="muted">{format_tick(y_value)}</text>'
            )

        for rate in rate_values:
            x = sx(rate)
            svg.append(
                f'<line x1="{x:.1f}" y1="{panel_top}" x2="{x:.1f}" '
                f'y2="{panel_top + panel_height}" class="grid" />'
            )
            if panel_index == len(prompt_values) - 1:
                svg.append(
                    f'<text x="{x:.1f}" y="{panel_top + panel_height + 24}" '
                    f'text-anchor="middle" font-size="12">{format_tick(rate)}</text>'
                )

        svg.append(
            f'<line x1="{left}" y1="{panel_top + panel_height}" '
            f'x2="{left + plot_width}" y2="{panel_top + panel_height}" '
            'class="axis" />'
        )

        prompt_rows = [
            row for row in rows if abs(prompt_mean(row) - prompt) < 1e-9
        ]
        by_assignment: dict[str, list[dict[str, str]]] = defaultdict(list)
        for row in prompt_rows:
            by_assignment[assignment_label(row)].append(row)

        for assignment in assignments:
            points = sorted(by_assignment.get(assignment, []), key=request_rate)
            if not points:
                continue
            path_points = [
                (
                    sx(request_rate(row)),
                    sy(to_float(row, column) * scale),
                )
                for row in points
            ]
            color = assignment_color(assignment)
            if len(path_points) > 1:
                commands = " ".join(
                    (
                        f"M {path_points[0][0]:.1f} {path_points[0][1]:.1f}",
                        *(
                            f"L {x:.1f} {y:.1f}"
                            for x, y in path_points[1:]
                        ),
                    )
                )
                svg.append(
                    f'<path d="{commands}" fill="none" stroke="{color}" '
                    'stroke-width="2.5" />'
                )
            for point_index, (x, y) in enumerate(path_points):
                svg.append(
                    f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{color}" />'
                )
                if point_index == len(path_points) - 1:
                    value = to_float(points[point_index], column) * scale
                    svg.append(
                        f'<text x="{x + 8:.1f}" y="{y + 4:.1f}" '
                        f'font-size="11" fill="{color}">{format_tick(value)}</text>'
                    )

    svg.append(
        f'<text x="{left + plot_width / 2}" y="{height - 28}" '
        'text-anchor="middle" font-size="13">Request rate (requests/s)</text>'
    )
    legend_x = left + plot_width + 34
    legend_y = top
    svg.append(
        f'<text x="{legend_x}" y="{legend_y - 18}" font-size="13" '
        'font-weight="700">Assignment</text>'
    )
    for index, assignment in enumerate(assignments):
        y = legend_y + index * 24
        color = assignment_color(assignment)
        svg.append(f'<circle cx="{legend_x}" cy="{y}" r="5" fill="{color}" />')
        svg.append(
            f'<text x="{legend_x + 13}" y="{y + 4}" font-size="12">{escape(assignment)}</text>'
        )

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def write_winner_matrix(path: Path, rows: list[dict[str, str]]) -> None:
    prompt_values = sorted({prompt_mean(row) for row in rows})
    rate_values = sorted({request_rate(row) for row in rows})
    grouped: dict[tuple[float, float], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[(prompt_mean(row), request_rate(row))].append(row)

    cell_width = 156
    cell_height = 82
    left = 150
    top = 112
    right = 36
    bottom = 46
    width = left + len(rate_values) * cell_width + right
    height = top + len(prompt_values) * cell_height + bottom

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="{width}" height="{height}" fill="#ffffff" />',
        (
            "<style>text{font-family:Arial,Helvetica,sans-serif;}"
            ".muted{fill:#626262}</style>"
        ),
        (
            f'<text x="{width / 2}" y="34" text-anchor="middle" '
            'font-size="24" font-weight="700">Best 4-GPU Assignment by Mean E2E</text>'
        ),
        (
            f'<text x="{width / 2}" y="58" text-anchor="middle" '
            'font-size="13" class="muted">Cells show the lowest mean E2E latency '
            "at each request-rate / average-input-token point.</text>"
        ),
    ]

    for col, rate in enumerate(rate_values):
        x = left + col * cell_width + cell_width / 2
        svg.append(
            f'<text x="{x:.1f}" y="{top - 24}" text-anchor="middle" '
            f'font-size="13" font-weight="700">{format_tick(rate)} req/s</text>'
        )
    for row_index, prompt in enumerate(prompt_values):
        y = top + row_index * cell_height + cell_height / 2
        svg.append(
            f'<text x="{left - 14}" y="{y + 4:.1f}" text-anchor="end" '
            f'font-size="13" font-weight="700">{format_tick(prompt)} tokens</text>'
        )

    for row_index, prompt in enumerate(prompt_values):
        for col, rate in enumerate(rate_values):
            candidates = grouped.get((prompt, rate), [])
            if not candidates:
                continue
            best = min(candidates, key=lambda row: to_float(row, "e2e_mean_ns"))
            assignment = assignment_label(best)
            e2e_ms = to_float(best, "e2e_mean_ns") * 1.0e-6
            ttft_ms = to_float(best, "ttft_mean_ns") * 1.0e-6
            x = left + col * cell_width
            y = top + row_index * cell_height
            color = assignment_color(assignment)
            svg.append(
                f'<rect x="{x}" y="{y}" width="{cell_width - 8}" '
                f'height="{cell_height - 8}" fill="{color}" opacity="0.16" '
                f'stroke="{color}" stroke-width="1.5" rx="4" />'
            )
            svg.append(
                f'<text x="{x + (cell_width - 8) / 2:.1f}" y="{y + 27}" '
                f'text-anchor="middle" font-size="16" font-weight="700" '
                f'fill="{color}">{escape(assignment)}</text>'
            )
            svg.append(
                f'<text x="{x + (cell_width - 8) / 2:.1f}" y="{y + 49}" '
                'text-anchor="middle" font-size="12">E2E '
                f'{e2e_ms:.1f} ms</text>'
            )
            svg.append(
                f'<text x="{x + (cell_width - 8) / 2:.1f}" y="{y + 65}" '
                'text-anchor="middle" font-size="11" class="muted">TTFT '
                f'{ttft_ms:.1f} ms</text>'
            )

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    rows = load_rows(Path(args.input))
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    for column, title, unit, scale, higher_is_better, filename in METRICS:
        write_metric_svg(
            output_dir / filename,
            rows,
            column,
            title,
            unit,
            scale,
            higher_is_better,
        )
    write_winner_matrix(output_dir / "pd_4gpu_pressure_winner_matrix.svg", rows)
    print(f"Wrote pressure plots to {output_dir}")


if __name__ == "__main__":
    main()
