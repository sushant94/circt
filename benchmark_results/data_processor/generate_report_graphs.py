#!/usr/bin/env python3
"""Generate report-ready benchmark graphs using matplotlib.

This script scans benchmark result subdirectories for ``summary.csv`` files and
emits SVG plots that can be included directly in Typst.

Outputs:
  - relative_to_btor2_percent.svg
  - absolute_times_grouped.svg
  - absolute_times_lines.svg

Run with the data_processor virtualenv:
  source benchmark_results/data_processor/.venv/bin/activate
  python benchmark_results/data_processor/generate_report_graphs.py
"""

from __future__ import annotations

import argparse
import csv
import os
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
RESULTS_ROOT = SCRIPT_DIR.parent
DEFAULT_OUTPUT_DIR = RESULTS_ROOT / "report_plots"
MPLCONFIGDIR = SCRIPT_DIR / ".mplconfig"

os.environ.setdefault("MPLCONFIGDIR", str(MPLCONFIGDIR))
MPLCONFIGDIR.mkdir(parents=True, exist_ok=True)

import matplotlib

matplotlib.use("Agg")

from matplotlib import pyplot as plt
from matplotlib.ticker import FuncFormatter


VARIANT_ORDER = [
    "btor2",
    "btor2pp",
    "btor2pp-split-mems",
    "btor2pp-split-reg-vecs",
]

VARIANT_LABELS = {
    "btor2": "BTOR2",
    "btor2pp": "BTOR2++",
    "btor2pp-split-mems": "BTOR2++ Mem Modularization",
    "btor2pp-split-reg-vecs": "BTOR2++ Reg(Vec(...)) Modularization",
}

VARIANT_COLORS = {
    "btor2": "#1d3557",
    "btor2pp": "#2a9d8f",
    "btor2pp-split-mems": "#e9c46a",
    "btor2pp-split-reg-vecs": "#e76f51",
}

TEST_ORDER_HINTS = [
    "Rocket",
    "SmallBoom",
    "MediumBoom",
    "LargeBoom",
    "MegaBoom",
]


@dataclass(frozen=True)
class Measurement:
    avg_seconds: float
    stdev_seconds: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate matplotlib SVG graphs from benchmark summary CSV files."
    )
    parser.add_argument(
        "--results-root",
        default=str(RESULTS_ROOT),
        help="Directory containing benchmark result subdirectories.",
    )
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Directory where SVG plots will be written.",
    )
    return parser.parse_args()


def pretty_test_name(raw_name: str) -> str:
    if "Rocket" in raw_name:
        return "Rocket"
    for prefix in ("SmallBoom", "MediumBoom", "LargeBoom", "MegaBoom"):
        if raw_name.startswith(prefix):
            return prefix
    simplified = raw_name.replace("NoCompressed", "")
    simplified = simplified.replace("Config", "")
    simplified = simplified.replace("V3", "")
    return simplified.strip("_- ")


def test_sort_key(name: str) -> tuple[int, str]:
    for index, hint in enumerate(TEST_ORDER_HINTS):
        if name.startswith(hint):
            return index, name
    return len(TEST_ORDER_HINTS), name


def seconds_formatter(value: float, _: float) -> str:
    return f"{value:.1f}s"


def percent_formatter(value: float, _: float) -> str:
    return f"{value:.0f}%"


def load_measurements(results_root: Path) -> dict[str, dict[str, Measurement]]:
    data: dict[str, dict[str, Measurement]] = {}

    for summary_path in sorted(results_root.glob("*/summary.csv")):
        with summary_path.open("r", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        if not rows:
            continue

        raw_test_name = rows[0]["test_name"]
        test_name = pretty_test_name(raw_test_name)
        test_data: dict[str, Measurement] = {}

        for row in rows:
            variant_name = row["variant_name"]
            if variant_name not in VARIANT_ORDER:
                continue
            test_data[variant_name] = Measurement(
                avg_seconds=float(row["avg_seconds"]),
                stdev_seconds=float(row["stdev_seconds"]),
            )

        if test_data:
            data[test_name] = test_data

    return dict(sorted(data.items(), key=lambda item: test_sort_key(item[0])))


def write_relative_bar_chart(
    measurements: dict[str, dict[str, Measurement]],
    output_path: Path,
) -> None:
    tests = list(measurements)
    variants = [variant for variant in VARIANT_ORDER if variant != "btor2"]
    x_positions = list(range(len(tests)))
    bar_width = 0.22

    fig, ax = plt.subplots(figsize=(11, 6.5))
    for variant_index, variant in enumerate(variants):
        offsets = [
            x + (variant_index - 1) * bar_width
            for x in x_positions
        ]
        deltas = []
        for test in tests:
            baseline = measurements[test]["btor2"].avg_seconds
            variant_time = measurements[test][variant].avg_seconds
            deltas.append(((variant_time - baseline) / baseline) * 100.0)
        ax.bar(
            offsets,
            deltas,
            width=bar_width * 0.92,
            color=VARIANT_COLORS[variant],
            label=VARIANT_LABELS[variant],
        )

    ax.axhline(0.0)
    ax.set_title("Compilation Time Relative to BTOR2 (%)")
    ax.set_ylabel("Percent difference vs BTOR2")
    ax.set_xticks(x_positions, tests)
    ax.yaxis.set_major_formatter(FuncFormatter(percent_formatter))
    ax.grid(axis="y")
    ax.legend()

    fig.savefig(output_path, format="svg")
    plt.close(fig)


def write_absolute_grouped_chart(
    measurements: dict[str, dict[str, Measurement]],
    output_path: Path,
) -> None:
    tests = list(measurements)
    x_positions = list(range(len(tests)))
    bar_width = 0.18

    fig, ax = plt.subplots(figsize=(11.5, 6.75))
    for variant_index, variant in enumerate(VARIANT_ORDER):
        offsets = [
            x + (variant_index - 1.5) * bar_width
            for x in x_positions
        ]
        averages = [measurements[test][variant].avg_seconds for test in tests]
        stdevs = [measurements[test][variant].stdev_seconds for test in tests]
        ax.bar(
            offsets,
            averages,
            width=bar_width * 0.92,
            color=VARIANT_COLORS[variant],
            label=VARIANT_LABELS[variant],
            yerr=stdevs,
            error_kw={
                "capsize": 3,
            },
        )

    ax.set_title("Compilation Time by Design and Backend")
    ax.set_ylabel("Average compile time")
    ax.set_xticks(x_positions, tests)
    ax.yaxis.set_major_formatter(FuncFormatter(seconds_formatter))
    ax.grid(axis="y")
    ax.legend()

    fig.savefig(output_path, format="svg")
    plt.close(fig)


def write_absolute_line_chart(
    measurements: dict[str, dict[str, Measurement]],
    output_path: Path,
) -> None:
    tests = list(measurements)
    x_positions = list(range(len(tests)))

    fig, ax = plt.subplots(figsize=(11.5, 6.5))
    for variant in VARIANT_ORDER:
        averages = [measurements[test][variant].avg_seconds for test in tests]
        stdevs = [measurements[test][variant].stdev_seconds for test in tests]
        ax.errorbar(
            x_positions,
            averages,
            yerr=stdevs,
            label=VARIANT_LABELS[variant],
            marker="o",
            capsize=3,
        )

    ax.set_title("Compilation Time Trends Across Designs")
    ax.set_ylabel("Average compile time")
    ax.set_xticks(x_positions, tests)
    ax.yaxis.set_major_formatter(FuncFormatter(seconds_formatter))
    ax.grid(axis="y")
    ax.legend()

    fig.savefig(output_path, format="svg")
    plt.close(fig)


def main() -> int:
    args = parse_args()
    results_root = Path(args.results_root).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    measurements = load_measurements(results_root)
    if not measurements:
        print(f"error: no summary.csv files found under {results_root}")
        return 1

    missing_variants = [
        test
        for test, variants in measurements.items()
        if any(variant not in variants for variant in VARIANT_ORDER)
    ]
    if missing_variants:
        missing = ", ".join(missing_variants)
        print(f"error: some expected variants are missing in: {missing}")
        return 1

    write_relative_bar_chart(
        measurements,
        output_dir / "relative_to_btor2_percent.svg",
    )
    write_absolute_grouped_chart(
        measurements,
        output_dir / "absolute_times_grouped.svg",
    )
    write_absolute_line_chart(
        measurements,
        output_dir / "absolute_times_lines.svg",
    )

    print(f"Wrote plots to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
