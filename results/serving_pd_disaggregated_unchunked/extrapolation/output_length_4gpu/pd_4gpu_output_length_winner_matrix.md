# Four-GPU Output-Length Winner Matrix

This matrix selects the lowest-E2E assignment for each request-rate and
average-output-token cell.

`P2/D2` wins the 8 rps cells, while `P3/D1` wins the 16 rps cells in this output
length sweep. That split suggests balanced assignment is best at moderate load,
while prefill-heavy assignment can edge out alternatives under the current
high-load backpressure curve.

The matrix is a planning aid for future measured 4-GPU experiments, not a
calibrated deployment recommendation.
