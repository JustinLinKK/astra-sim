# ASTRA-sim
[ASTRA-sim](https://astra-sim.github.io/) is a distributed AI system simulator. It models the end-to-end software and hardware stack of modern AI systems - encompassing workload scheduling, collective communication algorithms, and hardware architectures (compute/memory/network). Through a suite of APIs, it enables plug-and-play of external open/proprietary components for modeling different parts of the AI system. This provides end-to-end multi-fidelity simulation capabilities for aiding in design and deployment of next-generation distributed AI systems. 

## Analytical Modes
This repository includes a unified analytical layer for three inference studies:

1. `tp_pp_crossover`
   Sweeps TP versus PP tradeoffs for 70B-class dense models.
2. `serving_disagg_colocated`
   Runs the upgraded serving simulator for serial, colocated, chunked-prefill,
   and prefill/decode-disaggregated inference studies.
3. `attention_ffn_disaggregation`
   Compares homogeneous GPU against heterogeneous GPU+LPU placement for trillion-parameter MoE decode.

The shared implementation lives under [astra-sim/analytical](</home/justin/astra-sim/astra-sim/analytical>) and keeps model specs, hardware specs, interconnects, cost models, and result writers in one framework-native module.

## Quick Start
Build the analytical binaries with:

```bash
./build/astra_analytical/build.sh
```

The main binaries are:

- `build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware`
- `build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Unaware`

Run the shipped analytical configs with:

```bash
./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
  --analytical-config=$(realpath configs/tp_pp_70b.yaml)

./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
  --analytical-config=$(realpath configs/serving_disagg_colocated.yaml)

./build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware \
  --analytical-config=$(realpath configs/attention_ffn_gpu_lpu_moe.yaml)
```

Important behavior:

- `--analytical-config` is the public entrypoint for these analytical modes.
- `tp_pp_crossover` and `attention_ffn_disaggregation` are closed-form analytical runs and do not instantiate the event-driven serving runtime.
- `serving_disagg_colocated` still uses the existing serving simulator underneath, so serving behavior stays on the established path.

## Analytical Configs
The checked-in examples live in [configs](</home/justin/astra-sim/configs>):

- [configs/tp_pp_70b.yaml](/home/justin/astra-sim/configs/tp_pp_70b.yaml)
- [configs/serving_disagg_colocated.yaml](/home/justin/astra-sim/configs/serving_disagg_colocated.yaml)
- [configs/attention_ffn_gpu_lpu_moe.yaml](/home/justin/astra-sim/configs/attention_ffn_gpu_lpu_moe.yaml)

These YAMLs are fully explicit. Output paths are resolved relative to the YAML file location, which makes it easy to copy a config and redirect outputs without rewriting absolute paths.

## Outputs
`tp_pp_crossover` writes a row-oriented CSV plus a summary JSON containing pure-TP versus pure-PP crossover records.

`serving_disagg_colocated` keeps the existing request metrics CSV, summary JSON, and metadata JSON format from the serving simulator.

`attention_ffn_disaggregation` writes a row-oriented CSV plus a summary JSON comparing:

- `homogeneous_gpu`
- `heterogeneous_gpu_lpu`

## Calibration Data via Git LFS

Raw calibration telemetry is too large for normal Git history, and most of it
is not needed to fit or replay the paper models. The repository tracks compact
split `tar.zst` calibration archives with Git LFS and keeps the expanded
`calibrations/*/` folders ignored locally. Expanded folders are reconstructed
from the compact bundles when you need to run calibration or replay commands.

Archive parts are kept below 2 GB so they fit the portable GitHub LFS file-size
limit for Free and Pro accounts. See GitHub's
[Git LFS overview](https://docs.github.com/repositories/working-with-files/managing-large-files/about-git-large-file-storage)
and
[Git LFS billing notes](https://docs.github.com/billing/managing-billing-for-git-large-file-storage/about-billing-for-git-large-file-storage)
for the current limits and quota behavior.

Current compact calibration roots:

- `calibrations/unchunked_scaling_20260530_075459`
- `calibrations/chunked_prefill_scaling_20260605_105450`

Each compact bundle includes the files needed for fitting, replay validation,
and provenance:

- per-run `run_config.json`
- per-run `request_metrics.csv`
- per-run `pd_stage_metrics.csv`
- per-run `scheduler_trace.csv`
- compacted per-run `benchmark_result.json`
- request traces and trace metadata under `request_traces/`
- fitted cost-model JSON files
- validation summaries, comparison CSVs, closure notes, and replay plots

Each compact bundle intentionally omits raw or regenerated observability:

- `system_timeseries.csv`
- `scheduler_trace_raw.csv`
- `scheduler_trace_decode_raw.csv`
- `scheduler_trace_prefill_raw.csv`
- `available_metrics_summary.json`
- `run.log`
- `run.pid`
- `sim_vs_calibrated/simulation_runs/**`

The compacted `benchmark_result.json` keeps scalar throughput/latency and
provenance fields, and drops large arrays such as generated text, per-request
raw lists, and error dumps.

Install and verify the required host tools:

```bash
git lfs version
zstd --version
tar --version
```

Fetch the LFS archive parts after cloning or switching branches:

```bash
git lfs pull --include='calibrations/*.compact.tar.zst.part-*'
sha256sum -c calibrations/SHA256SUMS
git check-attr filter -- calibrations/*.compact.tar.zst.part-*
git lfs status
```

Restore both calibration roots from their split compact archive parts:

```bash
mkdir -p calibrations

for root in \
  unchunked_scaling_20260530_075459 \
  chunked_prefill_scaling_20260605_105450
do
  cat "calibrations/${root}.compact.tar.zst.part-"* \
    | tar --use-compress-program=zstd -xf - -C calibrations
done
```

Verify an archive before or after restoring it:

```bash
sha256sum -c calibrations/SHA256SUMS

for root in \
  unchunked_scaling_20260530_075459 \
  chunked_prefill_scaling_20260605_105450
do
  cat "calibrations/${root}.compact.tar.zst.part-"* | zstd -t -
  cat "calibrations/${root}.compact.tar.zst.part-"* \
    | tar --use-compress-program=zstd -tf - | wc -l
done

python3 -m json.tool calibrations/MANIFEST.json >/dev/null
```

The expected part hashes and stream hashes are recorded in
`calibrations/SHA256SUMS`; archive metadata, source file counts, and restore
commands are recorded in `calibrations/MANIFEST.json`.

To refresh the compact bundles after changing local expanded calibration roots,
run:

```bash
python3 tools/calibration/package_calibration_artifacts.py --force
sha256sum -c calibrations/SHA256SUMS
```

## Calibrated PD-Disaggregated Serving Result

The first calibrated PD-disaggregated serving result is intentionally scoped to
unchunked serving:

- calibrated modes: `colocated` and `pd_disaggregated`
- calibration hardware: two same-node NVIDIA `L40S` GPUs on the same
  motherboard
- colocated baseline: one `L40S` handles both prefill and decode
- PD run: one `L40S` prefill worker and one `L40S` decode worker
- handoff path: same-node GPU-to-GPU KV handoff; no inter-node network is in the
  calibrated setup

The tracked result package is
[results/serving_pd_disaggregated_unchunked](/home/justin/astra-sim/results/serving_pd_disaggregated_unchunked).
It contains calibration-vs-simulation plots and extrapolation studies without
including the expanded raw `calibrations/` directories. The chunked RPS-4 result
package is
[results/serving_chunked_rps4](/home/justin/astra-sim/results/serving_chunked_rps4).

The current acceptance gate passes for non-overloaded unchunked runs. Chunked
prefill is implemented and has diagnostic replay artifacts, but four-GPU
chunked results are still extrapolated until broader chunked calibration data is
collected.

## Reproducing Paper Experiments

Build the analytical binary first:

```bash
./build/astra_analytical/build.sh

export ASTRA_BIN=build/astra_analytical/build/bin/AstraSim_Analytical_Congestion_Aware
export SERVING_TEMPLATE=configs/serving_disagg_colocated.yaml
```

### Two-GPU Calibration Closure

Fit the unchunked request-latency cost model from the restored compact
calibration bundle:

```bash
python3 tools/calibration/fit_serving_cost_model.py \
  --calibration-root calibrations/unchunked_scaling_20260530_075459 \
  --modes colocated,pd_disaggregated \
  --fit-target request_latency \
  --skip-incomplete-runs \
  --output calibrations/unchunked_scaling_20260530_075459/fitted_unchunked_latency_cost_model.json
```

Replay the golden traces, regenerate calibration-vs-simulation plots, and fail
if the non-overloaded acceptance gate fails:

```bash
python3 tools/calibration/run_golden_calibration_loop.py \
  --calibration-root calibrations/unchunked_scaling_20260530_075459 \
  --binary "$ASTRA_BIN" \
  --fail-on-acceptance
```

The closure loop writes:

- `calibrations/unchunked_scaling_20260530_075459/fitted_unchunked_latency_cost_model.json`
- `calibrations/unchunked_scaling_20260530_075459/sim_vs_calibrated/comparison_summary.csv`
- `calibrations/unchunked_scaling_20260530_075459/sim_vs_calibrated/validation_summary.json`
- `calibrations/unchunked_scaling_20260530_075459/sim_vs_calibrated/plots/*.svg`

The compact bundle also keeps `scheduler_trace.csv`, so you can run the
optional stage-level fit without restoring raw scheduler telemetry:

```bash
python3 tools/calibration/fit_serving_cost_model.py \
  --calibration-root calibrations/unchunked_scaling_20260530_075459 \
  --modes colocated,pd_disaggregated \
  --fit-target stage \
  --skip-incomplete-runs \
  --output build/repro/unchunked_stage_fit.json
```

For chunked diagnostic replay, fit each restored chunk root while inheriting
the stable unchunked terms. The compact archive includes `chunk_128`,
`chunk_256`, and `chunk_512`; the active published chunked fit is `chunk_512`.

```bash
for chunk in chunk_128 chunk_256 chunk_512
do
  python3 tools/calibration/fit_serving_cost_model.py \
    --calibration-root "calibrations/chunked_prefill_scaling_20260605_105450/${chunk}" \
    --modes colocated_chunked,pd_disaggregated_chunked \
    --fit-target request_latency \
    --skip-incomplete-runs \
    --base-fit-json calibrations/unchunked_scaling_20260530_075459/fitted_unchunked_latency_cost_model.json \
    --output "calibrations/chunked_prefill_scaling_20260605_105450/${chunk}/fitted_chunked_prefill_latency_cost_model.json"
done
```

Run the active `chunk_512` diagnostic replay with that fit:

```bash
python3 tools/calibration/run_golden_calibration_loop.py \
  --calibration-root calibrations/chunked_prefill_scaling_20260605_105450/chunk_512 \
  --modes colocated_chunked,pd_disaggregated_chunked \
  --fit-json calibrations/chunked_prefill_scaling_20260605_105450/chunk_512/fitted_chunked_prefill_latency_cost_model.json \
  --binary "$ASTRA_BIN" \
  --skip-fit \
  --fail-on-acceptance
```

### Unchunked Extrapolation Sweeps

All sweep commands use tracked case files and write local outputs under
`build/repro`. Parse each sweep before plotting it. The checked-in paper
outputs under
`results/serving_pd_disaggregated_unchunked/extrapolation/` are compact
packages of these parsed CSV/JSON files plus the generated Markdown, SVG, and
TeX paper artifacts.

Compact GPU and TP scaling:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/pd_extrapolated_compact_sweep.json \
  --output-dir build/repro/compact_gpu_scaling \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/compact_gpu_scaling/summary.csv \
  --json-output build/repro/compact_gpu_scaling/summary.json \
  build/repro/compact_gpu_scaling

python3 tools/plot_pd_extrapolated_sweep.py \
  --input build/repro/compact_gpu_scaling/summary.csv \
  --output-dir build/repro/compact_gpu_scaling
```

Four-GPU worker assignment:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/pd_4gpu_worker_assignment_sweep.json \
  --output-dir build/repro/worker_assignment_4gpu \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/worker_assignment_4gpu/summary.csv \
  --json-output build/repro/worker_assignment_4gpu/summary.json \
  build/repro/worker_assignment_4gpu

python3 tools/plot_pd_worker_assignment_sweep.py \
  --input build/repro/worker_assignment_4gpu/summary.csv \
  --output-dir build/repro/worker_assignment_4gpu
```

Request-rate and prompt-pressure sweep:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/pd_4gpu_pressure_sweep.json \
  --output-dir build/repro/pressure_4gpu \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/pressure_4gpu/summary.csv \
  --json-output build/repro/pressure_4gpu/summary.json \
  build/repro/pressure_4gpu

python3 tools/plot_pd_pressure_sweep.py \
  --input build/repro/pressure_4gpu/summary.csv \
  --output-dir build/repro/pressure_4gpu
```

Output-length sensitivity:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/pd_4gpu_output_length_sweep.json \
  --output-dir build/repro/output_length_4gpu \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/output_length_4gpu/summary.csv \
  --json-output build/repro/output_length_4gpu/summary.json \
  build/repro/output_length_4gpu

python3 tools/plot_pd_output_length_sweep.py \
  --input build/repro/output_length_4gpu/summary.csv \
  --output-dir build/repro/output_length_4gpu
```

Fixed-rate 4-GPU input/output shape matrix:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/paper_crossover_cases/input_output_shape_4gpu.json \
  --output-dir build/repro/input_output_shape_4gpu \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/input_output_shape_4gpu/summary.csv \
  --json-output build/repro/input_output_shape_4gpu/summary.json \
  build/repro/input_output_shape_4gpu
```

Refresh the tracked result folders after rerunning the four core 4-GPU sweeps:

```bash
export RESULT_ROOT=results/serving_pd_disaggregated_unchunked/extrapolation

cp build/repro/compact_gpu_scaling/summary.csv \
  build/repro/compact_gpu_scaling/summary.json \
  build/repro/compact_gpu_scaling/pd_extrapolated_sweep_overview.md \
  build/repro/compact_gpu_scaling/pd_extrapolated_sweep_overview.svg \
  build/repro/compact_gpu_scaling/pd_extrapolated_tpot_ranked.md \
  build/repro/compact_gpu_scaling/pd_extrapolated_tpot_ranked.svg \
  build/repro/compact_gpu_scaling/pd_extrapolated_ttft_ranked.md \
  build/repro/compact_gpu_scaling/pd_extrapolated_ttft_ranked.svg \
  "$RESULT_ROOT/compact_gpu_scaling/"

cp build/repro/worker_assignment_4gpu/summary.csv \
  build/repro/worker_assignment_4gpu/summary.json \
  build/repro/worker_assignment_4gpu/pd_4gpu_worker_assignment.md \
  build/repro/worker_assignment_4gpu/pd_4gpu_worker_assignment.svg \
  build/repro/worker_assignment_4gpu/pd_4gpu_e2e_ranked.md \
  build/repro/worker_assignment_4gpu/pd_4gpu_e2e_ranked.svg \
  "$RESULT_ROOT/worker_assignment_4gpu/"

cp build/repro/pressure_4gpu/summary.csv \
  build/repro/pressure_4gpu/summary.json \
  build/repro/pressure_4gpu/pd_4gpu_pressure_e2e.md \
  build/repro/pressure_4gpu/pd_4gpu_pressure_e2e.svg \
  build/repro/pressure_4gpu/pd_4gpu_pressure_slo.md \
  build/repro/pressure_4gpu/pd_4gpu_pressure_slo.svg \
  build/repro/pressure_4gpu/pd_4gpu_pressure_throughput.md \
  build/repro/pressure_4gpu/pd_4gpu_pressure_throughput.svg \
  build/repro/pressure_4gpu/pd_4gpu_pressure_tpot.md \
  build/repro/pressure_4gpu/pd_4gpu_pressure_tpot.svg \
  build/repro/pressure_4gpu/pd_4gpu_pressure_ttft.md \
  build/repro/pressure_4gpu/pd_4gpu_pressure_ttft.svg \
  build/repro/pressure_4gpu/pd_4gpu_pressure_winner_matrix.md \
  build/repro/pressure_4gpu/pd_4gpu_pressure_winner_matrix.svg \
  "$RESULT_ROOT/pressure_4gpu/"

cp build/repro/output_length_4gpu/summary.csv \
  build/repro/output_length_4gpu/summary.json \
  build/repro/output_length_4gpu/pd_4gpu_output_length_e2e.md \
  build/repro/output_length_4gpu/pd_4gpu_output_length_e2e.svg \
  build/repro/output_length_4gpu/pd_4gpu_output_length_throughput.md \
  build/repro/output_length_4gpu/pd_4gpu_output_length_throughput.svg \
  build/repro/output_length_4gpu/pd_4gpu_output_length_tpot.md \
  build/repro/output_length_4gpu/pd_4gpu_output_length_tpot.svg \
  build/repro/output_length_4gpu/pd_4gpu_output_length_ttft.md \
  build/repro/output_length_4gpu/pd_4gpu_output_length_ttft.svg \
  build/repro/output_length_4gpu/pd_4gpu_output_length_winner_matrix.md \
  build/repro/output_length_4gpu/pd_4gpu_output_length_winner_matrix.svg \
  "$RESULT_ROOT/output_length_4gpu/"

cp build/repro/input_output_shape_4gpu/summary.csv \
  build/repro/input_output_shape_4gpu/summary.json \
  "$RESULT_ROOT/input_output_shape_4gpu/"
```

The `shape_matrix_section.tex` file in
`results/serving_pd_disaggregated_unchunked/extrapolation/input_output_shape_4gpu/`
is the paper-facing table derived from the parsed shape summary. Refresh it
whenever the shape sweep inputs or winner policy change.

### Global Token-Shape Crossover

The visible appendix matrix uses the 4 rps case files in
`configs/serving_examples/paper_crossover_rps4_cases/`. Run the colocated C4
and PD candidate cases separately, then parse them together:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/colocated.json \
  --case-file configs/serving_examples/paper_crossover_rps4_cases/c4_rps4.json \
  --output-dir build/repro/crossover_rps4/c4 \
  --binary "$ASTRA_BIN"

python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/paper_crossover_rps4_cases/pd_rps4.json \
  --output-dir build/repro/crossover_rps4/pd \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/crossover_rps4/summary.csv \
  --json-output build/repro/crossover_rps4/summary.json \
  build/repro/crossover_rps4/c4 build/repro/crossover_rps4/pd
```

The tracked paper-ready summary and LaTeX table for this matrix live in
`results/serving_pd_disaggregated_unchunked/extrapolation/crossover_c4_vs_p2d2_rps4/`.
The older 12 rps grid inputs are retained in
`configs/serving_examples/paper_crossover_cases/`.

To reproduce the older 12 rps crossover package, use the same two-run parse
flow with:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/colocated.json \
  --case-file configs/serving_examples/paper_crossover_cases/fig2_crossover_c4_token_shape_12rps.json \
  --output-dir build/repro/crossover_12rps/c4 \
  --binary "$ASTRA_BIN"

python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated.json \
  --case-file configs/serving_examples/paper_crossover_cases/fig2_crossover_pd_token_shape_12rps.json \
  --output-dir build/repro/crossover_12rps/pd \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/crossover_12rps/summary.csv \
  --json-output build/repro/crossover_12rps/summary.json \
  build/repro/crossover_12rps/c4 build/repro/crossover_12rps/pd
```

After either crossover rerun, copy the parsed summaries into the matching
tracked package:

```bash
cp build/repro/crossover_rps4/summary.csv \
  build/repro/crossover_rps4/summary.json \
  results/serving_pd_disaggregated_unchunked/extrapolation/crossover_c4_vs_p2d2_rps4/

cp build/repro/crossover_12rps/summary.csv \
  build/repro/crossover_12rps/summary.json \
  results/serving_pd_disaggregated_unchunked/extrapolation/crossover_c4_vs_p2d2/
```

The `crossover_matrix*.tex`, `winner_summary.json`, and
`global_crossover_summary.*` files are paper-facing reductions derived from
those parsed summaries. Refresh them when the winner policy or case grid
changes, then rebuild the paper.

### Chunked RPS-4 Sweep

The chunked paper sweep fixes request rate at 4 req/s and output length at 2048
tokens, then varies input length, chunk size, and four-GPU assignment. Use the
tracked case files in `configs/serving_examples/chunked_rps4_cases/`:

```bash
python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/colocated_chunked.json \
  --case-file configs/serving_examples/chunked_rps4_cases/c4_chunk_size_sweep.json \
  --output-dir build/repro/chunked_rps4/c4_chunk_size \
  --binary "$ASTRA_BIN"

python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated_chunked.json \
  --case-file configs/serving_examples/chunked_rps4_cases/p2d2_chunk_size_sweep.json \
  --output-dir build/repro/chunked_rps4/p2d2_chunk_size \
  --binary "$ASTRA_BIN"

python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/colocated_chunked.json \
  --case-file configs/serving_examples/chunked_rps4_cases/c4_chunk256_compare.json \
  --output-dir build/repro/chunked_rps4/c4_chunk256 \
  --binary "$ASTRA_BIN"

python3 tools/run_serving_sweep.py \
  --analytical-template "$SERVING_TEMPLATE" \
  --request-config configs/serving_examples/pd_disaggregated_chunked.json \
  --case-file configs/serving_examples/chunked_rps4_cases/pd_chunk256_assignment_compare.json \
  --output-dir build/repro/chunked_rps4/pd_chunk256 \
  --binary "$ASTRA_BIN"

python3 tools/parse_serving_outputs.py \
  --csv-output build/repro/chunked_rps4/summary_raw.csv \
  --json-output build/repro/chunked_rps4/summary_raw.json \
  build/repro/chunked_rps4/c4_chunk_size \
  build/repro/chunked_rps4/p2d2_chunk_size \
  build/repro/chunked_rps4/c4_chunk256 \
  build/repro/chunked_rps4/pd_chunk256
```

The tracked paper-ready chunked figures and summaries live in
`results/serving_chunked_rps4/`.

### Paper Rebuild

After refreshing any paper table or figure source, rebuild the visible PDF from
the paper directory:

```bash
cd paper
latexmk -g -pdf -interaction=nonstopmode -halt-on-error astrasim_paper.tex
```

## Tests
Run the full regression suite with:

```bash
./tests/run_all.sh
```

Run the analytical regression directly with:

```bash
./tests/rt_analytical/run.sh
```

Run the analytical logic tests directly with:

```bash
./build/astra_analytical/build/bin/AstraSim_Analytical_Logic_Tests
```

## Documentation
For focused usage guides covering config structure, commands, outputs, serving
runtime fields, and calibration artifacts, see
[docs/project/analytical-guide.md](/home/justin/astra-sim/docs/project/analytical-guide.md)
and
[docs/project/serving-runtime-guide.md](/home/justin/astra-sim/docs/project/serving-runtime-guide.md).


### Overview and Documentation
Here is a concise visual summary of ASTRA-sim, showing its layers and APIs:
![alt text](https://github.com/astra-sim/astra-sim/blob/master/docs/images/astrasim_overview_codesign.png)

For a comprehensive understanding of the tool, and to gain insights into its capabilities, please visit our [website](https://astra-sim.github.io/).

For information on how to use ASTRA-sim, please visit our [Wiki](https://astra-sim.github.io/astra-sim-docs/index.html).

ASTRA-sim accepts MLCommons Chakra Execution Traces as workload-layer inputs. For details, please visit [Chakra Github](https://github.com/mlcommons/chakra).


### Releases and Contributions

ASTRA-sim is currently at **version 2.0.**
The previous version, ASTRA-sim 1.0, is available in the `ASTRA-sim-1.0` [branch](https://github.com/astra-sim/astra-sim/tree/ASTRA-sim-1.0).

We encourage community contributions to ASTRA-sim via PRs.


## Contact Us
For any questions about using ASTRA-sim, you can email the ASTRA-sim User Mailing List: astrasim-users@googlegroups.com

To join the mailing list, please fill out the following form: https://forms.gle/18KVS99SG3k9CGXm6


We appreciate your interest and support in ASTRA-sim!
