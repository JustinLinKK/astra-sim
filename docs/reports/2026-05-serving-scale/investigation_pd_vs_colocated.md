# Investigation: PD Versus Colocated Goodput

## Chosen Question

Under ShareGPT-like traces, at what request rate and transfer bandwidth does
prefill/decode disaggregation beat colocated chunked serving on SLO goodput?

## Why This Question

This is the most decision-useful next experiment because:

- we already have colocated, chunked-prefill, and PD runtimes
- the answer directly depends on transfer cost, which is the main unknown in
  cluster-scale PD design
- it is the cleanest bridge from public SGLang and vLLM results to real
  cluster calibration

## Hypothesis

- colocated chunked serving should win at light load because it avoids transfer
  overhead and router coordination
- PD should win once prefill pressure is high enough and interconnect bandwidth
  is strong enough to keep decode workers fed without TTFT collapse

## Study Starter

Base request config:

- [realistic_goodput_sharegpt_like.json](/home/justin/astra-sim/configs/serving_examples/realistic_goodput_sharegpt_like.json)

Analytical wrapper for a 16-accelerator abstract ring:

- [serving_goodput_16gpu.yaml](/home/justin/astra-sim/configs/serving_goodput_16gpu.yaml)
- [network_16gpu_ring.yml](/home/justin/astra-sim/configs/network_16gpu_ring.yml)

The base config uses:

- a ShareGPT-like prompt and output range
- chunked prefill enabled by default
- a 70B-class dense model shape for KV-size estimation
- an SLO block so goodput is meaningful from the start
- a PD block that can be swept without rewriting the file

## Suggested Sweep

Use one sweep over architecture, request rate, and transfer bandwidth:

```bash
./tools/run_serving_sweep.py \
  --analytical-template configs/serving_goodput_16gpu.yaml \
  --request-config configs/serving_examples/realistic_goodput_sharegpt_like.json \
  --output-dir build/report_goodput_16gpu \
  --case-prefix goodput \
  --emit-event-trace \
  --set runtime.architecture=colocated_chunked,pd_disaggregated \
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
- colocated TP+PP if model size requires it
- PD prefill workers and decode workers separately profiled
- DeepSeek-family DP-attention run if the target model supports it

## Remaining Work After This Study

After this report-driven sweep, the work left is straightforward:

- replace placeholder cost-model constants with measured SGLang constants on a
  real multi-GPU cluster
- add TP, PP, EP, and DP-attention as first-class serving-layout parameters
- re-run this same PD-versus-colocated question with calibrated stage costs
