# Investigation: PD Versus Colocated Goodput

## Chosen Question

Under ShareGPT-like traces on the calibrated two-`L40S` single-node setup, when
does unchunked prefill/decode disaggregation beat unchunked colocated serving on
SLO goodput?

## Why This Question

This is the most decision-useful next experiment because:

- the current calibrated evidence covers unchunked colocated and unchunked PD
- the answer directly depends on transfer cost, which is the main unknown in
  PD design
- it is the cleanest bridge from the real two-`L40S` experiment to ASTRA-sim
  calibration

## Hypothesis

- colocated serving should win at light load because it avoids transfer
  overhead
- PD should win once prefill pressure is high enough and interconnect bandwidth
  is strong enough to keep decode workers fed without TTFT collapse

## Study Starter

Current calibrated replay:

```bash
python3 tools/calibration/run_golden_calibration_loop.py \
  --calibration-root calibrations/unchunked_scaling_20260530_075459 \
  --modes colocated,pd_disaggregated
```

Primary calibrated examples:

- [colocated.json](/home/justin/astra-sim/configs/serving_examples/colocated.json)
- [pd_disaggregated.json](/home/justin/astra-sim/configs/serving_examples/pd_disaggregated.json)

These are the inputs that should anchor the current two-`L40S` result. They use
unchunked serving and calibrated stage/transfer terms.

Future abstract sweep starter:

- [realistic_goodput_sharegpt_like.json](/home/justin/astra-sim/configs/serving_examples/realistic_goodput_sharegpt_like.json)

Analytical wrapper for a 16-accelerator abstract ring:

- [serving_goodput_16gpu.yaml](/home/justin/astra-sim/configs/serving_goodput_16gpu.yaml)
- [network_16gpu_ring.yml](/home/justin/astra-sim/configs/network_16gpu_ring.yml)

The base config uses:

- a ShareGPT-like prompt and output range
- unchunked prefill by default
- a 70B-class dense model shape for KV-size estimation
- an SLO block so goodput is meaningful from the start
- a PD block that can be swept without rewriting the file

## Future Abstract Sweep

Use this only as a future extrapolation sweep, not as the current calibrated
two-`L40S` evidence:

```bash
./tools/run_serving_sweep.py \
  --analytical-template configs/serving_goodput_16gpu.yaml \
  --request-config configs/serving_examples/realistic_goodput_sharegpt_like.json \
  --output-dir build/report_goodput_16gpu \
  --case-prefix goodput \
  --emit-event-trace \
  --set runtime.architecture=colocated,pd_disaggregated \
  --set scheduler.chunked_prefill_size=0 \
  --set trace.request_rate_per_second=8,16,32,64,96 \
  --set pd.transfer.bandwidth_bytes_per_s=25000000000.0,50000000000.0,100000000000.0

./tools/parse_serving_outputs.py \
  build/report_goodput_16gpu \
  --csv-output build/report_goodput_16gpu/summary.csv \
  --json-output build/report_goodput_16gpu/summary.json
```

## Readout

Primary outputs to compare:

- `goodput_reqs_per_sec`
- `slo_attainment_fraction`
- `ttft_mean_ns`
- `e2e_mean_ns`
- `transfer_duration_mean_ns`
- per-request failure reasons in the metrics CSV

Decision rule:

- if PD beats colocated on `goodput_reqs_per_sec` without a worse
  `slo_attainment_fraction`, it is the better serving layout at that operating
  point
- if PD only wins after transfer bandwidth is unrealistically high, the design
  is too network-sensitive

## What To Measure In The Real SGLang Cluster

When we move from published anchors to real calibration, collect the same study
with SGLang using:

- `python3 -m sglang.launch_server`
- `python3 -m sglang_router.launch_router --pd-disaggregation ...`
- `python3 -m sglang.bench_serving`

Recommended runtime slices:

- colocated TP-only baseline
- unchunked colocated baseline on the constrained hardware
- PD prefill workers and decode workers separately profiled
- larger TP/PP/DP-attention runs only as future extrapolation unless new real
  hardware measurements are collected

## Remaining Work After This Study

After this report-driven sweep, the work left is straightforward:

- keep the current main result limited to unchunked colocated versus unchunked
  PD on two `L40S` GPUs
- collect raw NCCL/router timing if higher-fidelity PD transfer claims are
  needed
- keep chunked-prefill results diagnostic until overload and broader rate-sweep
  coverage are collected
