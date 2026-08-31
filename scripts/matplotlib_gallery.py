#!/usr/bin/env python3
"""Generate matplotlib PNGs matching the VolcanoPlot gallery.

For each plot type implemented in VolcanoPlot, this script generates the
equivalent matplotlib PNG using the same data. The output directory is
gallery/matplotlib/ by default.

Usage:
    python3 scripts/matplotlib_gallery.py [output_dir]
"""

import sys
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")  # headless backend
import matplotlib.pyplot as plt
from matplotlib import cm
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401

# Use the same RNG seed as the C++ gallery (42) for reproducibility.
np.random.seed(42)

WIDTH, HEIGHT = 800, 600
DPI = 100


def save(fig, out_dir, name):
    path = os.path.join(out_dir, f"{name}.png")
    fig.savefig(path, dpi=DPI, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print(f"  wrote {path}")


def setup_ax(ax, title, xlabel="X", ylabel="Y"):
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)


def gen_scatter(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Scatter")
    x = np.random.randn(200)
    y = np.random.randn(200)
    ax.scatter(x, y, s=25, c="#1f77b4", label="data")
    ax.legend()
    save(fig, out_dir, "scatter")


def gen_line(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Line")
    x = np.linspace(0, 10, 200)
    ax.plot(x, np.sin(x), linewidth=2, c="#1f77b4", label="sin(x)")
    ax.legend()
    save(fig, out_dir, "line")


def gen_bar(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Bar", "Category", "Value")
    labels = ["A", "B", "C", "D", "E", "F", "G"]
    heights = [3, 7, 5, 8, 4, 6, 5]
    ax.bar(labels, heights, width=0.8)
    save(fig, out_dir, "bar")


def gen_grouped_bar(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Grouped Bar", "Category", "Value")
    groups = ["A", "B", "C", "D"]
    x = np.arange(len(groups))
    w = 0.25
    series = [
        [3, 5, 2, 4],
        [5, 3, 6, 2],
        [2, 4, 3, 5],
    ]
    for i, s in enumerate(series):
        ax.bar(x + (i - 1) * w, s, w, label=f"Series {i+1}")
    ax.set_xticks(x)
    ax.set_xticklabels(groups)
    ax.legend()
    save(fig, out_dir, "grouped_bar")


def gen_hist(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Histogram", "Value", "Count")
    data = np.random.randn(1000)
    ax.hist(data, bins=30, color="#1f77b4", alpha=0.5)
    save(fig, out_dir, "hist")


def gen_pie(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax.set_title("Pie")
    values = [30, 20, 25, 15, 10]
    labels = ["A", "B", "C", "D", "E"]
    ax.pie(values, labels=labels, autopct="%1.0f%%")
    save(fig, out_dir, "pie")


def gen_box(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Box Plot", "Group", "Value")
    data = [np.random.normal(g, 1.0 + g * 0.2, 100) for g in range(4)]
    ax.boxplot(data, labels=["G1", "G2", "G3", "G4"])
    save(fig, out_dir, "box")


def gen_violin(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Violin Plot", "Group", "Value")
    data = [np.random.normal(g * 0.5, 1.0, 150) for g in range(4)]
    ax.violinplot(data, showmeans=True)
    ax.set_xticks(range(1, 5))
    ax.set_xticklabels(["G1", "G2", "G3", "G4"])
    save(fig, out_dir, "violin")


def gen_stack(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Stack Plot", "X", "Y")
    x = np.linspace(0, 10, 50)
    ys = [np.sin(x + s) * 2 + 3 for s in range(3)]
    ax.stackplot(x, *ys, labels=["A", "B", "C"], colors=["#1f77b4", "#ff7f0e", "#2ca02c"])
    ax.legend()
    save(fig, out_dir, "stack")


def gen_stem(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Stem Plot", "X", "Y")
    x = np.arange(20)
    y = np.sin(x * 0.5)
    ax.stem(x, y)
    save(fig, out_dir, "stem")


def gen_step(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Step Plot", "X", "Y")
    x = np.linspace(0, 10, 30)
    y = np.exp(-x * 0.2)
    ax.step(x, y, where="mid")
    save(fig, out_dir, "step")


def gen_errorbar(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Errorbar", "X", "Y")
    x = np.linspace(0, 10, 15)
    y = np.sin(x) * 2
    ax.errorbar(x, y, yerr=0.3, fmt="o", color="#1f77b4", ecolor="black", capsize=3)
    save(fig, out_dir, "errorbar")


def gen_fill(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Fill", "X", "Y")
    x = np.linspace(0, 10, 100)
    y = np.sin(x) * np.exp(-x * 0.1)
    ax.fill(x, y, color="#1f77b4", alpha=0.5)
    save(fig, out_dir, "fill")


def gen_fill_between(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Fill Between", "X", "Y")
    x = np.linspace(0, 10, 100)
    y1 = np.sin(x) + 1
    y2 = np.sin(x) - 1
    ax.fill_between(x, y1, y2, color="#1f77b4", alpha=0.3)
    save(fig, out_dir, "fill_between")


def gen_broken_barh(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Broken Bar Horizontal", "X", "Y")
    ax.broken_barh([(0, 5), (7, 3)], (0, 1), facecolors="#1f77b4")
    ax.broken_barh([(2, 4), (8, 2)], (2, 1), facecolors="#2ca02c")
    save(fig, out_dir, "broken_barh")


def gen_heatmap(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax.set_title("Heatmap")
    x = np.linspace(0, 10, 30)
    y = np.linspace(0, 10, 20)
    X, Y = np.meshgrid(x, y)
    Z = np.sin(X * 0.5) * np.cos(Y * 0.5)
    im = ax.imshow(Z, extent=[0, 10, 0, 10], origin="lower", cmap="viridis", aspect="auto")
    plt.colorbar(im, ax=ax)
    save(fig, out_dir, "heatmap")


def gen_hist2d(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax.set_title("2D Histogram")
    x = np.random.randn(5000) * 2
    y = np.random.randn(5000) * 2
    h = ax.hist2d(x, y, bins=[40, 30], cmap="viridis")
    plt.colorbar(h[3], ax=ax)
    save(fig, out_dir, "hist2d")


def gen_hexbin(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax.set_title("Hexbin")
    x = np.random.randn(3000) * 2
    y = np.random.randn(3000) * 2
    hb = ax.hexbin(x, y, gridsize=25, cmap="viridis")
    plt.colorbar(hb, ax=ax)
    save(fig, out_dir, "hexbin")


def gen_contour(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Contour")
    x = np.linspace(-3, 3, 50)
    y = np.linspace(-3, 3, 50)
    X, Y = np.meshgrid(x, y)
    Z = np.sin(X) * np.cos(Y)
    ax.contour(X, Y, Z, levels=10, colors="black")
    save(fig, out_dir, "contour")


def gen_contourf(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Filled Contour")
    x = np.linspace(-3, 3, 50)
    y = np.linspace(-3, 3, 50)
    X, Y = np.meshgrid(x, y)
    Z = -(X**2 + Y**2)
    cf = ax.contourf(X, Y, Z, levels=20, cmap="viridis")
    plt.colorbar(cf, ax=ax)
    save(fig, out_dir, "contourf")


def gen_kde(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax.set_title("KDE")
    from scipy.stats import gaussian_kde
    x = np.random.randn(500)
    y = np.random.randn(500)
    try:
        kde = gaussian_kde(np.vstack([x, y]))
        xi, yi = np.mgrid[-4:4:100j, -4:4:100j]
        zi = kde(np.vstack([xi.ravel(), yi.ravel()])).reshape(xi.shape)
        im = ax.pcolormesh(xi, yi, zi, cmap="viridis", shading="auto")
        plt.colorbar(im, ax=ax)
    except ImportError:
        ax.hist2d(x, y, bins=30, cmap="viridis")
    save(fig, out_dir, "kde")


def gen_ecdf(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "ECDF", "Value", "CDF")
    data = np.sort(np.random.randn(500))
    y = np.arange(1, len(data) + 1) / len(data)
    ax.fill_between(data, 0, y, color="#1f77b4", alpha=0.3)
    ax.plot(data, y, color="#1f77b4", linewidth=1.5)
    save(fig, out_dir, "ecdf")


def gen_surface(out_dir):
    fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title("3D Surface")
    x = np.linspace(-5, 5, 40)
    y = np.linspace(-5, 5, 40)
    X, Y = np.meshgrid(x, y)
    Z = np.sin(X * 0.5) * np.cos(Y * 0.5)
    ax.plot_surface(X, Y, Z, cmap="viridis")
    save(fig, out_dir, "surface")


def gen_wireframe(out_dir):
    fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title("3D Wireframe")
    x = np.linspace(-5, 5, 20)
    y = np.linspace(-5, 5, 20)
    X, Y = np.meshgrid(x, y)
    Z = np.sin(X * 0.5) * np.cos(Y * 0.5)
    ax.plot_wireframe(X, Y, Z, color="#1f77b4")
    save(fig, out_dir, "wireframe")


def gen_scatter3d(out_dir):
    fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title("3D Scatter")
    x = np.random.randn(100) * 3
    y = np.random.randn(100) * 3
    z = np.random.randn(100) * 3
    ax.scatter(x, y, z, c="#1f77b4", s=25)
    save(fig, out_dir, "scatter3d")


def gen_plot3d(out_dir):
    fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title("3D Line")
    t = np.linspace(0, 4 * np.pi, 200)
    x = np.cos(t) * 3
    y = np.sin(t) * 3
    z = t * 0.5
    ax.plot(x, y, z, color="#1f77b4", linewidth=2)
    save(fig, out_dir, "plot3d")


def gen_bar3d(out_dir):
    fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title("3D Bar")
    xs, ys, dz = [], [], []
    for i in range(4):
        for j in range(4):
            xs.append(i + 0.15)
            ys.append(j + 0.15)
            dz.append(i + j + 1)
    ax.bar3d(xs, ys, [0] * 16, [0.7] * 16, [0.7] * 16, dz, shade=True)
    save(fig, out_dir, "bar3d")


def gen_mexican_hat(out_dir):
    fig = plt.figure(figsize=(WIDTH / DPI, HEIGHT / DPI))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_title("Mexican Hat Wavelet")
    x = np.linspace(-5, 5, 40)
    y = np.linspace(-5, 5, 40)
    X, Y = np.meshgrid(x, y)
    R2 = X**2 + Y**2
    sigma = 1.0
    Z = (2 - R2 / sigma**2) * np.exp(-R2 / (2 * sigma**2))
    ax.plot_surface(X, Y, Z, cmap="viridis")
    save(fig, out_dir, "mexican_hat")


def gen_chirp(out_dir):
    fig, ax = plt.subplots(figsize=(WIDTH / DPI, HEIGHT / DPI))
    setup_ax(ax, "Chirp Signal", "Time", "Amplitude")
    t = np.linspace(0, 1, 2000)
    f0, f1, duration = 1.0, 50.0, 1.0
    phase = 2 * np.pi * (f0 * t + 0.5 * (f1 - f0) * t**2 / duration)
    ax.plot(t, np.sin(phase), linewidth=1.5, color="#1f77b4")
    save(fig, out_dir, "chirp")


GENERATORS = [
    gen_scatter, gen_line, gen_bar, gen_grouped_bar, gen_hist, gen_pie,
    gen_box, gen_violin, gen_stack, gen_stem, gen_step, gen_errorbar,
    gen_fill, gen_fill_between, gen_broken_barh, gen_heatmap, gen_hist2d,
    gen_hexbin, gen_contour, gen_contourf, gen_kde, gen_ecdf,
    gen_surface, gen_wireframe, gen_scatter3d, gen_plot3d, gen_bar3d,
    gen_mexican_hat, gen_chirp,
]


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir", nargs="?", default="gallery/matplotlib")
    parser.add_argument("--filter", default=None,
                        help="Comma-separated list of plot names to generate (e.g. 'fill,line')")
    args = parser.parse_args()
    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    # Map generator function names (e.g. "gen_fill" → "fill").
    name_map = {}
    for gen in GENERATORS:
        name = gen.__name__.replace("gen_", "")
        name_map[name] = gen

    if args.filter:
        wanted = [n.strip() for n in args.filter.split(",") if n.strip()]
        gens = [name_map[n] for n in wanted if n in name_map]
        missing = [n for n in wanted if n not in name_map]
        if missing:
            print(f"WARNING: unknown plot name(s): {missing}", file=sys.stderr)
            print(f"  Available: {sorted(name_map.keys())}", file=sys.stderr)
    else:
        gens = GENERATORS

    print(f"Generating matplotlib gallery in {out_dir}/")
    for gen in gens:
        gen(out_dir)
    print(f"Done. Generated {len(gens)} plots.")


if __name__ == "__main__":
    main()
