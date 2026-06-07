# Goodput Calibration Vs Simulation

This plot compares good requests per second between measured calibration data
and simulator replay for colocated and PD-disaggregated unchunked serving.
Goodput uses the configured SLO thresholds rather than counting every completed
request as good.

The non-overloaded validation passes with near-zero colocated goodput MAPE and
0.11% PD-disaggregated goodput MAPE. That means the replay preserves throughput
and SLO-good request counts for the calibrated operating region.

The high-load overload point is retained as a diagnostic for saturation
behavior. It should not be used as proof that larger PD layouts are calibrated.
