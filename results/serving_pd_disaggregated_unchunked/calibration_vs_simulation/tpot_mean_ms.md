# TPOT Calibration Vs Simulation

This plot compares mean time-per-output-token from measured calibration data
against ASTRA-sim replay for unchunked colocated and PD-disaggregated serving.
It is the decode-side closure plot for the calibrated result.

The non-overloaded validation passes with colocated TPOT MAPE at 4.65% and
PD-disaggregated TPOT MAPE at 1.43%. PD decode timing is therefore the strongest
latency closure in the current fit.

The calibrated TPOT curve is scoped to the same-node two-`L40S` experiment.
Tensor-parallel or multi-worker decode behavior in the extrapolation plots is
not separately measured yet.
