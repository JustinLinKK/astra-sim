# Four-GPU Pressure Winner Matrix

This matrix selects the lowest-E2E 4-GPU assignment for each request-rate and
average-input-token cell.

`P2/D2` wins most cells, `P3/D1` wins several long-input or higher-pressure
cells, and `P1/D3` wins only the lightest short-input cell. The matrix makes the
tradeoff readable: decode-heavy assignment helps at light pressure, while
balanced or prefill-heavy assignment becomes more competitive as input pressure
increases.

All winners are simulator extrapolations. The matrix should guide which 4-GPU
layouts to measure next.
