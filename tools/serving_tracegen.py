#!/usr/bin/env python3
"""Generate serving request JSON configs for analytical serving runs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ARCHITECTURES = (
    "serial_baseline",
    "colocated",
    "colocated_chunked",
    "pd_disaggregated",
)

SCHEDULERS = ("serial", "decode_first", "prefill_first", "balanced")


def add_if_value(target: dict[str, Any], key: str, value: Any) -> None:
    if value is not None:
        target[key] = value


def infer_scheduler_policy(args: argparse.Namespace) -> str:
    if args.scheduler_policy:
        return args.scheduler_policy
    if args.architecture == "serial_baseline":
        return "serial"
    return "decode_first"


def build_request_config(args: argparse.Namespace) -> dict[str, Any]:
    config: dict[str, Any] = {
        "runtime": {
            "architecture": args.architecture,
        },
        "scheduler": {
            "max_running_requests": args.max_running_requests,
            "scheduler_policy": infer_scheduler_policy(args),
            "max_prefill_batch_tokens": args.max_prefill_batch_tokens,
            "max_decode_batch_requests": args.max_decode_batch_requests,
            "chunked_prefill_size": args.chunked_prefill_size,
            "prefill_max_requests": args.prefill_max_requests,
            "enable_mixed_chunk": args.enable_mixed_chunk,
        },
        "trace": {
            "seed": args.trace_seed,
            "num_requests": args.num_requests,
            "arrival_process": "poisson",
            "request_rate_per_second": args.request_rate_per_second,
            "prompt_tokens": {
                "distribution": "uniform",
                "min": args.prompt_min,
                "max": args.prompt_max,
            },
            "output_tokens": {
                "distribution": "uniform",
                "min": args.output_min,
                "max": args.output_max,
            },
        },
        "cost_model": {
            "prefill_base_latency_ns": args.prefill_base_latency_ns,
            "prefill_ns_per_token": args.prefill_ns_per_token,
            "decode_base_latency_ns": args.decode_base_latency_ns,
            "decode_ns_per_token": args.decode_ns_per_token,
            "prefill_batch_efficiency": args.prefill_batch_efficiency,
            "decode_batch_efficiency": args.decode_batch_efficiency,
            "decode_interference_factor": args.decode_interference_factor,
            "mixed_prefill_weight": args.mixed_prefill_weight,
        },
    }

    add_if_value(config["runtime"], "seed", args.runtime_seed)

    slo: dict[str, Any] = {}
    add_if_value(slo, "ttft_ns", args.ttft_slo_ns)
    add_if_value(slo, "tpot_ns", args.tpot_slo_ns)
    add_if_value(slo, "e2e_ns", args.e2e_slo_ns)
    if slo:
        config["slo"] = slo

    if args.event_trace_output:
        config["outputs"] = {"event_trace_output": args.event_trace_output}

    if any(
        value is not None
        for value in (
            args.model_name,
            args.num_layers,
            args.hidden_size,
            args.attention_heads,
            args.kv_heads,
            args.head_dim,
            args.bytes_per_kv_element,
        )
    ):
        config["model"] = {
            "name": args.model_name or "serving-model",
            "num_layers": args.num_layers or 0,
            "hidden_size": args.hidden_size or 0,
            "attention_heads": args.attention_heads or 0,
            "kv_heads": args.kv_heads or 0,
            "head_dim": args.head_dim or 0,
            "bytes_per_kv_element": args.bytes_per_kv_element or 0,
        }

    needs_pd = args.architecture == "pd_disaggregated" or args.emit_pd_defaults
    if needs_pd:
        config["pd"] = {
            "prefill_workers": args.prefill_workers,
            "decode_workers": args.decode_workers,
            "prefill_max_batch_tokens": args.pd_prefill_max_batch_tokens,
            "prefill_max_requests": args.pd_prefill_max_requests,
            "decode_max_batch_requests": args.pd_decode_max_batch_requests,
            "prefill_tp_degree": args.prefill_tp_degree,
            "decode_tp_degree": args.decode_tp_degree,
            "transfer": {
                "enabled": args.transfer_enabled,
                "latency_ns": args.transfer_latency_ns,
                "bandwidth_bytes_per_s": args.transfer_bandwidth_bytes_per_s,
                "efficiency": args.transfer_efficiency,
                "overlap_enabled": args.transfer_overlap_enabled,
                "bytes_per_prompt_token": args.transfer_bytes_per_prompt_token,
                "override_bytes_per_prompt_token": args.transfer_override_bytes,
            },
        }

    return config


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a serving request JSON config.",
    )
    parser.add_argument("--output", required=True, help="Path to the JSON file to write.")
    parser.add_argument(
        "--architecture",
        default="serial_baseline",
        choices=ARCHITECTURES,
        help="Serving runtime architecture.",
    )
    parser.add_argument(
        "--scheduler-policy",
        choices=SCHEDULERS,
        help="Serving scheduler policy. Defaults to serial for serial_baseline and decode_first otherwise.",
    )
    parser.add_argument("--runtime-seed", type=int, help="Optional runtime seed metadata.")
    parser.add_argument("--trace-seed", type=int, default=7, help="Seed for trace generation.")
    parser.add_argument("--num-requests", type=int, default=32, help="Number of generated requests.")
    parser.add_argument(
        "--request-rate-per-second",
        type=float,
        default=50.0,
        help="Poisson arrival rate in requests per second.",
    )
    parser.add_argument("--prompt-min", type=int, default=64)
    parser.add_argument("--prompt-max", type=int, default=256)
    parser.add_argument("--output-min", type=int, default=16)
    parser.add_argument("--output-max", type=int, default=64)
    parser.add_argument("--max-running-requests", type=int, default=1)
    parser.add_argument("--max-prefill-batch-tokens", type=int, default=0)
    parser.add_argument("--max-decode-batch-requests", type=int, default=1)
    parser.add_argument("--chunked-prefill-size", type=int, default=0)
    parser.add_argument("--prefill-max-requests", type=int, default=1)
    parser.add_argument(
        "--enable-mixed-chunk",
        action="store_true",
        help="Emit enable_mixed_chunk=true. v1 runtimes still use level-1 interleaving only.",
    )
    parser.add_argument("--ttft-slo-ns", type=int)
    parser.add_argument("--tpot-slo-ns", type=float)
    parser.add_argument("--e2e-slo-ns", type=int)
    parser.add_argument("--event-trace-output", help="Relative or absolute event trace CSV path.")
    parser.add_argument("--prefill-base-latency-ns", type=int, default=0)
    parser.add_argument("--prefill-ns-per-token", type=int, default=1000)
    parser.add_argument("--decode-base-latency-ns", type=int, default=0)
    parser.add_argument("--decode-ns-per-token", type=int, default=500)
    parser.add_argument("--prefill-batch-efficiency", type=float, default=1.0)
    parser.add_argument("--decode-batch-efficiency", type=float, default=1.0)
    parser.add_argument("--decode-interference-factor", type=float, default=1.0)
    parser.add_argument("--mixed-prefill-weight", type=float, default=1.0)
    parser.add_argument("--model-name")
    parser.add_argument("--num-layers", type=int)
    parser.add_argument("--hidden-size", type=int)
    parser.add_argument("--attention-heads", type=int)
    parser.add_argument("--kv-heads", type=int)
    parser.add_argument("--head-dim", type=int)
    parser.add_argument("--bytes-per-kv-element", type=int)
    parser.add_argument(
        "--emit-pd-defaults",
        action="store_true",
        help="Include a pd block even when architecture is not pd_disaggregated.",
    )
    parser.add_argument("--prefill-workers", type=int, default=1)
    parser.add_argument("--decode-workers", type=int, default=1)
    parser.add_argument("--pd-prefill-max-batch-tokens", type=int, default=0)
    parser.add_argument("--pd-prefill-max-requests", type=int, default=1)
    parser.add_argument("--pd-decode-max-batch-requests", type=int, default=1)
    parser.add_argument("--prefill-tp-degree", type=int, default=1)
    parser.add_argument("--decode-tp-degree", type=int, default=1)
    parser.add_argument("--transfer-enabled", action="store_true")
    parser.add_argument("--transfer-latency-ns", type=int, default=0)
    parser.add_argument("--transfer-bandwidth-bytes-per-s", type=float, default=1.0e9)
    parser.add_argument("--transfer-efficiency", type=float, default=1.0)
    parser.add_argument("--transfer-overlap-enabled", action="store_true")
    parser.add_argument("--transfer-bytes-per-prompt-token", type=int, default=0)
    parser.add_argument("--transfer-override-bytes", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    config = build_request_config(args)
    output_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote serving request config to {output_path}")


if __name__ == "__main__":
    main()
