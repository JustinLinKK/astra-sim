from __future__ import annotations

from dataclasses import dataclass
from math import ceil


@dataclass(frozen=True)
class DenseMemoryBreakdown:
    weight_bytes: int
    kv_cache_bytes: int
    activation_bytes: int
    workspace_bytes: int
    total_bytes: int


def estimate_dense_memory_per_gpu(
    parameter_count: int,
    num_layers: int,
    hidden_size: int,
    bytes_per_parameter: int,
    bytes_per_activation: int,
    bytes_per_kv_element: int,
    workspace_reserve_bytes: int,
    tp_degree: int,
    pp_degree: int,
    batch_size: int,
    sequence_length: int,
    num_microbatches: int,
    activation_multiplier: float,
) -> DenseMemoryBreakdown:
    microbatch_batch = ceil(batch_size / num_microbatches)
    weight_bytes = round(parameter_count * bytes_per_parameter / tp_degree / pp_degree)
    layers_per_stage = num_layers / pp_degree
    kv_cache_bytes = round(
        2.0
        * microbatch_batch
        * sequence_length
        * layers_per_stage
        * hidden_size
        / tp_degree
        * bytes_per_kv_element
    )
    activation_bytes = round(
        microbatch_batch * sequence_length * hidden_size * activation_multiplier * bytes_per_activation
    )
    total_bytes = weight_bytes + kv_cache_bytes + activation_bytes + workspace_reserve_bytes
    return DenseMemoryBreakdown(
        weight_bytes=weight_bytes,
        kv_cache_bytes=kv_cache_bytes,
        activation_bytes=activation_bytes,
        workspace_bytes=workspace_reserve_bytes,
        total_bytes=total_bytes,
    )
