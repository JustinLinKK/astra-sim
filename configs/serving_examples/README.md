# Serving Request Examples

These files are request JSON inputs for the analytical serving entrypoint in
[configs/serving_disagg_colocated.yaml](/home/justin/astra-sim/configs/serving_disagg_colocated.yaml).

Use them by copying the YAML and changing only `request_configuration`.

Examples:

- `colocated_chunked.json`: colocated runtime with chunked prefill and a balanced scheduler
- `pd_disaggregated.json`: prefill/decode-disaggregated runtime with transfer enabled
- `realistic_goodput_sharegpt_like.json`: a report-oriented starting point for
  realistic goodput sweeps against colocated versus PD layouts

Each example uses the new request-schema fields:

- `runtime`
- `scheduler`
- `slo`
- `model`
- `pd`
- `cost_model`

For the full field reference, see
[docs/project/serving-runtime-guide.md](/home/justin/astra-sim/docs/project/serving-runtime-guide.md).
