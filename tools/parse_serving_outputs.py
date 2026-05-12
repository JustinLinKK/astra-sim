#!/usr/bin/env python3
"""Normalize serving summary outputs into a flat CSV or JSON table."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any


SUMMARY_SUFFIXES = ("summary.json", "_summary.json")


def find_summary_files(paths: list[str]) -> list[Path]:
    summary_files: list[Path] = []
    for raw_path in paths:
        path = Path(raw_path)
        if path.is_file() and path.name.endswith(".json"):
            summary_files.append(path)
            continue
        if path.is_dir():
            for candidate in sorted(path.rglob("*.json")):
                if any(candidate.name.endswith(suffix) for suffix in SUMMARY_SUFFIXES):
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


def summary_to_row(summary_path: Path) -> dict[str, Any]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    metadata = maybe_load_metadata(summary_path)

    goodput = summary.get("goodput", {})
    row = {
        "case_name": coerce_summary_path_to_case_name(summary_path),
        "source_summary": str(summary_path),
        "architecture": summary.get("architecture"),
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
