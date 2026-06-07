# TTFT Calibration Vs Simulation

This plot compares mean time-to-first-token from measured calibration data
against ASTRA-sim replay for unchunked `colocated` and `pd_disaggregated` runs.
The x-axis is target request rate, and each mode has both calibrated and
simulation series.

The non-overloaded validation passes: colocated TTFT MAPE is 11.15% across 16
paired runs, and PD-disaggregated TTFT MAPE is 10.67% across 15 paired runs.
That means the calibrated simulator closes the first-token latency surface for
the current two-`L40S` setup within the configured 15% latency threshold.

The overload point is diagnostic rather than the main acceptance scope. Larger
worker-count or multi-node TTFT trends must still be treated as extrapolation.
