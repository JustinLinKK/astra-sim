# Calibration Data Gap Report

> Current status: this document is now partly historical. The old one-off
> `calibration/` snapshot described below has been superseded for the first
> commit by the unchunked matrix under
> `calibrations/unchunked_scaling_20260530_075459` and the tracked result
> package at `results/serving_pd_disaggregated_unchunked`. Use the current
> result package for calibration-vs-simulation proof; keep the older sections
> below as context for why the larger data contract exists.

## Purpose

This report compares the calibration data currently present under
`calibration/` with the data required by
[real_server_metrics_for_calibration.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/real_server_metrics_for_calibration.md).

The audience is the teammate updating the real-server collection pipeline. The
goal is to make it clear:

- what we already collect correctly
- what is partially collected but not yet usable for calibration
- what is still missing entirely
- what should be changed so the next data drop is directly usable for
  ASTRA-sim calibration

Snapshot analyzed in this report:

- workspace state on 2026-05-26
- `calibration/colocated/*`
- `calibration/pd_disaggregated/*`

Scope note:

- this report is only about the two unchunked serving modes:
  `colocated` and `pd_disaggregated`
- chunked-prefill calibration targets such as `colocated_chunked`,
  `pd_disaggregated_chunked`, and chunk-size sweeps are covered separately in
  [chunked_prefill_calibration_gap_report.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/chunked_prefill_calibration_gap_report.md)

## Executive Summary

This original summary describes the 2026-05-26 `calibration/` snapshot. The
newer `calibrations/` replacement data is evaluated in
[Latest Replacement Data Check (2026-05-29)](#latest-replacement-data-check-2026-05-29),
which supersedes the specific old missing-field observations below.

Current report scope:

- use only unchunked `colocated` and `pd_disaggregated` as primary calibration
  evidence
- scope all calibration claims to the available two-`L40S` same-motherboard
  experiment
- treat `colocated_chunked` and `pd_disaggregated_chunked` as diagnostic only:
  the current non-overload replay passes, but the full calibration matrix still
  needs overload and broader rate coverage
- publish tracked proof plots from
  `results/serving_pd_disaggregated_unchunked/calibration_vs_simulation`
  instead of committing raw calibration datasets

The current folder is a good first calibration scaffold, but it is not yet a
complete or interpretable calibration dataset.

What is already working well:

- the artifact layout matches the intended handoff structure
- both runtime modes have `run_config.json`, `request_metrics.csv`,
  `system_timeseries.csv`
- PD also has `pd_stage_metrics.csv`
- request-level TTFT, TPOT, E2E, request throughput, and token throughput are
  available
- PD has usable prefill and decode stage durations
- coarse GPU and engine-level utilization counters are being collected

What still blocks full simulator calibration:

- colocated request-level queueing and stage timestamps are blank
- PD transfer timing and transfer-byte fields are blank in both request and
  stage files
- topology and serving-layout metadata are incomplete
- goodput and SLO thresholds are missing
- scheduler and batch-shape traces are too coarse for fitting batching and
  interference behavior
- communication counters are too weak to fit TP, PP, EP, DP-attention, or PD
  transfer terms
- the current dataset is only one operating point per runtime mode, not a full
  calibration matrix
- each current run has only `64` requests, which is too small for stable p99,
  goodput, saturation, or distribution-sensitive calibration

Bottom line:

- we can use the current data for limited surface closure on TTFT, TPOT, E2E,
  and throughput for the two unchunked modes
- we cannot yet use it for an interpretable fit of queueing, batching,
  communication, or PD transfer knobs
- the next unchunked target is not to add chunked-prefill modes yet; it is to
  collect a calibration matrix for `colocated` and `pd_disaggregated` using the
  same request timeline, populated stage/queue fields, PD transfer data, and
  request-rate sweeps from light load to saturation

## Latest Replacement Data Check (2026-05-29)

Superseded by [Latest Replacement Data Check (2026-05-30)](#latest-replacement-data-check-2026-05-30).

## Latest Replacement Data Check (2026-05-30)

New data was reviewed under `calibrations/`:

- `calibrations/request_traces/gap_smoke20__sharegpt__rps_2__seed_0.csv`
- `calibrations/colocated/gap_smoke20__colocated__sharegpt_seed_0__rps_2__seed_0/`
- `calibrations/pd_disaggregated/gap_smoke20__pd_disaggregated__sharegpt_seed_0__rps_2__seed_0/`
- `calibrations/unchunked_gap_closure_summary.md`

Scale-up verdict:

- **Good to start the large-scale unchunked calibration matrix.**
- **Good enough to make the current unchunked two-`L40S` PD calibration the
  primary result once the fitted simulator closure is reported.**
- The prior PD transfer-timing blocker is closed for this phase:
  `request_metrics.csv` and `pd_stage_metrics.csv` now contain transfer timing
  for successful PD requests.
- Transfer timing is labeled as proxy-derived rather than raw NCCL timing:
  `pd_transfer.transfer_timing_source = proxy_derived`,
  `pd_transfer.transfer_timing_method = prefill_end_to_decode_start`, and
  `pd_transfer.transfer_timing_observation =
  proxy_handoff_interval_not_raw_nccl`.
- Treat this as acceptable for unchunked large-scale calibration, while noting
  that raw NCCL/router timing would still be a later fidelity improvement.

Latest smoke-run summary:

| Mode | Requests | Success | Good | Trace hash matched | Transfer timing | Scheduler rows | Max running | Max waiting |
| --- | ---: | ---: | ---: | --- | --- | ---: | ---: | ---: |
| `colocated` | 20 | 20 | 20 | yes | n/a | 538 | 4 | 0 |
| `pd_disaggregated` | 20 | 20 | 20 | yes | proxy-derived | 570 | 4 | 0 |

Remaining gaps are now scale and coverage gaps:

- each mode has only `20` requests
- only one request rate is present (`2 req/s`)
- there are no single-request service baselines
- there is no near-saturation or overloaded rate point
- there are no repeated seeds/timelines
- queueing coverage is absent (`max waiting_request_count = 0`)

Hardware limitation for this phase:

- the real experiment uses two `L40S` GPUs on the same motherboard
- colocated uses one GPU
- PD uses one prefill GPU and one decode GPU
- no inter-node transfer is represented by the calibration data
- the PD handoff timing is proxy-derived and should not be presented as raw
  NCCL transfer timing

The next run should scale the unchunked matrix with:

- paired `colocated` and `pd_disaggregated` runs on identical request trace
  hashes
- service baselines with at least `30` successful repetitions per
  prompt/output shape per mode
- normal rate-sweep points with at least `1,000` successful measured requests
  per mode
- validation runs with at least `5,000` successful measured requests per mode
- final validation and near-knee rate points across at least `3` seeds or saved
  timelines
- light, mid, near-saturation, and overloaded points
- regenerated `calibrations/unchunked_gap_closure_summary.md`

## Historical Files Reviewed

Original 2026-05-26 calibration package:

- `calibration/colocated/run_config.json`
- `calibration/colocated/request_metrics.csv`
- `calibration/colocated/system_timeseries.csv`
- `calibration/colocated/benchmark_result.json`
- `calibration/pd_disaggregated/run_config.json`
- `calibration/pd_disaggregated/request_metrics.csv`
- `calibration/pd_disaggregated/system_timeseries.csv`
- `calibration/pd_disaggregated/benchmark_result.json`
- `calibration/pd_disaggregated/pd_stage_metrics.csv`

## Historical Data Snapshot

### Colocated Run

Original 2026-05-26 colocated run summary:

- `runtime_architecture = colocated`
- `request_rate = 0.2`
- `num_prompts = 64`
- `duration = 327.165 s`
- `completed = 64`
- `failed = 0`
- `mean_ttft_ms = 71.840`
- `mean_tpot_ms = 24.886`
- `request_throughput = 0.196 req/s`
- `output_throughput = 37.382 tok/s`
- `total_token_throughput = 84.037 tok/s`

Observed request shape:

- prompt tokens range: `13` to `811`
- output tokens range: `3` to `768`

Useful coarse time-series signals already present:

- GPU utilization
- GPU memory used
- GPU power draw
- vLLM KV-cache usage percent
- vLLM running-request count
- vLLM waiting-request count
- cumulative TTFT/prefill/decode/queue-time counters
- netdev RX and TX bytes by interface

Important caveat:

- queue depth remained effectively zero in this run, so this operating point is
  not informative for queueing calibration even if the collector were emitting
  more detail
- observed running request count only reached about `4`, so this is a baseline
  light-load run rather than a saturation or batching-efficiency run

### PD Run

Current PD run summary:

- `runtime_architecture = pd_disaggregated`
- `request_rate = 0.2`
- `num_prompts = 64`
- `duration = 326.405 s`
- `completed = 64`
- `failed = 0`
- `mean_ttft_ms = 127.007`
- `mean_tpot_ms = 23.157`
- `request_throughput = 0.196 req/s`
- `output_throughput = 36.994 tok/s`
- `total_token_throughput = 83.758 tok/s`

Observed stage statistics from `pd_stage_metrics.csv`:

- prefill duration mean: about `48.7 ms`
- decode duration mean: about `4.41 s`
- proxy handoff duration mean: about `0.87 us`

Important caveat:

- `proxy_handoff_duration_ns` is not a substitute for actual transfer latency
- the transfer-specific columns are blank, so the current PD run cannot be used
  to fit PD transfer bandwidth, transfer latency, or transfer bytes per prompt
  token

## What The Current Dataset Can Support

The current dataset is strong enough for these limited tasks:

- compare colocated versus PD on TTFT, TPOT, E2E, request throughput, and token
  throughput for this exact unchunked workload
- run a first-pass closure test against simulator outputs at one operating point
- estimate rough PD prefill service time and decode service time on this
  single-host setup
- perform coarse sanity checks against GPU activity and engine utilization

The current dataset is not strong enough for:

- fitting queueing delay terms
- fitting batch-efficiency curves
- fitting decode interference under sustained load
- fitting PD transfer cost
- fitting topology-aware communication costs
- fitting goodput against SLO thresholds
- separating scheduler effects from compute effects with confidence

## Next-Step Target For Unchunked Modes

Before using this data to tune ASTRA-sim knobs, the next collection round should
turn the current two folders into a paired unchunked calibration matrix:

- `calibration/colocated/`
- `calibration/pd_disaggregated/`

For every comparable experiment, the two modes should use the same:

- request ids
- arrival timestamps or inter-arrival timeline
- prompt token counts
- requested output token counts
- dataset source and random seed
- SLO thresholds

The immediate target is:

- single-request service baselines for prefill-heavy, decode-heavy, and mixed
  prompt/output shapes
- request-rate sweeps from light load through saturation for both unchunked
  modes
- populated request-level stage and queue timestamps
- PD transfer timing and transfer-byte visibility
- scheduler or batch traces that show actual prefill and decode batch shapes
- one realistic validation trace with SLO goodput enabled

Out of scope for this unchunked report:

- adding `calibration/colocated_chunked/`
- adding `calibration/pd_disaggregated_chunked/`
- sweeping `chunked_prefill_size`
- collecting chunk-specific request fields such as `prefill_chunk_count`

Those are the next target for the chunked-prefill report, not for this two-mode
baseline report.

## Required Scale For The Next Unchunked Data Drop

The current `64` request runs are useful for smoke testing, but they are too
small for calibration. The next unchunked data drop should record enough
requests to estimate tails, queue buildup, and mode-to-mode differences without
overfitting one short run.

Minimum scale targets:

- single-request service baselines:
  at least `30` successful repetitions per prompt/output shape per mode
- normal request-rate sweep points:
  at least `1,000` measured successful requests per mode per rate point
- realistic validation runs:
  at least `5,000` measured successful requests per mode
- preferred validation scale:
  `10,000` measured successful requests per mode if runtime cost is acceptable
- warmup:
  discard at least the first `60 s` or first `100` completed requests before
  computing calibration summaries
- measurement duration:
  at least `300 s` for each steady-state sweep point unless `1,000` measured
  completions requires a longer run
- repeatability:
  run at least `3` seeds or saved timelines for the final validation workload
  and for rate points near the saturation knee

For overloaded points, it is acceptable to stop early if the run clearly reaches
the timeout or failure policy. Even then, keep enough detail to explain the
failure mode:

- accepted request count
- completed request count
- failed or timed-out request count
- final queue depth
- timeout threshold
- first timestamp where backlog became persistent

Recommended rate-sweep scale:

- start with a pilot sweep to find the approximate saturation knee
- then collect dense points around the knee, not only far below it
- include at least one light-load point, two mid-load points, two near-knee
  points, and one overloaded point for each mode
- use the same request timeline for colocated and PD at each target rate

A practical adaptive sweep is:

1. Run pilot rates such as `0.2`, `0.4`, `0.8`, `1.2`, `1.6`, `2.0`, `2.5`,
   and `3.0` requests/s until queue depth or timeout behavior appears.
2. Identify the first rate where one of these is true:
   persistent waiting queue, throughput plateau, p99 E2E jump, SLO miss
   fraction above target, or request failures.
3. Re-run a denser sweep around that knee, for example `0.7x`, `0.85x`,
   `1.0x`, `1.1x`, and `1.25x` of the knee rate.
4. Apply the same saved timelines to `colocated` and `pd_disaggregated`.

Do not treat the specific rates above as universal. The important requirement is
to cover the full curve from light load to saturation with enough requests per
point.

## Required Request Distribution

Every comparable run must use the same request distribution in both unchunked
modes. The current runs mostly line up on prompt lengths, but the exact replay
timeline is not stored as a first-class artifact. The next data drop should make
the request distribution explicit and replayable.

Save a request timeline artifact for every experiment:

- recommended path:
  `calibration/request_traces/<trace_name>.csv`
- each run should record `request_trace_path` and `request_trace_sha256` in
  `run_config.json`
- each mode should replay the same trace file, not regenerate a merely similar
  trace from the same high-level settings

Required `request_traces/*.csv` columns:

- `request_id`
- `arrival_time_ns`
- `interarrival_ns`
- `prompt_tokens`
- `output_tokens`
- `total_tokens`
- `arrival_process`
- `target_request_rate`
- `seed`
- `dataset_name`
- `dataset_record_id` if sourced from a dataset
- `prompt_bucket`
- `output_bucket`

For Poisson traffic:

- save the sampled inter-arrival times, not just `lambda` and `seed`
- record `target_request_rate` in requests/s
- record the random seed and generator implementation
- use the same sampled timeline for both modes

For fixed-concurrency or closed-loop traffic:

- record the concurrency target
- record the client-side think-time policy
- record whether the next request is submitted on arrival schedule or after a
  previous request completes

Recommended token-shape coverage:

- short prompt, short output:
  prompt `16-128`, output `16-64`
- short prompt, long output:
  prompt `16-128`, output `257-768`
- medium prompt, medium output:
  prompt `129-512`, output `65-256`
- long prompt, short output:
  prompt `513-1024`, output `16-64`
- long prompt, long output:
  prompt `513-1024`, output `257-768`
- max-context pressure case, if supported by the configured `max_model_len`:
  prompt above `1024` with enough output tokens to stress memory and decode

For ShareGPT-like validation, preserve the empirical distribution, but still
emit bucket summaries. At minimum, each validation trace should report:

- request count
- prompt token min, p50, p90, p95, p99, max
- output token min, p50, p90, p95, p99, max
- total token min, p50, p90, p95, p99, max
- count and fraction in each prompt bucket
- count and fraction in each output bucket

Target distribution rules:

- do not calibrate only on short prompts or only on short outputs
- include enough long-output requests to fit TPOT and decode interference
- include enough long-prompt requests to fit TTFT and prefill service time
- keep the exact same token shapes for colocated and PD comparisons
- keep failed or timed-out requests in `request_metrics.csv` with failure
  reasons instead of dropping them silently

## Recommended Next Data Layout

The current two top-level mode folders are fine for a single run. A calibration
matrix needs stable run ids so files do not overwrite each other.

Recommended layout:

```text
calibration/
  request_traces/
    sharegpt_seed0_rps_0_8.csv
    sharegpt_seed0_rps_1_2.csv
    service_baseline_shapes.csv
  colocated/
    <run_id>/
      run_config.json
      request_metrics.csv
      system_timeseries.csv
      benchmark_result.json
      scheduler_trace.csv
      batch_metrics.csv
  pd_disaggregated/
    <run_id>/
      run_config.json
      request_metrics.csv
      system_timeseries.csv
      benchmark_result.json
      pd_stage_metrics.csv
      scheduler_trace.csv
      batch_metrics.csv
```

Recommended `run_id` format:

- `<experiment>__<mode>__<trace>__rps_<rate>__seed_<seed>`

Examples:

- `rate_sweep__colocated__sharegpt_seed0__rps_0_8__seed_0`
- `rate_sweep__pd_disaggregated__sharegpt_seed0__rps_0_8__seed_0`
- `service_baseline__colocated__fixed_shapes__rps_na__seed_0`

Each run should also write a small manifest field in `run_config.json`:

- `experiment_name`
- `run_id`
- `calibration_mode`
- `request_trace_path`
- `request_trace_sha256`
- `target_request_count`
- `measured_request_count`
- `warmup_request_count`
- `warmup_duration_s`
- `measurement_duration_s`
- `target_request_rate`
- `actual_request_throughput`
- `saturation_label`, such as `light`, `mid`, `near_knee`, or `overloaded`

## Gap Analysis By Artifact

### 1. `run_config.json`

#### What Is Already Present

Both runtime modes already record several useful inputs:

- model path and tokenizer path
- vLLM version and commit
- core Hugging Face model geometry:
  `num_hidden_layers`, `hidden_size`, `num_attention_heads`,
  `num_key_value_heads`
- model dtype via `torch_dtype`
- `runtime_architecture`
- `request_rate`
- `num_prompts`
- dataset name and dataset path
- metrics collection interval
- GPU ids used for collector attachment

This is a solid base and should be kept.

#### What Is Missing

The required run metadata note expects many more non-fit inputs than are
currently captured.

Missing or not explicitly captured:

- dense versus MoE classification as an explicit field
- weight precision as a normalized serving field
- KV-cache precision
- GPU SKU
- GPU count used by the serving runtime
- GPU memory size
- GPU memory bandwidth
- node count
- intra-node interconnect type
- intra-node interconnect bandwidth
- inter-node network type
- inter-node network bandwidth
- TP degree
- PP degree
- EP degree
- DP-attention degree
- replica count
- scheduler caps:
  `max_running_requests`, `max_decode_batch_size`,
  `max_prefill_batch_tokens`, `max_prefill_batch_size`
- concurrency target if used
- warmup duration in seconds
- measurement duration in seconds
- request trace path and SHA-256
- target and measured request counts
- prompt, output, and total-token distribution summaries
- SLO thresholds for TTFT, TPOT, and E2E
- for PD:
  prefill worker count, decode worker count, router settings, transfer path,
  and whether prefill and decode use different layouts

Metadata consistency also needs attention:

- the colocated config still carries `prefill_gpu=0` and `decode_gpu=1` style
  fields even though the run is marked `runtime_architecture=colocated`
- that may be harmless bookkeeping, but it is ambiguous and should be clarified
  so downstream scripts do not misinterpret the layout

#### Why This Matters

Without this metadata, we cannot map observed latency and throughput onto the
correct simulator structure. The simulator needs to know whether slowdowns come
from:

- model shape
- precision choice
- parallelism layout
- hardware limits
- node topology
- scheduler caps

If these are omitted, the fit becomes fragile and hard to reuse.

#### Required Update

Update the run collector so `run_config.json` includes explicit normalized
fields for:

- model shape and model family
- serving layout
- hardware topology
- scheduler caps
- SLO thresholds
- request trace identity and scale
- PD routing and transfer topology

Recommended rule:

- if a field is fixed by the run setup and is not a calibration knob, record it
  in `run_config.json`

### 2. `request_metrics.csv`

#### What Is Already Present

Both colocated and PD request CSVs have the correct schema header and correctly
record:

- `request_id`
- `arrival_ts`
- `first_token_ts`
- `finish_ts`
- `prompt_tokens`
- `output_tokens`
- `total_tokens`
- `success_or_failure`
- `failure_reason`
- derived:
  `ttft_ns`, `tpot_ns`, `e2e_ns`

This means the file naming and base schema should stay as-is.

#### What Is Missing

The main issue is not the schema. The issue is that most of the important
columns are still empty or only partially populated.

Currently blank in the colocated request file:

- `service_start_ts`
- `prefill_queue_enter_ts`
- `prefill_start_ts`
- `prefill_end_ts`
- `transfer_queue_enter_ts`
- `transfer_start_ts`
- `transfer_end_ts`
- `decode_queue_enter_ts`
- `decode_start_ts`
- `queue_wait_ns`
- `prefill_queue_wait_ns`
- `decode_queue_wait_ns`
- `transfer_queue_wait_ns`
- `prefill_duration_ns`
- `transfer_duration_ns`
- `decode_duration_ns`

Currently populated in the PD request file:

- `service_start_ts`
- `prefill_queue_enter_ts`
- `prefill_start_ts`
- `prefill_end_ts`
- `decode_queue_enter_ts`
- `decode_start_ts`
- `queue_wait_ns`
- `prefill_queue_wait_ns`
- `decode_queue_wait_ns`
- `prefill_duration_ns`
- `decode_duration_ns`

Still blank in the PD request file:

- `transfer_queue_enter_ts`
- `transfer_start_ts`
- `transfer_end_ts`
- `transfer_queue_wait_ns`
- `transfer_duration_ns`

This is still the single biggest gap in the dataset. For colocated, we cannot
separate service time from queueing or prefill from decode. For PD, we can see
prefill and decode timing, but still cannot calibrate the transfer stage.

#### Why This Matters

These fields are the direct bridge from runtime measurements to simulator
components:

- queueing delay
- prefill service time
- transfer service time
- decode service time
- stage overlap

Without them, we only know the total request outcome, not how the runtime got
there.

That means we can match top-line TTFT or TPOT while still fitting the wrong:

- queue model
- batch model
- transfer model
- stage service constants

#### Required Update

Populate the existing blank columns instead of inventing a different schema.

Required behavior:

- emit real timestamps for service start, prefill start and end, decode start,
  and queue-entry times whenever the runtime can observe them
- keep the derived nanosecond columns too, because they are convenient for
  downstream analysis
- for PD, populate transfer queue, transfer start, and transfer end at the
  request level whenever possible

If exact request-level queue timestamps cannot be observed in the engine today,
the next-best fallback is:

- emit stage timestamps in a separate event stream
- reconstruct these fields during postprocessing
- still write the reconstructed values back into `request_metrics.csv`

### 3. `pd_stage_metrics.csv`

#### What Is Already Present

This file is present only for PD, which is correct.

It is already much closer to usable than the request CSV. It records:

- `client_request_id`
- `backend_request_id`
- `arrival_ts`
- `prefill_start_ts`
- `prefill_end_ts`
- `decode_start_ts`
- `first_decode_chunk_ts`
- `decode_end_ts`
- `prefill_duration_ns`
- `proxy_handoff_duration_ns`
- `decode_duration_ns`
- `e2e_proxy_duration_ns`

The file also lines up cleanly with the request CSV:

- `64` PD stage rows
- `64` unique request ids
- no stage rows missing from the request file

That is a very good sign.

#### What Is Missing

The transfer-specific fields that matter most for PD calibration are blank:

- `transfer_start_ts`
- `transfer_end_ts`
- `transfer_duration_ns`
- `transfer_bytes`

Also missing:

- failure reason detail for stage-specific failures
- explicit idle time or blocked time on decode waiting for transfer
- explicit blocked time on prefill waiting on router or handoff backpressure

#### Why This Matters

For PD calibration, the transfer path is not a minor detail. It is one of the
main unknowns in the simulator.

We specifically need to fit:

- PD fixed latency
- PD bandwidth
- PD efficiency
- bytes per prompt token
- overlap between transfer and decode

The current file gives us:

- prefill timing
- decode timing
- a tiny handoff bookkeeping duration

but not the actual network transfer cost.

#### Required Update

Make `pd_stage_metrics.csv` record real transfer information per request or per
batch:

- `transfer_start_ts`
- `transfer_end_ts`
- `transfer_duration_ns`
- `transfer_bytes`

If the runtime sometimes uses a same-host fast path instead of a real transfer,
record that explicitly with a mode flag such as:

- `transfer_mode = local_fast_path | loopback | nic | rdma`

That avoids accidentally fitting localhost handoff behavior as if it were real
cluster transfer.

### 4. `system_timeseries.csv`

#### What Is Already Present

The current time-series collection is useful and worth keeping.

Observed data sources:

- Prometheus scrape of vLLM metrics
- `nvidia-smi`
- `/proc/net/dev`

Observed cadence:

- roughly `1.07 s` to `1.08 s` for the current Prometheus samples

Useful counters already present:

- `gpu_utilization_percent`
- `gpu_memory_used_mb`
- `gpu_memory_utilization_percent`
- `gpu_power_draw_w`
- `vllm:kv_cache_usage_perc`
- `vllm:num_requests_running`
- `vllm:num_requests_waiting`
- `vllm:num_requests_waiting_by_reason`
- `vllm:request_prefill_time_seconds_*`
- `vllm:request_decode_time_seconds_*`
- `vllm:request_queue_time_seconds_*`
- `vllm:time_to_first_token_seconds_*`
- `vllm:iteration_tokens_total_*`
- `netdev_rx_bytes`
- `netdev_tx_bytes`

Network interfaces currently observed:

- `lo`
- `ens10f0`
- `ens10f1`
- `docker0`
- `enxb2f96d82fcec`

This is enough for coarse utilization sanity checks.

#### What Is Missing

The main gap is that the current time-series data is too aggregate and too
coarse for several calibration tasks.

Missing or insufficient for fitting:

- prefill batch request count per iteration
- prefill batch token count per iteration
- decode batch request count per iteration
- decode generated tokens per step
- explicit concurrent decode sequence count per step
- request queue depth over time
- prefill queue depth over time
- decode queue depth over time
- scheduler admission rate over time
- router queue depth
- router dispatch latency
- in-flight PD handoff count
- handoff queue depth
- handoff retries or failures
- NCCL collective summaries
- TP all-reduce time and bytes
- PP activation transfer time and bytes
- EP dispatch or combine time and bytes
- DP-attention synchronization counters
- per-link or per-NIC throughput attributable to serving traffic

Important additional limitation:

- several "estimated" counters exist in the file but remain all-zero in the
  current runs:
  `vllm:estimated_flops_per_gpu_total`,
  `vllm:estimated_read_bytes_per_gpu_total`,
  `vllm:estimated_write_bytes_per_gpu_total`
- these should not be relied on unless they are verified to become nonzero and
  meaningful

Important interpretation caveat:

- the current vLLM Prometheus counters are mainly cumulative histogram and sum
  series
- they are helpful for run-level totals
- they are not a substitute for event-level scheduler traces

#### Why This Matters

Batching and contention are core simulator behaviors. They are not fully
recoverable from coarse cumulative counters.

For example:

- `vllm:num_requests_running` gives a broad picture of concurrency
- it does not tell us which requests were in a prefill batch together
- it does not tell us decode step sizes or exact queue buildup

Similarly:

- netdev byte counters show some traffic
- they do not isolate PD transfer bytes from unrelated traffic
- they do not attribute bytes to specific requests or handoffs

#### Required Update

Keep `system_timeseries.csv`, but extend the collector in one of two ways:

- preferred: emit per-iteration scheduler traces or batch traces in a new file
- acceptable fallback: emit much richer per-sample queue and batch counters into
  `system_timeseries.csv`

Recommended additions if a new trace file is possible:

- `scheduler_trace.csv`
- `batch_metrics.csv`
- `collective_metrics.csv`

Minimum content needed from such traces:

- timestamp
- stage
- batch request count
- batch token count
- generated tokens this step
- running requests
- waiting requests
- stage queue depths
- worker id or engine id

### 5. `benchmark_result.json`

#### What Is Already Present

This file is useful as a run summary and should be kept.

It currently provides:

- request throughput
- token throughput
- TTFT summary statistics
- TPOT summary statistics
- concurrency summary
- prompt and output length arrays

#### What Is Missing

It does not currently solve the missing calibration pieces above, and it lacks:

- SLO thresholds
- request goodput
- per-SLO miss fractions
- explicit validation metadata tying the summary to simulator targets

#### Required Update

Keep this file as a summary artifact, but do not rely on it as the primary
calibration artifact. The detailed per-request and per-stage files are the ones
that must be strengthened.

## Gap Analysis By Calibration Requirement

### Required Run Metadata

Status:

- partially satisfied

Already present:

- model id
- model geometry
- serving engine version and commit
- dataset
- request rate
- runtime architecture

Still missing:

- hardware topology
- serving layout degrees
- scheduler caps
- SLO thresholds
- PD worker and router settings

Action:

- extend `run_config.json`

### Request-Level Metrics

Status:

- partially satisfied at the aggregate level
- not satisfied for stage and queue modeling

Already present:

- TTFT
- TPOT
- E2E
- token counts

Still missing:

- colocated stage timestamps
- colocated queue timestamps
- colocated derived stage durations
- PD transfer timestamps and transfer duration

Action:

- populate existing blank request CSV columns

### Throughput And Goodput Metrics

Status:

- throughput satisfied
- goodput not satisfied

Already present:

- completed requests
- completed tokens
- request throughput
- token throughput
- run duration

Still missing:

- TTFT SLO threshold
- TPOT SLO threshold
- E2E SLO threshold
- good request count
- request goodput
- SLO attainment fraction
- TTFT miss fraction
- TPOT miss fraction
- E2E miss fraction

Action:

- add SLO configuration to the run and compute goodput summaries

### Batch-Shape Metrics

Status:

- weakly satisfied

Already present:

- coarse running-request count
- coarse waiting-request count
- iteration token totals

Still missing:

- actual batch shapes
- stage-specific queue depth
- admission rate
- decode step size
- prefill batch token counts
- decode batch request counts

Action:

- emit scheduler or batch trace data

### GPU Compute And Memory Metrics

Status:

- partially satisfied

Already present:

- GPU utilization
- memory used
- memory utilization
- power draw
- KV-cache usage percent

Still missing:

- HBM bandwidth utilization
- achieved FLOPs from profiler data
- kernel breakdown by category
- reliable nonzero estimated byte or FLOP counters

Action:

- keep current GPU polling and add profiler-derived rollups for selected runs

### Communication Metrics

Status:

- not satisfied for calibration

Already present:

- coarse NIC byte counters by interface

Still missing:

- request-attributable PD transfer bytes
- request-attributable PD transfer latency
- steady-state PD transfer bandwidth
- NCCL collective summaries
- TP, PP, EP, and DP-attention communication counters
- per-link bandwidth attributable to serving traffic

Action:

- add communication-specific counters and PD transfer logging

### PD-Specific Stage Metrics

Status:

- partially satisfied

Already present:

- PD prefill timing
- PD decode timing
- first decode chunk timestamp

Still missing:

- actual transfer timing
- transfer bytes
- transfer overlap visibility
- decode idle time waiting for transfer
- prefill backpressure or router-blocked time

Action:

- extend `pd_stage_metrics.csv`

### Minimal Experiment Matrix

Status:

- not satisfied

Current coverage:

- one colocated run
- one PD run
- same target request rate
- same workload family
- prompt token counts line up across the two modes
- output token counts and inter-arrival deltas are close, but the exact request
  timeline is not saved as a first-class replay artifact

Still missing:

- colocated single-request baselines
- colocated concurrency sweep
- PD single-request baselines
- PD concurrency sweep
- PD worker-count sweeps
- realistic trace validation runs with SLO configuration

Action:

- expand from one-off runs to a calibration matrix

## Why The Current Runs Are Not Enough For Queueing Or Goodput Calibration

The current operating point is too narrow and too light-load to expose several
behaviors we need.

Evidence from the current runs:

- `request_rate = 0.2` in both modes
- max running requests only reached about `4`
- `vllm:num_requests_waiting` stayed at `0`
- queue-time cumulative sums remained near zero
- goodput was not configured

This means the current runs mostly probe service time under a modest online
load. They do not probe:

- saturation
- backlog formation
- admission control
- decode interference under heavy concurrency
- SLO miss modes

Those behaviors need targeted sweeps.

## Recommended Collector Changes

### Highest Priority

These should be implemented before the next data collection round.

1. Populate blank request-level stage and queue columns in
   `request_metrics.csv`.
2. Populate PD transfer timing and byte columns in `pd_stage_metrics.csv`.
3. Add SLO thresholds and goodput outputs to `run_config.json` and
   `benchmark_result.json`.
4. Record full hardware and serving-layout metadata in `run_config.json`.
5. Emit scheduler or batch-level traces for stage queue depth and batch shape.

### Second Priority

These are highly valuable, especially for diagnosing model mismatch.

1. Add profiler-derived rollups for prefill and decode compute.
2. Add collective and communication summaries.
3. Add explicit router and handoff counters for PD.
4. Distinguish same-host transfer from real NIC transfer.

## Recommended Next Unchunked Experiment Set

To turn the current two-mode unchunked collector into a proper calibration
source, the next collection round should include the following experiments for
`colocated` and `pd_disaggregated`.

Important comparison rule:

- every colocated-versus-PD comparison should replay the same request timeline
  and token shapes
- save that timeline explicitly, either as a standalone request trace or as
  enough fields in `request_metrics.csv` to reproduce it exactly
- do not include chunk-size sweeps in this report's target; those belong to the
  chunked-prefill calibration plan

### A. Colocated Single-Request Baselines

- concurrency `1`
- short prompt, short generation
- long prompt, short generation
- short prompt, long generation
- long prompt, long generation
- at least `30` successful measured repetitions per shape
- record each shape as an explicit row in the saved request trace

Purpose:

- fit prefill and decode service constants cleanly

### B. Colocated Concurrency Sweep

- keep model and hardware fixed
- sweep request rate from light load to saturation
- keep the same prompt and output distribution as the realistic validation run
- at least `1,000` measured successful requests per rate point
- collect at least one overloaded point where queueing, SLO misses, or failures
  are visible
- repeat near-knee points for at least `3` seeds or saved timelines

Purpose:

- fit batching efficiency, interference, and queueing

### C. PD Single-Request Baselines

- concurrency `1`
- same prompt and output sweep
- explicit prefill worker count and decode worker count recorded
- at least `30` successful measured repetitions per shape
- transfer timing and transfer bytes must be populated or the run should be
  labeled `transfer_unobservable`

Purpose:

- isolate prefill, transfer, and decode stage constants

### D. PD Concurrency Sweep

- sweep request rate to saturation
- if possible, sweep prefill worker count
- if possible, sweep decode worker count
- keep transfer path fixed per run
- at least `1,000` measured successful requests per rate point
- collect at least one near-knee point and one overloaded point for each worker
  configuration that matters

Purpose:

- fit overlap, backpressure, and handoff sensitivity

### E. Realistic Trace Validation

- ShareGPT-like or production-like trace mix
- colocated and PD on the same cluster
- same saved request timeline in both modes
- SLO thresholds turned on
- at least `5,000` measured successful requests per mode
- preferred `10,000` measured successful requests per mode
- at least `3` saved timelines or seeds for the final comparison

Purpose:

- final closure against simulator outputs

## Copy-Paste Plan For A Coding Agent

The following plan is written so it can be copied directly into a coding agent
task. It is intentionally concrete and limited to the unchunked modes.

Goal:

- close the unchunked calibration data gap for `colocated` and
  `pd_disaggregated`
- do not add chunked-prefill modes in this task
- produce replayable request traces, populated metrics, scale checks, and a
  run matrix that ASTRA-sim can use for calibration

Task 1. Add replayable request trace support:

- create `calibration/request_traces/`
- generate or save request trace CSVs with the required columns from
  "Required Request Distribution"
- make benchmark runs consume a saved trace instead of regenerating traffic
  independently per mode
- record `request_trace_path` and `request_trace_sha256` in every
  `run_config.json`
- ensure `colocated` and `pd_disaggregated` use identical request ids, arrival
  timestamps, prompt tokens, and requested output tokens for paired runs

Task 2. Strengthen `request_metrics.csv`:

- keep the existing column names
- populate colocated stage fields:
  `service_start_ts`, `prefill_queue_enter_ts`, `prefill_start_ts`,
  `prefill_end_ts`, `decode_queue_enter_ts`, `decode_start_ts`
- populate derived colocated durations:
  `queue_wait_ns`, `prefill_queue_wait_ns`, `decode_queue_wait_ns`,
  `prefill_duration_ns`, `decode_duration_ns`
- populate PD transfer fields:
  `transfer_queue_enter_ts`, `transfer_start_ts`, `transfer_end_ts`,
  `transfer_queue_wait_ns`, `transfer_duration_ns`
- keep failed and timed-out requests with `success_or_failure` and
  `failure_reason`

Task 3. Strengthen `pd_stage_metrics.csv`:

- populate `transfer_start_ts`, `transfer_end_ts`, `transfer_duration_ns`, and
  `transfer_bytes`
- add or record transfer mode metadata:
  `local_fast_path`, `loopback`, `nic`, `rdma`, or equivalent
- if true transfer timing is not observable, explicitly write
  `transfer_timing_available=false` and do not silently substitute proxy
  handoff time

Task 4. Add scheduler and batch traces:

- emit `scheduler_trace.csv` or `batch_metrics.csv` per run
- include timestamp, stage, worker id, batch request count, batch token count,
  generated tokens this step, running requests, waiting requests, and
  stage-specific queue depths
- for PD, include router queue depth, handoff queue depth, and in-flight
  transfer count if observable

Task 5. Record normalized run metadata:

- add hardware fields:
  GPU SKU, GPU count, memory size, memory bandwidth, node count, interconnect
  type, interconnect bandwidth, network type, and network bandwidth
- add model fields:
  dense or MoE, weight precision, KV-cache precision, model shape, and
  `max_model_len`
- add serving layout fields:
  TP, PP, EP, DP-attention, replica count, prefill worker count, decode worker
  count, router settings, and transfer path
- add scheduler caps:
  `max_running_requests`, `max_decode_batch_size`,
  `max_prefill_batch_tokens`, `max_prefill_batch_size`
- add SLO thresholds:
  TTFT, TPOT, and E2E

Task 6. Generate the unchunked experiment matrix:

- service baselines:
  at least `30` successful repetitions per prompt/output shape per mode
- rate sweeps:
  at least `1,000` measured successful requests per mode per rate point
- validation:
  at least `5,000` measured successful requests per mode, preferred `10,000`
- repeat final validation and near-knee rate points for at least `3` saved
  timelines or seeds
- include light, mid, near-knee, and overloaded points
- keep the exact same trace for paired colocated and PD runs

Task 7. Add validation checks before accepting a data drop:

- fail if any paired colocated and PD run use different request trace hashes
- fail if required request columns are missing
- fail if colocated stage fields are empty
- fail if PD transfer fields are empty without an explicit
  `transfer_timing_available=false` reason
- fail if a rate-sweep run has fewer than `1,000` measured successful requests
  unless it is explicitly labeled overloaded or failed
- fail if a validation run has fewer than `5,000` measured successful requests
- fail if SLO thresholds are absent
- fail if hardware, layout, or scheduler metadata is `unknown` for fields that
  are fixed by the run setup

Task 8. Produce a closure summary:

- write `calibration/unchunked_gap_closure_summary.md`
- include run ids, trace hashes, request counts, duration, request rate,
  throughput, goodput, TTFT/TPOT/E2E p50/p90/p99, queue-depth summaries, and
  missing-field counts
- include a table comparing each colocated run with its matching PD run
- explicitly list any fields still unavailable and why

## Definition Of Done For The Next Unchunked Data Drop

The next unchunked calibration handoff should be considered complete only if all
of the following are true:

- every run has `run_config.json`, `request_metrics.csv`,
  `system_timeseries.csv`
- every PD run also has `pd_stage_metrics.csv`
- every comparable colocated and PD run uses the same saved request timeline
- request trace files exist under `calibration/request_traces/`
- trace hashes are recorded and match across paired colocated and PD runs
- prompt and output token distribution summaries are recorded
- request CSV stage and queue columns are populated, not just present in the
  header
- PD stage CSV transfer columns are populated
- SLO thresholds and request goodput are recorded
- hardware topology and serving-layout metadata are recorded
- queue depth and batch-shape signals are available
- at least one single-request baseline and one concurrency sweep exist for each
  runtime mode
- service baselines have at least `30` successful measured repetitions per
  shape per mode
- normal rate-sweep points have at least `1,000` measured successful requests
  per mode, unless explicitly labeled overloaded or failed
- realistic validation runs have at least `5,000` measured successful requests
  per mode
- at least one realistic validation trace exists for both unchunked modes
- final validation and near-knee rate points include at least `3` seeds or saved
  timelines
- no chunked-prefill data is required for this definition of done

## Concrete Handoff To The Teammate

Please keep the current artifact names and keep the current CSV column names for
the two unchunked folders. The main need is to populate the existing schema more
completely, add the missing metadata and scheduler signals, and collect a small
matrix instead of only one light-load point.

The most important fixes are:

1. Save replayable request traces with explicit request counts and token
   distributions.
2. Fill in stage and queue timestamps in `request_metrics.csv`.
3. Fill in transfer timing and bytes in `pd_stage_metrics.csv`.
4. Add explicit hardware, layout, scheduler, and SLO metadata to
   `run_config.json`.
5. Save and replay the same request timeline for colocated and PD comparisons.
6. Add scheduler or batch-level traces so we can fit batching and queueing.
7. Collect more than one operating point per runtime mode, especially
   single-request baselines and request-rate sweeps to saturation.
8. Enforce scale checks: `30` repetitions per service shape, `1,000` measured
   successes per normal rate point, and `5,000` measured successes per
   validation run.

If those items are fixed, the next unchunked data drop will be much closer to a
directly usable ASTRA-sim calibration dataset instead of only a top-line
runtime comparison. Chunked-prefill folders and chunk-size sweeps should be
handled as the separate follow-up target described in the chunked-prefill gap
report.
