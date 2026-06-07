# TODO

## Extrapolated PD Serving Scale Simulation

- Run extrapolated simulations for multiple prefill GPUs and multiple decode GPUs.
- Sweep both independent worker scaling and per-worker parallelism:
  - `pd.prefill_workers`
  - `pd.decode_workers`
  - `pd.prefill_tp_degree`
  - `pd.decode_tp_degree`
  - `topology.prefill_layout`
  - `topology.decode_layout`
- Treat results beyond the calibrated `1 prefill GPU + 1 decode GPU` setup as extrapolation until matching real measurements are collected.
- Revisit calibration before using results as evidence:
  - `pd.transfer.*`
  - decode latency curve
  - first-token backpressure curve
  - prefill/decode batch efficiency
  - scheduler limits and SLO thresholds
