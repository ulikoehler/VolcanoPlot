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

WIDTH, HEIGHT = 400, 300
DPI = 100
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
