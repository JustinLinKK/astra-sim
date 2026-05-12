#!/usr/bin/env python3
"""Fit simple serving cost-model terms from measured stage durations."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
from typing import Any


PREFILL_STAGES = {"prefill", "serial_prefill", "colocated_prefill", "pd_prefill"}
DECODE_STAGES = {"decode", "serial_decode", "colocated_decode", "pd_decode"}


def maybe_bool(value: Any, default: bool) -> bool:
    if value is None or value == "":
        return default
    if isinstance(value, bool):
        return value
    lowered = str(value).strip().lower()
    if lowered in {"1", "true", "yes"}:
        return True
    if lowered in {"0", "false", "no"}:
        return False
    return default


def normalize_stage(value: str) -> str:
    lowered = value.strip().lower()
    if lowered in PREFILL_STAGES:
        return "prefill"
    if lowered in DECODE_STAGES:
        return "decode"
    return lowered


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def solve_two_feature_regression(samples: list[tuple[float, float, float]]) -> tuple[float, float]:
    s11 = sum(feature_a * feature_a for feature_a, _, _ in samples)
    s12 = sum(feature_a * feature_b for feature_a, feature_b, _ in samples)
    s22 = sum(feature_b * feature_b for _, feature_b, _ in samples)
    b1 = sum(feature_a * target for feature_a, _, target in samples)
    b2 = sum(feature_b * target for _, feature_b, target in samples)

    determinant = s11 * s22 - s12 * s12
    if abs(determinant) < 1e-12:
        raise ValueError("Unable to solve regression: singular normal equation.")

    coefficient_a = (b1 * s22 - b2 * s12) / determinant
    coefficient_b = (s11 * b2 - s12 * b1) / determinant
    return coefficient_a, coefficient_b


def compute_rmse(samples: list[tuple[float, float, float]], base: float, per_token: float) -> float:
    if not samples:
        return 0.0
    squared_error = 0.0
    for feature_a, feature_b, target in samples:
        prediction = feature_a * base + feature_b * per_token
        squared_error += (prediction - target) ** 2
    return math.sqrt(squared_error / len(samples))


def fit_cost_model(
    rows: list[dict[str, str]],
    stage_column: str,
    duration_column: str,
    tokens_column: str,
    decode_base_column: str,
    require_batch_completed: bool,
) -> dict[str, Any]:
    prefill_samples: list[tuple[float, float, float]] = []
    decode_samples: list[tuple[float, float, float]] = []

    for row in rows:
        if require_batch_completed and row.get("event") not in {None, "", "batch_completed"}:
            continue
        stage = normalize_stage(row.get(stage_column, ""))
        if stage not in {"prefill", "decode"}:
            continue
        duration_ns = float(row[duration_column])
        tokens = float(row[tokens_column])
        if stage == "prefill":
            prefill_samples.append((1.0, tokens, duration_ns))
            continue
        include_base = 1.0 if maybe_bool(row.get(decode_base_column), True) else 0.0
        decode_samples.append((include_base, tokens, duration_ns))

    if len(prefill_samples) < 2:
        raise ValueError("Need at least two prefill samples to fit the prefill cost model.")
    if len(decode_samples) < 2:
        raise ValueError("Need at least two decode samples to fit the decode cost model.")

    fit_method = {"prefill": "ols", "decode": "ols"}
    try:
        prefill_base, prefill_per_token = solve_two_feature_regression(prefill_samples)
    except ValueError:
        prefill_base = 0.0
        prefill_per_token = sum(
            target / feature_b for _, feature_b, target in prefill_samples if feature_b > 0
        ) / len(prefill_samples)
        fit_method["prefill"] = "fallback_zero_base"

    try:
        decode_base, decode_per_token = solve_two_feature_regression(decode_samples)
    except ValueError:
        decode_base = 0.0
        decode_per_token = sum(
            target / feature_b for _, feature_b, target in decode_samples if feature_b > 0
        ) / len(decode_samples)
        fit_method["decode"] = "fallback_zero_base"

    result = {
        "cost_model": {
            "prefill_base_latency_ns": max(0, round(prefill_base)),
            "prefill_ns_per_token": max(0, round(prefill_per_token)),
            "decode_base_latency_ns": max(0, round(decode_base)),
            "decode_ns_per_token": max(0, round(decode_per_token)),
            "prefill_batch_efficiency": 1.0,
            "decode_batch_efficiency": 1.0,
            "decode_interference_factor": 1.0,
            "mixed_prefill_weight": 1.0,
        },
        "fit_stats": {
            "prefill_samples": len(prefill_samples),
            "decode_samples": len(decode_samples),
            "fit_method": fit_method,
            "prefill_rmse_ns": compute_rmse(
                prefill_samples, prefill_base, prefill_per_token
            ),
            "decode_rmse_ns": compute_rmse(
                decode_samples, decode_base, decode_per_token
            ),
        },
    }
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fit a serving cost_model block from batch duration measurements."
    )
    parser.add_argument("--input", required=True, help="CSV input, often an event trace CSV.")
    parser.add_argument("--output", help="Optional JSON output path.")
    parser.add_argument("--stage-column", default="stage")
    parser.add_argument("--duration-column", default="duration_ns")
    parser.add_argument("--tokens-column", default="total_tokens")
    parser.add_argument(
        "--decode-base-column",
        default="include_base_latency",
        help="Optional decode base-latency indicator column. Missing values default to true.",
    )
    parser.add_argument(
        "--allow-non-completed-events",
        action="store_true",
        help="Use all rows instead of only batch_completed rows.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    rows = load_rows(Path(args.input))
    fitted = fit_cost_model(
        rows,
        stage_column=args.stage_column,
        duration_column=args.duration_column,
        tokens_column=args.tokens_column,
        decode_base_column=args.decode_base_column,
        require_batch_completed=not args.allow_non_completed_events,
    )
    rendered = json.dumps(fitted, indent=2) + "\n"
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
