# PD Calibration Scope: Unchunked First

## Summary

The calibrated result for this project should focus on the two unchunked serving
modes:

- `colocated`
- `pd_disaggregated`

Chunked-prefill modes are supported by the simulator and now have a diagnostic
non-overload calibration replay. Do not use `colocated_chunked` or
`pd_disaggregated_chunked` as main evidence for the current PD design result
until overload and broader rate-sweep coverage reach the same standard as the
unchunked PD result.

## Hardware Constraint

Due to funding constraints, the real PD calibration experiment uses only two
`L40S` GPUs on the same motherboard. All calibration claims should be scoped to
this single-node setup.

Actual calibration setup:

- colocated baseline: one `L40S` GPU serving both prefill and decode
- PD-disaggregated run: two `L40S` GPUs, one prefill worker and one decode worker
- topology: same motherboard, single node
- inter-node network: none
- handoff path: same-node GPU-to-GPU KV handoff over PCIe/NCCL
- transfer timing status: proxy-derived handoff timing, not raw NCCL timing

Any larger multi-node, TP, PP, EP, or DP-attention conclusions should be
presented as future extrapolation, not as calibrated experimental results.

## Simulator Design Used For Calibration

Keep the original ASTRA-sim serving runtime path and calibrate it against the
two-GPU experiment.

The PD simulator should be interpreted as three explicit stages:

- prefill service on the prefill worker
- prefill-to-decode KV handoff on the same-node transfer path
- decode service on the decode worker

The calibrated knobs for the current result are intentionally narrow:

- `cost_model.prefill_base_latency_ns`
- `cost_model.prefill_ns_per_token`
- `cost_model.decode_step_latency_ns`
- `cost_model.first_token_latency_ns`
- `pd.transfer.latency_ns`
- `pd.transfer.bandwidth_bytes_per_s`
- `pd.transfer.efficiency`

## Current Calibration Evidence

Use the unchunked calibration data as the primary evidence. The current
non-overload validation passes the configured `15%` latency MAPE threshold for
both modes:

- colocated: TTFT, TPOT, and E2E validation pass under the current threshold
- PD-disaggregated: TTFT, TPOT, and E2E validation pass under the current
  threshold

Throughput and goodput closure should also be reported from the unchunked
validation artifacts. Overload behavior should remain diagnostic unless more
high-load data is collected on the same two-`L40S` setup.

The portable tracked artifact package for this evidence is
[../../../results/serving_pd_disaggregated_unchunked](../../../results/serving_pd_disaggregated_unchunked).
Do not commit the raw `calibrations/` tree for this first result commit.

## Chunked Calibration Status

Chunked prefill now has a calibrated diagnostic artifact derived from the
`chunked_prefill_scaling_20260603_074838` chunked measurements and the stronger
`unchunked_scaling_20260530_075459` base scaling fit:

- `calibrations/chunked_prefill_scaling_20260603_074838/fitted_chunked_prefill_latency_cost_model.json`
- `calibrations/chunked_prefill_scaling_20260603_074838/sim_vs_calibrated/validation_summary.json`

The available non-overload chunked replay passes the configured `15%` latency
MAPE threshold for both `colocated_chunked` and `pd_disaggregated_chunked`.
Keep chunked-prefill claims diagnostic until the missing overload and broader
rate-sweep coverage is collected.

Required follow-up:

- reuse identical request timelines across all four serving modes
- collect chunk-size sweeps, request-rate sweeps, and overload points
- require `event_trace.csv`, `stage_metrics.csv`, chunk counts, chunk wait
  times, and PD transfer timing
- promote chunked-prefill results only after validation reaches the same
  standard as unchunked PD
