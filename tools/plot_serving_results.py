#!/usr/bin/env python3
"""Plot parsed serving sweep results from a flat CSV.

This script prefers matplotlib when available. If matplotlib is missing, it can
still emit simple SVG plots without external dependencies.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from html import escape
from pathlib import Path


def maybe_float(value: str):
    try:
        return float(value)
    except (TypeError, ValueError):
        return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot serving sweep results from parse_serving_outputs.py CSV output."
    )
    parser.add_argument("--input", required=True, help="Parsed CSV input.")
    parser.add_argument("--x", required=True, help="Column to use on the x axis.")
    parser.add_argument("--y", required=True, help="Column to use on the y axis.")
    parser.add_argument(
        "--series",
        default="architecture",
        help="Column to group into separate series.",
    )
    parser.add_argument(
        "--kind",
        choices=("line", "scatter"),
        default="line",
        help="Plot type.",
    )
    parser.add_argument("--title", default="Serving Sweep Results")
    parser.add_argument("--xlabel")
    parser.add_argument("--ylabel")
    parser.add_argument("--output", required=True, help="Output image path.")
    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        metavar="COLUMN=VALUE",
        help="Optional equality filter before plotting.",
    )
    return parser.parse_args()


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def apply_filters(rows: list[dict[str, str]], filters: list[str]) -> list[dict[str, str]]:
    filtered = rows
    for item in filters:
        if "=" not in item:
            raise ValueError(f"Invalid filter '{item}'. Expected column=value.")
        column, value = item.split("=", 1)
        filtered = [row for row in filtered if row.get(column) == value]
    return filtered


def main() -> None:
    args = parse_args()
    rows = load_rows(Path(args.input))
    rows = apply_filters(rows, args.filter)
    if not rows:
        raise SystemExit("No rows matched the requested plot inputs.")

    grouped: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for row in rows:
        series_name = row.get(args.series, "all")
        x_value = maybe_float(row[args.x])
        y_value = maybe_float(row[args.y])
        if not isinstance(x_value, (int, float)) or not isinstance(y_value, (int, float)):
            raise SystemExit(
                f"Columns '{args.x}' and '{args.y}' must be numeric for plotting."
            )
        grouped[str(series_name)].append((float(x_value), float(y_value)))

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if try_write_with_matplotlib(grouped, args, output_path):
        print(f"Wrote plot to {output_path}")
        return
    if output_path.suffix.lower() != ".svg":
        raise SystemExit(
            "matplotlib is not installed, so the fallback plot writer requires an .svg output path."
        )
    write_svg(grouped, args, output_path)
    print(f"Wrote SVG plot to {output_path}")


def try_write_with_matplotlib(
    grouped: dict[str, list[tuple[float, float]]],
    args: argparse.Namespace,
    output_path: Path,
) -> bool:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ModuleNotFoundError:
        return False

    figure, axis = plt.subplots(figsize=(8, 5))
    for series_name, points in sorted(grouped.items()):
        points.sort(key=lambda item: item[0])
        x_values = [point[0] for point in points]
        y_values = [point[1] for point in points]
        if args.kind == "scatter":
            axis.scatter(x_values, y_values, label=series_name)
        else:
            axis.plot(x_values, y_values, marker="o", label=series_name)

    axis.set_title(args.title)
    axis.set_xlabel(args.xlabel or args.x)
    axis.set_ylabel(args.ylabel or args.y)
    axis.grid(True, alpha=0.3)
    axis.legend()
    figure.tight_layout()
    figure.savefig(output_path, dpi=200)
    return True


def write_svg(
    grouped: dict[str, list[tuple[float, float]]],
    args: argparse.Namespace,
    output_path: Path,
) -> None:
    width = 900
    height = 560
    margin_left = 90
    margin_right = 220
    margin_top = 70
    margin_bottom = 90
    plot_width = width - margin_left - margin_right
    plot_height = height - margin_top - margin_bottom
    colors = [
        "#1f77b4",
        "#d62728",
        "#2ca02c",
        "#ff7f0e",
        "#8c564b",
        "#17becf",
    ]

    all_points = [point for points in grouped.values() for point in points]
    x_values = [point[0] for point in all_points]
    y_values = [point[1] for point in all_points]
    x_min = min(x_values)
    x_max = max(x_values)
    y_min = min(y_values)
    y_max = max(y_values)
    if x_min == x_max:
        x_max += 1.0
    if y_min == y_max:
        y_max += 1.0

    def scale_x(value: float) -> float:
        return margin_left + (value - x_min) / (x_max - x_min) * plot_width

    def scale_y(value: float) -> float:
        return margin_top + plot_height - (value - y_min) / (y_max - y_min) * plot_height

    svg_lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect x="0" y="0" width="{width}" height="{height}" fill="white" />',
        f'<text x="{width / 2}" y="32" font-size="20" text-anchor="middle">{escape(args.title)}</text>',
        f'<line x1="{margin_left}" y1="{margin_top + plot_height}" x2="{margin_left + plot_width}" y2="{margin_top + plot_height}" stroke="#333" />',
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" y2="{margin_top + plot_height}" stroke="#333" />',
    ]

    tick_count = 5
    for index in range(tick_count + 1):
        x_value = x_min + (x_max - x_min) * index / tick_count
        x_pos = scale_x(x_value)
        svg_lines.append(
            f'<line x1="{x_pos}" y1="{margin_top}" x2="{x_pos}" y2="{margin_top + plot_height}" stroke="#e0e0e0" />'
        )
        svg_lines.append(
            f'<text x="{x_pos}" y="{margin_top + plot_height + 24}" font-size="12" text-anchor="middle">{x_value:.2f}</text>'
        )

        y_value = y_min + (y_max - y_min) * index / tick_count
        y_pos = scale_y(y_value)
        svg_lines.append(
            f'<line x1="{margin_left}" y1="{y_pos}" x2="{margin_left + plot_width}" y2="{y_pos}" stroke="#e0e0e0" />'
        )
        svg_lines.append(
            f'<text x="{margin_left - 10}" y="{y_pos + 4}" font-size="12" text-anchor="end">{y_value:.2f}</text>'
        )

    svg_lines.append(
        f'<text x="{margin_left + plot_width / 2}" y="{height - 24}" font-size="14" text-anchor="middle">{escape(args.xlabel or args.x)}</text>'
    )
    svg_lines.append(
        f'<text x="24" y="{margin_top + plot_height / 2}" font-size="14" text-anchor="middle" transform="rotate(-90 24 {margin_top + plot_height / 2})">{escape(args.ylabel or args.y)}</text>'
    )

    legend_x = margin_left + plot_width + 24
    legend_y = margin_top + 18
    for series_index, (series_name, points) in enumerate(sorted(grouped.items())):
        color = colors[series_index % len(colors)]
        points.sort(key=lambda item: item[0])
        scaled = [(scale_x(x_value), scale_y(y_value)) for x_value, y_value in points]
        if args.kind == "line":
            polyline_points = " ".join(f"{x:.1f},{y:.1f}" for x, y in scaled)
            svg_lines.append(
                f'<polyline points="{polyline_points}" fill="none" stroke="{color}" stroke-width="2" />'
            )
        for x_pos, y_pos in scaled:
            svg_lines.append(
                f'<circle cx="{x_pos:.1f}" cy="{y_pos:.1f}" r="4" fill="{color}" />'
            )

        legend_entry_y = legend_y + series_index * 22
        svg_lines.append(
            f'<line x1="{legend_x}" y1="{legend_entry_y}" x2="{legend_x + 18}" y2="{legend_entry_y}" stroke="{color}" stroke-width="2" />'
        )
        svg_lines.append(
            f'<circle cx="{legend_x + 9}" cy="{legend_entry_y}" r="4" fill="{color}" />'
        )
        svg_lines.append(
            f'<text x="{legend_x + 28}" y="{legend_entry_y + 4}" font-size="12">{escape(series_name)}</text>'
        )

    svg_lines.append("</svg>")
    output_path.write_text("\n".join(svg_lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
