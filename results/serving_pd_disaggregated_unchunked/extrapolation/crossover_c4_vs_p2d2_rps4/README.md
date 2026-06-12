# Global C4 Versus PD Crossover at 4 rps

This folder preserves the request-rate-4 rerun of the four-GPU token-shape
crossover sweep. It compares colocated data-parallel `C4` against `P1/D3`,
`P2/D2`, and `P3/D1` PD splits while sweeping mean input and output token
length. The table uses the scaled grid with a `10000` mean-input row and a
`2048` mean-output column.

Reproducible from the tracked case files under
`configs/serving_examples/paper_crossover_rps4_cases/`. The root README's
`Global Token-Shape Crossover` section gives the copy-pasteable rerun commands.

Winner summary:

- `C4`: 50 of 88 cells
- `P1/D3`: 11 of 88 cells
- `P2/D2`: 18 of 88 cells
- `P3/D1`: 9 of 88 cells

The `10000` mean-input / `2048` mean-output corner is won by
`P2/D2` at 55973.7 ms mean E2E latency;
the best PD split is `P2/D2`, and best-PD-minus-`C4` is
-13170.5 ms.

The corresponding appendix table is exported in `crossover_matrix_rps4.tex` and
is also inserted into `paper/astrasim_paper.tex`.
