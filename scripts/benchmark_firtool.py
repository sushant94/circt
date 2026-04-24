#!/usr/bin/env python3
"""Benchmark firtool compilation times across tests and flag variants.

The script runs each test/variant combination multiple times, records the raw
timings in CSV, and writes a summary CSV with averages and other basic stats.

Examples:

  python3 scripts/benchmark_firtool.py Rocket.fir Sodor1.fir \
      --runs 10 \
      --preset hhoudini

  python3 scripts/benchmark_firtool.py Rocket.fir Sodor1.fir \
      --runs 10 \
      --preset hhoudini-compare

  python3 scripts/benchmark_firtool.py --config bench_config.json

  python3 scripts/benchmark_firtool.py \
      --preset hhoudini \
      --variant "mem-split::" \
      --variant "reg-vec-split::" \
      generated/Rocket.fir generated/SmallBoom.fir

For generated designs with per-design annotation files, prefer --config.

Config file format:

{
  "firtool": "build/bin/firtool",
  "preset": "hhoudini",
  "runs": 10,
  "tests": [
    "Rocket.fir",
    {
      "name": "sodor",
      "input": "Sodor1.fir",
      "args": ["--annotation-file", "Sodor1.anno.json"]
    }
  ],
  "variants": [
    {"name": "baseline", "args": []},
    {"name": "extra-flags", "args": ["--disable-opt"]}
  ]
}
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import shlex
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class TestCase:
    name: str
    input_path: Path
    args: list[str]


@dataclass(frozen=True)
class Variant:
    name: str
    args: list[str]
    generated_annotations: list[dict[str, Any]] = field(default_factory=list)


PRESET_COMMON_ARGS: dict[str, list[str]] = {
    "hhoudini": ["--format=fir", "--btor2pp"],
    "hhoudini-compare": ["--format=fir"],
}


PRESET_VARIANTS: dict[str, list[Variant]] = {
    "hhoudini-compare": [
        Variant(name="btor2", args=["--btor2"]),
        Variant(name="btor2pp", args=["--btor2pp"]),
        Variant(
            name="btor2pp-split-mems",
            args=["--btor2pp"],
            generated_annotations=[
                {"class": "circt.HHoudiniSplitMemsAnnotation"},
            ],
        ),
        Variant(
            name="btor2pp-split-reg-vecs",
            args=["--btor2pp"],
            generated_annotations=[
                {"class": "circt.HHoudiniSplitRegVecsAnnotation"},
                {"class": "sifive.enterprise.firrtl.ConvertMemToRegOfVecAnnotation$"},
            ],
        ),
    ],
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run firtool benchmarks, repeat each test/variant multiple times, "
            "and write graph-friendly CSV output."
        )
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Input FIRRTL/MLIR files to benchmark.",
    )
    parser.add_argument(
        "--firtool",
        default="build/bin/firtool",
        help="Path to the firtool binary. Default: %(default)s",
    )
    parser.add_argument(
        "--config",
        help="Optional JSON config file describing tests and variants.",
    )
    parser.add_argument(
        "--preset",
        choices=sorted(PRESET_COMMON_ARGS),
        help="Convenience preset for common firtool modes.",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=10,
        help="Number of benchmark runs per test/variant. Default: %(default)s",
    )
    parser.add_argument(
        "--warmup-runs",
        type=int,
        default=0,
        help="Warmup runs per test/variant before recording timings.",
    )
    parser.add_argument(
        "--common-args",
        default="",
        help="Extra arguments applied to every firtool invocation.",
    )
    parser.add_argument(
        "--variant",
        action="append",
        default=[],
        metavar="NAME::ARGS",
        help=(
            "Variant to benchmark. Example: "
            "\"btor2pp::--btor2pp --annotation-file foo.json\". "
            "Can be passed more than once."
        ),
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help=(
            "Directory for benchmark outputs. "
            "Default: benchmark_results/<timestamp>"
        ),
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="Continue benchmarking other cases if one command fails.",
    )
    return parser.parse_args()


def resolve_path(path_str: str, base_dir: Path) -> Path:
    path = Path(path_str)
    if path.is_absolute():
        return path
    return (base_dir / path).resolve()


def load_config(config_path: Path) -> dict[str, Any]:
    with config_path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_test_entry(entry: Any, base_dir: Path) -> TestCase:
    if isinstance(entry, str):
        input_path = resolve_path(entry, base_dir)
        return TestCase(name=input_path.stem, input_path=input_path, args=[])
    if isinstance(entry, dict):
        raw_input = entry.get("input")
        if not raw_input:
            raise ValueError(f"Test entry is missing 'input': {entry!r}")
        input_path = resolve_path(raw_input, base_dir)
        name = entry.get("name") or input_path.stem
        raw_args = entry.get("args", [])
        if not isinstance(raw_args, list):
            raise ValueError(f"Test args must be a list: {entry!r}")
        args = resolve_path_like_args([str(arg) for arg in raw_args], base_dir)
        return TestCase(name=name, input_path=input_path, args=args)
    raise ValueError(f"Unsupported test entry: {entry!r}")


def parse_variant_entry(entry: Any, base_dir: Path) -> Variant:
    if isinstance(entry, str):
        return parse_variant_string(entry)
    if isinstance(entry, dict):
        name = entry.get("name")
        args = entry.get("args", [])
        generated_annotations = entry.get("generated_annotations", [])
        if not name:
            raise ValueError(f"Variant entry is missing 'name': {entry!r}")
        if not isinstance(args, list):
            raise ValueError(f"Variant args must be a list: {entry!r}")
        if not isinstance(generated_annotations, list):
            raise ValueError(f"Variant generated_annotations must be a list: {entry!r}")
        resolved_args = resolve_path_like_args([str(arg) for arg in args], base_dir)
        return Variant(
            name=name,
            args=resolved_args,
            generated_annotations=generated_annotations,
        )
    raise ValueError(f"Unsupported variant entry: {entry!r}")


def parse_variant_string(value: str) -> Variant:
    if "::" not in value:
        raise ValueError(
            f"Invalid variant {value!r}. Expected format NAME::ARGS."
        )
    name, raw_args = value.split("::", 1)
    name = name.strip()
    if not name:
        raise ValueError(f"Variant name cannot be empty: {value!r}")
    return Variant(name=name, args=shlex.split(raw_args))


def resolve_path_like_args(args: list[str], base_dir: Path) -> list[str]:
    resolved: list[str] = []
    expect_path_for = {
        "--annotation-file",
        "--blackbox-path",
        "--output-final-mlir",
        "--output-hw-mlir",
        "--output-annotation-file",
        "--repl-seq-mem-file",
        "-I",
        "-o",
    }

    index = 0
    while index < len(args):
        arg = args[index]
        if arg in expect_path_for and index + 1 < len(args):
            resolved.append(arg)
            resolved.append(str(resolve_path(args[index + 1], base_dir)))
            index += 2
            continue

        if "=" in arg:
            flag, value = arg.split("=", 1)
            if flag in expect_path_for and value:
                resolved.append(f"{flag}={resolve_path(value, base_dir)}")
                index += 1
                continue

        resolved.append(arg)
        index += 1

    return resolved


def build_suite(args: argparse.Namespace) -> tuple[Path, int, int, list[str], list[TestCase], list[Variant]]:
    cwd = Path.cwd()
    config_data: dict[str, Any] = {}
    config_base_dir = cwd

    if args.config:
        config_path = resolve_path(args.config, cwd)
        config_data = load_config(config_path)
        config_base_dir = config_path.parent

    firtool = resolve_path(config_data.get("firtool", args.firtool), config_base_dir)
    preset = config_data.get("preset", args.preset)
    runs = config_data.get("runs", args.runs)
    warmup_runs = config_data.get("warmup_runs", args.warmup_runs)
    common_args: list[str] = []
    if preset:
        common_args.extend(PRESET_COMMON_ARGS[preset])
    common_args.extend(shlex.split(args.common_args))
    config_common_args = config_data.get("common_args", [])
    if config_common_args:
        common_args = [str(arg) for arg in config_common_args] + common_args
    common_args = resolve_path_like_args(common_args, config_base_dir)

    tests: list[TestCase] = []
    for entry in config_data.get("tests", []):
        tests.append(parse_test_entry(entry, config_base_dir))
    for raw_input in args.inputs:
        tests.append(parse_test_entry(raw_input, cwd))

    variants: list[Variant] = []
    for entry in config_data.get("variants", []):
        variants.append(parse_variant_entry(entry, config_base_dir))
    for entry in args.variant:
        variant = parse_variant_string(entry)
        variants.append(Variant(name=variant.name, args=resolve_path_like_args(variant.args, cwd)))

    if not tests:
        raise ValueError("No tests were provided. Pass inputs or use --config.")

    if not variants:
        if preset in PRESET_VARIANTS:
            variants = PRESET_VARIANTS[preset]
        else:
            variants = [Variant(name="baseline", args=[])]

    if runs <= 0:
        raise ValueError("--runs must be greater than 0.")
    if warmup_runs < 0:
        raise ValueError("--warmup-runs cannot be negative.")

    return firtool, runs, warmup_runs, common_args, tests, variants


def ensure_inputs_exist(firtool: Path, tests: list[TestCase]) -> None:
    if not firtool.is_file():
        raise FileNotFoundError(f"firtool not found: {firtool}")
    for test in tests:
        if not test.input_path.is_file():
            raise FileNotFoundError(f"Input file not found: {test.input_path}")


def make_output_dir(explicit_output_dir: str | None) -> Path:
    if explicit_output_dir:
        output_dir = resolve_path(explicit_output_dir, Path.cwd())
    else:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_dir = Path.cwd() / "benchmark_results" / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def summarize(values: list[float]) -> dict[str, float]:
    if not values:
        return {
            "avg_seconds": math.nan,
            "min_seconds": math.nan,
            "max_seconds": math.nan,
            "stdev_seconds": math.nan,
        }
    if len(values) == 1:
        stdev = 0.0
    else:
        stdev = statistics.stdev(values)
    return {
        "avg_seconds": statistics.mean(values),
        "min_seconds": min(values),
        "max_seconds": max(values),
        "stdev_seconds": stdev,
    }


def run_command(command: list[str]) -> tuple[float, int, str]:
    start = time.perf_counter()
    completed = subprocess.run(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    elapsed = time.perf_counter() - start
    return elapsed, completed.returncode, completed.stderr


def write_metadata(
    output_dir: Path,
    firtool: Path,
    runs: int,
    warmup_runs: int,
    common_args: list[str],
    tests: list[TestCase],
    variants: list[Variant],
) -> None:
    payload = {
        "timestamp": datetime.now().isoformat(),
        "firtool": str(firtool),
        "runs": runs,
        "warmup_runs": warmup_runs,
        "common_args": common_args,
        "tests": [
            {
                "name": test.name,
                "input": str(test.input_path),
                "args": test.args,
            }
            for test in tests
        ],
        "variants": [
            {
                "name": variant.name,
                "args": variant.args,
                "generated_annotations": variant.generated_annotations,
            }
            for variant in variants
        ],
    }
    with (output_dir / "metadata.json").open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)


def append_text_log(path: Path, lines: list[str]) -> None:
    with path.open("a", encoding="utf-8") as handle:
        for line in lines:
            handle.write(line)
            handle.write("\n")


def materialize_variant(variant: Variant, output_dir: Path) -> Variant:
    if not variant.generated_annotations:
        return variant

    annotations_dir = output_dir / "generated_annotations"
    annotations_dir.mkdir(parents=True, exist_ok=True)
    annotation_path = annotations_dir / f"{variant.name}.anno.json"
    with annotation_path.open("w", encoding="utf-8") as handle:
        json.dump(variant.generated_annotations, handle, indent=2)
        handle.write("\n")

    return Variant(
        name=variant.name,
        args=[*variant.args, "--annotation-file", str(annotation_path)],
        generated_annotations=variant.generated_annotations,
    )


def main() -> int:
    args = parse_args()

    try:
        firtool, runs, warmup_runs, common_args, tests, variants = build_suite(args)
        ensure_inputs_exist(firtool, tests)
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}", file=sys.stderr)
        return 1

    output_dir = make_output_dir(args.output_dir)
    variants = [materialize_variant(variant, output_dir) for variant in variants]
    raw_csv_path = output_dir / "raw_results.csv"
    summary_csv_path = output_dir / "summary.csv"
    text_log_path = output_dir / "results.txt"
    failures_log_path = output_dir / "failures.log"
    write_metadata(output_dir, firtool, runs, warmup_runs, common_args, tests, variants)
    append_text_log(
        text_log_path,
        [
            f"Benchmark started: {datetime.now().isoformat()}",
            f"firtool: {firtool}",
            f"runs: {runs}",
            f"warmup_runs: {warmup_runs}",
            f"common_args: {' '.join(shlex.quote(part) for part in common_args) if common_args else '(none)'}",
            "",
        ],
    )

    raw_rows: list[dict[str, Any]] = []
    summary_rows: list[dict[str, Any]] = []
    failures: list[str] = []

    total_cases = len(tests) * len(variants)
    completed_cases = 0

    for test in tests:
        for variant in variants:
            command = [
                str(firtool),
                str(test.input_path),
                *test.args,
                *common_args,
                *variant.args,
            ]
            print(
                f"[{completed_cases + 1}/{total_cases}] "
                f"{test.name} :: {variant.name}",
                flush=True,
            )
            append_text_log(
                text_log_path,
                [
                    f"[{completed_cases + 1}/{total_cases}] {test.name} :: {variant.name}",
                    f"input: {test.input_path}",
                    f"test_args: {' '.join(shlex.quote(part) for part in test.args) if test.args else '(none)'}",
                    f"command: {' '.join(shlex.quote(part) for part in command)}",
                ],
            )

            for _ in range(warmup_runs):
                _, return_code, stderr = run_command(command)
                if return_code != 0:
                    failures.append(
                        "\n".join(
                            [
                                f"Warmup failed for {test.name} :: {variant.name}",
                                f"Command: {' '.join(shlex.quote(part) for part in command)}",
                                stderr.rstrip(),
                                "",
                            ]
                        )
                    )
                    if not args.keep_going:
                        failures_log_path.write_text("".join(failures), encoding="utf-8")
                        append_text_log(
                            text_log_path,
                            [
                                "warmup failed",
                                f"details: see {failures_log_path}",
                                "",
                            ],
                        )
                        print(f"error: warmup failed, see {failures_log_path}", file=sys.stderr)
                        return 1
                    break

            timings: list[float] = []
            successful_runs = 0

            for run_index in range(1, runs + 1):
                elapsed, return_code, stderr = run_command(command)
                ok = return_code == 0
                raw_rows.append(
                    {
                        "test_name": test.name,
                        "input_file": str(test.input_path),
                        "test_args": " ".join(shlex.quote(part) for part in test.args),
                        "variant_name": variant.name,
                        "run_index": run_index,
                        "elapsed_seconds": f"{elapsed:.9f}",
                        "return_code": return_code,
                        "command": " ".join(shlex.quote(part) for part in command),
                    }
                )

                if ok:
                    timings.append(elapsed)
                    successful_runs += 1
                    append_text_log(
                        text_log_path,
                        [f"run {run_index}: {elapsed:.9f} s"],
                    )
                else:
                    append_text_log(
                        text_log_path,
                        [f"run {run_index}: FAILED (return code {return_code})"],
                    )
                    failures.append(
                        "\n".join(
                            [
                                f"Run failed for {test.name} :: {variant.name} :: run {run_index}",
                                f"Command: {' '.join(shlex.quote(part) for part in command)}",
                                stderr.rstrip(),
                                "",
                            ]
                        )
                    )
                    if not args.keep_going:
                        failures_log_path.write_text("".join(failures), encoding="utf-8")
                        write_csv(
                            raw_csv_path,
                            raw_rows,
                            [
                                "test_name",
                                "input_file",
                                "test_args",
                                "variant_name",
                                "run_index",
                                "elapsed_seconds",
                                "return_code",
                                "command",
                            ],
                        )
                        print(f"error: benchmark failed, see {failures_log_path}", file=sys.stderr)
                        return 1

            stats = summarize(timings)
            summary_rows.append(
                {
                    "test_name": test.name,
                    "input_file": str(test.input_path),
                    "test_args": " ".join(shlex.quote(part) for part in test.args),
                    "variant_name": variant.name,
                    "runs_requested": runs,
                    "runs_succeeded": successful_runs,
                    "avg_seconds": format_float(stats["avg_seconds"]),
                    "min_seconds": format_float(stats["min_seconds"]),
                    "max_seconds": format_float(stats["max_seconds"]),
                    "stdev_seconds": format_float(stats["stdev_seconds"]),
                }
            )
            append_text_log(
                text_log_path,
                [
                    (
                        "summary: "
                        f"avg={format_float(stats['avg_seconds'])} s, "
                        f"min={format_float(stats['min_seconds'])} s, "
                        f"max={format_float(stats['max_seconds'])} s, "
                        f"stdev={format_float(stats['stdev_seconds'])} s, "
                        f"success={successful_runs}/{runs}"
                    ),
                    "",
                ],
            )
            completed_cases += 1

    write_csv(
        raw_csv_path,
        raw_rows,
            [
                "test_name",
                "input_file",
                "test_args",
                "variant_name",
                "run_index",
                "elapsed_seconds",
            "return_code",
            "command",
        ],
    )
    write_csv(
        summary_csv_path,
        summary_rows,
        [
            "test_name",
            "input_file",
            "test_args",
            "variant_name",
            "runs_requested",
            "runs_succeeded",
            "avg_seconds",
            "min_seconds",
            "max_seconds",
            "stdev_seconds",
        ],
    )

    if failures:
        failures_log_path.write_text("".join(failures), encoding="utf-8")
        append_text_log(
            text_log_path,
            [
                f"Completed with failures. See {failures_log_path}",
                f"Raw results: {raw_csv_path}",
                f"Summary: {summary_csv_path}",
            ],
        )
        print(f"Completed with failures. See {failures_log_path}")
    else:
        append_text_log(
            text_log_path,
            [
                "Completed successfully.",
                f"Raw results: {raw_csv_path}",
                f"Summary: {summary_csv_path}",
            ],
        )
        print("Completed successfully.")

    print(f"Raw results: {raw_csv_path}")
    print(f"Summary: {summary_csv_path}")
    print(f"Text log: {text_log_path}")
    return 0


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def format_float(value: float) -> str:
    if math.isnan(value):
        return "nan"
    return f"{value:.9f}"


if __name__ == "__main__":
    raise SystemExit(main())
