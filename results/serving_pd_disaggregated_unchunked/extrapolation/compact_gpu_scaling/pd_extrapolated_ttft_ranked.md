# Compact PD TTFT Ranking

This ranked plot orders the compact extrapolation cases by mean TTFT improvement
against the calibrated two-GPU PD reference. Positive bars mean lower TTFT than
`P1xTP1 / D1xTP1`.

Balanced worker scaling provides the largest TTFT reduction in this sweep, with
`P8xTP1 / D8xTP1` reaching 165.69 ms versus the calibrated reference at
187.81 ms. Decode-worker and prefill-worker-only scaling improve less than
balanced worker scaling.

The ranking is useful for prioritizing future measured experiments, not for
claiming calibrated multi-worker performance.
