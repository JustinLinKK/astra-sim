# Four-GPU Output-Length TTFT

This plot sweeps average output-token length for 4-GPU PD assignments while
holding average input length at 512 tokens. The x-axis is request rate, and each
panel fixes output length.

The lowest TTFT point is `P2/D2` at 8 rps and 16 output tokens: 192.42 ms. At
16 rps, TTFT is dominated by load/backpressure and stays around 1.75-1.81 s
across output lengths.

This plot covers the output-length dimension that is not represented by the
input-pressure sweep. The rows are still 4-GPU extrapolations.
