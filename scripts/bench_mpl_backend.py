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


def case_line_10M(ax, rng):
    x = np.linspace(0, 100, 10_000_000)
    ax.plot(x, np.sin(x) + 0.3 * np.sin(37 * x))
    ax.set_title("line 10M")


def case_scatter_200k(ax, rng):
    n = 200_000
    ax.scatter(rng.random(n), rng.random(n), s=4, c=rng.random(n),
               cmap="viridis", alpha=0.6, marker="o")
    ax.set_title("scatter 200k")


def case_scatter_1M(ax, rng):
    n = 1_000_000
    ax.scatter(rng.random(n), rng.random(n), s=1, c=rng.random(n),
               cmap="viridis", marker=".")
    ax.set_title("scatter 1M")


def case_markers_100k(ax, rng):
    n = 100_000
    ax.plot(rng.random(n), rng.random(n), "o", ms=3,
            markerfacecolor="tab:blue", markeredgecolor="none",
            linestyle="none")
    ax.set_title("markers 100k")


def case_quadmesh(ax, rng):
    n = 500
    y, x = np.mgrid[0:1:complex(n), 0:1:complex(n)]
    c = np.sin(x * 20) * np.cos(y * 20)
    ax.pcolormesh(x, y, c, shading="auto", cmap="viridis")
    ax.set_title("pcolormesh 500x500")


def case_quadmesh_2M(ax, rng):
    n = 1500
    y, x = np.mgrid[0:1:complex(n), 0:1:complex(n)]
    c = np.sin(x * 20) * np.cos(y * 20)
    ax.pcolormesh(x, y, c, shading="auto", cmap="viridis")
    ax.set_title("pcolormesh 1500x1500")


def case_gouraud_1k(ax, rng):
    n = 1000
    y, x = np.mgrid[0:1:complex(n), 0:1:complex(n)]
    c = np.sin(x * 20) * np.cos(y * 20)
    ax.pcolormesh(x, y, c, shading="gouraud", cmap="viridis")
    ax.set_title("gouraud 1000x1000")


def case_quadmesh_1M(ax, rng):
    n = 1000
    y, x = np.mgrid[0:1:complex(n), 0:1:complex(n)]
    c = np.sin(x * 20) * np.cos(y * 20)
    ax.pcolormesh(x, y, c, shading="auto", cmap="viridis")
    ax.set_title("pcolormesh 1000x1000")


def case_imshow_9M(ax, rng):
    img = rng.random((3000, 3000))
    ax.imshow(img, cmap="viridis", interpolation="bilinear")
    ax.set_title("imshow 3000x3000")


def case_imshow_none(ax, rng):
    img = rng.random((2048, 2048))
    ax.imshow(img, cmap="viridis", interpolation="none")
    ax.set_title("imshow 2048x2048 none")


def case_pcolormesh_2M(ax, rng):
    # Non-square meshgrid — exercises the swapped-axis image path.
    yy, xx = np.meshgrid(np.linspace(0, 1, 1200),
                         np.linspace(0, 1, 1600))
    ax.pcolormesh(xx, yy, np.sin(xx * 8) * np.cos(yy * 8),
                  cmap="viridis")
    ax.set_title("pcolormesh 1600x1200")


def case_fill_between_1M(ax, rng):
    x = np.linspace(0, 100, 1_000_000)
    y1 = np.sin(x) + 0.3 * np.sin(37 * x)
    y2 = y1 - 0.5 - 0.2 * np.sin(5 * x)
    ax.fill_between(x, y1, y2, alpha=0.7)
    ax.set_title("fill_between 1M")


def case_eventplot(ax, rng):
    ax.eventplot(rng.random((2000, 50)) * 100,
                 orientation="horizontal", linelengths=0.8)
    ax.set_title("eventplot 2000x50")


def case_eventplot_20k(ax, rng):
    ax.eventplot(rng.random((20_000, 8)) * 100,
                 orientation="horizontal", linelengths=0.8)
    ax.set_title("eventplot 20000x8")


def case_scatter_2M(ax, rng):
    n = 2_000_000
    ax.scatter(rng.random(n), rng.random(n), s=1, marker=".",
               c="tab:blue")
    ax.set_title("scatter 2M")


def case_scatter_sizes_200k(ax, rng):
    n = 200_000
    ax.scatter(rng.random(n), rng.random(n),
               s=rng.random(n) * 30, c=rng.random(n), cmap="viridis")
    ax.set_title("scatter 200k sized")


def case_scatter_sizes_1M(ax, rng):
    n = 1_000_000
    ax.scatter(rng.random(n), rng.random(n),
               s=rng.random(n) * 36 + 2, c=rng.random(n),
               cmap="plasma")
    ax.set_title("scatter 1M sized+colored")


def case_bar_50k(ax, rng):
    x = np.arange(50_000)
    ax.bar(x, rng.random(50_000))
    ax.set_title("bar 50k")


def case_imshow_16M_none(ax, rng):
    img = rng.random((4000, 4000))
    ax.imshow(img, cmap="viridis", interpolation="none")
    ax.set_title("imshow 4000x4000 none")


def case_pcolormesh_7M(ax, rng):
    y, x = np.mgrid[0:1:2500j, 0:1:2800j]
    ax.pcolormesh(x, y, np.sin(x * 20) * np.cos(y * 20), cmap="viridis")
    ax.set_title("pcolormesh 2800x2500")


CASES = [
    ("line_2k",             case_line2k,        dict()),
    ("line_1M",             case_line_1M,       dict()),
    ("line_10M",            case_line_10M,      dict()),
    ("line_10M_dpi300",     case_line_10M,      dict(dpi=300)),
    ("scatter_200k",        case_scatter_200k,  dict()),
    ("scatter_200k_dpi300", case_scatter_200k,  dict(dpi=300)),
    ("scatter_1M",          case_scatter_1M,    dict()),
    ("markers_100k",        case_markers_100k,  dict()),
    ("quadmesh_250k",       case_quadmesh,      dict()),
    ("quadmesh_2M_dpi200",  case_quadmesh_2M,   dict(dpi=200)),
    ("pcolormesh_2M_dpi300", case_pcolormesh_2M, dict(dpi=300)),
    ("quadmesh_1M_dpi400",  case_quadmesh_1M,   dict(dpi=400)),
    ("gouraud_1k_dpi150",   case_gouraud_1k,    dict(dpi=150)),
    ("fill_1M_dpi300",      case_fill_between_1M, dict(dpi=300)),
    ("imshow_9M_dpi200",    case_imshow_9M,     dict(dpi=200)),
    ("imshow_none_dpi300",  case_imshow_none,   dict(dpi=300)),
    ("fill_between_1M",     case_fill_between_1M, dict()),
    ("eventplot_2000",      case_eventplot,     dict()),
    ("eventplot_20k",       case_eventplot_20k, dict()),
    # ── extreme large-data: where the GPU/texture paths dominate ──
    ("scatter_2M_dpi400",   case_scatter_2M,    dict(dpi=400)),
    ("scatter_sz_200k",     case_scatter_sizes_200k, dict()),
    ("scatter_sz_1M_dpi400", case_scatter_sizes_1M, dict(dpi=400)),
    ("bar_50k",             case_bar_50k,       dict()),
    ("imshow_16M_none_dpi300", case_imshow_16M_none, dict(dpi=300)),
    ("imshow_16M_none_dpi400", case_imshow_16M_none, dict(dpi=400)),
    ("pcolormesh_7M_dpi400", case_pcolormesh_7M, dict(dpi=400)),
]


def run(backend, repeats=3, nmulti=50, outdir="/tmp"):
    plt = _use(backend)
    rng = np.random.default_rng(7)
    tag = "VP  " if backend == "vp" else "Agg"
    results = {}
    for name, fn, kw in CASES:
        times_draw, times_png = [], []
        for r in range(repeats):
            fig = plt.figure(figsize=(6.4, 4.8), dpi=kw.get("dpi", 100))
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

    # repeated-plot workload: same figure config, different data each
    # time (amortizes per-canvas setup across figures).
    t0 = time.perf_counter()
    for i in range(nmulti):
        fig = plt.figure(figsize=(6.4, 4.8), dpi=100)
        ax = fig.add_subplot(111)
        x = np.linspace(0, 10, 2000)
        for j in range(3):
            ax.plot(x, np.sin(x + j) + rng.normal(0, 0.05, x.size),
                    label=f"s{j}")
        ax.scatter(rng.random(200) * 10, rng.random(200), s=8,
                   alpha=0.5)
        ax.legend()
        fig.savefig(f"{outdir}/bench_multi.png")
        plt.close(fig)
    dt = time.perf_counter() - t0
    print(f"[{tag}] {nmulti}x line_2k (varying data) total {dt:6.1f} s "
          f"({dt / nmulti * 1e3:.1f} ms/plot)", flush=True)
    return results


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    if which in ("agg", "both"):
        run("agg")
    if which in ("vp", "both"):
        run("vp")
