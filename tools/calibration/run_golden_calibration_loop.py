#!/usr/bin/env python3
"""Fit, simulate, compare, and validate against golden serving calibration data."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


DEFAULT_MODES = "colocated,pd_disaggregated"
DEFAULT_FIT_NAME = "fitted_unchunked_latency_cost_model.json"
DEFAULT_BINARY = "build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware"


def script_dir() -> Path:
    return Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--calibration-root", required=True)
    parser.add_argument("--modes", default=DEFAULT_MODES)
    parser.add_argument("--fit-json", help="Defaults to fitted_unchunked_latency_cost_model.json under the calibration root.")
    parser.add_argument("--output-dir", help="Defaults to sim_vs_calibrated under the calibration root.")
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--skip-fit", action="store_true")
    parser.add_argument("--skip-simulation", action="store_true")
    parser.add_argument(
        "--no-simulator-fidelity-adjustments",
        action="store_true",
        help="Disable simulator-fidelity fit adjustments for the request-latency artifact.",
    )
    parser.add_argument("--latency-mape-threshold-pct", type=float, default=15.0)
    parser.add_argument("--throughput-mape-threshold-pct", type=float, default=1.0)
    parser.add_argument("--overload-latency-mape-target-pct", type=float, default=50.0)
    parser.add_argument("--overload-throughput-mape-target-pct", type=float, default=100.0)
    parser.add_argument("--overload-run-marker", default="__rps_16__")
    parser.add_argument("--fail-on-acceptance", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    root = Path(args.calibration_root).resolve()
    fit_json = (
        Path(args.fit_json).resolve()
        if args.fit_json
        else root / DEFAULT_FIT_NAME
    )
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / "sim_vs_calibrated"

    fit_script = script_dir() / "fit_serving_cost_model.py"
    compare_script = script_dir() / "plot_simulation_vs_calibration.py"

    if not args.skip_fit:
        fit_command = [
            sys.executable,
            str(fit_script),
            "--calibration-root",
            str(root),
            "--modes",
            args.modes,
            "--run-scope",
            "all",
            "--fit-target",
            "request_latency",
            "--skip-incomplete-runs",
            "--output",
            str(fit_json),
        ]
        if not args.no_simulator_fidelity_adjustments:
            fit_command.append("--simulator-fidelity-adjustments")
        subprocess.run(fit_command, check=True)

    compare_command = [
        sys.executable,
        str(compare_script),
        "--calibration-root",
        str(root),
        "--modes",
        args.modes,
        "--fit-json",
        str(fit_json),
        "--output-dir",
        str(output_dir),
        "--binary",
        args.binary,
        "--latency-mape-threshold-pct",
        str(args.latency_mape_threshold_pct),
        "--throughput-mape-threshold-pct",
        str(args.throughput_mape_threshold_pct),
        "--overload-latency-mape-target-pct",
        str(args.overload_latency_mape_target_pct),
        "--overload-throughput-mape-target-pct",
        str(args.overload_throughput_mape_target_pct),
        "--overload-run-marker",
        args.overload_run_marker,
    ]
    if args.skip_simulation:
        compare_command.append("--skip-simulation")
    if args.fail_on_acceptance:
        compare_command.append("--fail-on-acceptance")
    subprocess.run(compare_command, check=True)


if __name__ == "__main__":
    main()
