# Four-GPU Pressure SLO Attainment

This plot shows SLO attainment across request-rate and input-length pressure.
It reports the fraction of requests that satisfy the configured TTFT, TPOT, and
E2E thresholds.

SLO attainment is 1.0 at 4 and 8 rps in this sweep, then drops around 12 rps and
falls further at 16 rps. This marks the simulated saturation region for the
current calibrated curves and 4-GPU extrapolated layouts.

The drop is useful for choosing future measurement points, especially near the
knee of the load curve.
