# Four-GPU Pressure E2E

This plot shows mean end-to-end latency under request-rate and input-length
pressure for the 4-GPU assignments.

The lowest E2E point is `P1/D3` at 4 rps and 128 input tokens: 880.61 ms. Across
the full grid, `P2/D2` wins most E2E cells, which suggests balanced worker
capacity is more robust once request rate and prompt length vary.

Because these are 4-GPU extrapolations, the plot should be read as a sensitivity
study and not as measured 4-GPU performance.
