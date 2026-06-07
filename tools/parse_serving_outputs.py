#!/usr/bin/env python3
"""Normalize serving summary outputs into a flat CSV or JSON table."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


SUMMARY_SUFFIXES = ("summary.json", "_summary.json")


def is_summary_object(path: Path) -> bool:
    try:
        return isinstance(json.loads(path.read_text(encoding="utf-8")), dict)
    except json.JSONDecodeError:
        return False


def find_summary_files(paths: list[str]) -> list[Path]:
    summary_files: list[Path] = []
    for raw_path in paths:
        path = Path(raw_path)
        if path.is_file() and path.name.endswith(".json"):
            if is_summary_object(path):
                summary_files.append(path)
            continue
        if path.is_dir():
            for candidate in sorted(path.rglob("*.json")):
                if any(
                    candidate.name.endswith(suffix)
                    for suffix in SUMMARY_SUFFIXES
                ) and is_summary_object(candidate):
                    summary_files.append(candidate)
            continue
        raise FileNotFoundError(f"Input path does not exist: {path}")
    return summary_files


def coerce_summary_path_to_case_name(path: Path) -> str:
    name = path.stem
    if name.endswith("_summary"):
        return name[: -len("_summary")]
    if name == "summary":
        return path.parent.name
    return name


def maybe_load_metadata(summary_path: Path) -> dict[str, Any]:
    sibling_candidates = [
        summary_path.with_name("metadata.json"),
        summary_path.with_name("request_metadata.json"),
        summary_path.with_name(summary_path.name.replace("summary", "metadata")),
    ]
    for candidate in sibling_candidates:
        if candidate.exists():
            return json.loads(candidate.read_text(encoding="utf-8"))
    return {}


def maybe_load_request_config(
    summary_path: Path, metadata: dict[str, Any]
) -> dict[str, Any]:
    candidates: list[Path] = []
    config_path = metadata.get("config_path")
    if isinstance(config_path, str) and config_path:
        candidates.append(Path(config_path))
    candidates.append(summary_path.with_name("request.json"))

    for candidate in candidates:
        if candidate.exists():
            return json.loads(candidate.read_text(encoding="utf-8"))
    return {}


def nested_get(root: dict[str, Any], dotted_path: str) -> Any:
    current: Any = root
    for key in dotted_path.split("."):
        if not isinstance(current, dict) or key not in current:
            return None
        current = current[key]
    return current


def token_distribution_mean(config: dict[str, Any], dotted_path: str) -> float | None:
    distribution = nested_get(config, dotted_path)
    if not isinstance(distribution, dict):
        return None
    if distribution.get("distribution") == "constant":
        value = distribution.get("value")
        return float(value) if isinstance(value, (int, float)) else None

    minimum = distribution.get("min")
    maximum = distribution.get("max")
    if isinstance(minimum, (int, float)) and isinstance(maximum, (int, float)):
        return (float(minimum) + float(maximum)) / 2.0
    return None


def summary_to_row(summary_path: Path) -> dict[str, Any]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    metadata = maybe_load_metadata(summary_path)
    request_config = maybe_load_request_config(summary_path, metadata)
    sweep = request_config.get("sweep", {})
    if not isinstance(sweep, dict):
        sweep = {}

    goodput = summary.get("goodput", {})
    row = {
        "case_name": sweep.get("case_name")
        or coerce_summary_path_to_case_name(summary_path),
        "source_summary": str(summary_path),
        "architecture": summary.get("architecture"),
        "scale_family": sweep.get("scale_family"),
        "calibration_scope": sweep.get("calibration_scope"),
        "prefill_logical_gpus": sweep.get("prefill_logical_gpus"),
        "decode_logical_gpus": sweep.get("decode_logical_gpus"),
        "total_logical_gpus": sweep.get("total_logical_gpus"),
        "pd_prefill_workers": nested_get(request_config, "pd.prefill_workers"),
        "pd_decode_workers": nested_get(request_config, "pd.decode_workers"),
        "pd_prefill_tp_degree": nested_get(
            request_config, "pd.prefill_tp_degree"
        ),
        "pd_decode_tp_degree": nested_get(request_config, "pd.decode_tp_degree"),
        "topology_deployment": nested_get(request_config, "topology.deployment"),
        "topology_prefill_layout_name": nested_get(
            request_config, "topology.prefill_layout.name"
        ),
        "topology_decode_layout_name": nested_get(
            request_config, "topology.decode_layout.name"
        ),
        "topology_prefill_tp_degree": nested_get(
            request_config, "topology.prefill_layout.tp_degree"
        ),
        "topology_decode_tp_degree": nested_get(
            request_config, "topology.decode_layout.tp_degree"
        ),
        "topology_prefill_pp_degree": nested_get(
            request_config, "topology.prefill_layout.pp_degree"
        ),
        "topology_decode_pp_degree": nested_get(
            request_config, "topology.decode_layout.pp_degree"
        ),
        "topology_prefill_ep_degree": nested_get(
            request_config, "topology.prefill_layout.ep_degree"
        ),
        "topology_decode_ep_degree": nested_get(
            request_config, "topology.decode_layout.ep_degree"
        ),
        "topology_prefill_dp_attention_degree": nested_get(
            request_config, "topology.prefill_layout.dp_attention_degree"
        ),
        "topology_decode_dp_attention_degree": nested_get(
            request_config, "topology.decode_layout.dp_attention_degree"
        ),
        "topology_prefill_dp_replica_count": nested_get(
            request_config, "topology.prefill_layout.dp_replica_count"
        ),
        "topology_decode_dp_replica_count": nested_get(
            request_config, "topology.decode_layout.dp_replica_count"
        ),
        "target_request_rate_per_second": nested_get(
            request_config, "target_request_rate_per_second"
        ),
        "trace_request_rate_per_second": nested_get(
            request_config, "trace.request_rate_per_second"
        ),
        "trace_num_requests": nested_get(request_config, "trace.num_requests"),
        "prompt_tokens_distribution": nested_get(
            request_config, "trace.prompt_tokens.distribution"
        ),
        "prompt_tokens_min": nested_get(request_config, "trace.prompt_tokens.min"),
        "prompt_tokens_max": nested_get(request_config, "trace.prompt_tokens.max"),
        "prompt_tokens_mean": token_distribution_mean(
            request_config, "trace.prompt_tokens"
        ),
        "output_tokens_distribution": nested_get(
            request_config, "trace.output_tokens.distribution"
        ),
        "output_tokens_min": nested_get(request_config, "trace.output_tokens.min"),
        "output_tokens_max": nested_get(request_config, "trace.output_tokens.max"),
        "output_tokens_mean": token_distribution_mean(
            request_config, "trace.output_tokens"
        ),
        "pressure_family": sweep.get("pressure_family"),
        "num_requests": summary.get("num_requests"),
        "total_prompt_tokens": summary.get("total_prompt_tokens"),
        "total_output_tokens": summary.get("total_output_tokens"),
        "makespan_ns": summary.get("makespan_ns"),
        "request_throughput_reqs_per_sec": summary.get(
            "request_throughput_reqs_per_sec"
        ),
        "output_token_throughput_tokens_per_sec": summary.get(
            "output_token_throughput_tokens_per_sec"
        ),
        "queue_delay_mean_ns": summary.get("queue_delay_ns", {}).get("mean"),
        "prefill_queue_delay_mean_ns": summary.get(
            "prefill_queue_delay_ns", {}
        ).get("mean"),
        "decode_queue_delay_mean_ns": summary.get("decode_queue_delay_ns", {}).get(
            "mean"
        ),
        "transfer_duration_mean_ns": summary.get("transfer_duration_ns", {}).get(
            "mean"
        ),
        "ttft_mean_ns": summary.get("ttft_ns", {}).get("mean"),
        "ttft_p99_ns": summary.get("ttft_ns", {}).get("p99"),
        "tpot_mean_ns": summary.get("tpot_ns", {}).get("mean"),
        "e2e_mean_ns": summary.get("e2e_ns", {}).get("mean"),
        "e2e_p99_ns": summary.get("e2e_ns", {}).get("p99"),
        "good_requests": goodput.get("good_requests"),
        "bad_requests": goodput.get("bad_requests"),
        "slo_attainment_fraction": goodput.get("slo_attainment_fraction"),
        "goodput_reqs_per_sec": goodput.get("goodput_reqs_per_sec"),
        "runtime_seed": metadata.get("runtime_seed"),
        "trace_seed": metadata.get("trace_seed"),
        "request_config_path": metadata.get("config_path"),
        "binary": metadata.get("binary"),
    }
    return row


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def print_brief(rows: list[dict[str, Any]]) -> None:
    if not rows:
        print("No serving summary files found.")
        return
    header = (
        "case_name",
        "architecture",
        "ttft_mean_ns",
        "e2e_mean_ns",
        "slo_attainment_fraction",
        "output_token_throughput_tokens_per_sec",
    )
    print(",".join(header))
    for row in rows:
        print(",".join(str(row[column]) for column in header))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect serving summary JSON files into a flat table."
    )
    parser.add_argument("inputs", nargs="+", help="Summary JSON files or directories.")
    parser.add_argument("--csv-output", help="Optional CSV output path.")
    parser.add_argument("--json-output", help="Optional JSON output path.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    summary_files = find_summary_files(args.inputs)
    rows = [summary_to_row(path) for path in summary_files]
    rows.sort(key=lambda row: row["case_name"])

    if args.csv_output:
        write_csv(Path(args.csv_output), rows)
    if args.json_output:
        Path(args.json_output).write_text(
            json.dumps(rows, indent=2) + "\n",
            encoding="utf-8",
        )
    print_brief(rows)


if __name__ == "__main__":
    main()
