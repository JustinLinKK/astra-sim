# Four-GPU Output-Length TPOT

This plot shows mean TPOT while output length changes. It separates per-output
token latency from total request length.

TPOT is flat within each request-rate tier: about 33.52 ms at 8 rps and
36.25 ms at 16 rps. Changing the number of output tokens increases E2E latency
and throughput volume, but it does not change the calibrated per-step decode
latency in this model.

That flatness is expected from the current rate-conditioned decode-step curve.
It is not proof that real long-output decoding has identical per-token cost.
