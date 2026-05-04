from __future__ import annotations


def ring_all_reduce_time_seconds(
    bytes_transferred: int,
    participants: int,
    bandwidth_bytes_per_s: float,
    latency_ns: int,
    efficiency: float,
) -> float:
    if bytes_transferred == 0 or participants <= 1 or bandwidth_bytes_per_s <= 0.0 or efficiency <= 0.0:
        return 0.0
    alpha = latency_ns / 1.0e9
    beta = 1.0 / (bandwidth_bytes_per_s * efficiency)
    return 2.0 * (participants - 1) * alpha + 2.0 * (participants - 1) / participants * bytes_transferred * beta


def point_to_point_time_seconds(
    bytes_transferred: int,
    bandwidth_bytes_per_s: float,
    latency_ns: int,
    efficiency: float,
) -> float:
    if bytes_transferred == 0 or bandwidth_bytes_per_s <= 0.0 or efficiency <= 0.0:
        return 0.0
    return latency_ns / 1.0e9 + bytes_transferred / (bandwidth_bytes_per_s * efficiency)
