#!/usr/bin/env python3
"""Benchmark: native VolcanoPlot API vs matplotlib (Agg + VP-backend).

Runs the same logical plots three ways:
  native — volcanoplot's own pyplot-like API (no matplotlib involved)
  agg    — matplotlib with the Agg backend
  vp     — matplotlib with module://volcanoplot.mpl_backend

    python3 scripts/bench_native_vs_mpl.py native
    python3 scripts/bench_native_vs_mpl.py mpl      # mpl Agg
    PYTHONPATH=build/python python3 scripts/bench_native_vs_mpl.py vp
    python3 scripts/bench_native_vs_mpl.py          # native + agg (+vp if built)
"""
import sys
import time

import numpy as np


def make_figure(kind, dpi=100):
    """Return (fig, ax, plot_fn_namespace)."""
    if kind == "native":
        import volcanoplot as vp
        fig = vp.figure(figsize=(6.4, 4.8), dpi=dpi)
        return fig, vp.gca(), vp
    import matplotlib
    matplotlib.use("module://volcanoplot.mpl_backend"
                   if kind == "vp" else "Agg")
    import matplotlib.pyplot as plt
    fig = plt.figure(figsize=(6.4, 4.8), dpi=dpi)
    return fig, fig.add_subplot(111), plt


def draw_fig(fig, kind, path):
    fig.savefig(path)


def close_fig(fig, ns, kind):
    if kind == "native":
        ns.close("all" if True else None)
    else:
        ns.close(fig)


def case_line(n):
    def fn(ax):
        x = np.linspace(0, 100, n)
        ax.plot(x, np.sin(x) + 0.3 * np.sin(37 * x))
    return fn, f"line_{n // 1_000_000}M"


def case_multiline(n, k):
    def fn(ax):
        x = np.linspace(0, 100, n)
        for i in range(k):
            ax.plot(x, np.sin(x * (1 + 0.01 * i) + i))
    return fn, f"{k}xline_{n // 1000}k"


def case_scatter(n):
    def fn(ax):
        rng = np.random.default_rng(1)
        ax.scatter(rng.random(n), rng.random(n), s=4)
    return fn, f"scatter_{n // 1000}k"


def case_hist(n):
    def fn(ax):
        rng = np.random.default_rng(2)
        ax.hist(rng.standard_normal(n), bins=200)
    return fn, f"hist_{n // 1_000_000}M"


def case_fill(n):
    def fn(ax):
        x = np.linspace(0, 100, n)
        y = np.sin(x)
        ax.fill_between(x, y - 0.5, y + 0.5)
    return fn, f"fill_{n // 1_000_000}M"


CASES = [
    case_line(2_000),
    case_line(1_000_000),
    case_line(10_000_000),
    case_multiline(100_000, 20),
    case_scatter(200_000),
    case_hist(10_000_000),
    case_fill(1_000_000),
]


def run(kind, repeats=3, nmulti=50):
    print(f"=== {kind} ===", flush=True)
    for fn, name in CASES:
        ts = []
        for _ in range(repeats):
            fig, ax, _ = make_figure(kind)
            fn(ax)
            t0 = time.perf_counter()
            draw_fig(fig, kind, "/tmp/bench_native.png")
            ts.append(time.perf_counter() - t0)
            try:
                if kind == "native":
                    import volcanoplot as vp
                    vp.close("all")
                else:
                    import matplotlib.pyplot as plt
                    plt.close(fig)
            except Exception:
                pass
        print(f"{name:<16} savefig {np.median(ts) * 1e3:9.1f} ms "
              f"(min {min(ts) * 1e3:.1f})", flush=True)

    # same-config/varying-data multi-plot workload
    rng = np.random.default_rng(3)
    t0 = time.perf_counter()
    for _ in range(nmulti):
        fig, ax, _ = make_figure(kind)
        x = np.linspace(0, 10, 2000)
        for j in range(3):
            ax.plot(x, np.sin(x + j) + rng.normal(0, 0.05, x.size))
        draw_fig(fig, kind, "/tmp/bench_native_multi.png")
        try:
            if kind == "native":
                import volcanoplot as vp
                vp.close("all")
            else:
                import matplotlib.pyplot as plt
                plt.close(fig)
        except Exception:
            pass
    dt = time.perf_counter() - t0
    print(f"{nmulti}x 3xline_2k total {dt:6.1f} s "
          f"({dt / nmulti * 1e3:.1f} ms/plot)", flush=True)


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("native", "all"):
        run("native")
    if which in ("mpl", "agg", "all"):
        run("agg")
    if which in ("vp", "all"):
        try:
            import volcanoplot  # noqa
            run("vp")
        except ImportError:
            print("volcanoplot not importable; skipping vp")
