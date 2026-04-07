#!/usr/bin/env python3
"""
Plot the selected random-loss results as four separate figures.

Inputs:
  - fig5_selected_variants.csv
  - fig6_selected_variants.csv
  - fig7_selected_variants.csv
  - fig8_selected_varients.csv

Outputs:
  - fig5_selected_variants.png
  - fig6_selected_variants.png
  - fig7_selected_variants.png
  - fig8_selected_varients.png
"""

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


plt.rcParams.update({
    "font.family": "DejaVu Serif",
    "font.size": 10,
    "figure.dpi": 150,
    "axes.spines.top": False,
    "axes.spines.right": False,
})

STYLE = {
    "fr": dict(color="#1f77b4", linestyle="-", marker="o", markersize=7,
               linewidth=2.2, label="FR"),
    "gfr": dict(color="#d62728", linestyle="--", marker="s", markersize=7,
                linewidth=2.2, label="GFR"),
    "reno": dict(color="#2ca02c", linestyle="-.", marker="^", markersize=7,
                 linewidth=1.8, label="Reno"),
    "sack": dict(color="#ff7f0e", linestyle=":", marker="D", markersize=6,
                 linewidth=1.8, label="Sack"),
    "jitter": dict(color="#6b7280", linestyle=(0, (5, 2)), marker="X", markersize=7,
                   linewidth=2.0, label="JA-GFR"),
}

ORDER = ["fr", "gfr", "reno", "sack", "jitter"]


def style_axes(ax):
    ax.set_facecolor("#f9f9f9")
    ax.yaxis.grid(True, linestyle="--", linewidth=0.6, color="#cccccc", zorder=0)
    ax.xaxis.grid(False)
    ax.set_axisbelow(True)


def add_fr_vs_reno_fill(ax, xs, fr, reno):
    ax.fill_between(xs, reno, fr, where=(fr >= reno), alpha=0.13, color="#1f77b4", zorder=1)


def add_delta_annotation(ax, x_value, fr_value, reno_value, x_span):
    delta = fr_value - reno_value
    if delta <= 0.05:
        return
    mid_y = (fr_value + reno_value) / 2.0
    offset_x = x_value - x_span * 0.22
    ax.annotate(
        f"+{delta:.2f} Mb/s\n(FR vs Reno)",
        xy=(x_value, mid_y),
        xytext=(offset_x, mid_y),
        fontsize=7.5,
        color="#1f77b4",
        va="center",
        arrowprops=dict(
            arrowstyle="-|>",
            color="#1f77b4",
            lw=0.9,
            connectionstyle="arc3,rad=0.0",
        ),
    )


def plot_single(csv_path, x_col, title, xlabel, out_path, xlim=None):
    df = pd.read_csv(csv_path)
    fig, ax = plt.subplots(figsize=(7.5, 4.4))
    fig.patch.set_facecolor("white")
    style_axes(ax)

    xs = df[x_col].to_numpy()
    fr = df["fr"].to_numpy()
    reno = df["reno"].to_numpy()
    add_fr_vs_reno_fill(ax, xs, fr, reno)

    for key in ORDER:
        ax.plot(df[x_col], df[key], zorder=3, **STYLE[key])

    add_delta_annotation(ax, xs[-1], fr[-1], reno[-1], xs[-1] - xs[0] if len(xs) > 1 else 1.0)

    ax.set_title(title, loc="left", fontsize=10, fontweight="bold", pad=6)
    ax.set_xlabel(xlabel, labelpad=4)
    ax.set_ylabel("Goodput  [Mb/s]", labelpad=4)
    if xlim is not None:
        ax.set_xlim(xlim)
    else:
        pad = (xs[-1] - xs[0]) * 0.05 if len(xs) > 1 else 1.0
        ax.set_xlim(xs[0] - pad, xs[-1] + pad)
    ax.set_ylim(0.0, max(df[col].max() for col in ORDER) * 1.18)
    ax.legend(loc="upper right", frameon=True, fontsize=8.5,
              framealpha=0.92, edgecolor="#bbbbbb", handlelength=2.5)

    fig.tight_layout()
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


def plot_fig8(csv_path, out_path):
    df = pd.read_csv(csv_path)
    fig, ax = plt.subplots(figsize=(7.5, 4.6))
    fig.patch.set_facecolor("white")
    style_axes(ax)

    mark_every = max(len(df) // 20, 1)
    for key in ORDER:
        fig8_style = dict(STYLE[key])
        fig8_style["markersize"] = 5
        fig8_style["markevery"] = mark_every
        ax.plot(df["time_s"], df[key], zorder=3, **fig8_style)

    ax.set_title("Fig. 8  Throughput vs Time", loc="left", fontsize=10, fontweight="bold", pad=6)
    ax.set_xlabel("Time  [s]", labelpad=4)
    ax.set_ylabel("Throughput  [Mb/s]", labelpad=4)
    ax.set_xlim(0, 200)
    ax.set_ylim(0.0, max(df[col].max() for col in ORDER) * 1.18)
    ax.legend(loc="upper right", frameon=True, fontsize=8.5,
              framealpha=0.92, edgecolor="#bbbbbb", handlelength=2.5)

    fig.tight_layout()
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


def main():
    base = Path(__file__).resolve().parent

    plot_single(
        base / "fig5_selected_variants.csv",
        "connections",
        "Fig. 5  Goodput vs Number of Connections",
        "Number of connections",
        base / "fig5_selected_variants.png",
        xlim=(0, 31),
    )

    plot_single(
        base / "fig6_selected_variants.csv",
        "bandwidth_Mbps",
        "Fig. 6  Goodput vs Bottleneck Bandwidth",
        "Bottleneck bandwidth  [Mb/s]",
        base / "fig6_selected_variants.png",
        xlim=(0, 160),
    )

    plot_single(
        base / "fig7_selected_variants.csv",
        "rtt_s",
        "Fig. 7  Goodput vs RTT",
        "RTT  [s]",
        base / "fig7_selected_variants.png",
        xlim=(0, 1.05),
    )

    plot_fig8(
        base / "fig8_selected_varients.csv",
        base / "fig8_selected_varients.png",
    )

    print(f"Saved: {base / 'fig5_selected_variants.png'}")
    print(f"Saved: {base / 'fig6_selected_variants.png'}")
    print(f"Saved: {base / 'fig7_selected_variants.png'}")
    print(f"Saved: {base / 'fig8_selected_varients.png'}")


if __name__ == "__main__":
    main()
