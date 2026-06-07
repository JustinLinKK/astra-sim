#!/usr/bin/env python3
"""Run calibrated ASTRA-sim cases and plot simulation versus measured data."""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
from collections import defaultdict
from pathlib import Path
from typing import Any


DEFAULT_BINARY = Path("build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware")
DEFAULT_CHUNKED_PREFILL_FIT_NAME = "fitted_chunked_prefill_latency_cost_model.json"
DEFAULT_STAGE_FIT_NAME = "fitted_unchunked_cost_model.json"
DEFAULT_LATENCY_FIT_NAME = "fitted_unchunked_latency_cost_model.json"
DEFAULT_MODES = ("colocated", "pd_disaggregated")
LATENCY_ACCEPTANCE_METRICS = ("ttft_mean_ns", "tpot_mean_ns", "e2e_mean_ns")
THROUGHPUT_ACCEPTANCE_METRICS = (
    "goodput_reqs_per_sec",
    "request_throughput_reqs_per_sec",
)
ACCEPTANCE_METRICS = LATENCY_ACCEPTANCE_METRICS + THROUGHPUT_ACCEPTANCE_METRICS


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def default_fit_path(root: Path) -> Path:
    chunked_fit = root / DEFAULT_CHUNKED_PREFILL_FIT_NAME
    if chunked_fit.exists():
        return chunked_fit
    latency_fit = root / DEFAULT_LATENCY_FIT_NAME
    if latency_fit.exists():
        return latency_fit
    return root / DEFAULT_STAGE_FIT_NAME


def to_int(value: Any, default: int = 0) -> int:
    if value is None or value == "":
        return default
    return int(float(value))


def to_float(value: Any, default: float = 0.0) -> float:
    if value is None or value == "":
        return default
    return float(value)


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    index = max(0, min(len(sorted_values) - 1, math.ceil(q * len(sorted_values)) - 1))
    return sorted_values[index]


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def format_rate_tick(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    return f"{value:g}"


def format_axis_tick(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    if abs(value) >= 10.0:
        return f"{value:.1f}".rstrip("0").rstrip(".")
    if abs(value) >= 1.0:
        return f"{value:.2f}".rstrip("0").rstrip(".")
    return f"{value:g}"


def nice_linear_ticks(max_value: float, tick_count: int = 5) -> list[float]:
    if max_value <= 0.0:
        return [0.0, 1.0]
    raw_step = max_value / tick_count
    magnitude = 10.0 ** math.floor(math.log10(raw_step))
    step = magnitude
    for multiplier in (1.0, 2.0, 5.0, 10.0):
        candidate = multiplier * magnitude
        if raw_step <= candidate:
            step = candidate
            break
    nice_max = math.ceil(max_value / step) * step
    count = int(round(nice_max / step))
    return [index * step for index in range(count + 1)]


def nice_log_ticks(min_value: float, max_value: float) -> tuple[float, float, list[float]]:
    multipliers = (1.0, 2.0, 5.0)
    start_exp = math.floor(math.log10(min_value)) - 1
    end_exp = math.ceil(math.log10(max_value)) + 1
    candidates = [
        multiplier * (10.0**exponent)
        for exponent in range(start_exp, end_exp + 1)
        for multiplier in multipliers
    ]
    candidates = sorted(set(candidates))
    lower = max(value for value in candidates if value <= min_value)
    upper = min(value for value in candidates if value >= max_value)
    ticks = [value for value in candidates if lower <= value <= upper]
    return lower, upper, ticks


def run_family(run_id: str) -> str:
    for prefix in ("service_baseline", "pilot_rate_sweep", "rate_sweep_near_knee"):
        if run_id.startswith(prefix):
            return prefix
    return "other"


def parse_modes(value: str) -> list[str]:
    modes = [mode.strip() for mode in value.split(",") if mode.strip()]
    if not modes:
        raise ValueError("At least one mode is required.")
    return modes


def is_pd_mode(mode: str) -> bool:
    return mode in {"pd_disaggregated", "pd_disaggregated_chunked"}


def runtime_architecture_for_mode(mode: str, run_config: dict[str, Any]) -> str:
    if mode == "colocated_chunked":
        return "colocated_chunked"
    if mode == "pd_disaggregated_chunked":
        return "pd_disaggregated"
    runtime_architecture = run_config.get("runtime_architecture")
    if isinstance(runtime_architecture, str) and runtime_architecture:
        return runtime_architecture
    return mode


def measured_summary(run_dir: Path, mode: str) -> dict[str, Any]:
    run_config = load_json(run_dir / "run_config.json")
    benchmark = load_json(run_dir / "benchmark_result.json")
    request_rows = [
        row
        for row in load_csv(run_dir / "request_metrics.csv")
        if row.get("success_or_failure") == "success"
    ]
    ttft = [to_float(row.get("ttft_ns")) for row in request_rows]
    tpot = [to_float(row.get("tpot_ns")) for row in request_rows]
    e2e = [to_float(row.get("e2e_ns")) for row in request_rows]
    good_requests = sum(1 for row in request_rows if row.get("request_good") == "true")
    request_count = len(request_rows)

    return {
        "source": "calibrated",
        "mode": mode,
        "run_id": run_dir.name,
        "run_family": run_family(run_dir.name),
        "target_request_rate": to_float(run_config.get("target_request_rate")),
        "request_count": request_count,
        "good_requests": good_requests,
        "request_throughput_reqs_per_sec": to_float(benchmark.get("request_throughput")),
        "goodput_reqs_per_sec": to_float(benchmark.get("request_goodput")),
        "output_token_throughput_tokens_per_sec": to_float(
            benchmark.get("output_throughput")
        ),
        "ttft_mean_ns": mean(ttft),
        "ttft_p99_ns": percentile(ttft, 0.99),
        "tpot_mean_ns": mean(tpot),
        "e2e_mean_ns": mean(e2e),
        "e2e_p99_ns": percentile(e2e, 0.99),
    }


def requests_from_measured_metrics(run_dir: Path) -> list[dict[str, int]]:
    rows = [
        row
        for row in load_csv(run_dir / "request_metrics.csv")
        if row.get("success_or_failure") == "success"
    ]
    rows.sort(key=lambda row: to_int(row.get("arrival_ts")))
    if not rows:
        return []
    first_arrival = to_int(rows[0].get("arrival_ts"))
    requests: list[dict[str, int]] = []
    for index, row in enumerate(rows):
        prompt_tokens = to_int(row.get("server_prompt_tokens")) or to_int(
            row.get("prompt_tokens"), 1
        )
        output_tokens = to_int(row.get("requested_output_tokens")) or to_int(
            row.get("output_tokens"), 1
        )
        requests.append(
            {
                "request_id": index,
                "arrival_time_ns": to_int(row.get("arrival_ts")) - first_arrival,
                "prompt_tokens": max(1, prompt_tokens),
                "output_tokens": max(1, output_tokens),
            }
        )
    return requests


def model_config(run_config: dict[str, Any]) -> dict[str, Any]:
    model = run_config.get("model", {})
    return {
        "name": model.get("model_name_or_path", "calibrated-llm"),
        "num_layers": to_int(model.get("num_layers")),
        "hidden_size": to_int(model.get("hidden_size")),
        "attention_heads": to_int(model.get("num_attention_heads")),
        "kv_heads": to_int(model.get("num_key_value_heads")),
        "head_dim": to_int(model.get("head_dim")),
        "bytes_per_kv_element": 2,
    }


def scheduler_config(run_config: dict[str, Any]) -> dict[str, Any]:
    scheduler = run_config.get("scheduler", {})
    policy = scheduler.get("scheduler_policy", "decode_first")
    policy = {
        "fcfs": "fcfs",
        "decode_first": "decode_first",
        "prefill_first": "prefill_first",
        "balanced": "balanced",
        "serial": "serial",
    }.get(policy, "decode_first")
    return {
        "max_running_requests": to_int(scheduler.get("max_running_requests"), 128),
        "scheduler_policy": policy,
        "max_prefill_batch_tokens": to_int(
            scheduler.get("max_prefill_batch_tokens"), 8192
        ),
        "max_decode_batch_requests": to_int(
            scheduler.get("max_decode_batch_requests")
            or scheduler.get("max_decode_batch_size"),
            128,
        ),
        "chunked_prefill_size": to_int(scheduler.get("chunked_prefill_size"), 0),
        "prefill_max_requests": to_int(scheduler.get("prefill_max_requests"), 128),
        "enable_mixed_chunk": bool(scheduler.get("enable_mixed_chunk", False)),
    }


def slo_config(run_config: dict[str, Any]) -> dict[str, Any]:
    slo = run_config.get("slo", {})
    return {
        "ttft_ns": to_int(slo.get("ttft_ns")),
        "tpot_ns": to_float(slo.get("tpot_ns")),
        "e2e_ns": to_int(slo.get("e2e_ns")),
    }


def pd_config(run_config: dict[str, Any], transfer: dict[str, Any]) -> dict[str, Any]:
    scheduler = run_config.get("scheduler", {})
    layout = run_config.get("layout", {})
    return {
        "prefill_workers": to_int(layout.get("prefill_worker_count"), 1),
        "decode_workers": to_int(layout.get("decode_worker_count"), 1),
        "prefill_max_batch_tokens": to_int(
            scheduler.get("max_prefill_batch_tokens"), 8192
        ),
        "prefill_max_requests": to_int(scheduler.get("prefill_max_requests"), 128),
        "decode_max_batch_requests": to_int(
            scheduler.get("max_decode_batch_requests")
            or scheduler.get("max_decode_batch_size"),
            128,
        ),
        "prefill_tp_degree": to_int(layout.get("tp_degree"), 1),
        "decode_tp_degree": to_int(layout.get("tp_degree"), 1),
        "transfer": transfer,
    }


def add_benchmark_window_config(
    request_config: dict[str, Any],
    run_config: dict[str, Any],
) -> None:
    measurement_duration_s = to_float(run_config.get("measurement_duration_s"))
    if measurement_duration_s <= 0.0:
        return
    warmup_duration_s = to_float(run_config.get("warmup_duration_s"))
    request_config["benchmark_start_ns"] = max(
        0, round(warmup_duration_s * 1.0e9)
    )
    request_config["benchmark_duration_ns"] = max(
        1, round(measurement_duration_s * 1.0e9)
    )


def build_request_config(
    mode: str,
    run_dir: Path,
    fit: dict[str, Any],
) -> dict[str, Any]:
    run_config = load_json(run_dir / "run_config.json")
    mode_fit = fit["modes"][mode]
    target_request_rate = to_float(run_config.get("target_request_rate"))
    request_config: dict[str, Any] = {
        "runtime": {
            "architecture": runtime_architecture_for_mode(mode, run_config),
            "seed": 0,
        },
        "scheduler": scheduler_config(run_config),
        "slo": slo_config(run_config),
        "model": model_config(run_config),
        "cost_model": dict(mode_fit["cost_model"]),
        "requests": requests_from_measured_metrics(run_dir),
    }
    scheduler_replay_adjustment = mode_fit.get("scheduler_replay_adjustment", {})
    if scheduler_replay_adjustment.get("scheduler_policy"):
        request_config["scheduler"]["scheduler_policy"] = scheduler_replay_adjustment[
            "scheduler_policy"
        ]
    if target_request_rate > 0.0:
        request_config["target_request_rate_per_second"] = target_request_rate
    if is_pd_mode(mode):
        request_config["pd"] = pd_config(run_config, mode_fit["transfer"])
    add_benchmark_window_config(request_config, run_config)
    return request_config


def write_analytical_yaml(path: Path, request_json: Path, summary_json: Path, metrics_csv: Path, metadata_json: Path) -> None:
    root = Path.cwd()
    lines = [
        "mode: serving_disagg_colocated",
        f"request_configuration: {request_json}",
        f"request_metrics_output: {metrics_csv}",
        f"request_summary_output: {summary_json}",
        f"request_run_metadata_output: {metadata_json}",
        "workload_configuration: empty",
        "comm_group_configuration: empty",
        f"system_configuration: {root / 'tests/rt_serving_baseline/inputs/system_cfg.json'}",
        f"remote_memory_configuration: {root / 'tests/rt_serving_baseline/inputs/remote_memory_cfg.json'}",
        f"network_configuration: {root / 'tests/rt_serving_baseline/inputs/network_cfg.yml'}",
        "logging_configuration: empty",
        f"logging_folder: {path.parent / 'log'}",
        "num_queues_per_dim: 1",
        "compute_scale: 1.0",
        "comm_scale: 1.0",
        "injection_scale: 1.0",
        "rendezvous_protocol: false",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_simulation(binary: Path, case_dir: Path, request_config: dict[str, Any]) -> dict[str, Any]:
    case_dir.mkdir(parents=True, exist_ok=True)
    request_json = case_dir / "request.json"
    summary_json = case_dir / "summary.json"
    metrics_csv = case_dir / "metrics.csv"
    metadata_json = case_dir / "metadata.json"
    analytical_yaml = case_dir / "analytical.yaml"
    stdout_path = case_dir / "stdout.txt"

    request_json.write_text(json.dumps(request_config, indent=2) + "\n", encoding="utf-8")
    write_analytical_yaml(analytical_yaml, request_json, summary_json, metrics_csv, metadata_json)
    with stdout_path.open("w", encoding="utf-8") as stdout:
        subprocess.run(
            [str(binary), f"--analytical-config={analytical_yaml}"],
            check=True,
            stdout=stdout,
            stderr=subprocess.STDOUT,
        )
    return load_json(summary_json)


def simulation_summary(
    sim_summary: dict[str, Any],
    measured: dict[str, Any],
) -> dict[str, Any]:
    goodput = sim_summary.get("goodput", {})
    return {
        "source": "simulation",
        "mode": measured["mode"],
        "run_id": measured["run_id"],
        "run_family": measured["run_family"],
        "target_request_rate": measured["target_request_rate"],
        "request_count": sim_summary.get("num_requests", 0),
        "good_requests": goodput.get("good_requests", 0),
        "request_throughput_reqs_per_sec": sim_summary.get(
            "request_throughput_reqs_per_sec", 0.0
        ),
        "goodput_reqs_per_sec": goodput.get("goodput_reqs_per_sec", 0.0),
        "output_token_throughput_tokens_per_sec": sim_summary.get(
            "output_token_throughput_tokens_per_sec", 0.0
        ),
        "ttft_mean_ns": sim_summary.get("ttft_ns", {}).get("mean", 0.0),
        "ttft_p99_ns": sim_summary.get("ttft_ns", {}).get("p99", 0.0),
        "tpot_mean_ns": sim_summary.get("tpot_ns", {}).get("mean", 0.0),
        "e2e_mean_ns": sim_summary.get("e2e_ns", {}).get("mean", 0.0),
        "e2e_p99_ns": sim_summary.get("e2e_ns", {}).get("p99", 0.0),
    }


def write_comparison_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "source",
        "mode",
        "run_id",
        "run_family",
        "target_request_rate",
        "request_count",
        "good_requests",
        "request_throughput_reqs_per_sec",
        "goodput_reqs_per_sec",
        "output_token_throughput_tokens_per_sec",
        "ttft_mean_ns",
        "ttft_p99_ns",
        "tpot_mean_ns",
        "e2e_mean_ns",
        "e2e_p99_ns",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def is_overloaded_run(run_id: str, overload_run_marker: str) -> bool:
    return overload_run_marker != "" and overload_run_marker in run_id


def metric_threshold_pct(
    metric: str,
    latency_mape_threshold_pct: float,
    throughput_mape_threshold_pct: float,
) -> float:
    if metric in LATENCY_ACCEPTANCE_METRICS:
        return latency_mape_threshold_pct
    return throughput_mape_threshold_pct


def paired_comparison_rows(
    rows: list[dict[str, Any]],
) -> dict[tuple[str, str], dict[str, dict[str, Any]]]:
    pairs: dict[tuple[str, str], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in rows:
        pairs[(str(row["mode"]), str(row["run_id"]))][str(row["source"])] = row
    return {
        key: pair
        for key, pair in pairs.items()
        if "calibrated" in pair and "simulation" in pair
    }


def summarize_metric_for_pairs(
    pairs: list[dict[str, dict[str, Any]]],
    metric: str,
    threshold_pct: float | None,
) -> dict[str, Any]:
    absolute_percentage_errors: list[float] = []
    skipped_zero_baseline = 0
    for pair in pairs:
        calibrated_value = to_float(pair["calibrated"].get(metric))
        simulation_value = to_float(pair["simulation"].get(metric))
        if calibrated_value == 0.0:
            skipped_zero_baseline += 1
            continue
        absolute_percentage_errors.append(
            abs(simulation_value - calibrated_value) / abs(calibrated_value) * 100.0
        )

    mape_pct = mean(absolute_percentage_errors)
    result: dict[str, Any] = {
        "mape_pct": mape_pct,
        "paired_runs": len(absolute_percentage_errors),
        "skipped_zero_baseline": skipped_zero_baseline,
    }
    if threshold_pct is not None:
        result["threshold_pct"] = threshold_pct
        result["pass"] = bool(absolute_percentage_errors and mape_pct <= threshold_pct)
    return result


def build_validation_summary(
    rows: list[dict[str, Any]],
    mode_names: list[str],
    latency_mape_threshold_pct: float,
    throughput_mape_threshold_pct: float,
    overload_latency_mape_target_pct: float,
    overload_throughput_mape_target_pct: float,
    overload_run_marker: str,
) -> dict[str, Any]:
    paired_rows = paired_comparison_rows(rows)
    mode_summaries: dict[str, Any] = {}
    failing_metrics: list[dict[str, Any]] = []

    for mode in mode_names:
        mode_pairs = [
            pair
            for (pair_mode, _), pair in paired_rows.items()
            if pair_mode == mode
        ]
        acceptance_pairs = [
            pair
            for pair in mode_pairs
            if not is_overloaded_run(
                str(pair["calibrated"]["run_id"]), overload_run_marker
            )
        ]
        overload_pairs = [
            pair
            for pair in mode_pairs
            if is_overloaded_run(
                str(pair["calibrated"]["run_id"]), overload_run_marker
            )
        ]

        metric_results: dict[str, Any] = {}
        mode_pass = True
        for metric in ACCEPTANCE_METRICS:
            threshold_pct = metric_threshold_pct(
                metric,
                latency_mape_threshold_pct,
                throughput_mape_threshold_pct,
            )
            metric_result = summarize_metric_for_pairs(
                acceptance_pairs, metric, threshold_pct
            )
            metric_results[metric] = metric_result
            if not metric_result["pass"]:
                mode_pass = False
                failing_metrics.append(
                    {
                        "mode": mode,
                        "metric": metric,
                        "mape_pct": metric_result["mape_pct"],
                        "threshold_pct": threshold_pct,
                    }
                )

        overload_metric_results: dict[str, Any] = {}
        overload_target_pass = True
        for metric in ACCEPTANCE_METRICS:
            threshold_pct = metric_threshold_pct(
                metric,
                overload_latency_mape_target_pct,
                overload_throughput_mape_target_pct,
            )
            metric_result = summarize_metric_for_pairs(
                overload_pairs, metric, threshold_pct
            )
            overload_metric_results[metric] = metric_result
            if not metric_result["pass"]:
                overload_target_pass = False

        mode_summaries[mode] = {
            "acceptance": {
                "scope": "non_overloaded",
                "paired_runs": len(acceptance_pairs),
                "pass": mode_pass,
                "metrics": metric_results,
            },
            "overload_diagnostic": {
                "scope": "overload_only",
                "paired_runs": len(overload_pairs),
                "target_pass": overload_target_pass,
                "metrics": overload_metric_results,
            },
        }

    return {
        "acceptance_scope": "non_overloaded",
        "overload_run_marker": overload_run_marker,
        "latency_mape_threshold_pct": latency_mape_threshold_pct,
        "throughput_mape_threshold_pct": throughput_mape_threshold_pct,
        "overload_latency_mape_target_pct": overload_latency_mape_target_pct,
        "overload_throughput_mape_target_pct": overload_throughput_mape_target_pct,
        "overall_pass": not failing_metrics,
        "modes": mode_summaries,
        "failing_metrics": failing_metrics,
    }


def write_validation_summary(
    json_path: Path,
    csv_path: Path,
    summary: dict[str, Any],
) -> None:
    json_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    fieldnames = [
        "mode",
        "scope",
        "metric",
        "mape_pct",
        "threshold_pct",
        "pass",
        "paired_runs",
        "skipped_zero_baseline",
    ]
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for mode, mode_summary in summary["modes"].items():
            for scope_name in ("acceptance", "overload_diagnostic"):
                scope_summary = mode_summary[scope_name]
                for metric, metric_summary in scope_summary["metrics"].items():
                    writer.writerow(
                        {
                            "mode": mode,
                            "scope": scope_summary["scope"],
                            "metric": metric,
                            "mape_pct": metric_summary["mape_pct"],
                            "threshold_pct": metric_summary.get("threshold_pct", ""),
                            "pass": metric_summary.get("pass", ""),
                            "paired_runs": metric_summary["paired_runs"],
                            "skipped_zero_baseline": metric_summary[
                                "skipped_zero_baseline"
                            ],
                        }
                    )


def aggregate_plot_points(
    rows: list[dict[str, Any]],
    metric: str,
) -> dict[str, list[tuple[float, float]]]:
    grouped_values: dict[tuple[str, float], list[float]] = defaultdict(list)
    for row in rows:
        series = f"{row['mode']} {row['source']}"
        grouped_values[(series, float(row["target_request_rate"]))].append(float(row[metric]))

    points: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for (series, x_value), values in grouped_values.items():
        points[series].append((x_value, mean(values)))
    return points


def write_svg_plot(
    path: Path,
    rows: list[dict[str, Any]],
    metric: str,
    title: str,
    ylabel: str,
    scale: float = 1.0,
    y_scale: str = "linear",
    x_scale: str = "linear",
) -> None:
    grouped = aggregate_plot_points(rows, metric)
    if not grouped:
        return

    width = 980
    height = 620
    left = 90
    right = 280
    top = 64
    bottom = 82
    plot_width = width - left - right
    plot_height = height - top - bottom
    colors = {
        "colocated calibrated": "#1565c0",
        "colocated simulation": "#ef6c00",
        "colocated_chunked calibrated": "#00838f",
        "colocated_chunked simulation": "#f4511e",
        "pd_disaggregated calibrated": "#2e7d32",
        "pd_disaggregated simulation": "#6a1b9a",
        "pd_disaggregated_chunked calibrated": "#558b2f",
        "pd_disaggregated_chunked simulation": "#8e24aa",
    }

    all_points = [
        (x_value, y_value * scale)
        for points in grouped.values()
        for x_value, y_value in points
    ]
    x_min = min(x for x, _ in all_points)
    x_max = max(x for x, _ in all_points)
    observed_x_values = sorted({x for x, _ in all_points})
    x_rank = {
        value: float(index)
        for index, value in enumerate(observed_x_values)
    }
    use_ordinal_x = x_scale == "ordinal"
    use_log_x = x_scale == "log2" and x_min > 0.0
    positive_y_values = [y for _, y in all_points if y > 0.0]
    use_log_y = y_scale == "log10" and len(positive_y_values) == len(all_points)
    y_max_data = max(y for _, y in all_points)
    if use_ordinal_x:
        x_min_plot = 0.0
        x_max_plot = float(max(1, len(observed_x_values) - 1))
        if len(observed_x_values) == 1:
            x_min_plot -= 0.5
            x_max_plot += 0.5
        x_tick_values = observed_x_values
    elif use_log_x:
        x_min_plot = math.log2(x_min)
        x_max_plot = math.log2(x_max)
        if x_min_plot == x_max_plot:
            x_min_plot -= 0.5
            x_max_plot += 0.5
        x_tick_values = [
            2.0**exponent
            for exponent in range(
                math.floor(math.log2(x_min)),
                math.ceil(math.log2(x_max)) + 1,
            )
            if x_min <= 2.0**exponent <= x_max
        ]
        if len(observed_x_values) <= 12:
            x_tick_values = observed_x_values
    else:
        if x_min == x_max:
            padding = max(0.5, abs(x_min) * 0.10)
            x_min -= padding
            x_max += padding
        x_min_plot = x_min
        x_max_plot = x_max
        if len(observed_x_values) <= 12:
            x_tick_values = observed_x_values
        else:
            tick_count = 5
            x_tick_values = [
                x_min + (x_max - x_min) * index / tick_count
                for index in range(tick_count + 1)
            ]
    if use_log_y:
        y_min, y_max, y_tick_values = nice_log_ticks(
            min(positive_y_values), max(positive_y_values)
        )
        y_min_plot = math.log10(y_min)
        y_max_plot = math.log10(y_max)
    else:
        y_tick_values = nice_linear_ticks(y_max_data * 1.08)
        y_min = y_tick_values[0]
        y_max = y_tick_values[-1]
        y_min_plot = y_min
        y_max_plot = y_max

    def sx(value: float) -> float:
        if use_ordinal_x:
            x_value = x_rank[value]
        elif use_log_x:
            x_value = math.log2(value)
        else:
            x_value = value
        return left + (x_value - x_min_plot) / (x_max_plot - x_min_plot) * plot_width

    def sy(value: float) -> float:
        y_value = math.log10(value) if use_log_y else value
        return top + plot_height - (y_value - y_min_plot) / (y_max_plot - y_min_plot) * plot_height

    svg: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="{width}" height="{height}" fill="white" />',
        f'<text x="{width / 2}" y="34" text-anchor="middle" font-size="22" font-family="sans-serif">{title}</text>',
        f'<line x1="{left}" y1="{top + plot_height}" x2="{left + plot_width}" y2="{top + plot_height}" stroke="#333" />',
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_height}" stroke="#333" />',
    ]

    for x_value in x_tick_values:
        x_pos = sx(x_value)
        svg.append(f'<line x1="{x_pos:.1f}" y1="{top}" x2="{x_pos:.1f}" y2="{top + plot_height}" stroke="#e4e4e4" />')
        svg.append(f'<text x="{x_pos:.1f}" y="{top + plot_height + 24}" text-anchor="middle" font-size="12" font-family="sans-serif">{format_rate_tick(x_value)}</text>')
    for y_value in y_tick_values:
        y_pos = sy(y_value)
        svg.append(f'<line x1="{left}" y1="{y_pos:.1f}" x2="{left + plot_width}" y2="{y_pos:.1f}" stroke="#e4e4e4" />')
        svg.append(f'<text x="{left - 10}" y="{y_pos + 4:.1f}" text-anchor="end" font-size="12" font-family="sans-serif">{format_axis_tick(y_value)}</text>')

    x_axis_label = (
        "Target request rate (req/s, log2 scale)"
        if use_log_x
        else (
            "Target request rate (req/s, evenly spaced observed rates)"
            if use_ordinal_x
            else "Target request rate (req/s, linear scale)"
        )
    )
    svg.append(f'<text x="{left + plot_width / 2}" y="{height - 28}" text-anchor="middle" font-size="14" font-family="sans-serif">{x_axis_label}</text>')
    y_axis_label = f"{ylabel} (log scale)" if use_log_y else ylabel
    svg.append(f'<text x="25" y="{top + plot_height / 2}" text-anchor="middle" font-size="14" font-family="sans-serif" transform="rotate(-90 25 {top + plot_height / 2})">{y_axis_label}</text>')

    legend_x = left + plot_width + 28
    legend_y = top + 20
    for index, (series, points) in enumerate(sorted(grouped.items())):
        points = sorted(points)
        color = colors.get(series, "#444")
        scaled_points = [(sx(x), sy(y * scale)) for x, y in points]
        polyline = " ".join(f"{x:.1f},{y:.1f}" for x, y in scaled_points)
        dash = ' stroke-dasharray="6 4"' if "simulation" in series else ""
        svg.append(f'<polyline points="{polyline}" fill="none" stroke="{color}" stroke-width="2.5"{dash} />')
        for x_pos, y_pos in scaled_points:
            svg.append(f'<circle cx="{x_pos:.1f}" cy="{y_pos:.1f}" r="4" fill="{color}" />')
        entry_y = legend_y + index * 25
        svg.append(f'<line x1="{legend_x}" y1="{entry_y}" x2="{legend_x + 24}" y2="{entry_y}" stroke="{color}" stroke-width="2.5"{dash} />')
        svg.append(f'<text x="{legend_x + 34}" y="{entry_y + 5}" font-size="12" font-family="sans-serif">{series}</text>')

    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--calibration-root", required=True)
    parser.add_argument(
        "--modes",
        default=",".join(DEFAULT_MODES),
        help="Comma-separated calibration mode directories to compare.",
    )
    parser.add_argument(
        "--fit-json",
        help=(
            "Fit artifact to use. Defaults to fitted_unchunked_latency_cost_model.json "
            "under the calibration root when present, otherwise fitted_unchunked_cost_model.json."
        ),
    )
    parser.add_argument("--output-dir", help="Defaults to sim_vs_calibrated under the calibration root.")
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--skip-simulation", action="store_true")
    parser.add_argument(
        "--x-scale",
        choices=["ordinal", "linear", "log2"],
        default="ordinal",
        help="X-axis scaling for request-rate plots.",
    )
    parser.add_argument(
        "--latency-mape-threshold-pct",
        type=float,
        default=15.0,
        help="Acceptance threshold for non-overloaded TTFT/TPOT/E2E MAPE.",
    )
    parser.add_argument(
        "--throughput-mape-threshold-pct",
        type=float,
        default=1.0,
        help="Acceptance threshold for non-overloaded goodput/throughput MAPE.",
    )
    parser.add_argument(
        "--overload-run-marker",
        default="__rps_16__",
        help="Run-id marker excluded from acceptance and kept as overload diagnostics.",
    )
    parser.add_argument(
        "--overload-latency-mape-target-pct",
        type=float,
        default=50.0,
        help="Diagnostic target for overload-only TTFT/TPOT/E2E MAPE.",
    )
    parser.add_argument(
        "--overload-throughput-mape-target-pct",
        type=float,
        default=100.0,
        help="Diagnostic target for overload-only goodput/throughput MAPE.",
    )
    parser.add_argument(
        "--fail-on-acceptance",
        action="store_true",
        help="Exit nonzero when non-overloaded acceptance metrics miss thresholds.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    root = Path(args.calibration_root).resolve()
    fit_path = Path(args.fit_json).resolve() if args.fit_json else default_fit_path(root)
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / "sim_vs_calibrated"
    binary = Path(args.binary).resolve()
    modes = parse_modes(args.modes)
    if not binary.exists() and not args.skip_simulation:
        raise SystemExit(f"Simulation binary does not exist: {binary}")

    fit = load_json(fit_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    simulation_root = output_dir / "simulation_runs"
    rows: list[dict[str, Any]] = []

    for mode in modes:
        mode_root = root / mode
        if not mode_root.exists():
            raise SystemExit(f"Calibration mode directory does not exist: {mode_root}")
        for run_dir in sorted(path for path in mode_root.iterdir() if path.is_dir()):
            required = [run_dir / "run_config.json", run_dir / "benchmark_result.json", run_dir / "request_metrics.csv"]
            if any(not path.exists() for path in required):
                continue
            measured = measured_summary(run_dir, mode)
            rows.append(measured)
            if args.skip_simulation:
                continue
            request_config = build_request_config(mode, run_dir, fit)
            case_dir = simulation_root / mode / run_dir.name
            sim_summary = run_simulation(binary, case_dir, request_config)
            rows.append(simulation_summary(sim_summary, measured))

    comparison_csv = output_dir / "comparison_summary.csv"
    write_comparison_csv(comparison_csv, rows)
    validation_summary = build_validation_summary(
        rows,
        mode_names=modes,
        latency_mape_threshold_pct=args.latency_mape_threshold_pct,
        throughput_mape_threshold_pct=args.throughput_mape_threshold_pct,
        overload_latency_mape_target_pct=args.overload_latency_mape_target_pct,
        overload_throughput_mape_target_pct=args.overload_throughput_mape_target_pct,
        overload_run_marker=args.overload_run_marker,
    )
    validation_summary_json = output_dir / "validation_summary.json"
    validation_summary_csv = output_dir / "validation_summary.csv"
    write_validation_summary(
        validation_summary_json, validation_summary_csv, validation_summary
    )

    plots_dir = output_dir / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)
    write_svg_plot(
        plots_dir / "ttft_mean_ms.svg",
        rows,
        "ttft_mean_ns",
        "Mean TTFT: simulation vs calibrated",
        "Mean TTFT (ms)",
        scale=1.0e-6,
        y_scale="log10",
        x_scale=args.x_scale,
    )
    write_svg_plot(
        plots_dir / "tpot_mean_ms.svg",
        rows,
        "tpot_mean_ns",
        "Mean TPOT: simulation vs calibrated",
        "Mean TPOT (ms)",
        scale=1.0e-6,
        x_scale=args.x_scale,
    )
    write_svg_plot(
        plots_dir / "e2e_mean_ms.svg",
        rows,
        "e2e_mean_ns",
        "Mean E2E: simulation vs calibrated",
        "Mean E2E (ms)",
        scale=1.0e-6,
        x_scale=args.x_scale,
    )
    write_svg_plot(
        plots_dir / "goodput_rps.svg",
        rows,
        "goodput_reqs_per_sec",
        "Goodput: simulation vs calibrated",
        "Goodput (req/s)",
        x_scale=args.x_scale,
    )

    print(f"Wrote comparison CSV to {comparison_csv}")
    print(f"Wrote validation summary to {validation_summary_json}")
    print(f"Wrote SVG plots to {plots_dir}")
    print(f"Used fit artifact {fit_path}")
    print(
        "Acceptance "
        + ("PASS" if validation_summary["overall_pass"] else "FAIL")
        + " for non-overloaded points"
    )
    if args.fail_on_acceptance and not validation_summary["overall_pass"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
