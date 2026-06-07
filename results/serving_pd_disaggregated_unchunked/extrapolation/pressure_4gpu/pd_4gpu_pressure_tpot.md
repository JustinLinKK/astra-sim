# Four-GPU Pressure TPOT

This plot shows mean TPOT across the same request-rate and input-length pressure
sweep. It isolates the decode-token latency surface from total E2E latency.

TPOT is mainly driven by the configured target request-rate decode curve:
approximately 27.84 ms at 4 rps, 33.52 ms at 8 rps, and higher at the overloaded
rates. Input length and worker assignment have much less effect on TPOT in this
current scalar model.

This is useful for spotting model limits: measured multi-worker decode
efficiency is not yet calibrated.
