#!/usr/bin/env python3
"""Generate matplotlib PNGs matching the VolcanoPlot microfeature gallery.

One PNG per microfeature — see docs/MICROFEATURES.md for the ordered
checklist. Feature numbers match examples/microgallery.cpp exactly.

Usage:
    python3 scripts/matplotlib_microgallery.py [output_dir]
        [--filter 026,043] [--jobs N]
"""

import os
import sys
import argparse
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import matplotlib
matplotlib.use("Agg")  # headless backend
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.path import Path as MplPath
from matplotlib.markers import MarkerStyle as MplMarker
import matplotlib.patches as mpatches
import datetime

np.random.seed(42)

WIDTH, HEIGHT = 800, 600
DPI = 200
FIGSIZE = (WIDTH / DPI, HEIGHT / DPI)


def new_fig():
    return plt.figure(figsize=FIGSIZE, dpi=DPI)


def mf_axes(fig):
    """One axes with matplotlib-default style (white bg, no grid)."""
    ax = fig.add_subplot(111)
    ax.set_facecolor("white")
    return ax


def save(fig, out_dir, name):
    path = os.path.join(out_dir, f"{name}.png")
    fig.savefig(path, dpi=DPI)
    plt.close(fig)


def sine_x():
    return np.linspace(0, 10, 60)


def few_x():
    return np.array([1, 2, 3, 4, 5])


def few_y():
    return np.array([1, 3, 2, 4, 3])


# ═══ Tier 0 — canvas & axes chrome ═══════════════════════════════════════

def f001_blank_canvas(fig, out):
    pass  # blank canvas — no axes


def f002_fig_facecolor(fig, out):
    fig.patch.set_facecolor("#dddddd")
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)


def f003_axes_facecolor(fig, out):
    ax = mf_axes(fig)
    ax.set_facecolor("#ffffe0")
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)


def f004_spines_box(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)


def f005_spines_left_bottom(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def f006_spines_none(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    for s in ax.spines.values():
        s.set_visible(False)


def f007_axes_inset_rect(fig, out):
    ax = fig.add_axes([0.25, 0.25, 0.5, 0.5])
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)


def f008_suptitle(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    fig.suptitle("Figure Title")


# ═══ Tier 1 — ticks & tick labels ═══════════════════════════════════════

def f009_ticks_major_x(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 1)
    ax.yaxis.set_visible(False)


def f010_ticks_major_y(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 10)
    ax.xaxis.set_visible(False)


def f011_ticks_none(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_xticks([]); ax.set_yticks([])


def f012_ticks_minor(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.minorticks_on()


def f013_ticks_dir_in(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.tick_params(direction="in")


def f014_ticks_dir_inout(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.tick_params(direction="inout")


def f015_ticks_long(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.tick_params(length=10)


def f016_ticks_wide(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.tick_params(width=2.5)


def f017_ticks_top_right(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.tick_params(top=True, right=True)


def f018_ticklabels_default(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)


def f019_ticklabels_fixed(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 4); ax.set_ylim(0, 1)
    ax.set_xticks([0, 1, 2, 3, 4])
    ax.set_xticklabels(["zero", "one", "two", "three", "four"])


def f020_ticklabels_format(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.xaxis.set_major_formatter(plt.FormatStrFormatter("%.2f"))
    ax.yaxis.set_major_formatter(plt.FormatStrFormatter("%.2f"))


def f021_ticklabels_sci(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1e6); ax.set_ylim(0, 1e-4)
    ax.ticklabel_format(axis="both", style="sci")


def f022_ticklabels_rotation(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 4); ax.set_ylim(0, 1)
    ax.set_xticks([0, 1, 2, 3, 4])
    ax.set_xticklabels(["alpha", "beta", "gamma", "delta", "eps"],
                     rotation=45)


def f023_ticklabels_hidden(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.xaxis.set_major_formatter(plt.NullFormatter())
    ax.yaxis.set_major_formatter(plt.NullFormatter())


def f024_minor_ticklabels(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 1)
    ax.minorticks_on()
    ax.xaxis.set_minor_formatter(plt.FormatStrFormatter("%.1f"))
    ax.yaxis.set_visible(False)


def f025_tick_nbins(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.xaxis.set_major_locator(plt.MaxNLocator(nbins=4))
    ax.yaxis.set_major_locator(plt.MaxNLocator(nbins=4))


# ═══ Tier 2 — grid ══════════════════════════════════════════════════════

def f026_grid_major(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.grid(True)


def f027_grid_x_only(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.grid(True, axis="x")


def f028_grid_y_only(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.grid(True, axis="y")


def f029_grid_minor(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.minorticks_on()
    ax.grid(True, which="minor")


def f030_grid_both(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.minorticks_on()
    ax.grid(True, which="both")


def f031_grid_dashed(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.grid(True, linestyle="--")


def f032_grid_color(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.grid(True, color="#c83c3c")


def f033_grid_linewidth(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.grid(True, linewidth=2.0)


def f034_grid_axisbelow(fig, out):
    ax = mf_axes(fig)
    ax.set_axisbelow(True)
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.grid(True)
    ax.plot(sine_x(), np.sin(sine_x()), color="black", linewidth=6)


# ═══ Tier 3 — labels & titles ═══════════════════════════════════════════

def f035_xlabel(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_xlabel("Time (s)")


def f036_ylabel(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_ylabel("Amplitude")


def f037_label_fontsize(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_xlabel("Big X", fontsize=22)


def f038_title(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_title("Axes Title")


def f039_title_color(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_title("Red Title", color="#c82828")


def f040_title_bold(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_title("Bold Title", fontweight="bold")


def f041_title_pad(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_title("Padded Title", pad=30)


def f042_label_color(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    ax.set_xlabel("Blue X", color="#1e3cc8")


# ═══ Tier 4 — lines ═════════════════════════════════════════════════════

def f043_line_solid(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f044_line_color(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), color="#d62728")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f045_line_width(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), linewidth=5)
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f046_line_dashed(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), linestyle="--")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f047_line_dotted(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), linestyle=":")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f048_line_dashdot(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), linestyle="-.")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f049_line_dashes_custom(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), dashes=[8, 3, 2, 3])
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f050_line_alpha(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), alpha=0.35, linewidth=4)
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f051_line_zorder(fig, out):
    ax = mf_axes(fig)
    x = sine_x()
    ax.plot(x, np.sin(x), color="#d62728", linewidth=8, zorder=2)
    ax.plot(x, -np.sin(x), color="black", linewidth=2, zorder=1)
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


def f052_line_prop_cycle(fig, out):
    ax = mf_axes(fig)
    x = sine_x()
    ax.plot(x, np.sin(x))
    ax.plot(x, np.cos(x))
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)


# ═══ Tier 5 — markers ═══════════════════════════════════════════════════

def f053_marker_circle(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=100, marker="o")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f054_marker_square(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=100, marker="s")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f055_marker_diamond(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=100, marker="D")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f056_marker_triangle(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=100, marker="^")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f057_marker_star(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=200, marker="*")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f058_marker_plus(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=150, marker="+")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f059_marker_size(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=400, marker="o")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f060_marker_hollow(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=150, marker="o", facecolors="none")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f061_marker_edge_color(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=150, marker="o",
               facecolors="none", edgecolors="#d62728")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f062_marker_with_line(fig, out):
    ax = mf_axes(fig)
    ax.plot(few_x(), few_y(), marker="o", markersize=8)
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f063_marker_path(fig, out):
    ax = mf_axes(fig)
    star = MplPath.unit_regular_star(5)
    ax.scatter(few_x(), few_y(), s=260, marker=star)
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f064_marker_tex(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), s=200, marker="$\\beta$")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


# ═══ Tier 6 — fills & reference regions ═════════════════════════════════

def f065_fill_polygon(fig, out):
    ax = mf_axes(fig)
    ax.fill([1, 4, 4, 2.5, 1], [1, 1, 3, 4, 3], color="#1f77b4")
    ax.set_xlim(0, 5); ax.set_ylim(0, 5)


def f066_fill_between(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0, 10, 80)
    ax.fill_between(x, np.sin(x) + 1, np.sin(x) - 1,
                    color="#1f77b4", alpha=0.4)
    ax.set_xlim(0, 10); ax.set_ylim(-2.2, 2.2)


def f067_fill_alpha(fig, out):
    ax = mf_axes(fig)
    ax.grid(True)
    ax.fill([0, 10, 10, 0], [0, 0, 5, 5], color="#1f77b4", alpha=0.3)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)


def f068_axhline(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.axhline(5.0, color="#d62728", linewidth=2)


def f069_axvline(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.axvline(5.0, color="#2ca02c", linewidth=2)


def f070_hlines(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.hlines([2, 5, 8], 1, 9, color="#1f77b4", linewidth=1.5)


def f071_vlines(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.vlines([2, 5, 8], 1, 9, color="#1f77b4", linewidth=1.5)


def f072_axhspan(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.axhspan(3, 6, color="#ffc83c", alpha=0.43)


def f073_axvspan(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.axvspan(3, 6, color="#ffc83c", alpha=0.43)


def f074_hatch_rect(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.add_patch(mpatches.Rectangle((2, 2), 6, 6, hatch="//",
                                    facecolor="#c8c8c8"))


# ═══ Tier 7 — scales & limits ═══════════════════════════════════════════

def f075_xlim_ylim(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(2, 8); ax.set_ylim(-0.5, 0.5)


def f076_invert_x(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.invert_xaxis()


def f077_invert_y(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.invert_yaxis()


def f078_log_y(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0, 5, 60)
    ax.plot(x, np.exp(x))
    ax.set_yscale("log")
    ax.set_xlim(0, 5)


def f079_log_x(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0.01, 100, 60)
    ax.plot(x, np.log(x))
    ax.set_xscale("log")
    ax.set_ylim(-5, 5)


def f080_loglog(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(1, 1000, 80)
    ax.plot(x, x * x)
    ax.set_xscale("log"); ax.set_yscale("log")


def f081_symlog(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(-100, 100, 120)
    ax.plot(x, x)
    ax.set_xscale("symlog")


def f082_aspect_equal(fig, out):
    ax = mf_axes(fig)
    ax.plot([0, 4, 4, 0, 0], [0, 0, 4, 4, 0])
    ax.set_xlim(-1, 9); ax.set_ylim(-1, 9)
    ax.set_aspect("equal")


def f083_categorical_x(fig, out):
    ax = mf_axes(fig)
    ax.bar(["A", "B", "C", "D"], [3, 7, 5, 8])


def f084_date_x(fig, out):
    ax = mf_axes(fig)
    days = [datetime.date(2024, m, 1) for m in range(1, 7)]
    ax.plot(days, [m * m for m in range(1, 7)])
    ax.xaxis_date()


def f085_secondary_y(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.1, 1.1)
    sec = ax.secondary_yaxis("right",
        functions=(lambda v: v * 57.2958, lambda v: v / 57.2958))
    sec.set_ylabel("deg")


# ═══ Tier 8 — legend ════════════════════════════════════════════════════

def f086_legend_basic(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="signal")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.legend()


def f087_legend_loc(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="signal")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.legend(loc="lower left")


def f088_legend_ncols(fig, out):
    ax = mf_axes(fig)
    x = sine_x()
    for i in range(4):
        ax.plot(x, np.sin(x + i * 0.4) + i * 0.4 - 0.6, label=f"s{i}")
    ax.set_xlim(0, 10)
    ax.legend(ncols=2)


def f089_legend_noframe(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="signal")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.legend(frameon=False)


def f090_legend_title(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="signal")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.legend(title="Series")


def f091_legend_outside(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="signal")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.legend(loc="center left", bbox_to_anchor=(1.02, 0.5))


def f092_legend_shadow(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="signal")
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.legend(shadow=True, fancybox=True)


# ═══ Tier 9 — colorbar ══════════════════════════════════════════════════

def small_grid():
    j, i = np.mgrid[0:6, 0:8]
    return (i + j).astype(float)


def f093_colorbar_basic(fig, out):
    ax = mf_axes(fig)
    im = ax.imshow(small_grid(), cmap="viridis", origin="lower",
                   extent=[0, 8, 0, 6], aspect="auto")
    fig.colorbar(im, ax=ax)


def f094_colorbar_extend(fig, out):
    ax = mf_axes(fig)
    im = ax.imshow(small_grid(), cmap="viridis", origin="lower",
                   extent=[0, 8, 0, 6], aspect="auto")
    fig.colorbar(im, ax=ax, extend="both")


def f095_colorbar_colormap(fig, out):
    ax = mf_axes(fig)
    im = ax.imshow(small_grid(), cmap="plasma", origin="lower",
                   extent=[0, 8, 0, 6], aspect="auto")
    fig.colorbar(im, ax=ax)


def f096_colorbar_width(fig, out):
    ax = mf_axes(fig)
    im = ax.imshow(small_grid(), cmap="viridis", origin="lower",
                   extent=[0, 8, 0, 6], aspect="auto")
    fig.colorbar(im, ax=ax, fraction=0.08)


# ═══ Tier 10 — text & annotations ═══════════════════════════════════════

def f097_text_data(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.text(5, 5, "data point")


def f098_text_axes(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.text(0.5, 0.5, "axes coords", transform=ax.transAxes)


def f099_text_rotation(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.text(5, 5, "rotated", rotation=45)


def f100_text_halign(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.axvline(5.0, color="#b4b4b4", linewidth=1)
    ax.text(5, 7, "left", ha="left")
    ax.text(5, 5, "center", ha="center")
    ax.text(5, 3, "right", ha="right")


def f101_text_bbox(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.text(5, 5, "boxed",
            bbox=dict(facecolor="#ffffa0", edgecolor="black"))


def f102_mathtext(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.text(5, 5, "$x^2 + \\alpha$", fontsize=19)


def f103_annotate_arrow(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    ax.annotate("min", xy=(4.71, -1.0), xytext=(6.5, -0.5),
                arrowprops=dict(arrowstyle="->"))


def f104_annotate_offset(fig, out):
    ax = mf_axes(fig)
    ax.scatter(few_x(), few_y(), marker="o")
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)
    ax.annotate("peak", xy=(4, 4), xytext=(8, 8),
                textcoords="offset points")


def f105_unicode_text(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.text(5, 5, "Größe α ≈ π")


# ═══ Tier 11 — multi-axes layout ════════════════════════════════════════

def f106_subplots_2x2(fig, out):
    for r in range(2):
        for c in range(2):
            ax = fig.add_subplot(2, 2, r * 2 + c + 1)
            x = np.linspace(0, 5, 30)
            ax.plot(x, np.sin(x + r * 2 + c))


def f107_subplots_sharex(fig, out):
    a = fig.add_subplot(2, 1, 1)
    b = fig.add_subplot(2, 1, 2, sharex=a)
    x = sine_x()
    a.plot(x, np.sin(x))
    b.plot(x, np.cos(x * 0.5))
    a.set_xlim(0, 10)


def f108_twinx(fig, out):
    ax = mf_axes(fig)
    x = sine_x()
    ax.plot(x, np.sin(x), color="#1f77b4")
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax2 = ax.twinx()
    ax2.plot(x, np.cos(x) * 50, color="#d62728")


def f109_inset_axes(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.3, 1.3)
    inset = ax.inset_axes([0.55, 0.55, 0.35, 0.35])
    inset.plot(sine_x(), np.sin(sine_x()), color="#d62728")
    inset.set_xlim(4, 6); inset.set_ylim(-1.1, 1.1)


def f110_mosaic(fig, out):
    axes = fig.subplot_mosaic([["A", "B"], ["C", "C"]])
    x = np.linspace(0, 5, 30)
    for ax in axes.values():
        ax.plot(x, np.sin(x))


# ═══ Tier 12 — misc composite ═══════════════════════════════════════════

def f111_errorbar_caps(fig, out):
    ax = mf_axes(fig)
    ax.errorbar(few_x(), few_y(), yerr=0.4, fmt="o", capsize=5)
    ax.set_xlim(0, 6); ax.set_ylim(0, 5)


def f112_bar_edges(fig, out):
    ax = mf_axes(fig)
    ax.bar(range(4), [3, 7, 5, 8], width=0.6)
    ax.set_xlim(-0.5, 3.5); ax.set_ylim(0, 9)


def f113_step_post(fig, out):
    ax = mf_axes(fig)
    ax.step([0, 1, 2, 3, 4, 5], [1, 3, 2, 4, 3, 5], where="post",
            color="#1f77b4", linewidth=2)
    ax.set_xlim(-0.5, 5.5); ax.set_ylim(0, 6)


def f114_eventplot_rows(fig, out):
    ax = mf_axes(fig)
    ax.eventplot([[1, 2, 4, 7], [0.5, 3, 6], [2, 5, 8, 9]])
    ax.set_xlim(0, 10); ax.set_ylim(-1, 3)


def f115_table_bottom(fig, out):
    ax = mf_axes(fig)
    ax.bar(range(3), [3, 7, 5])
    ax.set_xlim(-0.5, 2.5); ax.set_ylim(0, 9)
    ax.table(cellText=[["A", "B", "C"], ["3", "7", "5"]],
             loc="bottom")




def f116_arrowstyle_filled(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.annotate("-|>", xy=(2, 2), xytext=(4, 5),
                arrowprops=dict(arrowstyle="-|>"))
    ax.annotate("-[", xy=(7, 3), xytext=(5, 7),
                arrowprops=dict(arrowstyle="-["))
    ax.annotate("|-|", xy=(2, 8), xytext=(5, 6),
                arrowprops=dict(arrowstyle="|-|"))


def f117_arrowstyle_double(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.annotate("<|-|>", xy=(3, 3), xytext=(7, 3),
                arrowprops=dict(arrowstyle="<|-|>"))
    ax.annotate("<->", xy=(3, 7), xytext=(7, 7),
                arrowprops=dict(arrowstyle="<->"))


def f118_arrowstyle_fancy(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.annotate("fancy", xy=(3, 3), xytext=(6, 5),
                arrowprops=dict(arrowstyle="fancy"))
    ax.annotate("wedge", xy=(7, 8), xytext=(4, 6),
                arrowprops=dict(arrowstyle="wedge"))



def f119_clip_path(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    clip = mpatches.Circle((5, 5), 3.0, transform=ax.transData)
    for y, c in ((5, "blue"), (6.2, "red")):
        ln, = ax.plot([0, 10], [y, y], color=c, lw=3)
        ln.set_clip_path(clip)
    r = mpatches.Rectangle((3, 1), 4, 3, facecolor="#b4dcb4",
                           edgecolor="none")
    ax.add_patch(r)
    r.set_clip_path(clip)

def f120_boxstyle(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.add_patch(mpatches.FancyBboxPatch((0.8, 5.5), 3.4, 3.4,
                 boxstyle="sawtooth,pad=0.4", facecolor="#c8c8ff",
                 mutation_scale=1.0))
    ax.add_patch(mpatches.FancyBboxPatch((5.2, 5.5), 3.4, 3.4,
                 boxstyle="roundtooth,pad=0.4", facecolor="#c8ffc8",
                 mutation_scale=1.0))
    ax.add_patch(mpatches.FancyBboxPatch((3.2, 1.0), 3.6, 3.0,
                 boxstyle="round,pad=0.3,rounding_size=0.6",
                 facecolor="#ffdcc8", mutation_scale=1.0))

def f121_arrow_bezier(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.annotate("", xy=(2, 2), xytext=(5, 6),
                arrowprops=dict(arrowstyle="simple",
                                connectionstyle="arc3,rad=0.3"))
    ax.annotate("", xy=(8, 2), xytext=(5, 6),
                arrowprops=dict(arrowstyle="fancy",
                                connectionstyle="arc3,rad=-0.3"))
    ax.annotate("", xy=(2, 9), xytext=(8, 8),
                arrowprops=dict(arrowstyle="wedge,tail_width=0.5",
                                connectionstyle="arc3,rad=0.2"))


# ═══ Tier 15 — extended coverage (122-145) ═══════════════════════════════

def f122_colorbar_horizontal(fig, out):
    ax = mf_axes(fig)
    im = ax.imshow(small_grid(), cmap="viridis", origin="lower",
                   extent=[0, 8, 0, 6], aspect="auto")
    fig.colorbar(im, ax=ax, orientation="horizontal")


def f123_colorbar_shrink(fig, out):
    ax = mf_axes(fig)
    im = ax.imshow(small_grid(), cmap="viridis", origin="lower",
                   extent=[0, 8, 0, 6], aspect="auto")
    fig.colorbar(im, ax=ax, shrink=0.5)


def f124_marker_tex_beta(fig, out):
    ax = mf_axes(fig)
    ax.plot(few_x(), few_y(), marker=r"$\beta$", linestyle="none")


def f125_imshow_extent(fig, out):
    ax = mf_axes(fig)
    ax.imshow(small_grid(), cmap="viridis", origin="lower",
              extent=[-2, 2, -1, 1])
    ax.set_xlim(-2.5, 2.5); ax.set_ylim(-1.5, 1.5)


def f126_imshow_aspect_auto(fig, out):
    ax = mf_axes(fig)
    j, i = np.mgrid[0:16, 0:4]
    ax.imshow((i + j).astype(float), cmap="viridis", origin="upper",
              aspect="auto")


def f127_inset_indicator(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ins = ax.inset_axes([0.55, 0.55, 0.38, 0.35])
    ins.plot(sine_x(), np.sin(sine_x()))
    ins.set_xlim(4.0, 5.0); ins.set_ylim(-1.0, 0.0)
    ax.indicate_inset_zoom(ins)


def f128_sizebar(fig, out):
    from mpl_toolkits.axes_grid1.anchored_artists import AnchoredSizeBar
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.add_artist(AnchoredSizeBar(ax.transData, 2.0, "2 units",
                                  "lower right"))


def f129_anchored_text(fig, out):
    from matplotlib.offsetbox import AnchoredText
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.add_artist(AnchoredText("upper left note", loc="upper left"))


def _sphere_pts(n=8, r=1.0):
    pts = []
    for i in range(n):
        a = i * 0.4
        pts.append((np.cos(a) * (1 + i / 64), np.sin(a) * (1 + i / 64),
                    i / 16 - 2.0))
    return np.array(pts).T


def f130_scatter3d_depthshade(fig, out):
    ax = fig.add_subplot(111, projection="3d")
    ax.set_facecolor("white")
    x, y, z = [], [], []
    for i in range(64):
        a = i * 0.4
        x.append(np.cos(a) * (1 + i / 64))
        y.append(np.sin(a) * (1 + i / 64))
        z.append(i / 16 - 2.0)
    ax.scatter(x, y, z, depthshade=True)
    ax.set_xlim(-2, 2); ax.set_ylim(-2, 2); ax.set_zlim(-2, 2)


def f131_scatter3d_view_init(fig, out):
    ax = fig.add_subplot(111, projection="3d")
    ax.set_facecolor("white")
    ax.scatter([0, 1, 0, -1, 0], [0, 0, 0, 0, 0], [0, 0, 1, 0, -1],
               s=40, c="red", depthshade=False)
    ax.set_xlim(-1.5, 1.5); ax.set_ylim(-1.5, 1.5); ax.set_zlim(-1.5, 1.5)
    ax.view_init(elev=0, azim=-90)


def _surf_data(n=30):
    x = np.linspace(-3, 3, n)
    y = np.linspace(-3, 3, n)
    X, Y = np.meshgrid(x, y)
    return X, Y, np.sin(X) * np.cos(Y)


def f132_surface_shade(fig, out):
    ax = fig.add_subplot(111, projection="3d")
    ax.set_facecolor("white")
    X, Y, Z = _surf_data()
    ax.plot_surface(X, Y, Z, cmap="viridis", shade=True)


def f133_surface_noshade(fig, out):
    ax = fig.add_subplot(111, projection="3d")
    ax.set_facecolor("white")
    X, Y, Z = _surf_data()
    ax.plot_surface(X, Y, Z, cmap="viridis", shade=False)


def _quiver_field():
    x, y = np.meshgrid(np.arange(8), np.arange(8))
    return x, y, -(y - 3.5), x - 3.5


def f134_quiver_pivot_mid(fig, out):
    ax = mf_axes(fig)
    x, y, u, v = _quiver_field()
    ax.quiver(x, y, u, v, pivot="mid")
    ax.set_xlim(-1, 8); ax.set_ylim(-1, 8)


def f135_quiver_headwidth(fig, out):
    ax = mf_axes(fig)
    x, y, u, v = _quiver_field()
    ax.quiver(x, y, u, v, width=0.005, headwidth=5, headlength=7,
              headaxislength=6)
    ax.set_xlim(-1, 8); ax.set_ylim(-1, 8)


def _stream_grids(nan_hole=False, n=20):
    i = np.linspace(0, 3, n)
    X, Y = np.meshgrid(i, i)
    U = -(Y - 1.5)
    V = X - 1.5
    if nan_hole:
        U[:, 9:12] = np.nan
        V[:, 9:12] = np.nan
    return i, i, U, V


def f136_streamplot_arrowsize(fig, out):
    ax = mf_axes(fig)
    x, y, U, V = _stream_grids()
    ax.streamplot(x, y, U, V, arrowsize=2.0)
    ax.set_xlim(0, 3); ax.set_ylim(0, 3)


def f137_streamplot_nan_hole(fig, out):
    ax = mf_axes(fig)
    x, y, U, V = _stream_grids(nan_hole=True)
    ax.streamplot(x, y, U, V)
    ax.set_xlim(0, 3); ax.set_ylim(0, 3)


def f138_clabel_gap(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(-3, 3, 30)
    X, Y = np.meshgrid(x, x)
    cs = ax.contour(X, Y, X * X + Y * Y, levels=[2.0, 5.0, 8.0])
    ax.clabel(cs)


def f139_log_clip(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(-2, 2, 40)
    ax.plot(x, x)
    ax.set_yscale("log")
    ax.set_ylim(0.01, 3)


def f140_colorblind_cycle(fig, out):
    from cycler import cycler
    # mpl's okabe_ito listed colormap order (matches Volcano's
    # colormaps::okabe_ito stops).
    okabe_ito = ["#E69F00", "#56B4E9", "#009E73", "#F0E442",
                 "#0072B2", "#D55E00", "#CC79A7", "#000000"]
    ax = fig.add_subplot(111)
    ax.set_facecolor("white")
    ax.set_prop_cycle(cycler(color=okabe_ito))
    for i in range(4):
        ax.plot(sine_x(), np.sin(sine_x()) + i * 0.5)


def f141_legend_handlelength(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="sin")
    ax.plot(sine_x(), -np.sin(sine_x()), label="-sin")
    ax.legend(handlelength=4.0)


def f142_legend_labelcolor(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), label="sin")
    ax.legend(labelcolor="red")


def f143_pie_explode(fig, out):
    ax = mf_axes(fig)
    ax.pie([30, 25, 20, 15, 10], labels=["A", "B", "C", "D", "E"],
           explode=[0.08] * 5)
    ax.set_aspect("equal")


def f144_secondary_x(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10)
    sec = ax.secondary_xaxis("top", functions=(lambda x: x * 2,
                                               lambda x: x * 0.5))
    sec.set_xlabel("double x")


def f145_mathtext_frac_sum(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.text(5, 0.5, r"$\frac{x}{y} + \sum_{i=0}^{n} i$")

# ═══ Tier 16 — parity batch 10 (146-155) ═══════════════════════════════

def f146_locator_params(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.locator_params("x", nbins=4)


def f147_set_xbound(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.set_xbound(2, 8)


def f148_markevery_int(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), marker="o", markevery=5,
            markersize=7)
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)


def f149_pie_startangle(fig, out):
    ax = mf_axes(fig)
    ax.pie([30, 25, 20, 15, 10], labels=["A", "B", "C", "D", "E"],
           startangle=90, counterclock=False)
    ax.set_aspect("equal")


def f150_pie_autopct(fig, out):
    ax = mf_axes(fig)
    ax.pie([30, 25, 20, 15, 10], labels=["A", "B", "C", "D", "E"],
           autopct="%1.1f%%")
    ax.set_aspect("equal")


def f151_fill_between_where(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0, 10, 60)
    ax.fill_between(x, np.sin(x), 0,
                    where=(x > 2) & (x < 8), interpolate=True,
                    color="#1f77b4", alpha=0.5)
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)


def f152_stackplot_wiggle(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0, 10, 40)
    ax.stackplot(x, np.sin(x) + 1.5, np.cos(x * 0.7) + 1.0,
                 np.sin(x * 0.4) * 0.5 + 1.0, baseline="wiggle")


def f153_label_outer(fig, out):
    for r in range(2):
        for c in range(2):
            ax = fig.add_subplot(2, 2, r * 2 + c + 1)
            x = sine_x()
            ax.plot(x, np.sin(x))
            ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
            ax.label_outer()


def f154_axis_off(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.axis("off")


def f155_imshow_origin_lower(fig, out):
    ax = mf_axes(fig)
    g = np.zeros((4, 4))
    g[0, :] = 1.0
    ax.imshow(g, cmap="viridis", origin="lower",
              extent=(-0.5, 3.5, -0.5, 3.5))
    ax.set_xlim(-0.5, 4.5); ax.set_ylim(-0.5, 4.5)


def f156_marker_colors(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), marker="o", markevery=6,
            markersize=8, markerfacecolor="red",
            markeredgecolor="black", markeredgewidth=2)
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)


def f157_spine_center(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.spines["bottom"].set_position(("data", 0.0))
    ax.spines["left"].set_position(("data", 0.0))
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def f158_spine_outward_bounds(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    sp = ax.spines["bottom"]
    sp.set_position(("outward", 10))
    sp.set_bounds(2, 8)
    sp.set_color("red")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def f159_quiverkey(fig, out):
    ax = mf_axes(fig)
    X, Y = np.meshgrid(np.arange(4), np.arange(4))
    U = np.ones_like(X, dtype=float)
    V = np.full_like(X, 0.5, dtype=float)
    Q = ax.quiver(X.ravel(), Y.ravel(), U.ravel(), V.ravel())
    ax.set_xlim(-0.5, 3.5); ax.set_ylim(-0.5, 3.5)
    ax.quiverkey(Q, 0.9, 0.9, 1.0, "1 m/s", labelpos="E")


def f160_xkcd_sketch(fig, out):
    ax = mf_axes(fig)
    ax.set_sketch_params(scale=1.0, length=100.0, randomness=2.0)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.4, 1.4)


def f161_clip_off(fig, out):
    ax = mf_axes(fig)
    ax.plot([0, 10], [0, 40], color="red", clip_on=False)
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)


def f162_sticky_edges_bar(fig, out):
    ax = mf_axes(fig)
    ax.bar([1, 2, 3, 4, 5], [1, 3, 2, 4, 3])
    ax.autoscale()


def f163_stairs_fill(fig, out):
    ax = mf_axes(fig)
    ax.stairs([1, 3, 2, 4, 2], [0, 1, 2, 3, 4, 5], fill=True,
              color="blue")
    ax.set_xlim(0, 5); ax.set_ylim(0, 4.5)


def f164_pcolor(fig, out):
    ax = mf_axes(fig)
    C = np.array([[i * j / 12.0 for i in range(4)] for j in range(3)])
    ax.pcolor(np.arange(5), np.arange(4), C, cmap="viridis")
    ax.set_xlim(0, 4); ax.set_ylim(0, 3)


def f165_inset_zoom(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0, 10, 200)
    ax.plot(x, np.sin(x * 3.0))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    inner = ax.inset_axes([0.55, 0.55, 0.4, 0.4])
    inner.plot(sine_x(), np.sin(sine_x()))
    inner.set_xlim(2.0, 4.0); inner.set_ylim(-0.6, 0.6)
    ax.indicate_inset_zoom(inner)


def f166_patheffects_stroke(fig, out):
    import matplotlib.patheffects as pe
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()), color="blue", lw=2,
            path_effects=[pe.withStroke(linewidth=4,
                                        foreground="black")])
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)


def f167_patheffects_shadow(fig, out):
    import matplotlib.patheffects as pe
    ax = mf_axes(fig)
    xs = [i * 1.2 + 0.5 for i in range(8)]
    ys = [np.sin(i) * 0.6 + 0.5 for i in range(8)]
    ax.scatter(xs, ys, s=60, color="red",
               path_effects=[pe.withSimplePatchShadow()])
    ax.set_xlim(0, 10); ax.set_ylim(-0.5, 1.5)


def f168_ticks_both(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.xaxis.set_ticks_position("both")
    ax.yaxis.set_ticks_position("both")


def f169_transform_transaxes(fig, out):
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.plot([0, 1], [0, 1], color="red", lw=2,
            transform=ax.transAxes)


def f170_colorbar_ticks(fig, out):
    ax = mf_axes(fig)
    C = np.array([[i + j * 4 for i in range(4)] for j in range(4)]) / 15.0
    im = ax.imshow(C, cmap="viridis")
    cb = fig.colorbar(im, ax=ax, ticks=[0.0, 0.5, 1.0])
    cb.set_ticklabels(["low", "mid", "high"])
    cb.minorticks_on()


def f171_scatter_c(fig, out):
    ax = mf_axes(fig)
    x = [i * 1.2 + 0.5 for i in range(8)]
    y = [np.sin(i) * 0.6 + 0.5 for i in range(8)]
    coll = ax.scatter(x, y, c=[i / 7.0 for i in range(8)],
                      cmap="viridis")
    ax.set_xlim(0, 10); ax.set_ylim(-0.5, 1.5)
    fig.colorbar(coll, ax=ax)


def f172_scatter_s(fig, out):
    ax = mf_axes(fig)
    x = [i * 1.6 + 0.8 for i in range(6)]
    ax.scatter(x, [0.5] * 6, s=[20 + i * 60 for i in range(6)],
               c="blue")
    ax.set_xlim(0, 10); ax.set_ylim(0, 1)


def f173_text_boxstyle(fig, out):
    ax = mf_axes(fig)
    ax.plot(np.linspace(0, 10, 200),
            np.sin(np.linspace(0, 10, 200)))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.text(5.0, 0.8, "peak", ha="center", va="center",
            bbox=dict(boxstyle="round", fc="wheat", ec="k"))


def f174_legend_anchor(fig, out):
    ax = mf_axes(fig)
    (l,) = ax.plot(np.linspace(0, 10, 200),
                   np.sin(np.linspace(0, 10, 200)))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.legend(handles=[l], labels=["wave"], loc="upper left",
              bbox_to_anchor=(1, 1), edgecolor="red",
              facecolor="white")


def f175_imshow_norm(fig, out):
    from matplotlib.colors import LogNorm
    ax = mf_axes(fig)
    C = np.array([[10.0 ** ((i + j * 4) / 15.0 * 2.0) for i in range(4)]
                  for j in range(4)])
    im = ax.imshow(C, cmap="viridis", norm=LogNorm(vmin=1, vmax=100))
    fig.colorbar(im, ax=ax)


def f176_font_family(fig, out):
    ax = mf_axes(fig)
    ax.set_xlim(0, 1); ax.set_ylim(0, 1)
    for fam, y in zip(("serif", "sans-serif", "monospace"),
                      (0.75, 0.5, 0.25)):
        ax.text(0.05, y, fam, transform=ax.transAxes,
                family=fam, fontsize=14)


def f177_tick_labelsize(fig, out):
    ax = mf_axes(fig)
    ax.plot(np.linspace(0, 10, 60),
            np.sin(np.linspace(0, 10, 60)))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.tick_params(axis="x", labelsize=14)
    ax.tick_params(axis="y", labelsize=7)


def f178_fig_text(fig, out):
    ax = mf_axes(fig)
    ax.plot(np.linspace(0, 10, 60),
            np.sin(np.linspace(0, 10, 60)))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    fig.suptitle("suptitle", fontsize=14, fontweight="bold")
    fig.supxlabel("supxlabel", fontsize=9)
    fig.text(0.5, 0.5, "fig text", color="red", fontsize=11,
             ha="center")


def f179_imshow_rgb(fig, out):
    ax = mf_axes(fig)
    rgb = np.zeros((8, 8, 3))
    for j in range(8):
        for i in range(8):
            rgb[j, i] = (31 * i / 255.0, 31 * j / 255.0, 128 / 255.0)
    ax.imshow(rgb)


def f180_annotate_fontsize(fig, out):
    ax = mf_axes(fig)
    ax.plot(np.linspace(0, 10, 60),
            np.sin(np.linspace(0, 10, 60)))
    ax.set_xlim(0, 10); ax.set_ylim(-1.2, 1.2)
    ax.annotate("big", xy=(5.0, -0.96), xytext=(6.5, -0.6),
                fontsize=20, fontweight="bold",
                arrowprops={"arrowstyle": "->"})

def f181_gridspec_ratios(fig, out):
    import matplotlib.gridspec as gridspec
    gs = gridspec.GridSpec(2, 3, figure=fig,
                           width_ratios=[2, 1, 1],
                           height_ratios=[1, 2])
    top = fig.add_subplot(gs[0, :])
    top.plot(sine_x(), np.sin(sine_x()))
    for c in range(3):
        ax = fig.add_subplot(gs[1, c])
        ax.plot([0, 1], [c, c + 1])


def f182_gridspec_nested(fig, out):
    import matplotlib.gridspec as gridspec
    gs = gridspec.GridSpec(1, 2, figure=fig)
    left = fig.add_subplot(gs[0, 0])
    left.plot(sine_x(), np.sin(sine_x()))
    nested = gs[0, 1].subgridspec(2, 1)
    for r in range(2):
        ax = fig.add_subplot(nested[r, 0])
        ax.plot([0, 1], [r, 1 - r])


def f183_artist_props(fig, out):
    ax = mf_axes(fig)
    (ln,) = ax.plot(sine_x(), np.sin(sine_x()),
                    color="blue", lw=3)
    ln.set_alpha(0.4)
    sc = ax.scatter(range(12), [0.5] * 12, color="red", s=64)
    sc.set_zorder(5)
    (hidden,) = ax.plot([0, 10], [-0.9, 0.9],
                        color=(0, 180 / 255, 0), lw=4)
    hidden.set_visible(False)


def f184_collection_props(fig, out):
    ax = mf_axes(fig)
    x = np.linspace(0, 10, 20)
    y = 0.5 + 0.3 * np.sin(x)
    colors = [( (30 + 10 * i) / 255, 80 / 255,
               (220 - 10 * i) / 255) for i in range(20)]
    sizes = (5 + np.arange(20) * 0.5) ** 2
    coll = ax.scatter(x, y, s=sizes, c=colors)
    coll.set_edgecolor("k")
    coll.set_linewidth(1.5)
    ax.set_xlim(-0.5, 10.5); ax.set_ylim(0.0, 1.0)


def f185_return_handles(fig, out):
    ax = mf_axes(fig)
    data = np.array([np.sin(i * 0.31) * 2.0 + np.cos(i * 0.13)
                     for i in range(200)])
    n, bins, patches = ax.hist(data, bins=8)
    ax.axvline(1.0, color="red", lw=2)
    ax.set_xlim(-3.5, 3.5)


def f186_tri_explicit(fig, out):
    import matplotlib.tri as mtri
    ax = mf_axes(fig)
    x = np.array([0, 1, 2, 0, 1, 2, 0.5, 1.5])
    y = np.array([0, 0, 0, 1, 1, 1, 0.5, 0.5])
    tris = np.array([[0, 1, 6], [1, 6, 7], [1, 2, 7], [0, 6, 3],
                     [6, 3, 4], [6, 4, 7], [7, 4, 5], [2, 7, 5]])
    tri = mtri.Triangulation(x, y, tris)
    face = np.array([0.1, 0.3, 0.5, 0.7, 0.2, 0.9, 0.4, 0.6])
    ax.tripcolor(tri, facecolors=face)
    ax.triplot(tri, color="k", lw=0.7)
    ax.set_xlim(-0.1, 2.1); ax.set_ylim(-0.1, 1.1)


def f187_named_containers(fig, out):
    import matplotlib.gridspec as gridspec
    import matplotlib.container
    gs = gridspec.GridSpec(3, 1, figure=fig)
    ax = fig.add_subplot(gs[0, 0])
    x = np.linspace(0, 10, 12)
    y = np.sin(x)
    ye = 0.15 + 0.05 * np.abs(np.sin(x * 3))
    container = ax.errorbar(x, y, yerr=ye, capsize=4, fmt="o-", ms=4)
    assert isinstance(container,
                      matplotlib.container.ErrorbarContainer)
    ax.set_ylim(-1.5, 1.5)
    ax2 = fig.add_subplot(gs[1, 0])
    sc = ax2.stem(np.linspace(0, 10, 12), np.cos(np.arange(12) * 0.7))
    assert isinstance(sc, matplotlib.container.StemContainer)
    ax2.set_ylim(-1.3, 1.3)
    ax3 = fig.add_subplot(gs[2, 0])
    cols = ax3.eventplot([[1, 3, 5, 7], [0.5, 2.5, 6.5], [2, 4, 8]])
    ax3.set_xlim(0, 9); ax3.set_ylim(-0.6, 2.6)


def f188_date_locators(fig, out):
    import matplotlib.dates as mdates
    ax = mf_axes(fig)
    days = 18262 + np.arange(0, 181, 3)
    ax.plot(days, np.sin(np.arange(0, 181, 3) * 0.06), color="blue")
    ax.xaxis.set_major_locator(mdates.MonthLocator(interval=2))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%b %d"))
    ax.set_xlim(18262, 18442); ax.set_ylim(-1.2, 1.2)


def f189_anchored_artists(fig, out):
    from matplotlib.offsetbox import AnchoredText
    try:
        from mpl_toolkits.axes_grid1.anchored_artists import (
            AnchoredSizeBar)
    except Exception:
        AnchoredSizeBar = None
    ax = mf_axes(fig)
    ax.plot(sine_x(), np.sin(sine_x()))
    at = AnchoredText("anchored", loc="upper left", pad=0.4,
                      borderpad=0.5, frameon=True)
    ax.add_artist(at)
    if AnchoredSizeBar is not None:
        sb = AnchoredSizeBar(ax.transData, 2.0, "2 units",
                             loc="lower right", pad=0.2,
                             borderpad=0.5, sep=4, frameon=True)
        ax.add_artist(sb)


def f190_concise_dates(fig, out):
    import matplotlib.dates as mdates
    ax = mf_axes(fig)
    days = 18262 + np.arange(0, 61)
    ax.plot(days, np.cos(np.arange(0, 61) * 0.1),
            color=(214 / 255, 39 / 255, 40 / 255))
    loc = mdates.MonthLocator()
    ax.xaxis.set_major_locator(loc)
    ax.xaxis.set_minor_locator(mdates.DayLocator(7))
    ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(loc))
    ax.set_xlim(18262, 18322); ax.set_ylim(-1.2, 1.2)


# ═══ Registry ═══════════════════════════════════════════════════════════

# ═══ Registry ═══════════════════════════════════════════════════════════

# Feature name → generator. Names/numbers match microgallery.cpp exactly.
def _collect():
    import re
    reg = {}
    for name, fn in list(globals().items()):
        m = re.fullmatch(r"f(\d{3})_([a-z0-9_]+)", name)
        if m:
            reg[f"{m.group(1)}_{m.group(2)}"] = fn
    return reg


FEATURES = _collect()


def _render_one(args):
    """Worker: (name, out_dir) — creates its own figure."""
    name, out_dir = args
    fig = new_fig()
    try:
        FEATURES[name](fig, out_dir)
        save(fig, out_dir, name)
    except Exception as e:
        plt.close(fig)
        return f"  FAILED {name}: {e}"
    return f"  wrote {os.path.join(out_dir, name + '.png')}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir", nargs="?",
                        default="gallery_micro/matplotlib")
    parser.add_argument("--filter", default=None,
                        help="Comma-separated feature-name substrings")
    parser.add_argument("--jobs", "-j", type=int, default=1,
                        help="Parallel workers (0 = cpu count)")
    parser.add_argument("--list", action="store_true",
                        help="List feature names and exit")
    args = parser.parse_args()

    if args.list:
        for name in sorted(FEATURES):
            print(name)
        return

    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    names = sorted(FEATURES)
    if args.filter:
        wanted = [n.strip() for n in args.filter.split(",") if n.strip()]
        names = [n for n in names
                 if any(w in n for w in wanted)]
        missing = [w for w in wanted
                   if not any(w in n for n in FEATURES)]
        if missing:
            print(f"WARNING: unknown feature(s): {missing}",
                  file=sys.stderr)

    jobs = args.jobs if args.jobs > 0 else (os.cpu_count() or 1)
    print(f"Generating {len(names)} matplotlib microfeatures in "
          f"{out_dir}/ (jobs={jobs})")

    work = [(n, out_dir) for n in names]
    if jobs <= 1:
        for w in work:
            print(_render_one(w), flush=True)
    else:
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            for msg in pool.map(_render_one, work):
                print(msg, flush=True)

    print(f"Done. Generated {len(names)} microfeatures.")


if __name__ == "__main__":
    main()