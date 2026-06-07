#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
OUTPUT_DIR=${SCRIPT_DIR}/outputs
FIT_SCRIPT=${SCRIPT_DIR}/../../tools/calibration/fit_serving_cost_model.py

rm -rf ${OUTPUT_DIR}
mkdir -p ${OUTPUT_DIR}
ROOT_FIT_JSON=$(mktemp)
ROOT_STDOUT=$(mktemp)
LATENCY_FIT_JSON=$(mktemp)
LATENCY_STDOUT=$(mktemp)
ADJUSTED_LATENCY_FIT_JSON=$(mktemp)
ADJUSTED_LATENCY_STDOUT=$(mktemp)
trap 'rm -f "${ROOT_FIT_JSON}" "${ROOT_STDOUT}" "${LATENCY_FIT_JSON}" "${LATENCY_STDOUT}" "${ADJUSTED_LATENCY_FIT_JSON}" "${ADJUSTED_LATENCY_STDOUT}"' EXIT

echo "[$0] Fitting calibration anchors..."
${FIT_SCRIPT} \
    --input ${SCRIPT_DIR}/inputs/calibration_anchor.csv \
    --output ${OUTPUT_DIR}/fitted_cost_model.json > ${OUTPUT_DIR}/stdout.txt

python3 - "${OUTPUT_DIR}/fitted_cost_model.json" <<'PY'
import json
import sys

fit = json.load(open(sys.argv[1]))
cost = fit["cost_model"]
stats = fit["fit_stats"]

assert abs(cost["prefill_base_latency_ns"] - 100) <= 1
assert abs(cost["prefill_ns_per_token"] - 20) <= 1
assert abs(cost["decode_base_latency_ns"] - 200) <= 1
assert abs(cost["decode_ns_per_token"] - 5) <= 1
assert stats["prefill_rmse_ns"] <= 1e-9
assert stats["decode_rmse_ns"] <= 1e-9
PY

echo "[$0] Fitting aggregate calibration root..."
${FIT_SCRIPT} \
    --calibration-root ${SCRIPT_DIR}/inputs/calibration_root \
    --modes colocated,pd_disaggregated \
    --run-scope all \
    --skip-incomplete-runs \
    --output ${ROOT_FIT_JSON} > ${ROOT_STDOUT}

python3 - "${ROOT_FIT_JSON}" <<'PY'
import json
import sys

fit = json.load(open(sys.argv[1]))
colocated = fit["modes"]["colocated"]
pd = fit["modes"]["pd_disaggregated"]

assert fit["fit_target"] == "stage"
assert colocated["source_runs"] == {"discovered": 1, "used": 1}
assert pd["source_runs"] == {"discovered": 3, "used": 2}
assert fit["skipped_runs"] == [
    {
        "mode": "pd_disaggregated",
        "run_id": "incomplete_run",
        "reason": "missing required file(s): scheduler_trace.csv, pd_stage_metrics.csv",
    }
]

colocated_cost = colocated["cost_model"]
assert abs(colocated_cost["prefill_base_latency_ns"] - 10) <= 1
assert abs(colocated_cost["prefill_ns_per_token"] - 2) <= 1
assert abs(colocated_cost["decode_base_latency_ns"] - 20) <= 1
assert abs(colocated_cost["decode_ns_per_token"] - 3) <= 1

pd_cost = pd["cost_model"]
assert abs(pd_cost["prefill_base_latency_ns"] - 100) <= 1
assert abs(pd_cost["prefill_ns_per_token"] - 4) <= 1
assert abs(pd_cost["decode_base_latency_ns"] - 200) <= 1
assert abs(pd_cost["decode_ns_per_token"] - 5) <= 1
assert "decode_step_latency_curve" not in pd_cost
assert "first_token_backpressure_curve" not in pd_cost

transfer = pd["transfer"]
transfer_fit = pd["transfer_fit"]
assert transfer["enabled"] is True
assert abs(transfer["latency_ns"] - 50) <= 1
assert abs(transfer["bandwidth_bytes_per_s"] - 1.0e9) <= 1
assert transfer_fit["samples"] == 4
assert transfer_fit["timing_source"] == "proxy_derived"
PY

echo "[$0] Fitting request-latency calibration root..."
${FIT_SCRIPT} \
    --calibration-root ${SCRIPT_DIR}/inputs/calibration_root \
    --modes colocated,pd_disaggregated \
    --run-scope all \
    --fit-target request_latency \
    --skip-incomplete-runs \
    --output ${LATENCY_FIT_JSON} > ${LATENCY_STDOUT}

python3 - "${LATENCY_FIT_JSON}" <<'PY'
import json
import sys

fit = json.load(open(sys.argv[1]))
colocated = fit["modes"]["colocated"]
pd = fit["modes"]["pd_disaggregated"]

assert fit["fit_target"] == "request_latency"
assert fit["excluded_runs"] == [
    {
        "mode": "pd_disaggregated",
        "run_id": "pilot_rate_sweep__pd_disaggregated__rps_16__seed_0",
        "reason": "overload_service_fit_exclusion",
    }
]
assert colocated["source_runs"] == {"discovered": 1, "used": 1}
assert pd["source_runs"] == {"discovered": 3, "used": 1}
assert fit["skipped_runs"] == [
    {
        "mode": "pd_disaggregated",
        "run_id": "incomplete_run",
        "reason": "missing required file(s): request_metrics.csv, pd_stage_metrics.csv",
    }
]

colocated_cost = colocated["cost_model"]
assert abs(colocated_cost["prefill_base_latency_ns"] - 1000) <= 1
assert abs(colocated_cost["prefill_ns_per_token"] - 10) <= 1
assert abs(colocated_cost["decode_base_latency_ns"] - 0) <= 1
assert colocated_cost["decode_ns_per_token"] == 0
assert abs(colocated_cost["decode_step_latency_ns"] - 100) <= 1
assert colocated_cost["first_token_timing"] == "decode_start"
assert abs(colocated_cost["first_token_latency_ns"] - 10) <= 1

pd_cost = pd["cost_model"]
assert abs(pd_cost["prefill_base_latency_ns"] - 2000) <= 1
assert abs(pd_cost["prefill_ns_per_token"] - 20) <= 1
assert abs(pd_cost["decode_base_latency_ns"] - 0) <= 1
assert pd_cost["decode_ns_per_token"] == 0
assert abs(pd_cost["decode_step_latency_ns"] - 200) <= 1
assert pd_cost["first_token_timing"] == "decode_start"
assert abs(pd_cost["first_token_latency_ns"] - 20) <= 1
assert pd_cost["decode_step_latency_curve"] == {
    "enabled": True,
    "signal": "target_request_rate_per_second",
    "interpolation": "linear",
    "extrapolation": "clamp",
    "points": [
        {"request_rate_per_second": 2.0, "decode_step_latency_ns": 200},
        {"request_rate_per_second": 16.0, "decode_step_latency_ns": 600},
    ],
}
assert pd_cost["first_token_backpressure_curve"] == {
    "enabled": True,
    "signal": "target_request_rate_per_second",
    "model": "arrival_rank_linear",
    "interpolation": "linear",
    "extrapolation": "clamp",
    "points": [
        {
            "request_rate_per_second": 2.0,
            "base_latency_ns": 20,
            "knee_request_index": 0.0,
            "latency_ns_per_request_after_knee": 0.0,
        },
        {
            "request_rate_per_second": 16.0,
            "base_latency_ns": 20,
            "knee_request_index": 0.0,
            "latency_ns_per_request_after_knee": 0.0,
        },
    ],
}
curve_fit = pd["decode_step_latency_curve_fit"]
assert curve_fit["method"] == "successful_request_decode_duration_per_output_token_by_target_rate"
assert curve_fit["points"][0]["source_run_ids"] == ["run_a"]
assert curve_fit["points"][0]["overload_only"] is False
assert curve_fit["points"][1]["source_run_ids"] == [
    "pilot_rate_sweep__pd_disaggregated__rps_16__seed_0"
]
assert curve_fit["points"][1]["overload_only"] is True
assert pd["fit_stats"]["decode_step_latency_curve_points"] == 2
first_token_fit = pd["first_token_backpressure_curve_fit"]
assert first_token_fit["method"] == "ttft_residual_arrival_rank_linear"
assert first_token_fit["points"][0]["source_run_ids"] == ["run_a"]
assert first_token_fit["points"][0]["golden_ttft_pass_count"] == 1
assert first_token_fit["points"][0]["predicted_ttft_pass_count"] == 1
assert first_token_fit["points"][0]["overload_only"] is False
assert first_token_fit["points"][1]["source_run_ids"] == [
    "pilot_rate_sweep__pd_disaggregated__rps_16__seed_0"
]
assert first_token_fit["points"][1]["golden_ttft_pass_count"] == 1
assert first_token_fit["points"][1]["predicted_ttft_pass_count"] == 1
assert first_token_fit["points"][1]["overload_only"] is True
assert pd["fit_stats"]["first_token_backpressure_curve_points"] == 2

transfer = pd["transfer"]
transfer_fit = pd["transfer_fit"]
assert transfer["enabled"] is True
assert abs(transfer["latency_ns"] - 50) <= 1
assert abs(transfer["bandwidth_bytes_per_s"] - 1.0e9) <= 1
assert transfer_fit["samples"] == 4
assert transfer_fit["timing_source"] == "proxy_derived"
PY

echo "[$0] Fitting request-latency calibration root with simulator adjustments..."
${FIT_SCRIPT} \
    --calibration-root ${SCRIPT_DIR}/inputs/calibration_root \
    --modes colocated,pd_disaggregated \
    --run-scope all \
    --fit-target request_latency \
    --skip-incomplete-runs \
    --simulator-fidelity-adjustments \
    --output ${ADJUSTED_LATENCY_FIT_JSON} > ${ADJUSTED_LATENCY_STDOUT}

python3 - "${ADJUSTED_LATENCY_FIT_JSON}" <<'PY'
import json
import sys

fit = json.load(open(sys.argv[1]))
colocated = fit["modes"]["colocated"]
pd = fit["modes"]["pd_disaggregated"]

assert fit["fit_target"] == "request_latency"
assert fit["simulator_fidelity_adjustments"] is True

colocated_cost = colocated["cost_model"]
assert colocated_cost["prefill_base_latency_ns"] == 850
assert colocated_cost["prefill_ns_per_token"] == 8
assert colocated_cost["decode_step_latency_ns"] == 85
assert colocated["simulator_fidelity_adjustment"]["unadjusted_costs"] == {
    "prefill_base_latency_ns": 1000,
    "prefill_ns_per_token": 10,
    "decode_step_latency_ns": 100,
}

pd_cost = pd["cost_model"]
assert pd_cost["prefill_base_latency_ns"] == 2000
assert pd_cost["prefill_ns_per_token"] == 20
assert pd_cost["decode_step_latency_ns"] == 200
assert pd_cost["decode_step_latency_curve"]["points"] == [
    {"request_rate_per_second": 2.0, "decode_step_latency_ns": 200},
    {"request_rate_per_second": 16.0, "decode_step_latency_ns": 600},
]
assert pd_cost["first_token_backpressure_curve"]["points"] == [
    {
        "request_rate_per_second": 2.0,
        "base_latency_ns": 20,
        "knee_request_index": 0.0,
        "latency_ns_per_request_after_knee": 0.0,
    },
    {
        "request_rate_per_second": 16.0,
        "base_latency_ns": 20,
        "knee_request_index": 0.0,
        "latency_ns_per_request_after_knee": 0.0,
    },
]
assert "simulator_fidelity_adjustment" not in pd
PY

echo "[$0] Checking validation-summary acceptance logic..."
python3 - "${SCRIPT_DIR}/../../tools/calibration/plot_simulation_vs_calibration.py" <<'PY'
import importlib.util
import sys

spec = importlib.util.spec_from_file_location("plotter", sys.argv[1])
plotter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plotter)

rows = []
for mode in ("colocated", "pd_disaggregated"):
    for source, ttft, tpot, e2e, goodput, throughput in (
        ("calibrated", 100.0, 50.0, 1000.0, 2.0, 2.0),
        ("simulation", 110.0, 55.0, 1090.0, 1.99, 2.0),
    ):
        rows.append(
            {
                "source": source,
                "mode": mode,
                "run_id": f"pilot_rate_sweep__{mode}__rps_2__seed_0",
                "run_family": "pilot_rate_sweep",
                "target_request_rate": 2.0,
                "ttft_mean_ns": ttft,
                "tpot_mean_ns": tpot,
                "e2e_mean_ns": e2e,
                "goodput_reqs_per_sec": goodput,
                "request_throughput_reqs_per_sec": throughput,
            }
        )
    for source, ttft in (("calibrated", 1000.0), ("simulation", 100.0)):
        rows.append(
            {
                "source": source,
                "mode": mode,
                "run_id": f"pilot_rate_sweep__{mode}__rps_16__seed_0",
                "run_family": "pilot_rate_sweep",
                "target_request_rate": 16.0,
                "ttft_mean_ns": ttft,
                "tpot_mean_ns": 50.0,
                "e2e_mean_ns": 1000.0,
                "goodput_reqs_per_sec": 2.0,
                "request_throughput_reqs_per_sec": 2.0,
            }
        )

summary = plotter.build_validation_summary(
    rows,
    mode_names=["colocated", "pd_disaggregated"],
    latency_mape_threshold_pct=15.0,
    throughput_mape_threshold_pct=1.0,
    overload_latency_mape_target_pct=50.0,
    overload_throughput_mape_target_pct=100.0,
    overload_run_marker="__rps_16__",
)

assert summary["overall_pass"] is True
for mode in ("colocated", "pd_disaggregated"):
    assert summary["modes"][mode]["acceptance"]["paired_runs"] == 1
    assert summary["modes"][mode]["overload_diagnostic"]["paired_runs"] == 1
    assert summary["modes"][mode]["acceptance"]["metrics"]["ttft_mean_ns"]["mape_pct"] == 10.0
    assert summary["modes"][mode]["acceptance"]["metrics"]["goodput_reqs_per_sec"]["mape_pct"] < 1.0
    assert summary["modes"][mode]["overload_diagnostic"]["metrics"]["ttft_mean_ns"]["mape_pct"] == 90.0
    assert summary["modes"][mode]["overload_diagnostic"]["metrics"]["ttft_mean_ns"]["pass"] is False
    assert summary["modes"][mode]["overload_diagnostic"]["target_pass"] is False
PY

echo "[$0] Ok."
