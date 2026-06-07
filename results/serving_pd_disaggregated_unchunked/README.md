# Calibrated Unchunked PD-Disaggregated Serving Results

This package is the tracked, portable result set for the first
PD-disaggregated serving simulator commit. It keeps the commit independent from
the ignored raw `build/` and `calibrations/` trees.

## Scope

- Primary calibrated modes: unchunked `colocated` and unchunked
  `pd_disaggregated`
- Calibration hardware: two same-node `L40S` GPUs on the same motherboard
- PD layout for measured calibration: one prefill GPU and one decode GPU
- Chunked-prefill status: implemented and diagnostically replayed elsewhere,
  but not promoted as a primary result until broader chunked calibration data is
  collected

## Calibration Closure

The calibration-vs-simulation artifacts live in
`calibration_vs_simulation/`. The validation summary reports
`overall_pass: true` for the non-overloaded acceptance scope.

Non-overload MAPE highlights:

- `colocated`: TTFT 11.15%, TPOT 4.65%, E2E 4.86%, goodput approximately 0%
- `pd_disaggregated`: TTFT 10.67%, TPOT 1.43%, E2E 1.67%, goodput 0.11%

The calibration plots compare measured calibration rows against simulator replay
rows for the same request traces.

## Extrapolation Studies

The extrapolation artifacts live under `extrapolation/`:

- `compact_gpu_scaling/`: broad worker and TP scaling from the calibrated PD
  reference
- `worker_assignment_4gpu/`: exact 2-4 GPU prefill/decode worker assignment
  tradeoff
- `pressure_4gpu/`: request-rate and input-token pressure for 4-GPU
  assignments
- `output_length_4gpu/`: request-rate and output-token pressure for 4-GPU
  assignments

Only the `P1/D1` two-GPU point is calibrated. All larger worker-count, TP, and
4-GPU assignment points are simulator extrapolations until matching real
measurements are collected.
