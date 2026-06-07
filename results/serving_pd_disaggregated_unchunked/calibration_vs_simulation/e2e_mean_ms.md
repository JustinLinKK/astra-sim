# E2E Calibration Vs Simulation

This plot compares mean end-to-end request latency from measured calibration
data against simulator replay. It combines queueing, prefill, same-node PD
handoff, decode, and output-token generation into one closure view.

The non-overloaded validation passes with colocated E2E MAPE at 4.86% and
PD-disaggregated E2E MAPE at 1.67%. This shows the simulator reproduces the
overall request-latency shape after the calibrated first-token and decode-step
curves are applied.

This plot supports the unchunked commit claim only. Chunked-prefill E2E behavior
has a diagnostic replay but still needs the broader chunked calibration matrix.
