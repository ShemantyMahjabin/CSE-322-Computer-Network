#!/usr/bin/env python3

import argparse
import csv
import os
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


VARY_COLUMNS = [
    ("n_nodes", "Number of Nodes"),
    ("n_flows", "Number of Flows"),
    ("packets_per_second", "Packets Per Second"),
    ("coverage_multiplier", "Coverage Area (x Tx_range)"),
]

METRICS = [
    ("throughput_mbps", "Throughput (Mb/s)"),
    ("mean_throughput_per_node_mbps", "Mean Throughput per Node (Mb/s)"),
    ("mean_delay_ms", "Mean Delay (ms)"),
    ("pdr_pct", "PDR (%)"),
    ("drop_ratio_pct", "Drop Ratio (%)"),
    ("total_energy_j", "Total Energy (J)"),
]

VARIANT_ORDER = ["NewReno", "FR", "GFR", "JA-GFR"]
VARIANT_COLORS = {
    "NewReno": "#4c566a",
    "FR": "#bf616a",
    "GFR": "#5e81ac",
    "JA-GFR": "#d08770",
}


def variant_label(name):
    mapping = {
        "ns3::TcpNewReno": "NewReno",
        "ns3::TcpFr": "FR",
        "ns3::TcpFrGfr": "GFR",
        "ns3::TcpFrGfrJitter": "JA-GFR",
    }
    return mapping.get(name, name)


def read_rows(path):
    rows = []
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            for key in [
                "n_nodes",
                "n_flows",
                "packets_per_second",
                "coverage_multiplier",
                "simulation_time_s",
                "throughput_mbps",
                "mean_throughput_per_node_mbps",
                "mean_delay_ms",
                "pdr_pct",
                "drop_ratio_pct",
                "total_energy_j",
                "mean_energy_per_node_j",
            ]:
                if key == "mean_throughput_per_node_mbps" and key not in row:
                    row[key] = float(row["throughput_mbps"]) / float(row["n_nodes"])
                else:
                    row[key] = float(row[key])
            row["variant_label"] = variant_label(row["variant"])
            rows.append(row)
    return rows


def detect_varying_column(rows):
    for key, label in VARY_COLUMNS:
        values = sorted({row[key] for row in rows})
        if len(values) > 1:
            return key, label, values
    raise ValueError("No varying parameter found")


def format_value(value):
    if float(value).is_integer():
        return str(int(value))
    return f"{value:g}"


def build_info_lines(rows, varying_key):
    first = rows[0]
    labels = {
        "n_nodes": "Nodes",
        "n_flows": "Flows",
        "packets_per_second": "PPS",
        "coverage_multiplier": "Coverage",
        "simulation_time_s": "Sim Time",
    }
    info_order = [
        "n_nodes",
        "n_flows",
        "packets_per_second",
        "coverage_multiplier",
        "simulation_time_s",
    ]
    lines = ["Static 802.15.4 TCP comparison", f"Varied: {labels[varying_key]}"]
    for key in info_order:
        if key == varying_key:
            continue
        suffix = " x Tx_range" if key == "coverage_multiplier" else (" s" if key == "simulation_time_s" else "")
        lines.append(f"{labels[key]}: {format_value(first[key])}{suffix}")
    return "\n".join(lines)


def plot_csv(path, figures_dir):
    rows = read_rows(path)
    varying_key, varying_label, x_values = detect_varying_column(rows)

    grouped = {}
    for row in rows:
        grouped[(row["variant_label"], row[varying_key])] = row

    variants = [name for name in VARIANT_ORDER if any(row["variant_label"] == name for row in rows)]
    variants.extend(
        sorted({row["variant_label"] for row in rows if row["variant_label"] not in VARIANT_ORDER})
    )

    fig, axes = plt.subplots(3, 2, figsize=(12, 11))
    axes = axes.flatten()

    for axis, (metric_key, metric_label) in zip(axes, METRICS):
        for variant in variants:
            variant_x = [x for x in x_values if (variant, x) in grouped]
            if not variant_x:
                continue
            variant_y = [grouped[(variant, x)][metric_key] for x in variant_x]
            axis.plot(
                variant_x,
                variant_y,
                marker="o",
                linewidth=2,
                color=VARIANT_COLORS.get(variant, "#2e3440"),
                label=variant,
            )
        axis.set_xlabel(varying_label)
        axis.set_ylabel(metric_label)
        axis.grid(True, linestyle="--", alpha=0.3)
        axis.legend()

    fig.suptitle(f"Wireless 802.15.4: varying {varying_label}", fontsize=14)
    fig.text(0.5, 0.02, " | ".join(build_info_lines(rows, varying_key).splitlines()), ha="center", fontsize=10)
    fig.tight_layout(rect=(0, 0.05, 1, 0.97))

    stem = Path(path).stem
    output_path = figures_dir / f"{stem}.png"
    fig.savefig(output_path, dpi=220)
    plt.close(fig)
    return output_path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input-dir",
        default="scratch/wireless802.15.4",
        help="Directory containing wireless 802.15.4 CSV files",
    )
    parser.add_argument(
        "--figures-dir",
        default="scratch/wireless802.15.4/figures",
        help="Output directory for PNG figures",
    )
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    figures_dir = Path(args.figures_dir)
    figures_dir.mkdir(parents=True, exist_ok=True)

    csv_paths = sorted(input_dir.glob("*.csv"))
    if not csv_paths:
        raise SystemExit(f"No CSV files found in {input_dir}")

    for csv_path in csv_paths:
        output_path = plot_csv(csv_path, figures_dir)
        print(output_path)


if __name__ == "__main__":
    main()
