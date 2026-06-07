# Four-GPU Output-Length Throughput

This plot shows output-token throughput as average output length increases.
Higher values are better.

Throughput increases with output length because each accepted request emits more
tokens. The best point is `P2/D2` at 16 rps and 128 output tokens, reaching
1600.49 output tokens/s. Short-output cases emit far fewer tokens even when
request throughput is similar.

This plot is useful for distinguishing request throughput from token throughput.
It should not be interpreted as calibrated 4-GPU long-output capacity.
