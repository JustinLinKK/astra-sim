# Architecture And Calibration Plan

## Goal

Extend the current analytical layer so one study can reason about a model with:

- tensor parallelism
- pipeline parallelism
- expert parallelism
- DP-attention
- colocated or prefill/decode-disaggregated serving

across a cluster of abstracted accelerators.

## What Exists Today

Current coverage is split across three separate paths:

- [TpPpCrossoverModel.cc](/home/justin/astra-sim/astra-sim/analytical/TpPpCrossoverModel.cc) models dense TP and PP for throughput, latency, communication, and memory feasibility.
- [AttentionFfnDisaggregationModel.cc](/home/justin/astra-sim/astra-sim/analytical/AttentionFfnDisaggregationModel.cc) models GPU versus GPU+LPU MoE placement, but not EP or DP-attention inside the serving loop.
- The serving stack under [astra-sim/workload](/home/justin/astra-sim/astra-sim/workload) models serial, colocated, chunked prefill, and PD request scheduling with goodput outputs.

That means the building blocks exist, but they are not yet composed into one
cluster-scale serving model.

## Gap To Target

To hit the requested target, we need four extensions.

### 1. Add a Parallelism Layout Object

Files to extend:

- [AnalyticalConfig.hh](/home/justin/astra-sim/astra-sim/analytical/AnalyticalConfig.hh)
- [AnalyticalConfig.cc](/home/justin/astra-sim/astra-sim/analytical/AnalyticalConfig.cc)
- [ServingConfig.hh](/home/justin/astra-sim/astra-sim/workload/ServingConfig.hh)
- [ServingConfig.cc](/home/justin/astra-sim/astra-sim/workload/ServingConfig.cc)

Add one shared layout description with at least:

- `tp_degree`
- `pp_degree`
- `ep_degree`
- `dp_attention_degree`
- `dp_replica_count`
- optional `prefill_layout`
- optional `decode_layout`

Why:

- TP/PP is currently analytical-only.
- EP and DP-attention are not represented in the serving request schema.
- PD needs separate prefill and decode layouts if those tiers scale differently.

### 2. Split Serving Stage Cost Into Model-Parallel Components

Files to extend:

- [ServingCostModel.hh](/home/justin/astra-sim/astra-sim/workload/ServingCostModel.hh)
- [ServingCostModel.cc](/home/justin/astra-sim/astra-sim/workload/ServingCostModel.cc)
- [AnalyticalCostModels.hh](/home/justin/astra-sim/astra-sim/analytical/AnalyticalCostModels.hh)
- [AnalyticalCostModels.cc](/home/justin/astra-sim/astra-sim/analytical/AnalyticalCostModels.cc)

Current serving cost terms are coarse:

- prefill base
- prefill per token
- decode base
- decode per token
- transfer cost

Target serving cost should instead be decomposed into:

- attention compute
- FFN or expert compute
- TP all-reduce or all-gather
- PP activation send or receive
- EP token dispatch all-to-all
- DP-attention KV savings and synchronization overhead
- PD KV transfer

Why:

- Without this split, we cannot map published TP, PP, EP, or DP-attention
  results onto interpretable simulator knobs.

### 3. Make Worker Pools Topology-Aware

Files to extend:

- [ServingRuntime.hh](/home/justin/astra-sim/astra-sim/workload/ServingRuntime.hh)
- [ColocatedServingRuntime.cc](/home/justin/astra-sim/astra-sim/workload/ColocatedServingRuntime.cc)
- [PdServingRuntime.cc](/home/justin/astra-sim/astra-sim/workload/PdServingRuntime.cc)

Add worker-group semantics instead of treating every worker as one identical
accelerator:

- colocated group with TP-only or TP+PP placement
- prefill worker group
- decode worker group
- expert worker group if EP is enabled
- DP-attention groups that own disjoint KV shards

Why:

- Goodput changes come from placement plus scheduling together.
- TP, PP, EP, and DP-attention affect which requests can batch together and how
  much memory each worker sees.

### 4. Add One Composite Analytical Mode

Files to extend:

- [AnalyticalConfig.hh](/home/justin/astra-sim/astra-sim/analytical/AnalyticalConfig.hh)
- [AnalyticalModelFactory.cc](/home/justin/astra-sim/astra-sim/analytical/AnalyticalModelFactory.cc)
- new `ServingScaleModel.hh/.cc`

This new mode should:

- take a dense or MoE model spec
- take a cluster and interconnect spec
- take a serving trace or request JSON
- take colocated versus PD topology
- emit request-level TTFT, TPOT, E2E, and goodput

Why:

- The current split between closed-form studies and serving runtime is useful,
  but it leaves cluster parallelism outside the request-level serving outputs.

## Calibration Anchors From Published vLLM And SGLang Results

Official sources used in this report:

- vLLM performance update, Sep 5 2024:
  `https://vllm.ai/blog/perf-update`
- vLLM distributed serving docs:
  `https://docs.vllm.ai/en/v0.6.5/serving/distributed_serving.html`
- SGLang Llama serving blog, Jul 25 2024:
  `https://www.lmsys.org/blog/2024-07-25-sglang-llama3/`
- SGLang bench serving guide:
  `https://docs.sglang.io/developer_guide/bench_serving`
- SGLang server arguments:
  `https://docs.sglang.io/docs/advanced_features/server_arguments`
- SGLang multi-node deployment:
  `https://docs.sglang.io/docs/references/multi_node_deployment/multi_node_index`
- SGLang DeepSeek usage with DP-attention:
  `https://docs.sglang.io/docs/basic_usage/deepseek_v3`

Published anchors we can use immediately:

- vLLM reports 2.7x throughput improvement and 5x faster TPOT for Llama 8B,
  plus 1.8x throughput improvement and 2x lower TPOT for Llama 70B, on ShareGPT
  benchmarks comparing v0.6.0 with v0.5.3.
- vLLM documents TP and PP serving, and recommends TP equal to GPUs per node and
  PP equal to node count for multi-node deployments.
- SGLang reports up to 5000 output tokens/s on Llama-8B in short-input offline
  benchmarks on 1xA100, online RPS above 10 in the same family of tests, and up
  to 3.1x higher throughput than vLLM on Llama-70B.
- SGLang documents TP, PP, EP, DP, PD, and multi-node launch arguments in one
  runtime.
- SGLang reports up to 1.9x decode-throughput improvement from DP-attention on
  DeepSeek-family models.

What these anchors are good for:

- initializing batch-efficiency and interference priors
- setting expected speedup ranges for TP, PP, EP, and DP-attention
- choosing realistic sweep directions for colocated versus PD

What they are not good for:

- fitting exact per-stage nanosecond constants
- recovering inter-node transfer overlap behavior
- separating scheduler effects from kernel effects

That is why the remaining work is real SGLang cluster calibration.

## Recommended Calibration Sequence

### Phase A. Public-Anchor Prior Fit

Use [published_benchmark_anchors.json](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/published_benchmark_anchors.json)
to set initial priors for:

- `prefill_batch_efficiency`
- `decode_batch_efficiency`
- `decode_interference_factor`
- PD transfer bandwidth assumptions
- PP bubble penalty assumptions

This phase should only aim for directionally correct throughput ranking.

### Phase B. Single-Node Reproduction

Reproduce single-node public setups with SGLang and vLLM:

- 1xH100 or 1xA100 dense serving
- 4xH100 or 8xH100 dense TP serving
- ShareGPT-style online benchmarks

From these runs, fit:

- prefill base and per-token terms
- decode base and per-token terms
- batch efficiency curves by batch size

### Phase C. Multi-GPU And Multi-Node SGLang Fit

Run SGLang on a real cluster with:

- TP only
- TP + PP
- EP for MoE
- DP-attention on DeepSeek-family models
- colocated and PD deployments

Collect:

- TTFT
- ITL or TPOT
- request throughput
- output-token throughput
- GPU utilization
- network throughput
- prefill-only and decode-only profiling when PD is enabled

Fit the new composite serving cost model from those measurements.

### Phase D. Simulator Closure Test

Re-run the same traces in ASTRA-sim and accept the fit only if:

- throughput ranking matches runtime results
- TTFT and E2E stay within an agreed error band
- PD versus colocated cross-over points move in the correct direction when load
  or transfer bandwidth changes

## What Is Left

The highest-value unfinished item is:

- calibrate the simulator against real SGLang multi-GPU and multi-node runs

Concretely, what is still missing:

- real TP+PP scaling anchors on the exact cluster we care about
- real EP dispatch and combine overheads
- real DP-attention memory savings versus synchronization cost
- real PD transfer and router behavior under realistic traces

The public blogs and docs are enough to structure the model. They are not
enough to close the loop without cluster measurements.
