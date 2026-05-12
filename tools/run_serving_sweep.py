#!/usr/bin/env python3
"""Run a parameter sweep over serving request JSON overrides."""

from __future__ import annotations

import argparse
import itertools
import json
import re
import subprocess
from collections import OrderedDict
from copy import deepcopy
from pathlib import Path
from typing import Any


DEFAULT_BINARY = (
    Path("build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware")
)

TEMPLATE_PATH_KEYS = {
    "request_configuration",
    "request_metrics_output",
    "request_summary_output",
    "request_run_metadata_output",
    "workload_configuration",
    "comm_group_configuration",
    "system_configuration",
    "remote_memory_configuration",
    "network_configuration",
    "logging_configuration",
    "logging_folder",
}


def parse_scalar(text: str) -> Any:
    lowered = text.lower()
    if lowered in {"true", "false"}:
        return lowered == "true"
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def sanitize_token(value: Any) -> str:
    token = str(value)
    token = token.replace("/", "_")
    token = re.sub(r"[^A-Za-z0-9._-]+", "_", token)
    return token.strip("_") or "value"


def parse_axis(spec: str) -> tuple[str, list[Any]]:
    if "=" not in spec:
        raise ValueError(f"Invalid sweep axis '{spec}'. Expected path=value1,value2.")
    path, raw_values = spec.split("=", 1)
    values = [parse_scalar(item) for item in raw_values.split(",") if item]
    if not values:
        raise ValueError(f"Sweep axis '{spec}' has no values.")
    return path, values


def set_nested_value(root: dict[str, Any], dotted_path: str, value: Any) -> None:
    current: dict[str, Any] = root
    keys = dotted_path.split(".")
    for key in keys[:-1]:
        if key not in current or not isinstance(current[key], dict):
            current[key] = {}
        current = current[key]
    current[keys[-1]] = value


def load_flat_yaml(path: Path) -> OrderedDict[str, Any]:
    data: OrderedDict[str, Any] = OrderedDict()
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if line[0].isspace():
            raise ValueError(
                f"{path}:{line_number} is indented. run_serving_sweep.py only supports flat top-level YAML templates."
            )
        key, sep, value = line.partition(":")
        if not sep:
            continue
        data[key.strip()] = parse_scalar(value.strip())
    return data


def dump_flat_yaml(data: OrderedDict[str, Any]) -> str:
    lines: list[str] = []
    for key, value in data.items():
        if isinstance(value, bool):
            rendered = "true" if value else "false"
        elif isinstance(value, (int, float)):
            rendered = str(value)
        elif value is None:
            rendered = "null"
        else:
            rendered = json.dumps(str(value))
        lines.append(f"{key}: {rendered}")
    return "\n".join(lines) + "\n"


def resolve_template_paths(
    data: OrderedDict[str, Any], template_path: Path
) -> OrderedDict[str, Any]:
    resolved = OrderedDict(data)
    template_dir = template_path.resolve().parent
    for key, value in resolved.items():
        if key not in TEMPLATE_PATH_KEYS:
            continue
        if not isinstance(value, str) or value in {"", "empty"}:
            continue
        resolved[key] = str((template_dir / value).resolve())
    return resolved


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a serving sweep by patching a base request JSON.",
    )
    parser.add_argument("--analytical-template", required=True, help="Flat serving analytical YAML template.")
    parser.add_argument("--request-config", required=True, help="Base serving request JSON.")
    parser.add_argument("--output-dir", required=True, help="Directory for generated configs and outputs.")
    parser.add_argument("--binary", default=str(DEFAULT_BINARY), help="Analytical binary to execute.")
    parser.add_argument(
        "--set",
        action="append",
        default=[],
        metavar="PATH=V1,V2",
        help="Apply a JSON override sweep on a dotted path, for example runtime.architecture=serial_baseline,colocated.",
    )
    parser.add_argument(
        "--emit-event-trace",
        action="store_true",
        help="Override outputs.event_trace_output in each generated request config.",
    )
    parser.add_argument("--case-prefix", default="case")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    analytical_template_path = Path(args.analytical_template)
    analytical_template = resolve_template_paths(
        load_flat_yaml(analytical_template_path),
        analytical_template_path,
    )
    base_request_config = json.loads(Path(args.request_config).read_text(encoding="utf-8"))
    output_root = Path(args.output_dir).resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    axes = [parse_axis(spec) for spec in args.set]
    if not axes:
        axes = [("__identity__", ["baseline"])]

    axis_names = [name for name, _ in axes]
    axis_values = [values for _, values in axes]

    manifest: list[dict[str, Any]] = []
    binary_path = Path(args.binary).resolve()

    for case_index, combination in enumerate(itertools.product(*axis_values), 1):
        request_config = deepcopy(base_request_config)
        case_tokens = [args.case_prefix, f"{case_index:03d}"]
        applied_overrides: dict[str, Any] = {}
        for axis_name, value in zip(axis_names, combination):
            if axis_name == "__identity__":
                continue
            set_nested_value(request_config, axis_name, value)
            applied_overrides[axis_name] = value
            case_tokens.append(f"{axis_name.split('.')[-1]}_{sanitize_token(value)}")

        case_name = "__".join(case_tokens)
        case_dir = (output_root / case_name).resolve()
        case_dir.mkdir(parents=True, exist_ok=True)

        request_json_path = (case_dir / "request.json").resolve()
        summary_json_path = (case_dir / "summary.json").resolve()
        metrics_csv_path = (case_dir / "metrics.csv").resolve()
        metadata_json_path = (case_dir / "metadata.json").resolve()
        stdout_path = (case_dir / "stdout.txt").resolve()
        analytical_yaml_path = (case_dir / "analytical.yaml").resolve()

        if args.emit_event_trace:
            set_nested_value(
                request_config,
                "outputs.event_trace_output",
                str((case_dir / "event_trace.csv").resolve()),
            )

        request_json_path.write_text(
            json.dumps(request_config, indent=2) + "\n",
            encoding="utf-8",
        )

        analytical_case = OrderedDict(analytical_template)
        analytical_case["request_configuration"] = str(request_json_path)
        analytical_case["request_metrics_output"] = str(metrics_csv_path)
        analytical_case["request_summary_output"] = str(summary_json_path)
        analytical_case["request_run_metadata_output"] = str(metadata_json_path)
        analytical_yaml_path.write_text(
            dump_flat_yaml(analytical_case),
            encoding="utf-8",
        )

        manifest_entry = {
            "case_name": case_name,
            "request_config": str(request_json_path),
            "analytical_config": str(analytical_yaml_path),
            "summary": str(summary_json_path),
            "metrics": str(metrics_csv_path),
            "metadata": str(metadata_json_path),
            "stdout": str(stdout_path),
            "applied_overrides": applied_overrides,
        }
        manifest.append(manifest_entry)

        if args.dry_run:
            continue

        with stdout_path.open("w", encoding="utf-8") as stdout_file:
            subprocess.run(
                [str(binary_path), f"--analytical-config={analytical_yaml_path}"],
                check=True,
                stdout=stdout_file,
                stderr=subprocess.STDOUT,
            )

    (output_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote {len(manifest)} sweep case(s) to {output_root}")


if __name__ == "__main__":
    main()
