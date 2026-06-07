# Chunked Prefill Calibration Gap Report

## Current Status: Diagnostic Calibration, Not Main Evidence

Chunked-prefill modes are simulator-supported and now have a diagnostic
calibration artifact for the available non-overload runs. The main report should
still use unchunked `colocated` and unchunked `pd_disaggregated` calibration as
the primary result on the two-`L40S` same-motherboard setup.

The current chunked fit lives at
`calibrations/chunked_prefill_scaling_20260603_074838/fitted_chunked_prefill_latency_cost_model.json`.
It uses chunk-specific prefill timing from
`chunked_prefill_scaling_20260603_074838` and inherits decode, transfer, and
rate/backpressure curves from
`unchunked_scaling_20260530_075459/fitted_unchunked_latency_cost_model.json`.
The replay validation passes the configured non-overload acceptance thresholds,
but overload coverage and broader chunked rate sweeps are still missing.

For the first PD-disaggregated simulator commit, publish only the unchunked
result package at `results/serving_pd_disaggregated_unchunked`. Keep this
chunked report as the follow-up data contract for promoting chunked-prefill from
diagnostic status to primary evidence.

## Purpose

This report extends the serving-scale calibration handoff from the current
two-mode snapshot into the chunked-prefill modes we need next:

- `colocated`
- `colocated_chunked`
- `pd_disaggregated`
- `pd_disaggregated_chunked` as a calibration label for
  `runtime.architecture = pd_disaggregated` with
  `scheduler.chunked_prefill_size > 0`

It is a companion to
[calibration_data_gap_report.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/calibration_data_gap_report.md),
which analyzed the current real-data snapshot for `colocated` and
`pd_disaggregated`.

Important scope note:

- all shared calibration requirements from
  [calibration_data_gap_report.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/calibration_data_gap_report.md)
  still apply
- this document only covers the additional data needed to calibrate
  `colocated_chunked` and chunked-prefill behavior inside
  `pd_disaggregated`

The audience is the teammate updating the real-server collection pipeline.

## Executive Summary

Chunked prefill is not just "colocated with one more knob." It changes the
serving behavior we need to fit in both colocated and PD-disaggregated serving.

In unchunked modes, long-prompt prefill is one continuous service block. In
chunked modes, that same prompt may be split into many prefill chunks, re-enter
the scheduler multiple times, and interleave with decode work from other
requests. In PD mode, those chunks also interact with prefill workers, decode
workers, and the final KV transfer handoff.

That means chunked-prefill calibration needs additional observability for:

- how many chunks each request was split into
- how large those chunks actually were
- how long requests waited between chunks
- how often decode work ran between chunks
- whether long prompts were making forward progress without starving decode
- whether chunking improved TTFT and goodput at the cost of more scheduler
  overhead
- whether PD chunking changes prefill-worker occupancy, transfer timing, or
  decode-worker starvation

The latest workspace now includes real chunked mode coverage:

- `calibrations/chunked_prefill_scaling_20260603_074838/colocated_chunked/*`
- `calibrations/chunked_prefill_scaling_20260603_074838/pd_disaggregated_chunked/*`

The remaining chunked-prefill problem is now coverage depth: there are service
baselines and one pilot rate point, but not the full overload, validation, and
chunk-size sweep matrix.

## Current Status

### What Exists Today

The current real calibration snapshot includes:

- `calibration/colocated/*`
- `calibration/pd_disaggregated/*`
- `calibration/colocated_chunked/*`
- `calibration/pd_disaggregated_chunked/*`

Recommended naming convention:

- use `calibration/pd_disaggregated_chunked/` for real-server data when PD
  chunking is enabled
- keep `runtime_architecture = pd_disaggregated` in configs if the runtime does
  not expose a separate architecture value
- add `calibration_mode = pd_disaggregated_chunked` or
  `prefill_chunking_enabled = true` so downstream scripts can distinguish this
  from unchunked PD

### What Exists In The Simulator As A Reference Shape

The simulator already treats `colocated_chunked` as a first-class runtime mode
and also supports chunked prefill inside `pd_disaggregated` when
`scheduler.chunked_prefill_size > 0`:

- [ServingTypes.hh](/home/justin/astra-sim/astra-sim/workload/ServingTypes.hh)
- [ServingConfig.hh](/home/justin/astra-sim/astra-sim/workload/ServingConfig.hh)
- [ColocatedServingRuntime.cc](/home/justin/astra-sim/astra-sim/workload/ColocatedServingRuntime.cc)
- [PdServingRuntime.cc](/home/justin/astra-sim/astra-sim/workload/PdServingRuntime.cc)

Relevant scheduler knobs already modeled in ASTRA-sim:

- `chunked_prefill_size`
- `prefill_max_requests`
- `max_prefill_batch_tokens`
- `max_decode_batch_requests`
- `max_running_requests`
- `scheduler_policy`
- `enable_mixed_chunk`

Relevant chunk-aware request metrics already modeled in ASTRA-sim:

- `prefill_chunk_count`
- `max_prefill_chunk_tokens`
- `prefill_service_ns`
- `prefill_stage_wait_ns`
- `total_prefill_queue_wait_ns`

Relevant trace outputs already modeled in ASTRA-sim:

- `event_trace_output`
- `stage_metrics_output`

Chunked simulator tests also validate the exact behaviors we need to observe in
real runtime data:

- fixed-size prefill chunk splitting
- interleaving between decode work and long-prompt prefill chunks

References:

- [tests/rt_serving_chunked/readme.txt](/home/justin/astra-sim/tests/rt_serving_chunked/readme.txt)
- [tests/rt_serving_chunked/run.sh](/home/justin/astra-sim/tests/rt_serving_chunked/run.sh)
- [tests/rt_serving_pd/run.sh](/home/justin/astra-sim/tests/rt_serving_pd/run.sh)

## Why Chunked Prefill Needs Extra Calibration Data

The general two-mode report focuses on:

- request-level latency
- queueing
- throughput
- PD transfer

Chunked prefill adds a different failure mode:

- the top-line request latency can look fine while the scheduler is chunking
  badly

Examples:

- a long prompt could be split into too many tiny chunks and suffer excessive
  rescheduling overhead
- a long prompt could be split correctly, but decode work might still be
  starved between chunks
- TTFT could improve for short requests while long requests suffer poor forward
  progress
- average TPOT could stay flat while chunk requeue delays grow dramatically

Those effects are only visible if we collect chunk-aware traces.

## Request Timeline Recommendation

Yes, the calibration package should include a request timeline, and all four
mode variants should be replayed against the same timeline whenever possible:

- `colocated`
- `colocated_chunked`
- `pd_disaggregated`
- `pd_disaggregated_chunked`

Use real production or benchmark arrival timestamps if they are available. If
the traffic has to be synthetic, use a Poisson arrival process as the default
open-loop load generator for steady-state sweeps.

Important clarification:

- model arrivals as inter-arrival times sampled from an exponential
  distribution with rate `lambda` requests per second
- do not model this as a vague per-request probability
- if the collector or load generator only supports discrete time buckets, the
  per-bucket request probability is only an approximation:
  `p = 1 - exp(-lambda * delta_t)`, or `p ~= lambda * delta_t` when
  `delta_t` is very small

For calibration, the request timeline should record at least:

- `request_id`
- `arrival_time_ns`
- `prompt_tokens`
- `output_tokens`
- dataset or trace source
- random seed if generated synthetically
- arrival process name, such as `recorded`, `poisson`, or `burst`
- target request rate or offered load

Poisson traffic is useful for saturation curves and queueing calibration, but
it should not be the only traffic shape. Chunked prefill especially needs:

- single-request prompt-length sweeps for service-time isolation
- deterministic mixed short-and-long prompt traces for scheduler debugging
- Poisson request-rate sweeps for goodput and queueing behavior
- burst or replay traces if production traffic is bursty

The key rule is to hold the request timeline and token shapes fixed across the
four modes. Otherwise, mode differences can be confused with traffic
differences.

## Desired Artifact Packages For Chunked Modes

The real chunked-prefill datasets should live under:

- `calibration/colocated_chunked/`
- `calibration/pd_disaggregated_chunked/`

Required artifacts:

- `run_config.json`
- `request_metrics.csv`
- `system_timeseries.csv`
- `benchmark_result.json`

Additional chunk-specific artifacts strongly recommended:

- `event_trace.csv`
- `stage_metrics.csv`

Additional PD chunked artifacts strongly recommended:

- `pd_stage_metrics.csv`
- transfer timing and transfer-byte fields in `request_metrics.csv`
- network or transfer-path counters in `system_timeseries.csv`

Why these extra files matter:

- `request_metrics.csv` is good for per-request closure
- `event_trace.csv` is needed to see interleaving and scheduler order
- `stage_metrics.csv` is needed to see per-batch chunk shapes and queue depth at
  schedule time
- `pd_stage_metrics.csv` and transfer fields are needed to separate PD prefill
  chunking from final KV handoff behavior

## Additional Metadata Needed For Chunked Prefill

Everything from the general gap report still applies. In addition, chunked
prefill needs these mode-specific run inputs recorded explicitly.

### Required Chunked Scheduler Metadata

Record these fields in `run_config.json`:

- `calibration_mode`, with values such as `colocated_chunked` or
  `pd_disaggregated_chunked`
- `runtime_architecture`, with `colocated_chunked` for colocated chunking and
  `pd_disaggregated` for PD chunking in the current simulator shape
- `prefill_chunking_enabled`
- `chunked_prefill_size`
- `prefill_max_requests`
- `max_prefill_batch_tokens`
- `max_decode_batch_requests`
- `max_running_requests`
- `scheduler_policy`
- `enable_mixed_chunk`

For PD chunked-prefill runs, also record:

- `pd.prefill_workers`
- `pd.decode_workers`
- `pd.prefill_max_batch_tokens`
- `pd.prefill_max_requests`
- `pd.decode_max_batch_requests`
- `pd.prefill_tp_degree`
- `pd.decode_tp_degree`
- `pd.transfer.enabled`
- transfer latency, bandwidth, efficiency, and bytes-per-token assumptions

### Helpful Additional Scheduler Metadata

If the runtime exposes them, also record:

- prefill-first versus decode-first priority policy
- whether decode can preempt pending prefill chunks
- any fairness heuristic for long prompts
- whether chunked prefill is applied to all prompts or only above a threshold
- prompt-length threshold for chunking if one exists

### Why This Matters

The same `chunked_prefill_size` can behave very differently depending on:

- how many requests may share a prefill batch
- how decode is prioritized
- whether mixed chunk batching is enabled
- whether prefill and decode are colocated or split across PD worker pools
- whether final KV transfer is enabled and visible in the measured path

Without these settings, measured chunk behavior cannot be mapped back to the
actual scheduler policy being calibrated.

## Additional Request-Level Metrics Needed For Chunked Prefill

The base request-level fields from the general report still apply. For chunked
prefill, we also need chunk-aware per-request measurements.

### Required Extra Request Columns

Add these fields to `request_metrics.csv` for both `colocated_chunked` and
`pd_disaggregated_chunked` runs:

- `prefill_chunk_count`
- `max_prefill_chunk_tokens`
- `prefill_service_ns`
- `prefill_stage_wait_ns`
- `total_prefill_queue_wait_ns`

If the runtime can expose them cleanly, also add:

- `first_prefill_chunk_start_ts`
- `last_prefill_chunk_end_ts`
- `chunk_requeue_count`
- `decode_interleaving_count`
- `prefill_resume_count`

For PD chunked-prefill runs, also keep the PD fields from the base report:

- `transfer_queue_enter_ns`
- `transfer_start_ns`
- `transfer_end_ns`
- `transfer_service_ns`
- `transfer_stage_wait_ns`
- `total_transfer_queue_wait_ns`
- `transfer_handoff_count`
- `max_transfer_chunk_tokens`
- transfer bytes for the final KV handoff

### Meaning Of The Chunked-Specific Fields

- `prefill_chunk_count`
  Number of prefill chunks used to complete the prompt.
- `max_prefill_chunk_tokens`
  Largest chunk size actually scheduled for the request.
- `prefill_service_ns`
  Total service time spent executing prefill chunks.
- `prefill_stage_wait_ns`
  Time request spent paused between chunk services while still in prefill phase.
- `total_prefill_queue_wait_ns`
  Aggregate queueing time for all chunk admissions.
- `chunk_requeue_count`
  Number of times a partially prefilling request returned to the queue.
- `decode_interleaving_count`
  Number of decode batches from other requests inserted between this request's
  prefill chunks.

### Why These Fields Matter

Chunked calibration is not only about end-to-end TTFT. We also need to know:

- whether long prompts were actually split
- whether the chosen chunk size was respected in practice
- whether chunking created too much scheduler overhead
- whether long requests made steady progress
- whether PD chunking changed transfer arrival timing or decode-stage wait

Without `prefill_chunk_count` and the stage-wait fields, we cannot tell whether
chunking is helping fairness or just fragmenting work.

## Additional Batch-Level And Trace Data Needed

This is the most important chunked-prefill addition beyond the base report.

### 1. `event_trace.csv`

For chunked-prefill calibration, this file should be treated as required, not
optional.

Recommended columns, matching the simulator trace shape where possible:

- `time_ns`
- `event`
- `batch_id`
- `worker_id`
- `stage`
- `request_ids`
- `request_count`
- `total_tokens`
- `duration_ns`
- `include_base_latency`
- `running_request_count`
- `admission_queue_depth`
- `prefill_queue_depth`
- `transfer_queue_depth`
- `decode_queue_depth`
- `inflight_transfer_count`

This file must capture at least:

- request arrival events
- chunked prefill batch scheduled events
- chunked prefill batch completed events
- PD transfer or KV handoff events when running `pd_disaggregated_chunked`
- decode batch scheduled events
- decode batch completed events
- request finished events

Why it is required:

- it is the clearest way to see whether decode work actually interleaved
  between long-prompt chunks
- for PD chunking, it shows whether decode waited on chunked prefill or on the
  final transfer handoff
- it lets us validate the exact chunk progression pattern, not just the final
  request outcome

### 2. `stage_metrics.csv`

For chunked-prefill runs, capture one row per scheduled batch with at least:

- `batch_id`
- `worker_id`
- `stage`
- `request_ids`
- `request_count`
- `total_tokens`
- `scheduled_at_ns`
- `completed_at_ns`
- `duration_ns`
- `running_request_count`
- `admission_queue_depth`
- `prefill_queue_depth`
- `transfer_queue_depth`
- `decode_queue_depth`
- `inflight_transfer_count`

Chunked-prefill-specific additions if possible:

- `chunk_request_id`
- `chunk_index_within_request`
- `remaining_prefill_tokens_before`
- `remaining_prefill_tokens_after`
- `is_final_prefill_chunk`
- `pd_worker_pool` when the stage belongs to PD prefill, transfer, or decode

Why it is required:

- request-level aggregates alone cannot reconstruct batch shapes accurately
- stage metrics let us fit chunk service cost as a function of chunk token count
- stage metrics let us see whether queue buildup happened in admission,
  prefill, transfer, or decode

### 3. Chunk Shape Summaries

At run summary level, record:

- mean `prefill_chunk_count`
- p50 `prefill_chunk_count`
- p90 `prefill_chunk_count`
- p99 `prefill_chunk_count`
- `total_prefill_chunks`
- mean `max_prefill_chunk_tokens`
- p50, p90, p99, and max prefill chunk token count
- fraction of requests with `prefill_chunk_count > 1`

These map directly to "is chunking actually happening, and how aggressively?"

## Additional Time-Series Needed For Chunked Prefill

The generic `system_timeseries.csv` is still useful, but chunked mode needs
more scheduler-visible time-series than unchunked colocated mode.

### Required Chunked Time-Series Signals

Collect these over time, ideally at per-iteration or per-scheduler-step
granularity if possible:

- active running requests
- active decode sequences
- prefill queue depth
- transfer queue depth for PD chunked runs
- decode queue depth
- chunked prefill batches scheduled per interval
- transfer handoffs started and completed per interval for PD chunked runs
- decode batches scheduled per interval
- prefill chunk tokens scheduled per interval
- number of incomplete chunked requests in flight
- number of requests currently paused between prefill chunks
- number of requests waiting for transfer or decode admission after prefill

### High-Value Optional Signals

If the runtime can emit them, also collect:

- long-request forward-progress rate
- average remaining prompt tokens of partially prefilling requests
- average time gap between consecutive chunks of the same request
- fraction of scheduler steps assigned to decode versus chunked prefill
- fraction of decode batches delayed because prefill chunk work was chosen
- fraction of prefill chunks delayed because decode protection was active
- fraction of PD decode workers idle while waiting for chunked prefill or
  transfer completion

### Why These Signals Matter

Chunked-prefill quality depends on balance:

- too much decode priority and long prompts stall forever
- too much chunked prefill priority and decode TPOT collapses
- in PD chunked mode, transfer and decode queues can hide the prefill-side root
  cause unless they are recorded separately

The balance cannot be inferred reliably from request-level TTFT alone.

## Additional Communication And Memory Signals For Chunked Prefill

Colocated chunked prefill does not need PD transfer logging. PD chunked prefill
does, because final KV handoff cost can be confused with prefill chunking if we
only look at TTFT.

Recommended extra signals:

- KV-cache usage over time during chunk-heavy runs
- GPU memory used during long-prompt chunk progression
- batch-level prompt-token totals versus decode-token totals
- if available, HBM bandwidth during prefill-heavy and decode-heavy intervals
- for PD chunked runs, network RX and TX bytes on the transfer path
- for PD chunked runs, transfer bytes per request and transfer duration per
  request
- for PD chunked runs, prefill-worker and decode-worker utilization split

Why:

- chunking is often used to keep long prompts from dominating memory and prefill
  occupancy
- memory pressure and long-prompt batching pressure are part of what chunking is
  supposed to control
- PD chunking must be separated from transfer cost, otherwise the fit may assign
  transfer delay to the wrong prefill or decode knob

## Chunked-Specific Calibration Questions

The extra data above is needed to answer these questions:

1. Is the chosen `chunked_prefill_size` too small, causing excessive scheduler
   overhead?
2. Is it too large, causing long-prompt chunks to block decode too much?
3. Are long prompts making forward progress between decode steps?
4. Are short requests getting better TTFT because of chunking, or just because
   the run is under light load?
5. At what request rates does `colocated_chunked` outperform plain `colocated`
   on goodput?
6. At what request rates does `pd_disaggregated_chunked` outperform unchunked
   `pd_disaggregated`?
7. Does PD chunking reduce prefill-worker head-of-line blocking, or does the
   final transfer/decode stage dominate TTFT anyway?
8. At what point does `colocated_chunked` become close enough to PD behavior
   that transfer-free chunking is the better design?

Those questions cannot be answered from TTFT and TPOT summaries alone.

## Recommended Chunked Calibration Matrix

To calibrate chunked prefill, we should not run only one chunk size and one load
point. The matrix should compare all four mode labels with the same request
timeline wherever the comparison is mode-facing:

- `colocated`
- `colocated_chunked`
- `pd_disaggregated`
- `pd_disaggregated_chunked`

### A. Single-Request Prompt-Length Sweep

- concurrency `1`
- fixed output length
- sweep prompt length from below chunk size to several multiples of chunk size
- run this for `colocated_chunked` and `pd_disaggregated_chunked`

Purpose:

- verify chunk splitting logic
- measure service overhead introduced by chunking itself
- isolate prefill chunk service time before queueing and transfer interactions

### B. Chunk-Size Sweep

- fix model and hardware
- use same request mix
- sweep `chunked_prefill_size`, for example:
  `128`, `256`, `512`, `1024`, `2048`
- run the same sweep for colocated chunking and PD chunking

Purpose:

- fit the tradeoff between prefill fragmentation and decode protection
- see whether the best colocated chunk size differs from the best PD chunk size

### C. Mixed Short-And-Long Prompt Workload

- combine short prompts and long prompts in the same run
- use realistic output lengths
- run with moderate and high offered load
- use an explicit request timeline first, then repeat with Poisson timelines

Purpose:

- observe real interleaving behavior
- measure whether short requests gain TTFT while long requests still progress

### D. Four-Mode Request-Rate Sweep

- same hardware
- same workload mix
- same random seed or recorded timeline across modes
- compare `colocated`, `colocated_chunked`, `pd_disaggregated`, and
  `pd_disaggregated_chunked`
- sweep request rate to saturation

Purpose:

- identify where chunking begins to pay off
- fit the scheduler-side benefit of chunking rather than assuming it
- separate architecture effects from traffic-shape effects

Poisson is appropriate here because request rate is the independent variable:
generate inter-arrival times from `Exp(lambda)` and repeat each `lambda` point
across all four modes.

### E. PD Chunking Isolation Sweep

- keep PD worker counts and transfer settings fixed
- compare `pd_disaggregated` against `pd_disaggregated_chunked`
- if possible, run once with transfer enabled and once with transfer disabled or
  minimized

Purpose:

- isolate the benefit of chunked prefill on the PD prefill side
- prevent final KV transfer latency from being fitted as prefill chunk overhead

### F. Scheduler Policy Sweep

- if supported, compare policy variants
- compare `enable_mixed_chunk = false` and `true`
- compare any prefill/decode priority variants
- apply the same policy sweep to colocated chunking and PD chunking when both
  runtimes expose the same policy

Purpose:

- separate chunk-size effects from scheduler-policy effects

### G. Realistic Goodput Validation

- ShareGPT-like or production-like trace mix
- SLO thresholds enabled
- compare all four modes:
  `colocated`, `colocated_chunked`, `pd_disaggregated`,
  `pd_disaggregated_chunked`
- prefer recorded request timestamps if available
- use Poisson replay only when recorded timestamps are unavailable

Purpose:

- final decision-useful comparison across the four serving mode labels

## Definition Of Done For Chunked Prefill Data

The chunked-prefill calibration handoff should be considered complete only if
all of the following are true:

- `calibration/colocated_chunked/` exists
- `calibration/pd_disaggregated_chunked/` exists, or PD chunked-prefill runs are
  otherwise clearly labeled and separated from unchunked PD
- each chunked dataset contains `run_config.json`, `request_metrics.csv`,
  `system_timeseries.csv`, `benchmark_result.json`
- each chunked dataset also contains `event_trace.csv` and `stage_metrics.csv`
- PD chunked runs also contain `pd_stage_metrics.csv` or equivalent
  prefill/decode/transfer stage records
- `run_config.json` records chunked scheduler knobs explicitly
- `run_config.json` records whether the run is colocated chunked or PD chunked
- `request_metrics.csv` contains chunk-aware fields such as
  `prefill_chunk_count` and `max_prefill_chunk_tokens`
- PD chunked `request_metrics.csv` keeps transfer timing and transfer-byte
  fields populated
- the request timeline is recorded and reused across comparable modes
- traces show actual interleaving between chunked prefill and decode under mixed
  load
- there is at least one single-request prompt-length sweep
- there is at least one chunk-size sweep
- there is at least one chunked-versus-unchunked colocated sweep
- there is at least one chunked-versus-unchunked PD sweep
- there is at least one realistic goodput validation run with SLO thresholds

## Concrete Handoff To The Teammate

Please treat `colocated_chunked` and `pd_disaggregated_chunked` as distinct
calibration modes, not just unlabeled variants of the existing modes.

Most important remaining additions beyond the base two-mode calibration report:

1. Extend the real `calibration/colocated_chunked/` and
   `calibration/pd_disaggregated_chunked/` datasets with overload, validation,
   and chunk-size sweep coverage.
2. Record chunked scheduler metadata such as `chunked_prefill_size`,
   `prefill_max_requests`, and `enable_mixed_chunk`.
3. Record and replay the same request timeline across the four comparison modes.
4. Use Poisson arrivals for request-rate sweeps when real timestamps are not
   available, with `lambda` and seed recorded.
5. Add chunk-aware request metrics such as `prefill_chunk_count`,
   `max_prefill_chunk_tokens`, and total prefill stage wait.
6. Keep PD transfer timing and bytes populated for `pd_disaggregated_chunked`.
7. Always emit `event_trace.csv` for chunked runs.
8. Emit `stage_metrics.csv` so we can fit per-chunk batch shapes and queue
   buildup.
9. Collect chunk-size sweeps and chunked-versus-unchunked comparisons for both
   colocated and PD-disaggregated serving, not just one run.

If those items are added, we will have a calibration package that can
actually explain chunked-prefill behavior instead of only comparing headline
latency numbers.
