#!/usr/bin/env python3

import argparse
import csv
import math
import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


PARAMETER_ORDER = ["nodes", "flows", "pps", "speed"]
PARAMETER_LABELS = {
    "nodes": "Number of Nodes",
    "flows": "Number of Flows",
    "pps": "Packets Per Second",
    "speed": "Speed (m/s)",
}
VARIANT_ORDER = ["FR", "GFR", "JA-GFR", "NewReno"]
METRICS = [
    ("throughput_mbps", "Throughput (Mb/s)"),
    ("mean_delay_ms", "Mean Delay (ms)"),
    ("pdr_pct", "PDR (%)"),
    ("drop_ratio_pct", "Drop Ratio (%)"),
    ("total_energy_consumption_j", "Total Energy (J)"),
]
VARIANT_COLORS = {
    "NewReno": "#4c566a",
    "FR": "#bf616a",
    "GFR": "#5e81ac",
    "JA-GFR": "#d08770",
}


def read_summary(path):
    rows = []
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            row["vary_value"] = float(row["vary_value"])
            row["run"] = int(row["run"])
            for key in [
                "n_nodes",
                "n_flows",
                "packets_per_second",
                "packet_size_bytes",
                "node_speed_mps",
                "tx_range_m",
                "area_scale",
                "field_side_m",
                "throughput_mbps",
                "mean_delay_ms",
                "pdr_pct",
                "drop_ratio_pct",
                "total_energy_consumption_j",
                "mean_energy_per_node_j",
                "tx_packets",
                "rx_packets",
                "lost_packets",
            ]:
                row[key] = float(row[key])
            rows.append(row)
    return rows


def mean(values):
    return sum(values) / len(values) if values else 0.0


def stddev(values):
    if len(values) < 2:
        return 0.0
    mu = mean(values)
    return math.sqrt(sum((value - mu) ** 2 for value in values) / (len(values) - 1))


def aggregate(rows):
    grouped = defaultdict(lambda: defaultdict(list))
    for row in rows:
        key = (row["vary_parameter"], row["vary_value"], row["variant_label"])
        for metric, _ in METRICS:
            grouped[key][metric].append(row[metric])

    summary = {}
    for key, metric_map in grouped.items():
        summary[key] = {}
        for metric, values in metric_map.items():
            summary[key][metric] = (mean(values), stddev(values))
    return summary


def write_aggregate_csv(summary, output_path):
    with open(output_path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "vary_parameter",
                "vary_value",
                "variant_label",
                "metric",
                "mean",
                "stddev",
            ]
        )
        for parameter in PARAMETER_ORDER:
            values = sorted({value for p, value, _ in summary.keys() if p == parameter})
            variants = [variant for variant in VARIANT_ORDER if any(
                p == parameter and label == variant for p, _, label in summary.keys()
            )]
            extra_variants = sorted({
                label for p, _, label in summary.keys()
                if p == parameter and label not in VARIANT_ORDER
            })
            for value in values:
                for variant in variants + extra_variants:
                    key = (parameter, value, variant)
                    if key not in summary:
                        continue
                    for metric, _ in METRICS:
                        mean_value, std_value = summary[key][metric]
                        writer.writerow(
                            [parameter, value, variant, metric, f"{mean_value:.6f}", f"{std_value:.6f}"]
                        )


def plot_parameter(summary, parameter, figures_dir):
    values = sorted({value for p, value, _ in summary.keys() if p == parameter})
    if not values:
        return

    variants = [variant for variant in VARIANT_ORDER if any(
        p == parameter and label == variant for p, _, label in summary.keys()
    )]
    extra_variants = sorted({
        label for p, _, label in summary.keys()
        if p == parameter and label not in VARIANT_ORDER
    })
    variants.extend(extra_variants)

    fig, axes = plt.subplots(3, 2, figsize=(12, 10))
    axes = axes.flatten()

    for axis, (metric_key, metric_label) in zip(axes, METRICS):
        for variant in variants:
            variant_values = [value for value in values if (parameter, value, variant) in summary]
            if not variant_values:
                continue
            means = [summary[(parameter, value, variant)][metric_key][0] for value in variant_values]
            errors = [summary[(parameter, value, variant)][metric_key][1] for value in variant_values]
            axis.errorbar(
                variant_values,
                means,
                yerr=errors,
                marker="o",
                linewidth=2,
                capsize=4,
                color=VARIANT_COLORS.get(variant, "#2e3440"),
                label=variant,
            )
        axis.set_xlabel(PARAMETER_LABELS[parameter])
        axis.set_ylabel(metric_label)
        axis.grid(True, linestyle="--", alpha=0.3)
        axis.legend()

    axes[-1].axis("off")
    axes[-1].text(
        0.0,
        0.95,
        "TCP-variant comparison\n"
        "One checklist parameter is varied at a time.\n"
        "Points show the mean across runs.\n"
        "Error bars show sample standard deviation.",
        va="top",
        fontsize=11,
    )

    fig.suptitle(f"802.11 Mobile TCP Checklist: varying {PARAMETER_LABELS[parameter]}", fontsize=14)
    fig.tight_layout()
    fig.savefig(os.path.join(figures_dir, f"wifi_mobile_{parameter}_metrics.png"), dpi=220)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", required=True, help="CSV written by run_wifi_mobile_checklist.sh")
    parser.add_argument("--figures", required=True, help="Output directory for PNG figures")
    args = parser.parse_args()

    os.makedirs(args.figures, exist_ok=True)
    rows = read_summary(args.summary)
    summary = aggregate(rows)
    write_aggregate_csv(summary, os.path.join(os.path.dirname(args.summary), "aggregated_summary.csv"))

    for parameter in PARAMETER_ORDER:
        plot_parameter(summary, parameter, args.figures)


if __name__ == "__main__":
    main()
