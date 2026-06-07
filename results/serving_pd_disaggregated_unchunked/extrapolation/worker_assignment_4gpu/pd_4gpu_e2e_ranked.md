# Four-GPU E2E Ranking

This plot ranks the worker-assignment sweep by mean end-to-end latency. Lower
E2E is better, and non-baseline rows are extrapolated.

`P2/D2` ranks best at 1092.75 ms mean E2E. `P1/D3` follows at 1096.07 ms, while
`P3/D1` reaches 1098.77 ms. The result suggests that balanced prefill/decode
capacity is the safest first 4-GPU follow-up experiment for this workload.

The ranking is a simulator-generated design hint. It should be validated with
real 4-GPU data before it is used as a calibrated claim.
