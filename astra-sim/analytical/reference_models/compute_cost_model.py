from __future__ import annotations


def compute_time_seconds(flops: float, peak_flops: float, efficiency: float, parallel_units: int) -> float:
    if peak_flops <= 0.0 or efficiency <= 0.0 or parallel_units <= 0:
        return 0.0
    return flops / (peak_flops * efficiency * parallel_units)


def dense_layer_flops(batch_size: int, sequence_length: int, hidden_size: int, ffn_hidden_size: int) -> float:
    projection_flops = 8.0 * batch_size * sequence_length * hidden_size * hidden_size
    attention_mix_flops = 4.0 * batch_size * sequence_length * sequence_length * hidden_size
    ffn_flops = 6.0 * batch_size * sequence_length * hidden_size * ffn_hidden_size
    return projection_flops + attention_mix_flops + ffn_flops


def attention_flops_per_layer(batch_size: int, sequence_length: int, hidden_size: int) -> float:
    return 8.0 * batch_size * hidden_size * hidden_size + 4.0 * batch_size * sequence_length * hidden_size


def router_flops_per_layer(batch_size: int, hidden_size: int, experts_per_layer: int) -> float:
    return 2.0 * batch_size * hidden_size * experts_per_layer


def expert_flops_per_layer(batch_size: int, active_experts_per_token: int, hidden_size: int, expert_hidden_size: int) -> float:
    return 6.0 * batch_size * active_experts_per_token * hidden_size * expert_hidden_size
