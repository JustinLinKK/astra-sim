# Four-GPU Pressure TTFT

This plot sweeps request rate and average input-token length for 4-GPU PD
assignments. Each panel fixes average input length, and each line is a
prefill/decode worker split.

The lowest TTFT in the sweep appears at `P1/D3`, 4 rps, and 128 input tokens:
147.37 ms. At higher request rates, TTFT rises sharply, especially around
12-16 rps where SLO attainment starts to drop.

The plot shows how load and prompt length stress first-token latency, but all
4-GPU rows are extrapolated from the calibrated two-GPU model.
