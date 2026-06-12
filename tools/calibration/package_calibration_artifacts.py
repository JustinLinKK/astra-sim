#!/usr/bin/env python3
"""Package compact calibration artifacts for Git LFS.

The package keeps only files needed for fitting, replay validation, and paper
artifact provenance. It intentionally drops large raw telemetry such as
system_timeseries.csv and scheduler_trace_raw.csv.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


DEFAULT_ROOTS = (
    "unchunked_scaling_20260530_075459",
    "chunked_prefill_scaling_20260605_105450",
)

INCLUDE_NAMES = {
    "run_config.json",
    "request_metrics.csv",
    "pd_stage_metrics.csv",
    "scheduler_trace.csv",
    "comparison_summary.csv",
    "validation_summary.csv",
    "validation_summary.json",
    "excluded_runs.json",
    "fitted_unchunked_latency_cost_model.json",
    "fitted_chunked_prefill_latency_cost_model.json",
    "unchunked_gap_closure_summary.md",
    "chunked_prefill_gap_closure_summary.md",
    "run_command.sh",
}

EXCLUDE_NAMES = {
    ".DS_Store",
    "available_metrics_summary.json",
    "system_timeseries.csv",
    "scheduler_trace_raw.csv",
    "scheduler_trace_decode_raw.csv",
    "scheduler_trace_prefill_raw.csv",
    "run.log",
    "run.pid",
}

BENCHMARK_DROP_KEYS = {
    "input_lens",
    "output_lens",
    "ttfts",
    "itls",
    "start_times",
    "generated_texts",
    "errors",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create compact split tar.zst calibration archives."
    )
    parser.add_argument(
        "--calibrations-dir",
        type=Path,
        default=Path("calibrations"),
        help="Directory containing expanded calibration roots.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("calibrations"),
        help="Directory for archive parts and manifest files.",
    )
    parser.add_argument(
        "--staging-dir",
        type=Path,
        default=Path("build/calibration_artifact_staging"),
        help="Temporary staging root for compact archive contents.",
    )
    parser.add_argument(
        "--roots",
        default=",".join(DEFAULT_ROOTS),
        help="Comma-separated calibration roots to package.",
    )
    parser.add_argument(
        "--part-size",
        default="1800M",
        help="Split size passed to split -b.",
    )
    parser.add_argument(
        "--zstd-level",
        default="-19",
        help="Compression level passed to zstd.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace existing compact archive parts and staging directories.",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_stream(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def is_under_request_traces(relative: Path) -> bool:
    return "request_traces" in relative.parts


def is_excluded_simulation_output(relative: Path) -> bool:
    parts = relative.parts
    return "sim_vs_calibrated" in parts and "simulation_runs" in parts


def should_include(relative: Path) -> bool:
    name = relative.name
    if is_excluded_simulation_output(relative):
        return False
    if name in EXCLUDE_NAMES:
        return False
    if is_under_request_traces(relative):
        return True
    if name == "benchmark_result.json":
        return True
    if name in INCLUDE_NAMES:
        return True
    if name.endswith(".svg") and "plots" in relative.parts:
        return True
    return False


def compact_benchmark_result(source: Path, target: Path) -> dict[str, Any]:
    original = json.loads(source.read_text(encoding="utf-8"))
    compact: dict[str, Any] = {}
    dropped_keys: list[str] = []

    for key, value in original.items():
        if key in BENCHMARK_DROP_KEYS or isinstance(value, list):
            dropped_keys.append(key)
            continue
        compact[key] = value

    compact["_compact_artifact"] = {
        "source_file": "benchmark_result.json",
        "dropped_keys": sorted(dropped_keys),
        "original_bytes": source.stat().st_size,
    }
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(compact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return {
        "original_bytes": source.stat().st_size,
        "compact_bytes": target.stat().st_size,
        "dropped_keys": sorted(dropped_keys),
    }


def copy_artifact(source_root: Path, staging_root: Path, relative: Path) -> dict[str, Any]:
    source = source_root / relative
    target = staging_root / relative
    if source.name == "benchmark_result.json":
        result = compact_benchmark_result(source, target)
        result["compacted"] = True
        return result

    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    return {
        "original_bytes": source.stat().st_size,
        "compact_bytes": target.stat().st_size,
        "compacted": False,
    }


def collect_root(source_root: Path, staging_root: Path) -> dict[str, Any]:
    if staging_root.exists():
        shutil.rmtree(staging_root)
    staging_root.mkdir(parents=True, exist_ok=True)

    included_files = 0
    source_files = 0
    source_bytes = 0
    included_original_bytes = 0
    staged_bytes = 0
    compacted_benchmarks = 0
    excluded_by_name: Counter[str] = Counter()
    excluded_bytes_by_name: defaultdict[str, int] = defaultdict(int)

    for source in sorted(path for path in source_root.rglob("*") if path.is_file()):
        relative = source.relative_to(source_root)
        source_files += 1
        source_bytes += source.stat().st_size

        if should_include(relative):
            result = copy_artifact(source_root, staging_root, relative)
            included_files += 1
            included_original_bytes += int(result["original_bytes"])
            staged_bytes += int(result["compact_bytes"])
            if result.get("compacted"):
                compacted_benchmarks += 1
        else:
            key = source.name
            if is_excluded_simulation_output(relative):
                key = "sim_vs_calibrated/simulation_runs/**"
            excluded_by_name[key] += 1
            excluded_bytes_by_name[key] += source.stat().st_size

    staged_file_count = sum(1 for path in staging_root.rglob("*") if path.is_file())
    if staged_file_count != included_files:
        raise RuntimeError(
            f"Staged file count mismatch for {source_root}: "
            f"{staged_file_count} != {included_files}"
        )

    return {
        "source_files": source_files,
        "source_bytes": source_bytes,
        "included_files": included_files,
        "included_original_bytes": included_original_bytes,
        "staged_bytes": staged_bytes,
        "compacted_benchmark_results": compacted_benchmarks,
        "excluded_by_name": {
            key: {
                "count": excluded_by_name[key],
                "bytes": excluded_bytes_by_name[key],
            }
            for key in sorted(excluded_by_name)
        },
    }


def remove_existing_parts(output_dir: Path, root_name: str) -> None:
    for pattern in (
        f"{root_name}.compact.tar.zst.part-*",
        f"{root_name}.tar.zst.part-*",
    ):
        for path in output_dir.glob(pattern):
            path.unlink()


def create_archive(
    staging_dir: Path,
    output_dir: Path,
    root_name: str,
    part_size: str,
    zstd_level: str,
) -> list[Path]:
    prefix = output_dir / f"{root_name}.compact.tar.zst.part-"
    command = (
        f"tar -C {staging_dir} -cf - {root_name} | "
        f"zstd -T0 {zstd_level} --no-progress | "
        f"split -b {part_size} -d -a 3 - {prefix}"
    )
    subprocess.run(
        [
            "bash",
            "-lc",
            (
                "set -euo pipefail; "
                f"tar -C {staging_dir} -cf - {root_name} "
                f"| zstd -T0 {zstd_level} --no-progress "
                f"| split -b {part_size} -d -a 3 - {prefix}"
            ),
        ],
        check=True,
    )
    parts = sorted(output_dir.glob(f"{root_name}.compact.tar.zst.part-*"))
    if not parts:
        raise RuntimeError(f"No archive parts produced for {root_name}: {command}")
    return parts


def repo_relative(path: Path) -> str:
    try:
        return path.relative_to(Path.cwd()).as_posix()
    except ValueError:
        return path.as_posix()


def main() -> None:
    args = parse_args()
    roots = [item.strip() for item in args.roots.split(",") if item.strip()]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.staging_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, Any] = {
        "format": "astra-sim compact calibration artifacts",
        "version": 1,
        "part_size": args.part_size,
        "zstd_level": args.zstd_level,
        "included_policy": {
            "included": sorted(INCLUDE_NAMES)
            + [
                "benchmark_result.json compacted",
                "request_traces/**",
                "sim_vs_calibrated/plots/*.svg",
            ],
            "excluded": sorted(EXCLUDE_NAMES)
            + ["sim_vs_calibrated/simulation_runs/**"],
        },
        "roots": [],
    }

    checksums: list[str] = []

    for root_name in roots:
        source_root = args.calibrations_dir / root_name
        if not source_root.exists():
            raise FileNotFoundError(source_root)
        staging_root = args.staging_dir / root_name
        print(f"Staging compact calibration root: {root_name}")
        stats = collect_root(source_root, staging_root)

        if args.force:
            remove_existing_parts(args.output_dir, root_name)
        elif list(args.output_dir.glob(f"{root_name}.compact.tar.zst.part-*")):
            raise FileExistsError(
                f"Archive parts already exist for {root_name}; rerun with --force."
            )

        print(f"Compressing compact calibration root: {root_name}")
        parts = create_archive(
            args.staging_dir,
            args.output_dir,
            root_name,
            args.part_size,
            args.zstd_level,
        )

        part_records = []
        for part in parts:
            digest = sha256_file(part)
            part_records.append(
                {
                    "path": repo_relative(part),
                    "bytes": part.stat().st_size,
                    "sha256": digest,
                }
            )
            checksums.append(f"{digest}  {repo_relative(part)}")

        stream_digest = sha256_stream(parts)
        checksums.append(
            f"# stream_sha256 {stream_digest}  {root_name}.compact.tar.zst"
        )
        manifest["roots"].append(
            {
                "name": root_name,
                "source_path": repo_relative(source_root),
                "staged_path": repo_relative(staging_root),
                "restore_command": (
                    f"cat calibrations/{root_name}.compact.tar.zst.part-* "
                    "| tar --use-compress-program=zstd -xf - -C calibrations"
                ),
                "archive_parts": part_records,
                "stream_sha256": stream_digest,
                **stats,
            }
        )

    manifest_path = args.output_dir / "MANIFEST.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    checksums_path = args.output_dir / "SHA256SUMS"
    checksums_path.write_text("\n".join(checksums) + "\n", encoding="utf-8")

    print(f"Wrote {manifest_path}")
    print(f"Wrote {checksums_path}")


if __name__ == "__main__":
    main()
