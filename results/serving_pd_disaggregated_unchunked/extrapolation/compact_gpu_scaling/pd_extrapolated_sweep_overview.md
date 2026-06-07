# Compact PD GPU Scaling Overview

This plot shows broad extrapolated PD scaling families relative to the calibrated
`P1xTP1 / D1xTP1` reference. The panels show TTFT, TPOT, E2E, and output-token
throughput improvement versus the two-GPU baseline.

The best row in this compact sweep is balanced worker scaling at
`P8xTP1 / D8xTP1`, with mean TTFT of 165.69 ms, mean E2E of 1084.09 ms, and
196.18 output tokens/s. TP-only rows stay flat in this scalar serving cost model,
which is an important limitation rather than a speedup result.

Only the baseline point is calibrated. The rest of the plot explores simulator
sensitivity to configured worker counts and TP fields until matching real
measurements exist.
