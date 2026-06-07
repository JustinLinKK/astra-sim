# Four-GPU Output-Length E2E

This plot shows mean end-to-end latency as output length increases from 16 to
128 tokens at 8 and 16 rps.

E2E grows with output length because each request spends more decode steps in
service. The lowest E2E point is `P2/D2` at 8 rps and 16 output tokens:
612.38 ms. At 128 output tokens, E2E rises into the 4.37-4.78 s range depending
on request rate and assignment.

The plot shows decode-length pressure on the simulator, but the 4-GPU assignment
effects remain extrapolated.
