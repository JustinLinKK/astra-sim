# Serving Scale Report

Current status:

- primary committed evidence is unchunked `colocated` versus unchunked
  `pd_disaggregated`
- the validated calibration scope is two same-node `L40S` GPUs on the same
  motherboard
- tracked plots and portable summaries live in
  [../../../results/serving_pd_disaggregated_unchunked](../../../results/serving_pd_disaggregated_unchunked)
- chunked-prefill is implemented and has diagnostic replay artifacts, but it is
  waiting on broader chunked calibration data before becoming a main result

This folder now contains both the original investigation notes and a LaTeX
source package that documents the simulator's default serving formulas and the
new topology-aware serving-scale formulas.

Primary documents:

- [latex/main.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/main.tex)
- [pd_calibration_plan.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/pd_calibration_plan.md)
- [architecture_and_calibration.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/architecture_and_calibration.md)
- [investigation_pd_vs_colocated.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/investigation_pd_vs_colocated.md)
- [real_server_metrics_for_calibration.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/real_server_metrics_for_calibration.md)
- [calibration_data_gap_report.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/calibration_data_gap_report.md)
- [chunked_prefill_calibration_gap_report.md](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/chunked_prefill_calibration_gap_report.md)
- [published_benchmark_anchors.json](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/published_benchmark_anchors.json)
- [results/serving_pd_disaggregated_unchunked](/home/justin/astra-sim/results/serving_pd_disaggregated_unchunked)

Current calibration scope:

- the main calibrated result is unchunked `colocated` versus unchunked
  `pd_disaggregated`
- the real experiment is constrained to two `L40S` GPUs on the same motherboard
- chunked-prefill modes now have a passing non-overload diagnostic replay under
  `calibrations/chunked_prefill_scaling_20260603_074838`, using the
  `unchunked_scaling_20260530_075459` base fit plus chunk-specific prefill
  timing; keep them out of main claims until overload and broader rate coverage
  are collected

LaTeX package contents:

- [latex/main.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/main.tex)
- [latex/sections/01_metric_definitions.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/sections/01_metric_definitions.tex)
- [latex/sections/02_current_default_formulas.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/sections/02_current_default_formulas.tex)
- [latex/sections/03_topology_aware_formulas.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/sections/03_topology_aware_formulas.tex)
- [latex/sections/04_colocated_vs_pd_composition.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/sections/04_colocated_vs_pd_composition.tex)
- [latex/sections/05_symbol_to_config_mapping.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/sections/05_symbol_to_config_mapping.tex)
- [latex/sections/06_calibration_knobs.tex](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/sections/06_calibration_knobs.tex)
- [latex/build.sh](/home/justin/astra-sim/docs/reports/2026-05-serving-scale/latex/build.sh)

The LaTeX report is the canonical formula reference for this serving refactor.
Each equation is written directly as display math in the `.tex` sources so it
can be copied into papers or calibration notes without translation from Markdown.

PDF generation is optional in this workspace. The helper build script first
tries `latexmk`, then `pdflatex`, and otherwise prints instructions because no
LaTeX toolchain is installed here by default.

Runnable study starters referenced by the report:

- [realistic_goodput_sharegpt_like.json](/home/justin/astra-sim/configs/serving_examples/realistic_goodput_sharegpt_like.json)
- [pd_extrapolated_compact_sweep.json](/home/justin/astra-sim/configs/serving_examples/pd_extrapolated_compact_sweep.json)
- [pd_4gpu_worker_assignment_sweep.json](/home/justin/astra-sim/configs/serving_examples/pd_4gpu_worker_assignment_sweep.json)
- [pd_4gpu_pressure_sweep.json](/home/justin/astra-sim/configs/serving_examples/pd_4gpu_pressure_sweep.json)
- [pd_4gpu_output_length_sweep.json](/home/justin/astra-sim/configs/serving_examples/pd_4gpu_output_length_sweep.json)
- [serving_goodput_16gpu.yaml](/home/justin/astra-sim/configs/serving_goodput_16gpu.yaml)
- [network_16gpu_ring.yml](/home/justin/astra-sim/configs/network_16gpu_ring.yml)
