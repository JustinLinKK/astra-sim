# Compact PD TPOT Ranking

This ranked plot orders compact extrapolation cases by mean TPOT improvement
against the calibrated two-GPU PD reference.

The current compact sweep shows TPOT effectively flat across the scaling cases:
the best rows remain at 33.52 ms per output token. That reflects the current
rate-dependent scalar decode-step model, where worker scaling changes queueing
and admission pressure more than per-token decode service time.

This is a modeling signal. It says the current extrapolation does not yet encode
measured TP or multi-worker decode efficiency improvements.
