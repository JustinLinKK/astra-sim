# Serving Request Examples

These files are request JSON inputs for the analytical serving entrypoint in
[configs/serving_disagg_colocated.yaml](/home/justin/astra-sim/configs/serving_disagg_colocated.yaml).

Use them by copying the YAML and changing only `request_configuration`.

Examples:

- `colocated.json`: calibrated unchunked colocated runtime
- `colocated_chunked.json`: calibrated colocated runtime with chunked prefill
- `pd_disaggregated.json`: calibrated unchunked prefill/decode-disaggregated runtime with transfer enabled
- `pd_disaggregated_chunked.json`: calibrated PD runtime with chunked prefill,
  using unchunked scaling curves plus chunk-specific prefill timing
- `realistic_goodput_sharegpt_like.json`: a report-oriented starting point for
  unchunked realistic goodput sweeps against colocated versus PD layouts
- `pd_extrapolated_compact_sweep.json`: compact PD GPU/TP extrapolation cases
- `pd_4gpu_worker_assignment_sweep.json`: exact 2-4 GPU role-allocation cases
- `pd_4gpu_pressure_sweep.json`: 4-GPU request-rate and input-token pressure cases
- `pd_4gpu_output_length_sweep.json`: 4-GPU request-rate and output-token pressure cases
- `paper_crossover_rps4_cases/`: tracked case files for the current 4 rps
  global token-shape appendix matrix
- `paper_crossover_rps2_cases/`: tracked case files for the 2 rps appendix
  sibling package
- `paper_crossover_cases/`: older 12 rps token-shape case files retained for
  the Fig. 2-style grid reproduction path
- `chunked_rps4_cases/`: tracked case files for the chunked RPS-4 paper sweep

Current calibration scope:

- use `colocated.json` and `pd_disaggregated.json` as the primary calibrated
  unchunked examples
- colocated examples use the full 16-GPU server budget as an explicit
  colocated GPU pool; smaller C4 studies override both `server_gpu_count` and
  `placement.colocated.gpu_count`
- use `colocated_chunked.json` and `pd_disaggregated_chunked.json` for chunked
  diagnostic studies; the available non-overload replay passes validation, but
  overload coverage is still missing
- scope these examples to the two-`L40S`, same-motherboard experiment
- treat any `prefill_gpu_count`, `decode_gpu_count`, or TP setting beyond
  `P1/D1` as extrapolation until matching measurements exist

Each example uses the new request-schema fields:

- `runtime`
- `resources`
- `placement`
- `scheduler`
- `slo`
- `model`
- `cost_model`
- `target_request_rate_per_second` for calibrated rate-dependent PD curves

For the full field reference, see
[docs/project/serving-runtime-guide.md](/home/justin/astra-sim/docs/project/serving-runtime-guide.md).
