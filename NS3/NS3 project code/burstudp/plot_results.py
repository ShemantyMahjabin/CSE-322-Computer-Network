#!/usr/bin/env python3
"""
plot_results.py  —  TCP Faster Recovery simulation plots
Reads fig2_connections.csv, fig3_bandwidth.csv, fig4_rtt.csv and produces
a three-panel PDF that matches the layout of Casetti et al. (2002).

Usage:
    python3 plot_results.py [--base PATH]   (default: scratch/burstyudp/)
"""

import os
import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ── Style ─────────────────────────────────────────────────────────────────────
plt.rcParams.update({
    "font.family":       "DejaVu Serif",
    "font.size":         10,
    "figure.dpi":        150,
    "axes.spines.top":   False,
    "axes.spines.right": False,
})

STYLE = {
    "FR":   dict(color="#1f77b4", linestyle="-",   marker="o", markersize=7,
                 linewidth=2.2, label="FR"),
    "GFR":  dict(color="#d62728", linestyle="--",  marker="s", markersize=7,
                 linewidth=2.2, label="GFR"),
    "JA-GFR": dict(color="#9467bd", linestyle="-", marker="P", markersize=7,
                 linewidth=2.2, label="JA-GFR"),
    "Reno": dict(color="#2ca02c", linestyle="-.",  marker="^", markersize=7,
                 linewidth=1.8, label="Reno"),
    "Sack": dict(color="#ff7f0e", linestyle=":",   marker="D", markersize=6,
                 linewidth=1.8, label="Sack"),
}
VARIANTS = ["FR", "GFR", "JA-GFR", "Reno", "Sack"]


def load(csv_path, col_x):
    if not os.path.isfile(csv_path):
        print(f"[info] Missing: {csv_path}")
        return {}
    df = pd.read_csv(csv_path, header=None,
                     names=["variant", col_x, "goodput_mbs"])
    out = {}
    for v in VARIANTS:
        sub = df[df["variant"] == v].sort_values(col_x)
        if not sub.empty:
            out[v] = (sub[col_x].to_numpy(), sub["goodput_mbs"].to_numpy())
    return out


def auto_ylim(data, pad=0.15):
    if not data:
        return (0.0, 1.0)
    all_y = np.concatenate([y for _, y in data.values()])
    return (0.0, all_y.max() * (1.0 + pad))


def plot_sub(ax, data, title, xlabel, xlim, ylim=None):
    if ylim is None:
        ylim = auto_ylim(data)

    ax.set_facecolor("#f9f9f9")
    ax.yaxis.grid(True, linestyle="--", linewidth=0.6, color="#cccccc", zorder=0)
    ax.set_axisbelow(True)

    # shaded gap between FR and Reno to make the improvement obvious
    if "FR" in data and "Reno" in data:
        xs_fr, ys_fr = data["FR"]
        xs_rn, ys_rn = data["Reno"]
        ys_rn_i = np.interp(xs_fr, xs_rn, ys_rn)
        ax.fill_between(xs_fr, ys_rn_i, ys_fr,
                        where=(ys_fr >= ys_rn_i),
                        alpha=0.13, color="#1f77b4", zorder=1)

    for v in VARIANTS:
        if v in data:
            xs, ys = data[v]
            ax.plot(xs, ys, zorder=3, **STYLE[v])

    # delta annotation at rightmost shared point
    if "FR" in data and "Reno" in data:
        xs_fr, ys_fr = data["FR"]
        xs_rn, ys_rn = data["Reno"]
        cx = min(xs_fr[-1], xs_rn[-1])
        yf = float(np.interp(cx, xs_fr, ys_fr))
        yr = float(np.interp(cx, xs_rn, ys_rn))
        delta = yf - yr
        if delta > 0.05:
            mid_y = (yf + yr) / 2
            offset_x = cx - (xlim[1] - xlim[0]) * 0.20
            ax.annotate(
                f"+{delta:.2f} Mb/s\n(FR vs Reno)",
                xy=(cx, mid_y), xytext=(offset_x, mid_y),
                fontsize=7.5, color="#1f77b4", va="center",
                arrowprops=dict(arrowstyle="-|>", color="#1f77b4",
                                lw=0.9, connectionstyle="arc3,rad=0.0"),
            )

    ax.set_title(title, loc="left", fontsize=10, fontweight="bold", pad=6)
    ax.set_xlabel(xlabel, labelpad=4)
    ax.set_ylabel("Goodput  [Mb/s]", labelpad=4)
    ax.set_xlim(xlim)
    ax.set_ylim(ylim)
    leg = ax.legend(loc="upper right", frameon=True, fontsize=8.5,
                    framealpha=0.92, edgecolor="#bbbbbb", handlelength=2.5)
    legend_handles = getattr(leg, "legend_handles", getattr(leg, "legendHandles", []))
    for lh in legend_handles:
        lh.set_alpha(1.0)


def save_single_plot(base, filename, data, title, xlabel, xlim, ylabel="Goodput  [Mb/s]"):
    if not data:
        print(f"[info] Skipping empty plot for {filename}")
        return

    fig, ax = plt.subplots(1, 1, figsize=(7.5, 4.2), constrained_layout=True)
    fig.patch.set_facecolor("white")
    plot_sub(ax, data, title, xlabel, xlim)
    ax.set_ylabel(ylabel, labelpad=4)

    out_png = base + filename
    fig.savefig(out_png, bbox_inches="tight", dpi=150)
    plt.close(fig)
    print(f"Saved: {out_png}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="scratch/burstyudp/",
                        help="Directory containing the CSV result files")
    args = parser.parse_args()
    base = args.base.rstrip("/") + "/"

    data_conn = load(base + "fig2_connections.csv", "n")
    data_bw   = load(base + "fig3_bandwidth.csv",   "bw")
    data_rtt  = load(base + "fig4_rtt.csv",          "rtt")

    save_single_plot(
        base,
        "fig2_connections.png",
        data_conn,
        "Fig. 2  Goodput vs Number of Connections",
        "Number of connections",
        xlim=(0, 31),
    )

    save_single_plot(
        base,
        "fig3_bandwidth.png",
        data_bw,
        "Fig. 3  Goodput vs Bottleneck Bandwidth",
        "Bottleneck bandwidth  [Mb/s]",
        xlim=(0, 160),
    )

    save_single_plot(
        base,
        "fig4_rtt.png",
        data_rtt,
        "Fig. 4  Goodput vs RTT",
        "RTT  [s]",
        xlim=(0, 1.05),
    )


if __name__ == "__main__":
    main()
