Regression Test Specifications

BINARY:
	Analytical backend with unified analytical mode support enabled.

INPUTS:
		CONFIGS:
				TP/PP crossover and attention/FFN disaggregation YAML configs from `configs/`.

OUTPUTS & REFERENCES:
				CSV and JSON comparison for deterministic analytical summaries, plus a repeat-run determinism check for TP/PP crossover.

USAGE:
			Run `./tests/rt_analytical/run.sh` from the repository root.
			This script rebuilds the analytical backend, runs the shipped TP/PP crossover and attention/FFN disaggregation configs,
			compares outputs against the references in `tests/rt_analytical/refs/`,
			and performs an additional energy-consistency property check for the attention/FFN disaggregation mode.
