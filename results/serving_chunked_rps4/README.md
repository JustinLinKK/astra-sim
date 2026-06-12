# Chunked Serving Sweep at 4 rps

Reproducible from the tracked case files under
`configs/serving_examples/chunked_rps4_cases/`. The root README's
`Chunked RPS-4 Sweep` section gives the copy-pasteable rerun commands. This
folder keeps the paper-ready parsed summaries and SVG figures.

Fixed parameters:

- request rate: 4 requests/s
- output length: 2048 tokens
- trace size: 256 requests
- input lengths: 128, 256, 512, 1024, 1536, 2048, 3072, 4096, 6144, 7168, 10000 tokens
- chunk-size sweeps: C4 and P2/D2 over chunk sizes 64, 128, 256, 512, and 1024
- chunk-256 assignment comparison: C4, P1/D3, P2/D2, and P3/D1

Figures:

- `chunk_size_sweep_e2e.svg`: chunk-size E2E sweep for C4 colocated and P2/D2 PD
- `chunk_size_sweep_ttft.svg`: chunk-size TTFT sweep for C4 colocated and P2/D2 PD
- `chunk256_c4_vs_p2d2_e2e.svg`: chunk-256 E2E assignment comparison with C4, P1/D3, P2/D2, and P3/D1
- `chunk256_c4_vs_p2d2_ttft.svg`: chunk-256 TTFT assignment comparison with C4, P1/D3, P2/D2, and P3/D1
- `chunk256_assignment_e2e.svg` and `chunk256_assignment_ttft.svg`: clearer aliases for the expanded chunk-256 comparison figures

Best C4 chunk size by input length:

| Input tokens | Best chunk | E2E mean (ms) |
|---:|---:|---:|
| 128 | 128 | 49633.7 |
| 256 | 256 | 49782.6 |
| 512 | 512 | 50079.0 |
| 1024 | 1024 | 50674.7 |
| 1536 | 1024 | 52356.0 |
| 2048 | 1024 | 52950.7 |
| 3072 | 1024 | 55226.1 |
| 4096 | 1024 | 57502.9 |
| 6144 | 1024 | 62051.7 |
| 7168 | 1024 | 64324.4 |
| 10000 | 1024 | 70820.9 |

Best P2/D2 chunk size by input length:

| Input tokens | Best chunk | E2E mean (ms) |
|---:|---:|---:|
| 128 | 128 | 50411.0 |
| 256 | 256 | 50414.7 |
| 512 | 512 | 50422.3 |
| 1024 | 1024 | 50437.4 |
| 1536 | 1024 | 50483.9 |
| 2048 | 1024 | 50500.6 |
| 3072 | 1024 | 50572.8 |
| 4096 | 1024 | 50660.8 |
| 6144 | 1024 | 51040.4 |
| 7168 | 1024 | 51712.4 |
| 10000 | 1024 | 62165.2 |

Chunk-256 winners by input length:

| Input tokens | E2E winner | E2E mean (ms) | TTFT winner | TTFT mean (ms) |
|---:|:---|---:|:---|---:|
| 128 | P1/D3 | 48595.0 | P1/D3 | 130.7 |
| 256 | P1/D3 | 48600.6 | P1/D3 | 136.4 |
| 512 | P1/D3 | 48652.9 | P2/D2 | 178.7 |
| 1024 | P1/D3 | 48850.8 | P2/D2 | 261.8 |
| 1536 | P1/D3 | 50303.0 | P3/D1 | 348.2 |
| 2048 | P2/D2 | 50783.4 | P3/D1 | 431.7 |
| 3072 | P2/D2 | 52379.7 | P3/D1 | 651.3 |
| 4096 | P2/D2 | 62902.5 | P3/D1 | 1288.2 |
| 6144 | C4 | 80936.3 | P3/D1 | 13045.1 |
| 7168 | C4 | 85531.5 | P3/D1 | 25270.5 |
| 10000 | C4 | 98594.7 | C4 | 55714.1 |
