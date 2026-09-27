#!/usr/bin/env python3
"""Benchmark: matplotlib Agg vs VolcanoPlot mpl-backend (headless).

Runs identical figures through both backends and reports per-plot
draw() and savefig() timings.

    # VolcanoPlot backend (built extension):
    PYTHONPATH=build/python python3 scripts/bench_mpl_backend.py vp

    # Agg baseline:
    python3 scripts/bench_mpl_backend.py agg

    # Both:
    python3 scripts/bench_mpl_backend.py
"""
import sys
import time

import numpy as np


def _use(backend):
    import matplotlib
    if backend == "vp":
        matplotlib.use("module://volcanoplot.mpl_backend")
    else:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    return plt


def case_line2k(ax, rng):
    x = np.linspace(0, 10, 2000)
    for i in range(3):
        ax.plot(x, np.sin(x + i) + rng.normal(0, 0.05, x.size),
                label=f"s{i}")
    ax.scatter(rng.random(200) * 10, rng.random(200), s=8, alpha=0.5)
    ax.legend()
    ax.set_title("line 2k + scatter 200")


def case_line_1M(ax, rng):
    x = np.linspace(0, 100, 1_000_000)
    ax.plot(x, np.sin(x) + 0.3 * np.sin(37 * x))
    ax.set_title("line 1M")


def case_scatter_200k(ax, rng):
    n = 200_000
    ax.scatter(rng.random(n), rng.random(n), s=4, c=rng.random(n),
               cmap="viridis", alpha=0.6, marker="o")
    ax.set_title("scatter 200k")


def case_quadmesh(ax, rng):
    n = 500
    y, x = np.mgrid[0:1:complex(n), 0:1:complex(n)]
    c = np.sin(x * 20) * np.cos(y * 20)
    ax.pcolormesh(x, y, c, shading="auto", cmap="viridis")
    ax.set_title("pcolormesh 500x500")


def case_markers_100k(ax, rng):
    n = 100_000
    ax.plot(rng.random(n), rng.random(n), "o", ms=3,
            markerfacecolor="tab:blue", markeredgecolor="none",
            linestyle="none")
    ax.set_title("markers 100k")


CASES = [
    ("line_2k", case_line2k),
    ("line_1M", case_line_1M),
    ("scatter_200k", case_scatter_200k),
    ("markers_100k", case_markers_100k),
    ("quadmesh_250k", case_quadmesh),
]


def run(backend, repeats=3, nmulti=50, outdir="/tmp"):
    plt = _use(backend)
    rng = np.random.default_rng(7)
    tag = "VP  " if backend == "vp" else "Agg"
    results = {}
    for name, fn in CASES:
        times_draw, times_png = [], []
        for r in range(repeats):
            fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
            ax = fig.add_subplot(111)
            fn(ax, rng)
            t0 = time.perf_counter()
            fig.canvas.draw()
            t1 = time.perf_counter()
            fig.savefig(f"{outdir}/bench_{name}.png")
            t2 = time.perf_counter()
            plt.close(fig)
            times_draw.append(t1 - t0)
            times_png.append(t2 - t1)
        med = np.median
        results[name] = med(times_draw)
        print(f"[{tag}] {name:<14} draw {med(times_draw)*1e3:8.1f} ms "
              f"png {med(times_png)*1e3:8.1f} ms   "
              f"(min draw {min(times_draw)*1e3:.1f})", flush=True)

    # repeated-plot workload on the small case
    t0 = time.perf_counter()
    for i in range(nmulti):
        fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
        ax = fig.add_subplot(111)
        case_line2k(ax, rng)
        fig.savefig(f"{outdir}/bench_multi.png")
        plt.close(fig)
    dt = time.perf_counter() - t0
    print(f"[{tag}] {nmulti}x line_2k total {dt:6.1f} s "
          f"({dt / nmulti * 1e3:.1f} ms/plot)", flush=True)
    return results


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    if which in ("agg", "both"):
        run("agg")
    if which in ("vp", "both"):
        run("vp")
