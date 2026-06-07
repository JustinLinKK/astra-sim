# Four-GPU Pressure Throughput

This plot shows output-token throughput across the pressure grid. Higher values
are better.

Throughput rises with request rate and is highest at 16 rps with short prompts.
The best point is `P2/D2` at 16 rps and 128 input tokens, reaching
435.69 output tokens/s. Longer input sequences reduce effective throughput by
increasing prefill pressure and queueing.

The throughput trend is extrapolated from the calibrated request-rate curves and
should be measured before making 4-GPU capacity claims.
