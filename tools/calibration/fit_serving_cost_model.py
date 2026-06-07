#!/usr/bin/env python3
"""Fit simple serving cost-model terms from measured stage durations."""

from __future__ import annotations

import argparse
import copy
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from typing import Any


PREFILL_STAGES = {"prefill", "serial_prefill", "colocated_prefill", "pd_prefill"}
DECODE_STAGES = {"decode", "serial_decode", "colocated_decode", "pd_decode"}
DEFAULT_MODES = ("colocated", "pd_disaggregated")
FIT_TARGETS = ("stage", "request_latency")
OVERLOAD_SERVICE_FIT_EXCLUSION_REASON = "overload_service_fit_exclusion"
CHUNKED_BASE_MODES = {
    "colocated_chunked": "colocated",
    "pd_disaggregated_chunked": "pd_disaggregated",
}
BASE_FIT_COST_MODEL_FIELDS = (
    "decode_base_latency_ns",
    "decode_ns_per_token",
    "decode_step_latency_ns",
    "decode_step_latency_curve",
)
PROXY_TRANSFER_TIMING = {
    "timing_source": "proxy_derived",
    "timing_method": "prefill_end_to_decode_start",
    "timing_observation": "proxy_handoff_interval_not_raw_nccl",
}
SIMULATOR_FIDELITY_ADJUSTMENTS = {
    "colocated": {
        "prefill_scale": 0.85,
        "decode_step_scale": 0.85,
        "method": "simulator_run_grid_search",
        "reason": (
            "request-level prefill/decode latency coefficients are charged "
            "through colocated batch scheduling"
        ),
    },
}
OVERLOAD_DECODE_STEP_CAPACITY_FACTOR = 1.16


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


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def positive_row_float(row: dict[str, str], *names: str) -> float:
    for name in names:
        value = row.get(name, "")
        if value == "":
            continue
        number = float(value)
        if number > 0:
            return number
    return 0.0


def positive_json_float(value: Any) -> float:
    if value is None or value == "":
        return 0.0
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0.0
    return number if number > 0.0 else 0.0


def load_run_config(run_dir: Path) -> dict[str, Any]:
    run_config_path = run_dir / "run_config.json"
    if not run_config_path.exists():
        return {}
    with run_config_path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def decode_capacity_request_slots(run_config: dict[str, Any]) -> float:
    scheduler = run_config.get("scheduler", {})
    layout = run_config.get("layout", {})
    max_decode_batch_requests = positive_json_float(
        scheduler.get("max_decode_batch_requests")
        or scheduler.get("max_decode_batch_size")
    )
    decode_worker_count = positive_json_float(layout.get("decode_worker_count"))
    if max_decode_batch_requests <= 0.0:
        return 0.0
    if decode_worker_count <= 0.0:
        decode_worker_count = 1.0
    return max_decode_batch_requests * decode_worker_count


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


def mean_or_zero(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def percentile_or_zero(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    index = max(
        0,
        min(len(sorted_values) - 1, math.ceil(q * len(sorted_values)) - 1),
    )
    return sorted_values[index]


def fit_stage_samples(
    prefill_samples: list[tuple[float, float, float]],
    decode_samples: list[tuple[float, float, float]],
) -> dict[str, Any]:
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


def collect_stage_samples(
    rows: list[dict[str, str]],
    stage_column: str,
    duration_column: str,
    tokens_column: str,
    decode_base_column: str,
    require_batch_completed: bool,
) -> tuple[list[tuple[float, float, float]], list[tuple[float, float, float]]]:
    prefill_samples: list[tuple[float, float, float]] = []
    decode_samples: list[tuple[float, float, float]] = []

    for row in rows:
        if require_batch_completed and row.get("event") not in {None, "", "batch_completed"}:
            continue
        stage = normalize_stage(row.get(stage_column, ""))
        if stage not in {"prefill", "decode"}:
            continue
        if row.get(duration_column, "") == "" or row.get(tokens_column, "") == "":
            continue
        duration_ns = float(row[duration_column])
        tokens = float(row[tokens_column])
        if duration_ns <= 0 or tokens <= 0:
            continue
        if stage == "prefill":
            prefill_samples.append((1.0, tokens, duration_ns))
            continue
        include_base = 1.0 if maybe_bool(row.get(decode_base_column), True) else 0.0
        decode_samples.append((include_base, tokens, duration_ns))

    return prefill_samples, decode_samples


def fit_cost_model(
    rows: list[dict[str, str]],
    stage_column: str,
    duration_column: str,
    tokens_column: str,
    decode_base_column: str,
    require_batch_completed: bool,
) -> dict[str, Any]:
    prefill_samples, decode_samples = collect_stage_samples(
        rows,
        stage_column=stage_column,
        duration_column=duration_column,
        tokens_column=tokens_column,
        decode_base_column=decode_base_column,
        require_batch_completed=require_batch_completed,
    )
    return fit_stage_samples(prefill_samples, decode_samples)


def parse_modes(value: str) -> list[str]:
    modes = [mode.strip() for mode in value.split(",") if mode.strip()]
    if not modes:
        raise ValueError("At least one mode must be specified.")
    return modes


def is_pd_mode(mode: str) -> bool:
    return mode in {"pd_disaggregated", "pd_disaggregated_chunked"}


def is_chunked_mode(mode: str) -> bool:
    return mode in {"colocated_chunked", "pd_disaggregated_chunked"}


def shift_first_token_backpressure_curve(
    curve: dict[str, Any],
    base_latency_offset_ns: float,
) -> dict[str, Any]:
    shifted = copy.deepcopy(curve)
    for point in shifted.get("points", []):
        point["base_latency_ns"] = max(
            0,
            round(positive_json_float(point.get("base_latency_ns")) + base_latency_offset_ns),
        )
    return shifted


def inherit_base_fit_for_chunked_mode(
    mode_fit: dict[str, Any],
    mode: str,
    base_fit: dict[str, Any],
    base_fit_path: Path,
) -> None:
    base_mode = CHUNKED_BASE_MODES.get(mode)
    if base_mode is None:
        return
    base_mode_fit = base_fit.get("modes", {}).get(base_mode)
    if base_mode_fit is None:
        raise ValueError(
            f"Base fit {base_fit_path} does not contain required mode {base_mode!r}."
        )

    cost_model = mode_fit["cost_model"]
    base_cost_model = base_mode_fit.get("cost_model", {})
    inherited_fields: list[str] = []

    for field in BASE_FIT_COST_MODEL_FIELDS:
        if field not in base_cost_model:
            continue
        cost_model[field] = copy.deepcopy(base_cost_model[field])
        inherited_fields.append(f"cost_model.{field}")

    if "decode_step_latency_curve" in base_cost_model:
        inherited_curve_fit: dict[str, Any] = {
            "method": "inherited_from_base_fit",
            "source_mode": base_mode,
            "source_fit_json": str(base_fit_path),
        }
        source_curve_fit = base_mode_fit.get("decode_step_latency_curve_fit")
        if source_curve_fit:
            inherited_curve_fit["source_curve_fit"] = copy.deepcopy(source_curve_fit)
        mode_fit["decode_step_latency_curve_fit"] = inherited_curve_fit
        mode_fit["fit_stats"]["decode_step_latency_curve_points"] = len(
            base_cost_model["decode_step_latency_curve"].get("points", [])
        )

    first_token_curve_offset_ns = 0.0
    if is_pd_mode(mode) and "first_token_backpressure_curve" in base_cost_model:
        first_token_curve_offset_ns = (
            positive_json_float(cost_model.get("first_token_latency_ns"))
            - positive_json_float(base_cost_model.get("first_token_latency_ns"))
        )
        cost_model["first_token_backpressure_curve"] = (
            shift_first_token_backpressure_curve(
                base_cost_model["first_token_backpressure_curve"],
                first_token_curve_offset_ns,
            )
        )
        inherited_fields.append("cost_model.first_token_backpressure_curve")
        inherited_curve_fit = {
            "method": "inherited_from_base_fit_with_scalar_offset",
            "source_mode": base_mode,
            "source_fit_json": str(base_fit_path),
            "base_latency_offset_ns": round(first_token_curve_offset_ns),
        }
        source_curve_fit = base_mode_fit.get("first_token_backpressure_curve_fit")
        if source_curve_fit:
            inherited_curve_fit["source_curve_fit"] = copy.deepcopy(source_curve_fit)
        mode_fit["first_token_backpressure_curve_fit"] = inherited_curve_fit
        mode_fit["fit_stats"]["first_token_backpressure_curve_points"] = len(
            cost_model["first_token_backpressure_curve"].get("points", [])
        )

    if is_pd_mode(mode):
        for field in ("transfer", "transfer_fit"):
            if field not in base_mode_fit:
                continue
            mode_fit[field] = copy.deepcopy(base_mode_fit[field])
            inherited_fields.append(field)

    mode_fit["fit_stats"]["base_fit_inherited_fields"] = inherited_fields
    mode_fit["base_fit_inheritance"] = {
        "source_fit_json": str(base_fit_path),
        "source_root": base_fit.get("source_root"),
        "source_mode": base_mode,
        "policy": (
            "retain chunked prefill and first-token scalar terms; inherit "
            "decode service, PD transfer, and rate curves from the unchunked fit"
        ),
        "inherited_fields": inherited_fields,
        "retained_chunked_fields": [
            "cost_model.prefill_base_latency_ns",
            "cost_model.prefill_ns_per_token",
            "cost_model.first_token_latency_ns",
        ],
        "first_token_curve_base_latency_offset_ns": round(
            first_token_curve_offset_ns
        ),
    }


def required_stage_run_files(mode: str) -> list[str]:
    files = ["scheduler_trace.csv"]
    if is_pd_mode(mode):
        files.append("pd_stage_metrics.csv")
    return files


def required_request_latency_run_files(mode: str) -> list[str]:
    files = ["request_metrics.csv"]
    if is_pd_mode(mode):
        files.append("pd_stage_metrics.csv")
    return files


def is_overload_service_fit_exclusion(run_id: str) -> bool:
    return run_id.startswith("pilot_rate_sweep") and "__rps_16__" in run_id


def collect_transfer_samples(path: Path) -> list[tuple[float, float, float]]:
    transfer_samples: list[tuple[float, float, float]] = []
    for row in load_rows(path):
        if row.get("success_or_failure") != "success":
            continue
        duration_ns = positive_row_float(row, "transfer_duration_ns")
        transfer_bytes = positive_row_float(row, "transfer_bytes")
        if duration_ns <= 0 or transfer_bytes <= 0:
            continue
        transfer_samples.append((1.0, transfer_bytes, duration_ns))
    return transfer_samples


def collect_mode_samples(
    mode_root: Path,
    mode: str,
    skip_incomplete_runs: bool,
    skipped_runs: list[dict[str, str]],
) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    int,
    int,
]:
    prefill_samples: list[tuple[float, float, float]] = []
    decode_samples: list[tuple[float, float, float]] = []
    transfer_samples: list[tuple[float, float, float]] = []
    discovered_runs = 0
    used_runs = 0

    if not mode_root.exists():
        raise ValueError(f"Calibration mode directory does not exist: {mode_root}")

    for run_dir in sorted(path for path in mode_root.iterdir() if path.is_dir()):
        discovered_runs += 1
        missing = [
            name for name in required_stage_run_files(mode) if not (run_dir / name).exists()
        ]
        if missing:
            reason = "missing required file(s): " + ", ".join(missing)
            if not skip_incomplete_runs:
                raise ValueError(f"Incomplete run {run_dir}: {reason}")
            skipped_runs.append({"mode": mode, "run_id": run_dir.name, "reason": reason})
            continue

        rows = load_rows(run_dir / "scheduler_trace.csv")
        run_prefill, run_decode = collect_stage_samples(
            rows,
            stage_column="stage",
            duration_column="duration_ns",
            tokens_column="batch_total_tokens",
            decode_base_column="include_base_latency",
            require_batch_completed=True,
        )
        prefill_samples.extend(run_prefill)
        decode_samples.extend(run_decode)

        if is_pd_mode(mode):
            transfer_samples.extend(collect_transfer_samples(run_dir / "pd_stage_metrics.csv"))

        used_runs += 1

    return prefill_samples, decode_samples, transfer_samples, discovered_runs, used_runs


def collect_request_latency_samples(
    mode_root: Path,
    mode: str,
    skip_incomplete_runs: bool,
    skipped_runs: list[dict[str, str]],
    excluded_runs: list[dict[str, str]],
) -> tuple[
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[tuple[float, float, float]],
    list[float],
    dict[float, dict[str, Any]],
    dict[float, dict[str, Any]],
    int,
    int,
]:
    prefill_samples: list[tuple[float, float, float]] = []
    decode_samples: list[tuple[float, float, float]] = []
    transfer_samples: list[tuple[float, float, float]] = []
    first_token_residuals: list[float] = []
    colocated_chunked_service_baseline_prefill_samples: list[
        tuple[float, float, float]
    ] = []
    colocated_chunked_service_baseline_stage_waits: list[float] = []
    decode_curve_groups: dict[float, dict[str, Any]] = defaultdict(
        lambda: {
            "decode_duration_ns": 0.0,
            "output_tokens": 0.0,
            "samples": 0,
            "source_run_ids": set(),
            "overload_source_run_ids": set(),
            "decode_capacity_request_slots": 0.0,
        }
    )
    first_token_curve_groups: dict[float, dict[str, Any]] = defaultdict(
        lambda: {
            "samples": [],
            "source_run_ids": set(),
            "overload_source_run_ids": set(),
            "ttft_slo_ns": 0.0,
        }
    )
    discovered_runs = 0
    used_runs = 0

    if not mode_root.exists():
        raise ValueError(f"Calibration mode directory does not exist: {mode_root}")

    for run_dir in sorted(path for path in mode_root.iterdir() if path.is_dir()):
        discovered_runs += 1
        missing = [
            name
            for name in required_request_latency_run_files(mode)
            if not (run_dir / name).exists()
        ]
        if missing:
            reason = "missing required file(s): " + ", ".join(missing)
            if not skip_incomplete_runs:
                raise ValueError(f"Incomplete run {run_dir}: {reason}")
            skipped_runs.append({"mode": mode, "run_id": run_dir.name, "reason": reason})
            continue

        if is_pd_mode(mode):
            transfer_samples.extend(collect_transfer_samples(run_dir / "pd_stage_metrics.csv"))

        run_config = load_run_config(run_dir) if is_pd_mode(mode) else {}
        target_request_rate = (
            positive_json_float(run_config.get("target_request_rate"))
            if is_pd_mode(mode)
            else 0.0
        )
        overload_excluded = is_overload_service_fit_exclusion(run_dir.name)
        rows = load_rows(run_dir / "request_metrics.csv")
        if is_pd_mode(mode) and target_request_rate > 0.0:
            group = decode_curve_groups[target_request_rate]
            first_token_group = first_token_curve_groups[target_request_rate]
            ttft_slo_ns = positive_json_float(
                run_config.get("slo", {}).get("ttft_ns")
            )
            if ttft_slo_ns > 0.0:
                if first_token_group["ttft_slo_ns"] <= 0.0:
                    first_token_group["ttft_slo_ns"] = ttft_slo_ns
                else:
                    first_token_group["ttft_slo_ns"] = min(
                        first_token_group["ttft_slo_ns"], ttft_slo_ns
                    )
            capacity_slots = decode_capacity_request_slots(run_config)
            if capacity_slots > 0.0:
                if group["decode_capacity_request_slots"] <= 0.0:
                    group["decode_capacity_request_slots"] = capacity_slots
                else:
                    group["decode_capacity_request_slots"] = min(
                        group["decode_capacity_request_slots"], capacity_slots
                    )
            success_index = 0
            for row in rows:
                if row.get("success_or_failure") != "success":
                    continue
                output_tokens = positive_row_float(row, "output_tokens")
                decode_duration_ns = positive_row_float(row, "decode_duration_ns")
                if output_tokens <= 0 or decode_duration_ns <= 0:
                    continue
                group["decode_duration_ns"] += decode_duration_ns
                group["output_tokens"] += output_tokens
                group["samples"] += 1

                ttft_ns = positive_row_float(row, "ttft_ns")
                prefill_duration_ns = positive_row_float(row, "prefill_duration_ns")
                transfer_duration_ns = positive_row_float(row, "transfer_duration_ns")
                if ttft_ns > 0 and prefill_duration_ns > 0:
                    residual_ns = max(
                        0.0,
                        ttft_ns - prefill_duration_ns - transfer_duration_ns,
                    )
                    ttft_pass_value = row.get("ttft_slo_pass", "")
                    if ttft_pass_value != "":
                        ttft_pass = maybe_bool(ttft_pass_value, False)
                    elif ttft_slo_ns > 0.0:
                        ttft_pass = ttft_ns <= ttft_slo_ns
                    else:
                        ttft_pass = True
                    first_token_group["samples"].append(
                        {
                            "rank": float(success_index),
                            "residual_ns": residual_ns,
                            "prefill_duration_ns": prefill_duration_ns,
                            "transfer_duration_ns": transfer_duration_ns,
                            "ttft_slo_pass": ttft_pass,
                        }
                    )
                success_index += 1
            if group["samples"] > 0 or first_token_group["samples"]:
                group["source_run_ids"].add(run_dir.name)
                first_token_group["source_run_ids"].add(run_dir.name)
                if overload_excluded:
                    group["overload_source_run_ids"].add(run_dir.name)
                    first_token_group["overload_source_run_ids"].add(
                        run_dir.name
                    )

        if overload_excluded:
            excluded_runs.append(
                {
                    "mode": mode,
                    "run_id": run_dir.name,
                    "reason": OVERLOAD_SERVICE_FIT_EXCLUSION_REASON,
                }
            )
            continue

        for row in rows:
            if row.get("success_or_failure") != "success":
                continue

            prompt_tokens = positive_row_float(
                row, "server_prompt_tokens", "prompt_tokens"
            )
            prefill_duration_ns = positive_row_float(row, "prefill_duration_ns")
            prefill_chunk_count = positive_row_float(row, "prefill_chunk_count")
            prefill_service_ns = positive_row_float(row, "prefill_service_ns")
            if (
                is_chunked_mode(mode)
                and prompt_tokens > 0
                and prefill_chunk_count > 0
                and prefill_service_ns > 0
            ):
                prefill_sample = (
                    prefill_chunk_count,
                    prompt_tokens,
                    prefill_service_ns,
                )
                prefill_samples.append(prefill_sample)
                if (
                    mode == "colocated_chunked"
                    and run_dir.name.startswith("service_baseline")
                ):
                    colocated_chunked_service_baseline_prefill_samples.append(
                        prefill_sample
                    )
                    prefill_stage_wait_ns = positive_row_float(
                        row, "prefill_stage_wait_ns"
                    )
                    if prefill_stage_wait_ns > 0:
                        colocated_chunked_service_baseline_stage_waits.append(
                            prefill_stage_wait_ns
                        )
            elif prompt_tokens > 0 and prefill_duration_ns > 0:
                prefill_samples.append((1.0, prompt_tokens, prefill_duration_ns))

            output_tokens = positive_row_float(row, "output_tokens")
            decode_duration_ns = positive_row_float(row, "decode_duration_ns")
            if output_tokens > 0 and decode_duration_ns > 0:
                decode_samples.append((1.0, output_tokens, decode_duration_ns))

            ttft_ns = positive_row_float(row, "ttft_ns")
            transfer_duration_ns = positive_row_float(row, "transfer_duration_ns")
            if ttft_ns > 0 and prefill_duration_ns > 0:
                first_token_residuals.append(
                    max(0.0, ttft_ns - prefill_duration_ns - transfer_duration_ns)
                )

        used_runs += 1

    if (
        mode == "colocated_chunked"
        and colocated_chunked_service_baseline_prefill_samples
    ):
        prefill_samples = colocated_chunked_service_baseline_prefill_samples
        first_token_stage_wait_ns = percentile_or_zero(
            colocated_chunked_service_baseline_stage_waits, 0.10
        )
        if first_token_stage_wait_ns > 0:
            first_token_residuals = [first_token_stage_wait_ns]

    return (
        prefill_samples,
        decode_samples,
        transfer_samples,
        first_token_residuals,
        decode_curve_groups,
        first_token_curve_groups,
        discovered_runs,
        used_runs,
    )


def build_decode_step_latency_curve(
    groups: dict[float, dict[str, Any]],
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    points: list[dict[str, Any]] = []
    fit_points: list[dict[str, Any]] = []
    for request_rate in sorted(groups):
        group = groups[request_rate]
        output_tokens = group["output_tokens"]
        decode_duration_ns = group["decode_duration_ns"]
        if output_tokens <= 0.0:
            continue
        raw_decode_step_latency_ns = max(0, round(decode_duration_ns / output_tokens))
        adjusted_decode_step_latency_ns = raw_decode_step_latency_ns
        capacity_limited = False
        capacity_limited_step_latency_ns = 0
        if (
            group["decode_capacity_request_slots"] > 0.0
            and group["samples"] > 0
            and request_rate > 0.0
        ):
            mean_output_tokens = output_tokens / group["samples"]
            if mean_output_tokens > 0.0:
                capacity_limited_step_latency_ns = max(
                    0,
                    round(
                        1.0e9
                        * group["decode_capacity_request_slots"]
                        / (request_rate * mean_output_tokens)
                        * OVERLOAD_DECODE_STEP_CAPACITY_FACTOR
                    ),
                )
                if (
                    capacity_limited_step_latency_ns > 0
                    and raw_decode_step_latency_ns
                    > capacity_limited_step_latency_ns
                ):
                    adjusted_decode_step_latency_ns = (
                        capacity_limited_step_latency_ns
                    )
                    capacity_limited = True
        source_run_ids = sorted(group["source_run_ids"])
        overload_source_run_ids = sorted(group["overload_source_run_ids"])
        points.append(
            {
                "request_rate_per_second": request_rate,
                "decode_step_latency_ns": adjusted_decode_step_latency_ns,
            }
        )
        fit_points.append(
            {
                "request_rate_per_second": request_rate,
                "decode_step_latency_ns": adjusted_decode_step_latency_ns,
                "raw_request_decode_step_latency_ns": raw_decode_step_latency_ns,
                "capacity_limited_step_latency_ns": capacity_limited_step_latency_ns,
                "capacity_limited": capacity_limited,
                "capacity_factor": OVERLOAD_DECODE_STEP_CAPACITY_FACTOR,
                "samples": group["samples"],
                "output_tokens": output_tokens,
                "decode_duration_ns": decode_duration_ns,
                "decode_capacity_request_slots": group[
                    "decode_capacity_request_slots"
                ],
                "source_run_ids": source_run_ids,
                "overload_source_run_ids": overload_source_run_ids,
                "overload_only": bool(source_run_ids)
                and len(overload_source_run_ids) == len(source_run_ids),
            }
        )

    if not points:
        return None, None
    curve = {
        "enabled": True,
        "signal": "target_request_rate_per_second",
        "interpolation": "linear",
        "extrapolation": "clamp",
        "points": points,
    }
    curve_fit = {
        "method": "successful_request_decode_duration_per_output_token_by_target_rate",
        "signal": "target_request_rate_per_second",
        "points": fit_points,
    }
    return curve, curve_fit


def solve_rank_linear_backpressure(
    samples: list[dict[str, Any]],
    knee_request_index: int,
) -> tuple[float, float, float, int]:
    if not samples:
        return 0.0, 0.0, 0.0, 0
    features = [
        max(0.0, sample["rank"] - float(knee_request_index))
        for sample in samples
    ]
    targets = [sample["residual_ns"] for sample in samples]
    n = float(len(samples))
    sx = sum(features)
    sy = sum(targets)
    sxx = sum(feature * feature for feature in features)
    sxy = sum(feature * target for feature, target in zip(features, targets))
    determinant = n * sxx - sx * sx
    if abs(determinant) < 1e-12:
        base = sy / n
        slope = 0.0
    else:
        base = (sy * sxx - sx * sxy) / determinant
        slope = (n * sxy - sx * sy) / determinant
    if slope < 0.0:
        slope = 0.0
        base = sy / n
    if base < 0.0:
        base = 0.0
        slope = sxy / sxx if sxx > 0.0 else 0.0

    squared_error = 0.0
    predicted_ttft_pass_count = 0
    for sample, feature, target in zip(samples, features, targets):
        prediction = base + slope * feature
        squared_error += (prediction - target) ** 2
        ttft_slo_ns = sample.get("ttft_slo_ns", 0.0)
        if ttft_slo_ns <= 0.0:
            continue
        predicted_ttft_ns = (
            sample["prefill_duration_ns"]
            + sample["transfer_duration_ns"]
            + prediction
        )
        if predicted_ttft_ns <= ttft_slo_ns:
            predicted_ttft_pass_count += 1
    rmse = math.sqrt(squared_error / len(samples))
    return base, slope, rmse, predicted_ttft_pass_count


def fit_rank_linear_backpressure(
    samples: list[dict[str, Any]],
) -> dict[str, Any]:
    if not samples:
        raise ValueError("Need at least one first-token sample.")
    max_rank = max(int(sample["rank"]) for sample in samples)
    best: dict[str, Any] | None = None
    for knee in range(max_rank + 1):
        base, slope, rmse, predicted_ttft_pass_count = (
            solve_rank_linear_backpressure(samples, knee)
        )
        candidate = {
            "base_latency_ns": max(0, round(base)),
            "knee_request_index": float(knee),
            "latency_ns_per_request_after_knee": max(0.0, slope),
            "rmse_ns": rmse,
            "predicted_ttft_pass_count": predicted_ttft_pass_count,
        }
        if best is None or candidate["rmse_ns"] < best["rmse_ns"]:
            best = candidate
    assert best is not None
    return best


def build_first_token_backpressure_curve(
    groups: dict[float, dict[str, Any]],
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    points: list[dict[str, Any]] = []
    fit_points: list[dict[str, Any]] = []
    for request_rate in sorted(groups):
        group = groups[request_rate]
        samples = group["samples"]
        if not samples:
            continue
        for sample in samples:
            sample["ttft_slo_ns"] = group["ttft_slo_ns"]
        fit = fit_rank_linear_backpressure(samples)
        source_run_ids = sorted(group["source_run_ids"])
        overload_source_run_ids = sorted(group["overload_source_run_ids"])
        points.append(
            {
                "request_rate_per_second": request_rate,
                "base_latency_ns": fit["base_latency_ns"],
                "knee_request_index": fit["knee_request_index"],
                "latency_ns_per_request_after_knee": fit[
                    "latency_ns_per_request_after_knee"
                ],
            }
        )
        fit_points.append(
            {
                "request_rate_per_second": request_rate,
                "base_latency_ns": fit["base_latency_ns"],
                "knee_request_index": fit["knee_request_index"],
                "latency_ns_per_request_after_knee": fit[
                    "latency_ns_per_request_after_knee"
                ],
                "samples": len(samples),
                "rmse_ns": fit["rmse_ns"],
                "golden_ttft_pass_count": sum(
                    1 for sample in samples if sample["ttft_slo_pass"]
                ),
                "predicted_ttft_pass_count": fit[
                    "predicted_ttft_pass_count"
                ],
                "ttft_slo_ns": group["ttft_slo_ns"],
                "source_run_ids": source_run_ids,
                "overload_source_run_ids": overload_source_run_ids,
                "overload_only": bool(source_run_ids)
                and len(overload_source_run_ids) == len(source_run_ids),
            }
        )

    if not points:
        return None, None
    curve = {
        "enabled": True,
        "signal": "target_request_rate_per_second",
        "model": "arrival_rank_linear",
        "interpolation": "linear",
        "extrapolation": "clamp",
        "points": points,
    }
    curve_fit = {
        "method": "ttft_residual_arrival_rank_linear",
        "signal": "target_request_rate_per_second",
        "model": "arrival_rank_linear",
        "points": fit_points,
    }
    return curve, curve_fit


def fit_transfer(samples: list[tuple[float, float, float]]) -> dict[str, Any]:
    if len(samples) < 2:
        raise ValueError("Need at least two transfer samples to fit PD transfer.")
    fit_method = "ols"
    try:
        latency_ns, ns_per_byte = solve_two_feature_regression(samples)
    except ValueError:
        latency_ns = 0.0
        ns_per_byte = sum(
            target / feature_b
            for _, feature_b, target in samples
            if feature_b > 0.0
        ) / len(samples)
        fit_method = "fallback_zero_latency"
    if ns_per_byte <= 0:
        raise ValueError("PD transfer fit produced non-positive ns_per_byte.")
    bandwidth_bytes_per_s = 1.0e9 / ns_per_byte
    return {
        "transfer": {
            "enabled": True,
            "latency_ns": max(0, round(latency_ns)),
            "bandwidth_bytes_per_s": bandwidth_bytes_per_s,
            "efficiency": 1.0,
            "overlap_enabled": False,
            "bytes_per_prompt_token": 0,
            "override_bytes_per_prompt_token": False,
        },
        "transfer_fit": {
            "samples": len(samples),
            "fit_method": fit_method,
            "latency_ns": latency_ns,
            "ns_per_byte": ns_per_byte,
            "bandwidth_bytes_per_s": bandwidth_bytes_per_s,
            "rmse_ns": compute_rmse(samples, latency_ns, ns_per_byte),
            **PROXY_TRANSFER_TIMING,
        },
    }


def fit_calibration_root(
    root: Path,
    modes: list[str],
    run_scope: str,
    skip_incomplete_runs: bool,
    fit_target: str,
    simulator_fidelity_adjustments: bool,
    base_fit_path: Path | None,
) -> dict[str, Any]:
    if run_scope != "all":
        raise ValueError("Only --run-scope all is currently supported.")
    if fit_target not in FIT_TARGETS:
        raise ValueError(f"Unsupported fit target: {fit_target}")
    if base_fit_path is not None and fit_target != "request_latency":
        raise ValueError("--base-fit-json requires --fit-target request_latency.")

    skipped_runs: list[dict[str, str]] = []
    excluded_runs: list[dict[str, str]] = []
    mode_results: dict[str, Any] = {}
    base_fit = load_json(base_fit_path) if base_fit_path is not None else None

    for mode in modes:
        if fit_target == "stage":
            (
                prefill_samples,
                decode_samples,
                transfer_samples,
                discovered_runs,
                used_runs,
            ) = collect_mode_samples(root / mode, mode, skip_incomplete_runs, skipped_runs)
        else:
            (
                prefill_samples,
                decode_samples,
                transfer_samples,
                first_token_residuals,
                decode_curve_groups,
                first_token_curve_groups,
                discovered_runs,
                used_runs,
            ) = collect_request_latency_samples(
                root / mode,
                mode,
                skip_incomplete_runs,
                skipped_runs,
                excluded_runs,
            )
            if (
                is_chunked_mode(mode)
                and len({sample[0] for sample in prefill_samples}) <= 1
            ):
                prefill_samples = [
                    (0.0, token_feature, target)
                    for _, token_feature, target in prefill_samples
                ]
        mode_fit = fit_stage_samples(prefill_samples, decode_samples)
        if fit_target == "request_latency":
            cost_model = mode_fit["cost_model"]
            cost_model["decode_step_latency_ns"] = cost_model["decode_ns_per_token"]
            cost_model["decode_ns_per_token"] = 0
            cost_model["first_token_timing"] = "decode_start"
            cost_model["first_token_latency_ns"] = max(
                0, round(mean_or_zero(first_token_residuals))
            )
            mode_fit["fit_stats"]["first_token_samples"] = len(
                first_token_residuals
            )
            mode_fit["fit_stats"][
                "first_token_latency_method"
            ] = "mean_ttft_minus_prefill_minus_transfer"
            if mode == "colocated_chunked":
                mode_fit["fit_stats"][
                    "first_token_latency_method"
                ] = "service_baseline_prefill_stage_wait_p10"
                mode_fit["fit_stats"][
                    "prefill_fit_scope"
                ] = "service_baseline_chunked_prefill_service"
                mode_fit["scheduler_replay_adjustment"] = {
                    "scheduler_policy": "prefill_first",
                    "method": "simulator_run_grid_search",
                    "reason": (
                        "golden colocated chunked traces admit prefill chunks "
                        "ahead of decode backlog during service-baseline replay"
                    ),
                }
            if simulator_fidelity_adjustments and mode in SIMULATOR_FIDELITY_ADJUSTMENTS:
                adjustment = SIMULATOR_FIDELITY_ADJUSTMENTS[mode]
                original_costs = {
                    "prefill_base_latency_ns": cost_model["prefill_base_latency_ns"],
                    "prefill_ns_per_token": cost_model["prefill_ns_per_token"],
                    "decode_step_latency_ns": cost_model["decode_step_latency_ns"],
                }
                cost_model["prefill_base_latency_ns"] = max(
                    0,
                    round(
                        cost_model["prefill_base_latency_ns"]
                        * adjustment["prefill_scale"]
                    ),
                )
                cost_model["prefill_ns_per_token"] = max(
                    0,
                    round(
                        cost_model["prefill_ns_per_token"]
                        * adjustment["prefill_scale"]
                    ),
                )
                cost_model["decode_step_latency_ns"] = max(
                    0,
                    round(
                        cost_model["decode_step_latency_ns"]
                        * adjustment["decode_step_scale"]
                    ),
                )
                mode_fit["simulator_fidelity_adjustment"] = {
                    "enabled": True,
                    "method": adjustment["method"],
                    "reason": adjustment["reason"],
                    "prefill_scale": adjustment["prefill_scale"],
                    "decode_step_scale": adjustment["decode_step_scale"],
                    "unadjusted_costs": original_costs,
                }
            if is_pd_mode(mode):
                decode_curve, decode_curve_fit = build_decode_step_latency_curve(
                    decode_curve_groups
                )
                if decode_curve is not None:
                    cost_model["decode_step_latency_curve"] = decode_curve
                    mode_fit["decode_step_latency_curve_fit"] = decode_curve_fit
                    mode_fit["fit_stats"]["decode_step_latency_curve_points"] = len(
                        decode_curve["points"]
                    )
                first_token_curve, first_token_curve_fit = (
                    build_first_token_backpressure_curve(first_token_curve_groups)
                )
                if first_token_curve is not None:
                    cost_model["first_token_backpressure_curve"] = (
                        first_token_curve
                    )
                    mode_fit["first_token_backpressure_curve_fit"] = (
                        first_token_curve_fit
                    )
                    mode_fit["fit_stats"][
                        "first_token_backpressure_curve_points"
                    ] = len(first_token_curve["points"])
        mode_fit["source_runs"] = {
            "discovered": discovered_runs,
            "used": used_runs,
        }
        if is_pd_mode(mode):
            mode_fit.update(fit_transfer(transfer_samples))
        if base_fit is not None and is_chunked_mode(mode):
            inherit_base_fit_for_chunked_mode(
                mode_fit,
                mode,
                base_fit,
                base_fit_path,
            )
        mode_results[mode] = mode_fit

    return {
        "source_root": str(root.resolve()),
        "run_scope": run_scope,
        "fit_target": fit_target,
        "simulator_fidelity_adjustments": simulator_fidelity_adjustments,
        "base_fit_json": str(base_fit_path) if base_fit_path is not None else None,
        "modes": mode_results,
        "skipped_runs": skipped_runs,
        "excluded_runs": excluded_runs,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fit a serving cost_model block from batch duration measurements."
    )
    parser.add_argument("--input", help="CSV input, often an event trace CSV.")
    parser.add_argument(
        "--calibration-root",
        help="Calibration root containing mode/run directories to aggregate.",
    )
    parser.add_argument("--output", help="Optional JSON output path.")
    parser.add_argument(
        "--modes",
        default=",".join(DEFAULT_MODES),
        help="Comma-separated modes for --calibration-root.",
    )
    parser.add_argument("--run-scope", default="all", choices=["all"])
    parser.add_argument(
        "--fit-target",
        default="stage",
        choices=FIT_TARGETS,
        help="Fit stage durations from scheduler_trace.csv or request-level latency metrics.",
    )
    parser.add_argument(
        "--skip-incomplete-runs",
        action="store_true",
        help="Skip run directories missing required aggregate-fit files.",
    )
    parser.add_argument(
        "--simulator-fidelity-adjustments",
        action="store_true",
        help=(
            "Apply fit-time corrections derived from simulator-vs-calibration "
            "comparisons for directly runnable request-latency artifacts."
        ),
    )
    parser.add_argument(
        "--base-fit-json",
        help=(
            "Optional request-latency fit to inherit stable base terms from. "
            "For chunked modes this keeps chunk-specific prefill timing from "
            "the calibration root while inheriting decode/transfer/rate curves "
            "from the corresponding unchunked mode."
        ),
    )
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
    if bool(args.input) == bool(args.calibration_root):
        raise SystemExit("Specify exactly one of --input or --calibration-root.")
    if args.base_fit_json and not args.calibration_root:
        raise SystemExit("--base-fit-json requires --calibration-root.")

    if args.calibration_root:
        fitted = fit_calibration_root(
            Path(args.calibration_root),
            modes=parse_modes(args.modes),
            run_scope=args.run_scope,
            skip_incomplete_runs=args.skip_incomplete_runs,
            fit_target=args.fit_target,
            simulator_fidelity_adjustments=args.simulator_fidelity_adjustments,
            base_fit_path=Path(args.base_fit_json) if args.base_fit_json else None,
        )
    else:
        if args.fit_target != "stage":
            raise SystemExit("--fit-target request_latency requires --calibration-root.")
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
