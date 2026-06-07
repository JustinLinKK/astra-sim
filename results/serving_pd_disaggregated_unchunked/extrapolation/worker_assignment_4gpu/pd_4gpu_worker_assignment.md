# Four-GPU Worker Assignment

This plot compares exact PD worker assignments under a budget of up to four
logical GPUs. TP is fixed at one, so each worker consumes one logical GPU. The
panels show TTFT, TPOT, E2E, and output-token throughput improvement relative to
the calibrated `P1/D1` reference.

The best four-GPU assignment in this sweep is balanced `P2/D2`: mean TTFT
174.35 ms, mean E2E 1092.75 ms, and 196.10 output tokens/s. TPOT remains flat
at 33.52 ms because the current decode-step curve is calibrated by request rate,
not by measured multi-worker decode efficiency.

Only `P1/D1` is calibrated. `P2/D2`, `P3/D1`, and `P1/D3` are extrapolated
worker-allocation studies.
