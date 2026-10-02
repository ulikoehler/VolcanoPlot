#!/usr/bin/env python3
"""Three-way benchmark matrix: mpl-Agg vs mpl+VolcanoPlot vs native C++ VP.

Every case builds the *same logical plot* — same data, same figure size
(6.4x4.8in), same dpi — on all three stacks:

    agg    stock matplotlib, Agg backend          (python3 ... agg)
    vp     matplotlib frontend + VP backend       (PYTHONPATH=build/python ... vp)
    cpp    raw C++ API, zero Python               (built example binary)

Vulkan initialization is measured separately and never included in the
per-case metric — the numbers represent steady-state ("the user wants to
make 100 plots"), so each stack gets a global warm-up (recorded as the
"_init" pseudo-case) and each case gets one untimed warm-up rep before
the timed reps (first-use pipeline compilation is excluded).

Usage (each stack runs in its own process — the backend is
process-global):

    python3 scripts/bench_matrix.py all [--outdir gallery/benchmark]

which is equivalent to:

    python3 scripts/bench_matrix.py dump
    MPLBACKEND=Agg python3 scripts/bench_matrix.py agg
    PYTHONPATH=build/python python3 scripts/bench_matrix.py vp
    python3 scripts/bench_matrix.py cpp
    python3 scripts/bench_matrix.py report

Outputs (in --outdir, default gallery/benchmark/):

    <case>_<stack>.png         rendered output for visual comparison
    data/<fn>/*.npy            identical input arrays shared with the
                               C++ bench (data manifest: data/manifest.tsv)
    bench_matrix_<stack>.json  per-case median timings
    benchmark_report.md        merged report with per-case images
"""
import argparse
import json
import os
import subprocess
import sys
import time

import numpy as np

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUTDIR = os.path.join(ROOT_DIR, "gallery", "benchmark")
CPP_BENCH = os.path.join(ROOT_DIR, "build", "examples", "example_bench_matrix")

STACKS = ("agg", "vp", "cpp")          # stacks 'all' runs
REPORT_ORDER = ("agg", "vp", "cpp", "native")  # any json found is reported

# -------------------------------------------------------------------
# cases: (name, category, build(ax), figure-kwargs)
# build() receives an Axes-like object; must be API-compatible across
# matplotlib Axes and volcanoplot Axes. Each build is also replayed
# against a recording stub to dump identical input data for the C++
# bench — keep random data generation inside build() so the recorded
# arrays are exactly what was plotted.
# -------------------------------------------------------------------


def _rng(seed=7):
    return np.random.default_rng(seed)


# ── small: fixed overhead dominates ──────────────────────────────────

def c_line_100(ax):
    x = np.linspace(0, 10, 100)
    ax.plot(x, np.sin(x))
    ax.set_title("line 100")


def c_line_2k(ax):
    r = _rng(1)
    x = np.linspace(0, 10, 2000)
    for i in range(3):
        ax.plot(x, np.sin(x + i) + r.normal(0, 0.05, x.size),
                label=f"s{i}")
    ax.scatter(r.random(200) * 10, r.random(200), s=8, alpha=0.5)
    ax.legend()
    ax.set_title("line 2k + scatter")


def c_scatter_100(ax):
    r = _rng(2)
    ax.scatter(r.random(100), r.random(100), s=30, c=r.random(100),
               cmap="viridis")


def c_bar_10(ax):
    ax.bar(np.arange(10), _rng(3).random(10))


def c_errorbar_50(ax):
    r = _rng(4)
    x = np.linspace(0, 1, 50)
    ax.errorbar(x, np.sin(6 * x), yerr=0.1 + r.random(50) * 0.1,
                fmt="o-", capsize=3)


def c_boxplot(ax):
    r = _rng(5)
    ax.boxplot([r.standard_normal(500) * (i + 1)
                for i in range(6)])


def c_text_50(ax):
    for i in range(50):
        ax.text(0.02 + (i % 10) * 0.09, 0.05 + (i // 10) * 0.18,
                f"label {i}", fontsize=7)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)


def c_pie(ax):
    ax.pie([15, 30, 45, 10], labels=["a", "b", "c", "d"],
           autopct="%1.0f%%")


def c_multi_line2k(ax):
    # The 50-figure amortized workload's per-figure content.
    x = np.linspace(0, 10, 2000)
    r = _rng(3)
    for j in range(3):
        ax.plot(x, np.sin(x + j) + r.normal(0, 0.05, x.size))


# ── medium ────────────────────────────────────────────────────────────

def c_line_10k(ax):
    x = np.linspace(0, 10, 10_000)
    ax.plot(x, np.sin(x) * np.exp(-x / 10))


def c_line_100k(ax):
    x = np.linspace(0, 100, 100_000)
    ax.plot(x, np.sin(x) + 0.3 * np.sin(37 * x))


def c_multiline_20x100k(ax):
    x = np.linspace(0, 100, 100_000)
    for i in range(20):
        ax.plot(x, np.sin(x * (1 + 0.01 * i) + i))


def c_scatter_10k(ax):
    r = _rng(6)
    ax.scatter(r.random(10_000), r.random(10_000), s=10,
               c=r.random(10_000), cmap="viridis", alpha=0.7)


def c_scatter_50k(ax):
    r = _rng(6)
    ax.scatter(r.random(50_000), r.random(50_000), s=6,
               c=r.random(50_000), cmap="viridis", alpha=0.7)


def c_hist_100k(ax):
    ax.hist(_rng(7).standard_normal(100_000), bins=100)


def c_bar_1k(ax):
    ax.bar(np.arange(1000), _rng(8).random(1000))


def c_bar_5k(ax):
    ax.bar(np.arange(5000), _rng(8).random(5000))


def c_contourf_100(ax):
    y, x = np.mgrid[0:1:100j, 0:1:100j]
    ax.contourf(x, y, np.sin(x * 12) * np.cos(y * 12), levels=15,
                cmap="viridis")


def c_quiver_30x30(ax):
    y, x = np.mgrid[0:1:30j, 0:1:30j]
    ax.quiver(x, y, -y, x)


def c_step_50k(ax):
    x = np.linspace(0, 10, 50_000)
    ax.step(x, np.sin(5 * x), where="mid")


def c_fill_100k(ax):
    x = np.linspace(0, 100, 100_000)
    ax.fill_between(x, np.sin(x) - 0.5, np.sin(x) + 0.5, alpha=0.6)


def c_eventplot_2k(ax):
    ax.eventplot(_rng(9).random((2000, 50)) * 100,
                 orientation="horizontal", linelengths=0.8)


def c_stackplot(ax):
    x = np.linspace(0, 10, 1000)
    r = _rng(10)
    ax.stackplot(x, r.random((5, 1000)).cumsum(0))


def c_errorbar_5k(ax):
    r = _rng(11)
    x = np.linspace(0, 1, 5000)
    ax.errorbar(x, np.sin(6 * x), yerr=0.1 + r.random(5000) * 0.1,
                capsize=0)


def c_violin(ax):
    r = _rng(12)
    ax.violinplot([r.standard_normal(2000) * (i + 1)
                   for i in range(8)])


def c_stem_2k(ax):
    x = np.linspace(0, 4 * np.pi, 2000)
    ax.stem(x, np.sin(x))


# ── large ─────────────────────────────────────────────────────────────

def c_line_1M(ax):
    x = np.linspace(0, 100, 1_000_000)
    ax.plot(x, np.sin(x) + 0.3 * np.sin(37 * x))


def c_line_10M(ax):
    x = np.linspace(0, 100, 10_000_000)
    ax.plot(x, np.sin(x) + 0.3 * np.sin(37 * x))


def c_scatter_200k(ax):
    r = _rng(13)
    ax.scatter(r.random(200_000), r.random(200_000), s=4,
               c=r.random(200_000), cmap="viridis", alpha=0.6)


def c_scatter_1M(ax):
    r = _rng(13)
    ax.scatter(r.random(1_000_000), r.random(1_000_000), s=1,
               c=r.random(1_000_000), cmap="viridis", marker=".")


def c_scatter_2M(ax):
    r = _rng(13)
    ax.scatter(r.random(2_000_000), r.random(2_000_000), s=1,
               marker=".", c="tab:blue")


def c_scatter_sz_200k(ax):
    r = _rng(14)
    ax.scatter(r.random(200_000), r.random(200_000),
               s=r.random(200_000) * 30, c=r.random(200_000),
               cmap="viridis")


def c_scatter_sz_1M(ax):
    r = _rng(14)
    ax.scatter(r.random(1_000_000), r.random(1_000_000),
               s=r.random(1_000_000) * 36 + 2, c=r.random(1_000_000),
               cmap="plasma")


def c_markers_100k(ax):
    r = _rng(15)
    ax.plot(r.random(100_000), r.random(100_000), "o", markersize=3)


def c_hist_10M(ax):
    ax.hist(_rng(16).standard_normal(10_000_000), bins=200)


def c_fill_1M(ax):
    x = np.linspace(0, 100, 1_000_000)
    y = np.sin(x) + 0.3 * np.sin(37 * x)
    ax.fill_between(x, y - 0.5 - 0.2 * np.sin(5 * x), y, alpha=0.7)


def c_bar_50k(ax):
    ax.bar(np.arange(50_000), _rng(17).random(50_000))


def c_eventplot_20k(ax):
    ax.eventplot(_rng(18).random((20_000, 8)) * 100,
                 orientation="horizontal", linelengths=0.8)


def c_pcolormesh_2M(ax):
    # 1D edge vectors — valid on mpl AND the native binding.
    x = np.linspace(0, 1, 1601)
    y = np.linspace(0, 1, 1201)
    c = np.sin(x[:-1][None, :] * 8) * np.cos(y[:-1][:, None] * 8)
    ax.pcolormesh(x, y, c, cmap="viridis")


def c_pcolormesh_7M(ax):
    x = np.linspace(0, 1, 2801)
    y = np.linspace(0, 1, 2501)
    c = np.sin(x[:-1][None, :] * 20) * np.cos(y[:-1][:, None] * 20)
    ax.pcolormesh(x, y, c, cmap="viridis")


def c_quadmesh_1M(ax):
    x = np.linspace(0, 1, 1001)
    y = np.linspace(0, 1, 1001)
    c = np.sin(x[:-1][:, None] * 20) * np.cos(y[:-1][None, :] * 20)
    ax.pcolormesh(x, y, c, shading="auto", cmap="viridis")


def c_imshow_4M_none(ax):
    ax.imshow(_rng(19).random((2048, 2048)), cmap="viridis",
              interpolation="none")


def c_imshow_16M_none(ax):
    ax.imshow(_rng(19).random((4000, 4000)), cmap="viridis",
              interpolation="none")


def c_imshow_9M_bilinear(ax):
    ax.imshow(_rng(19).random((3000, 3000)), cmap="viridis",
              interpolation="bilinear")


def c_contourf_400(ax):
    y, x = np.mgrid[0:1:400j, 0:1:400j]
    ax.contourf(x, y, np.sin(x * 12) * np.cos(y * 12), levels=20,
                cmap="viridis")


CASES = [
    # name                  fn                 figkw    category
    ("line_100",            c_line_100,        {},               "small"),
    ("line_2k",             c_line_2k,         {},               "small"),
    ("scatter_100",         c_scatter_100,     {},               "small"),
    ("bar_10",              c_bar_10,          {},               "small"),
    ("errorbar_50",         c_errorbar_50,     {},               "small"),
    ("boxplot_6",           c_boxplot,         {},               "small"),
    ("text_50",             c_text_50,         {},               "small"),
    ("pie_4",               c_pie,             {},               "small"),
    ("line_10k",            c_line_10k,        {},               "medium"),
    ("line_100k",           c_line_100k,       {},               "medium"),
    ("multiline_20x100k",   c_multiline_20x100k, {},             "medium"),
    ("scatter_10k",         c_scatter_10k,     {},               "medium"),
    ("scatter_50k",         c_scatter_50k,     {},               "medium"),
    ("hist_100k",           c_hist_100k,       {},               "medium"),
    ("bar_1k",              c_bar_1k,          {},               "medium"),
    ("bar_5k",              c_bar_5k,          {},               "medium"),
    ("contourf_100",        c_contourf_100,    {},               "medium"),
    ("quiver_30x30",        c_quiver_30x30,    {},               "medium"),
    ("step_50k",            c_step_50k,        {},               "medium"),
    ("fill_100k",           c_fill_100k,       {},               "medium"),
    ("eventplot_2k",        c_eventplot_2k,    {},               "medium"),
    ("stackplot_5x1k",      c_stackplot,       {},               "medium"),
    ("errorbar_5k",         c_errorbar_5k,     {},               "medium"),
    ("violin_8x2k",         c_violin,          {},               "medium"),
    ("stem_2k",             c_stem_2k,         {},               "medium"),
    ("line_1M",             c_line_1M,         {},               "large"),
    ("line_10M",            c_line_10M,        {},               "large"),
    ("line_10M_dpi300",     c_line_10M,        {"dpi": 300},     "large"),
    ("scatter_200k",        c_scatter_200k,    {},               "large"),
    ("scatter_200k_dpi300", c_scatter_200k,    {"dpi": 300},     "large"),
    ("scatter_1M",          c_scatter_1M,      {},               "large"),
    ("scatter_2M_dpi400",   c_scatter_2M,      {"dpi": 400},     "large"),
    ("scatter_sz_200k",     c_scatter_sz_200k, {},               "large"),
    ("scatter_sz_1M_dpi400", c_scatter_sz_1M,  {"dpi": 400},     "large"),
    ("markers_100k",        c_markers_100k,    {},               "large"),
    ("hist_10M",            c_hist_10M,        {},               "large"),
    ("fill_1M",             c_fill_1M,         {},               "large"),
    ("fill_1M_dpi300",      c_fill_1M,         {"dpi": 300},     "large"),
    ("bar_50k",             c_bar_50k,         {},               "large"),
    ("eventplot_20k",       c_eventplot_20k,   {},               "large"),
    ("pcolormesh_2M",       c_pcolormesh_2M,   {},               "large"),
    ("pcolormesh_2M_dpi300", c_pcolormesh_2M,  {"dpi": 300},     "large"),
    ("pcolormesh_7M_dpi400", c_pcolormesh_7M,  {"dpi": 400},     "large"),
    ("quadmesh_1M_dpi400",  c_quadmesh_1M,     {"dpi": 400},     "large"),
    ("imshow_4M_none_dpi300", c_imshow_4M_none, {"dpi": 300},    "large"),
    ("imshow_16M_none_dpi400", c_imshow_16M_none, {"dpi": 400},  "large"),
    ("imshow_9M_bilinear",  c_imshow_9M_bilinear, {"dpi": 200},  "large"),
    ("contourf_400",        c_contourf_400,    {},               "large"),
]

NMULTI = 50


def _new_fig_ax(stack, dpi):
    if stack == "native":
        import volcanoplot as vp
        fig = vp.figure(figsize=(6.4, 4.8), dpi=dpi)
        return fig, vp.gca()
    import matplotlib.pyplot as plt
    fig = plt.figure(figsize=(6.4, 4.8), dpi=dpi)
    return fig, fig.add_subplot(111)


def _close(stack, fig):
    if stack == "native":
        import volcanoplot as vp
        vp.close("all")
    else:
        import matplotlib.pyplot as plt
        plt.close(fig)


def _warmup(stack, outdir):
    """One mixed-primitive figure: brings up the Vulkan instance/device,
    font atlas and the common render pipelines. Its savefig wall time is
    reported as the per-stack "_init" metric and never added to a case."""
    fig, ax = _new_fig_ax(stack, 100)
    x = np.linspace(0, 1, 100)
    r = _rng(0)
    ax.plot(x, x)
    ax.scatter(r.random(20), r.random(20))
    ax.bar(np.arange(4), [1.0, 2.0, 0.5, 1.5])
    ax.imshow(r.random((8, 8)), cmap="viridis")
    ax.text(0.5, 0.5, "warmup")
    t0 = time.perf_counter()
    fig.savefig(os.path.join(outdir, "_warmup.png"))
    dt = time.perf_counter() - t0
    _close(stack, fig)
    return dt * 1e3


def run(stack, outdir, repeats=3):
    os.makedirs(outdir, exist_ok=True)
    if stack == "native":
        import volcanoplot as vp  # noqa: F401
    else:
        import matplotlib
        matplotlib.use("module://volcanoplot.mpl_backend"
                       if stack == "vp" else "Agg")
        import matplotlib.pyplot as plt  # noqa: F401

    out = {"_init": {"cat": "meta", "ms": _warmup(stack, outdir),
                     "min_ms": None, "err": None,
                     "note": "vulkan/pipeline init — excluded from all"
                             " case metrics"}}
    print(f"[{stack:6}] {'_init':<22} {out['_init']['ms']:9.1f} ms",
          flush=True)

    for name, fn, kw, cat in CASES:
        ts = []
        err = None
        # rep 0 is an untimed warm-up (pipeline compile, font glyphs, …)
        for rep in range(repeats + 1):
            fig, ax = _new_fig_ax(stack, kw.get("dpi", 100))
            try:
                fn(ax)
            except Exception as e:  # unsupported in native API etc.
                err = f"{type(e).__name__}: {e}"
                _close(stack, fig)
                break
            t0 = time.perf_counter()
            try:
                fig.savefig(os.path.join(outdir, f"{name}_{stack}.png"))
            except Exception as e:
                err = f"{type(e).__name__}: {e}"
                _close(stack, fig)
                break
            if rep > 0:
                ts.append(time.perf_counter() - t0)
            _close(stack, fig)
        out[name] = {"cat": cat,
                     "ms": (float(np.median(ts)) * 1e3) if ts else None,
                     "min_ms": (min(ts) * 1e3) if ts else None,
                     "err": err}
        print(f"[{stack:6}] {name:<22} "
              f"{out[name]['ms'] if out[name]['ms'] else 'ERR':>9} "
              f"{'ms' if ts else err or ''}", flush=True)

    # same-config multi-figure workload (amortized setup)
    t0 = time.perf_counter()
    for _ in range(NMULTI):
        fig, ax = _new_fig_ax(stack, 100)
        c_multi_line2k(ax)
        fig.savefig(os.path.join(outdir, f"multi_50x_line2k_{stack}.png"))
        _close(stack, fig)
    dt = time.perf_counter() - t0
    out["multi_50x_line2k"] = {"cat": "multi", "ms": dt / NMULTI * 1e3,
                               "min_ms": dt / NMULTI * 1e3, "err": None}
    print(f"[{stack:6}] multi_50x_line2k      {dt / NMULTI * 1e3:9.1f} "
          f"ms/plot", flush=True)

    with open(os.path.join(outdir, f"bench_matrix_{stack}.json"), "w") as f:
        json.dump(out, f, indent=1)
    return out


# -------------------------------------------------------------------
# data dump — identical inputs for the C++ bench
# -------------------------------------------------------------------

class _RecordingAx:
    """Stub Axes: swallows every method call and dumps each array-like
    argument as <fn_name>/<method><call#>.<pos|kwarg>.npy so the C++
    bench can replay byte-identical inputs."""

    def __init__(self, datadir):
        os.makedirs(datadir, exist_ok=True)
        self._dir = datadir
        self._calls = {}

    def __getattr__(self, method):
        def rec(*args, **kw):
            k = self._calls.get(method, 0)
            self._calls[method] = k + 1
            for i, a in enumerate(args):
                self._dump(f"{method}{k}.{i}", a)
            for key, a in kw.items():
                self._dump(f"{method}{k}.{key}", a)
        return rec

    def _dump(self, tag, v):
        try:
            a = np.asarray(v)
        except Exception:
            return
        if a.ndim == 0 or a.dtype.kind not in "fiub":
            return
        np.save(os.path.join(self._dir, f"{tag}.npy"), a)


def dump_data(outdir):
    """Write data/<fn>/*.npy + manifest.tsv (case<TAB>dpi<TAB>fn<TAB>cat)."""
    data_root = os.path.join(outdir, "data")
    os.makedirs(data_root, exist_ok=True)
    manifest = []
    seen = set()
    for name, fn, kw, cat in CASES + [("multi_50x_line2k",
                                      c_multi_line2k, {}, "multi")]:
        dataname = fn.__name__[2:]  # c_line_10M -> line_10M (shared by
        # dpi variants — the data does not depend on dpi)
        manifest.append(f"{name}\t{kw.get('dpi', 100)}\t{dataname}\t{cat}")
        if dataname not in seen:
            seen.add(dataname)
            fn(_RecordingAx(os.path.join(data_root, dataname)))
    with open(os.path.join(data_root, "manifest.tsv"), "w") as f:
        f.write("#case\tdpi\tdata\tcat\n")
        f.write("\n".join(manifest) + "\n")
    print(f"dumped {len(seen)} case data sets to {data_root}", flush=True)


# -------------------------------------------------------------------
# C++ stack
# -------------------------------------------------------------------

def run_cpp(outdir):
    if not os.path.isfile(os.path.join(outdir, "data", "manifest.tsv")):
        dump_data(outdir)
    if not os.path.isfile(CPP_BENCH):
        subprocess.run(
            ["cmake", "--build", "build",
             "--target", "example_bench_matrix", "-j4"],
            cwd=ROOT_DIR, check=True)
    subprocess.run([CPP_BENCH, outdir], cwd=ROOT_DIR, check=True)


# -------------------------------------------------------------------
# report
# -------------------------------------------------------------------

def _load_stacks(outdir):
    stacks = {}
    for s in REPORT_ORDER:
        p = os.path.join(outdir, f"bench_matrix_{s}.json")
        try:
            stacks[s] = json.load(open(p))
        except OSError:
            pass
    return stacks


def report(outdir):
    stacks = _load_stacks(outdir)
    present = [s for s in REPORT_ORDER if s in stacks]
    names = [n for n, *_ in CASES] + ["multi_50x_line2k"]
    hdr = f"{'case':<22}" + "".join(f"{s:>9}" for s in present) + \
          "".join(f"{f'{s}/Agg':>9}" for s in present if s != "agg")
    print(hdr)
    print("-" * len(hdr))
    init = " ".join(f"{s}={stacks[s].get('_init', {}).get('ms'):.0f}ms"
                    for s in present
                    if stacks[s].get("_init", {}).get("ms"))
    print(f"init (separate, warm-up): {init}")
    for n in names:
        row = [f"{n:<22}"]
        a = stacks.get("agg", {}).get(n, {}).get("ms")
        for s in present:
            x = stacks[s].get(n, {}).get("ms")
            e = stacks[s].get(n, {}).get("err")
            row.append(f"{x:9.1f}" if x else f"{'ERR' if e else '—':>9}")
        for s in present:
            if s == "agg":
                continue
            v = stacks[s].get(n, {}).get("ms")
            row.append(f"{a / v:8.2f}x" if a and v else f"{'—':>9}")
        print(" ".join(row))

    # markdown report into the gallery folder, with per-case images
    lines = ["# Benchmark report", "",
             "Stack columns: `agg` = stock matplotlib, `vp` = matplotlib "
             "+ VolcanoPlot backend, `cpp` = native VolcanoPlot C++ API, "
             "`native` = VolcanoPlot python bindings.", "",
             "Median `savefig` wall time in ms (warm; init excluded — "
             "steady-state '100 plots' metric).", "",
             f"Init (measured separately): {init}", ""]
    for cat in ("small", "medium", "large", "multi"):
        rows = [n for n, *_ in CASES if dict((nm, c) for nm, _, _, c in
                CASES)[n] == cat] or (["multi_50x_line2k"]
                                      if cat == "multi" else [])
        if not rows:
            continue
        lines.append(f"## {cat}\n")
        lines.append("| case | " + " | ".join(present) + " | " +
                     " | ".join(f"{s}/Agg" for s in present if s != "agg")
                     + " |")
        lines.append("|---|" + "---:|" * (len(present) +
                     max(0, len(present) - 1)))
        for n in rows:
            a = stacks.get("agg", {}).get(n, {}).get("ms")
            cells = []
            for s in present:
                x = stacks[s].get(n, {}).get("ms")
                cells.append(f"{x:.1f}" if x else "—")
            for s in present:
                if s == "agg":
                    continue
                v = stacks[s].get(n, {}).get("ms")
                cells.append(f"{a / v:.2f}x" if a and v else "—")
            lines.append(f"| {n} | " + " | ".join(cells) + " |")
        lines.append("")
    lines.append("## Rendered output (same logical plot per row)\n")
    for n in names:
        imgs = " ".join(f"![{n} {s}]({n}_{s}.png)"
                        for s in present
                        if os.path.isfile(os.path.join(
                            outdir, f"{n}_{s}.png")))
        if imgs:
            lines.append(f"### {n}\n\n{imgs}\n")
    rp = os.path.join(outdir, "benchmark_report.md")
    with open(rp, "w") as f:
        f.write("\n".join(lines))
    print(f"\nwrote {rp}")


# -------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cmd", nargs="?", default="all",
                    choices=["all", "agg", "vp", "native", "cpp",
                             "dump", "report"])
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR)
    ap.add_argument("--repeats", type=int, default=3)
    args = ap.parse_args()
    outdir = args.outdir
    os.makedirs(outdir, exist_ok=True)

    if args.cmd in ("agg", "vp", "native"):
        run(args.cmd, outdir, args.repeats)
        return
    if args.cmd == "dump":
        dump_data(outdir)
        return
    if args.cmd == "cpp":
        run_cpp(outdir)
        return
    if args.cmd == "report":
        report(outdir)
        return

    # all: dump identical data, then one process per stack (sequential —
    # parallel runs would contend for the GPU/CPU and skew timings)
    dump_data(outdir)
    env_agg = dict(os.environ, MPLBACKEND="Agg")
    env_vp = dict(os.environ,
                  PYTHONPATH=os.path.join(ROOT_DIR, "build/python"))
    for stack, env in (("agg", env_agg), ("vp", env_vp)):
        subprocess.run([sys.executable, os.path.abspath(__file__), stack,
                        "--outdir", outdir], env=env, cwd=ROOT_DIR,
                       check=True)
    run_cpp(outdir)
    report(outdir)


if __name__ == "__main__":
    main()
