from __future__ import annotations


def device_energy_joules(
    busy_time_seconds: float,
    total_time_seconds: float,
    active_power_w: float,
    idle_power_w: float,
) -> float:
    clamped_total = max(total_time_seconds, 0.0)
    clamped_busy = min(max(busy_time_seconds, 0.0), clamped_total)
    idle_time = clamped_total - clamped_busy
    return clamped_busy * active_power_w + idle_time * idle_power_w
