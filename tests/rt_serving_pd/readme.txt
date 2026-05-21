Prefill/decode disaggregated serving regression cases.

This suite validates:
- PD without transfer,
- PD with explicit transfer delay,
- PD chunked prefill splitting and chunk-aware request metrics,
- two-stage prefill/decode overlap,
- decode-worker split sensitivity.
