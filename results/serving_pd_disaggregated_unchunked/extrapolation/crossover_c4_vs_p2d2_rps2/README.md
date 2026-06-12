# Global C4 Versus PD Crossover at 2 rps

This folder preserves the request-rate-2 rerun of the four-GPU token-shape
crossover sweep. It compares colocated data-parallel `C4` against `P1/D3`,
`P2/D2`, and `P3/D1` PD splits while sweeping mean input and output token
length. The table has been extended with a `10000` mean-input row and a `2048`
mean-output column.

Reproducible from the tracked case files under
`configs/serving_examples/paper_crossover_cases/` and
`configs/serving_examples/paper_crossover_rps2_cases/`. The root README's
`Global Token-Shape Crossover` section shows the current 4 rps command shape;
use the same `run_serving_sweep.py` and `parse_serving_outputs.py` flow with
the 2 rps case files for this sibling package.

Winner summary:

- `C4`: 72 of 88 cells
- `P1/D3`: 1 of 88 cells
- `P2/D2`: 15 of 88 cells
- `P3/D1`: 0 of 88 cells

The corresponding appendix table is exported in `crossover_matrix_rps2.tex` and
is also inserted into `paper/astrasim_paper.tex`.
