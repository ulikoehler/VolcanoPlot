#!/usr/bin/env python3
"""Matplotlib reference renders for the web gallery.

Each generator mirrors the exact data produced by web/demo/multi.html so
the side-by-side comparison is apples-to-apples. Randomness uses the same
32-bit LCG as the demo (seed 12345).

Usage: python3 scripts/matplotlib_webgallery.py <out_dir> [--filter a,b]
"""

import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.stats import gaussian_kde

W, H, DPI = 640, 480, 100

_state = [12345]

def rnd():
    _state[0] = (_state[0] * 1103515245 + 12345) & 0xFFFFFFFF
    return _state[0] / 2**32


def fig_ax():
    fig, ax = plt.subplots(figsize=(W / DPI, H / DPI), dpi=DPI)
    return fig, ax


def save(fig, out, name):
    fig.savefig(os.path.join(out, name + ".png"), dpi=DPI)
    plt.close(fig)
    print(name)


def grid2d(W2, H2, fn):
    g = np.zeros((H2, W2))
    for j in range(H2):
        for i in range(W2):
            g[j, i] = fn(i, j)
    return g


def heat_data(n=64, m=48):
    return grid2d(n, m, lambda i, j: np.sin(i * 0.2) * np.cos(j * 0.15))


def scatter_data(n=4000):
    xs = np.array([np.sin(i * 0.61) + np.sin(i * 1.13) * 0.4
                   for i in range(n)])
    ys = np.array([np.cos(i * 0.37) + np.cos(i * 0.83) * 0.4
                   for i in range(n)])
    return xs, ys


def contour_grid():
    return grid2d(50, 40, lambda i, j:
                  np.sin(np.hypot(i * 0.2 - 5, j * 0.2 - 4)))


def surface_grid(W2=40, H2=30):
    return grid2d(W2, H2, lambda i, j:
                  np.sin(np.hypot(i * 0.3 - 6, j * 0.3 - 4.5)))


def sine_curve(n=256, f=0.08):
    xs = np.linspace(0, 1, n)
    ys = np.sin(np.arange(n) * f)
    return xs, ys


def tri_data(N=80):
    a = np.arange(N) * 0.7
    r = 0.2 + 0.75 * (np.arange(N) % 9) / 9
    xs, ys = np.cos(a) * r, np.sin(a) * r
    zs = np.sin(xs * 8) + np.cos(ys * 8)
    return xs, ys, zs


def signals(n=2048):
    s = np.array([np.sin(i * 0.3) + np.sin(i * 1.1) * 0.4 +
                  (rnd() - 0.5) * 0.3 for i in range(n)])
    s2 = np.array([np.sin(i * 0.3 + 0.5) + (rnd() - 0.5) * 0.3
                   for i in range(n)])
    return s, s2


GENS = {}


def gen(name):
    def deco(fn):
        GENS[name] = fn
        return fn
    return deco


@gen("bar")
def _(out):
    fig, ax = fig_ax()
    ax.bar(['a', 'b', 'c', 'd'], [3, 7, 2, 5], color='#2a7')
    save(fig, out, "bar")


@gen("hist")
def _(out):
    fig, ax = fig_ax()
    s = np.array([np.sin(i * 0.7) + np.sin(i * 1.3) * 0.5
                  for i in range(2000)])
    ax.hist(s, bins=20, color='#e00000')
    save(fig, out, "hist")


@gen("pie")
def _(out):
    fig, ax = fig_ax()
    ax.pie([40, 30, 20, 10], labels=['w', 'x', 'y', 'z'])
    save(fig, out, "pie")


@gen("heat")
def _(out):
    fig, ax = fig_ax()
    ax.imshow(heat_data(), cmap='viridis', origin='upper')
    save(fig, out, "heat")


@gen("pcm")
def _(out):
    fig, ax = fig_ax()
    nx, ny = 33, 25
    xs = np.linspace(0, 1, nx)
    ys = np.linspace(0, 1, ny)
    cs = grid2d(nx - 1, ny - 1,
                lambda i, j: np.sin(i * 0.4) * np.cos(j * 0.35))
    ax.pcolormesh(xs, ys, cs)
    save(fig, out, "pcm")


@gen("surface")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = fig.add_subplot(111, projection='3d')
    g = surface_grid()
    X, Y = np.meshgrid(np.arange(g.shape[1]), np.arange(g.shape[0]))
    ax.plot_surface(X, Y, g, cmap='viridis')
    ax.view_init(elev=30, azim=-60)
    save(fig, out, "surface")


@gen("contour")
def _(out):
    fig, ax = fig_ax()
    ax.contour(contour_grid(), levels=8)
    save(fig, out, "contour")


@gen("hist2d")
def _(out):
    fig, ax = fig_ax()
    xs, ys = scatter_data()
    ax.hist2d(xs, ys, bins=24)
    save(fig, out, "hist2d")


@gen("kde")
def _(out):
    fig, ax = fig_ax()
    xs, ys = scatter_data()
    k = gaussian_kde(np.vstack([xs, ys]))
    xi, yi = np.mgrid[xs.min():xs.max():80j, ys.min():ys.max():80j]
    zi = k(np.vstack([xi.ravel(), yi.ravel()])).reshape(xi.shape)
    ax.pcolormesh(xi, yi, zi, cmap='viridis', shading='auto')
    save(fig, out, "kde")


@gen("box")
def _(out):
    fig, ax = fig_ax()
    ax.boxplot([[1, 2, 3, 4, 5, 2.5], [2, 4, 6, 8, 3],
                [0.5, 1, 1.5, 2, 9]])
    save(fig, out, "box")


@gen("stem")
def _(out):
    fig, ax = fig_ax()
    xs = np.arange(40)
    ax.stem(xs, np.sin(xs * 0.4))
    save(fig, out, "stem")


@gen("quiver")
def _(out):
    fig, ax = fig_ax()
    x, y = np.meshgrid(np.linspace(0, 1, 10), np.linspace(0, 1, 10))
    u, v = -np.sin(y * 6.28), np.cos(x * 6.28)
    ax.quiver(x, y, u, v)
    save(fig, out, "quiver")


@gen("subplot")
def _(out):
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(W / DPI, H / DPI), dpi=DPI)
    xs, ys = sine_curve()
    a1.plot(xs, ys, color='#e00000')
    a2.plot(xs, np.cos(np.arange(256) * 0.08), color='#0060e0')
    save(fig, out, "subplot")


@gen("violin")
def _(out):
    fig, ax = fig_ax()
    groups = []
    for k in range(3):
        n = 2000
        i = np.arange(n)
        v = 3 + k * 2 + np.sin(i * 0.7 + k) * np.exp(-((i - n / 2) ** 2) / 4000)
        groups.append(v)
    parts = ax.violinplot(groups, showmeans=False, showextrema=True)
    for pc in parts['bodies']:
        pc.set_facecolor('#209040')
        pc.set_edgecolor('#209040')
    save(fig, out, "violin")


@gen("stackplot")
def _(out):
    fig, ax = fig_ax()
    xs = np.arange(40)
    l1 = 2 + np.sin(xs * 0.3)
    l2 = 1.5 + np.cos(xs * 0.2)
    l3 = 1 + np.sin(xs * 0.5)
    ax.stackplot(xs, l1, l2, l3)
    save(fig, out, "stackplot")


@gen("fill")
def _(out):
    fig, ax = fig_ax()
    t = np.linspace(0, 6.28, 50)
    xs = np.cos(t) * (1 + 0.3 * np.sin(3 * t))
    ys = np.sin(t) * (1 + 0.3 * np.sin(3 * t))
    ax.fill(xs, ys, color='#e00000')
    save(fig, out, "fill")


@gen("spy")
def _(out):
    fig, ax = fig_ax()
    n = 32
    d = np.zeros((n, n))
    for i in range(n):
        d[i, i] = 1
        if i + 4 < n:
            d[i, i + 4] = 1
        if i % 7 == 0:
            d[i, n - 1 - i] = 1
    ax.spy(d)
    save(fig, out, "spy")


@gen("tripcolor")
def _(out):
    fig, ax = fig_ax()
    a = np.arange(60) * 0.7
    r = 0.3 + 0.65 * (np.arange(60) % 8) / 8
    xs, ys = np.cos(a) * r, np.sin(a) * r
    zs = np.sin(xs * 9) + np.cos(ys * 9)
    ax.tripcolor(xs, ys, zs)
    save(fig, out, "tripcolor")


@gen("streamplot")
def _(out):
    fig, ax = fig_ax()
    n = 24
    x = np.linspace(0, 6.28, n)
    y = np.linspace(0, 6.28, n)
    X, Y = np.meshgrid(x, y)
    ax.streamplot(X, Y, np.sin(Y), np.cos(X))
    save(fig, out, "streamplot")


@gen("matshow")
def _(out):
    fig, ax = fig_ax()
    ax.matshow(grid2d(24, 24, lambda i, j:
                      np.sin(i * 0.5) * np.cos(j * 0.4)))
    save(fig, out, "matshow")


@gen("pcolorfast")
def _(out):
    fig, ax = fig_ax()
    ax.pcolorfast(grid2d(24, 24, lambda i, j:
                         np.sin(i * 0.5) * np.cos(j * 0.4)))
    save(fig, out, "pcolorfast")


@gen("brokenbarh")
def _(out):
    fig, ax = fig_ax()
    for x, w, y, h in [(10, 40, 15, 10), (180, 30, 15, 10),
                       (50, 110, 5, 10), (122, 25, 5, 10)]:
        ax.broken_barh([(x, w)], (y, h))
    ax.set_xlim(0, 220)
    ax.set_ylim(0, 35)
    save(fig, out, "brokenbarh")


@gen("tricontour")
def _(out):
    fig, ax = fig_ax()
    xs, ys, zs = tri_data()
    ax.tricontour(xs, ys, zs)
    save(fig, out, "tricontour")


@gen("triplot")
def _(out):
    fig, ax = fig_ax()
    xs, ys, _ = tri_data()
    ax.triplot(xs, ys)
    save(fig, out, "triplot")


@gen("trisurf")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = fig.add_subplot(111, projection='3d')
    xs, ys, zs = tri_data()
    ax.plot_trisurf(xs, ys, zs, cmap='viridis')
    ax.view_init(elev=30, azim=-60)
    save(fig, out, "trisurf")


@gen("specgram")
def _(out):
    fig, ax = fig_ax()
    s, _ = signals()
    ax.specgram(s, Fs=100)
    save(fig, out, "specgram")


@gen("spectrum")
def _(out):
    fig, ax = fig_ax()
    s, _ = signals()
    ax.magnitude_spectrum(s, Fs=100, scale='linear')
    save(fig, out, "spectrum")


@gen("psd")
def _(out):
    fig, ax = fig_ax()
    s, _ = signals()
    ax.psd(s, Fs=100)
    save(fig, out, "psd")


@gen("csd")
def _(out):
    fig, ax = fig_ax()
    s, s2 = signals()
    ax.csd(s, s2, Fs=100)
    save(fig, out, "csd")


@gen("xcorr")
def _(out):
    fig, ax = fig_ax()
    s, s2 = signals()
    ax.xcorr(s, s2, normed=True)
    save(fig, out, "xcorr")


@gen("cohere")
def _(out):
    fig, ax = fig_ax()
    s, s2 = signals()
    ax.cohere(s, s2, Fs=100)
    save(fig, out, "cohere")


@gen("wireframe")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = fig.add_subplot(111, projection='3d')
    g = grid2d(30, 24, lambda i, j:
               np.sin(np.hypot(i * 0.4 - 6, j * 0.4 - 4.8)))
    X, Y = np.meshgrid(np.arange(g.shape[1]), np.arange(g.shape[0]))
    ax.plot_wireframe(X, Y, g)
    ax.view_init(elev=30, azim=-60)
    save(fig, out, "wireframe")


@gen("annotate")
def _(out):
    fig, ax = fig_ax()
    xs = np.linspace(0, 1, 64)
    ys = np.sin(np.arange(64) * 0.15)
    ax.plot(xs, ys, color='#e00000', label='sine')
    ax.axhline(0, color='#0060e0', lw=1.5)
    ax.axvline(0.5, color='#00a040', lw=1.5)
    ax.axhspan(0.6, 0.9, color='#00e0e0', alpha=0.25)
    ax.hlines([0.3, -0.5], 0, 1, color='#8040c0', lw=1)
    ax.set_title('Annotated')
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.text(0.5, 0.9, 'peak zone', transform=ax.transAxes)
    ax.legend(loc='upper right')
    mappable = matplotlib.cm.ScalarMappable(cmap='viridis')
    fig.colorbar(mappable, ax=ax)
    save(fig, out, "annotate")



# ── second batch: 3-D + exotic plot types ────────────────────────────

def _ax3d(fig):
    ax = fig.add_subplot(111, projection='3d')
    ax.view_init(elev=30, azim=-60)
    return ax


def _helix(n=200):
    t = np.arange(n) / n * 12.56
    return np.cos(t), np.sin(t), t / 12.56


def _cloud(n=60):
    i = np.arange(n)
    a = i * 0.7
    r = 0.3 + 0.65 * (i % 8) / 8
    return np.cos(a) * r, np.sin(a) * r, (i % 10) / 10


@gen("plot3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    ax.plot(*_helix())
    save(fig, out, "plot3d")


@gen("scatter3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    ax.scatter(*_cloud())
    save(fig, out, "scatter3d")


@gen("quiver3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    x, y, z = _cloud()
    ax.quiver(x, y, z, -y, x, np.full_like(z, 0.2))
    save(fig, out, "quiver3d")


@gen("errorbar3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    x, y, z = _cloud()
    ax.errorbar(x, y, z, zerr=0.15, fmt='o')
    save(fig, out, "errorbar3d")


@gen("bar3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    i = np.arange(36)
    ax.bar3d((i % 6) * 0.25, (i // 6) * 0.25, np.zeros(36),
             0.15, 0.15, 0.3 + np.abs(np.sin(i * 0.9)) * 0.7)
    save(fig, out, "bar3d")


def _contour3d_grid():
    return grid2d(30, 24, lambda i, j:
                  np.sin(np.hypot(i * 0.4 - 6, j * 0.4 - 4.8)))


@gen("contour3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    g = _contour3d_grid()
    X, Y = np.meshgrid(np.arange(g.shape[1]), np.arange(g.shape[0]))
    ax.contour(X, Y, g)
    save(fig, out, "contour3d")


@gen("contourf3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    g = _contour3d_grid()
    X, Y = np.meshgrid(np.arange(g.shape[1]), np.arange(g.shape[0]))
    ax.contourf(X, Y, g)
    save(fig, out, "contourf3d")


@gen("voxels")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    n = 8
    i, j, k = np.indices((n, n, n))
    f = (i - 3.5) ** 2 + (j - 3.5) ** 2 + (k - 3.5) ** 2 < 12
    ax.voxels(f)
    save(fig, out, "voxels")


@gen("text3d")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    ax.plot([0, 1], [0, 1], [0, 1])
    ax.text(0.5, 0.5, 0.9, 'peak')
    save(fig, out, "text3d")


@gen("barbs")
def _(out):
    fig, ax = fig_ax()
    n = 10
    i, j = np.meshgrid(np.arange(n), np.arange(n))
    ax.barbs(i, j, np.cos(i * 0.6) * 20, np.sin(j * 0.6) * 20)
    save(fig, out, "barbs")


@gen("groupedbar")
def _(out):
    fig, ax = fig_ax()
    heights = np.array([[3, 7, 2, 5], [4, 2, 6, 3], [2, 5, 3, 6]])
    x = np.arange(heights.shape[1])
    w = 0.8 / heights.shape[0]
    for gi, h in enumerate(heights):
        ax.bar(x + gi * w, h, width=w)
    save(fig, out, "groupedbar")


@gen("figimage")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    w, h = 80, 60
    i, j = np.meshgrid(np.arange(w), np.arange(h))
    arr = np.stack([(i * 4) & 0xFF, (j * 4) & 0xFF,
                    np.full_like(i, 0x80), np.full_like(i, 0xFF)],
                   axis=-1).astype(np.uint8)
    fig.figimage(arr)
    save(fig, out, "figimage")


@gen("chirp")
def _(out):
    from scipy.signal import chirp as _chirp
    fig, ax = fig_ax()
    t = np.linspace(0, 1, 2048)
    ax.plot(t, _chirp(t, 1, 1, 20))
    save(fig, out, "chirp")


@gen("mexicanhat")
def _(out):
    fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI)
    ax = _ax3d(fig)
    s, r = 1.5, 8
    x = np.linspace(-r, r, 50)
    X, Y = np.meshgrid(x, x)
    r2 = (X ** 2 + Y ** 2) / s ** 2
    ax.plot_surface(X, Y, (1 - r2) * np.exp(-r2 / 2), cmap='viridis')
    save(fig, out, "mexicanhat")


@gen("barlabel")
def _(out):
    fig, ax = fig_ax()
    hs = [3, 7, 2, 5]
    bars = ax.bar(['a', 'b', 'c', 'd'], hs, color='#2a7')
    ax.bar_label(bars)
    save(fig, out, "barlabel")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--filter", default=None)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    names = (a.filter.split(",") if a.filter else sorted(GENS))
    for name in names:
        if name in GENS:
            GENS[name](a.out_dir)
        else:
            print(f"skip {name}: no generator")


if __name__ == "__main__":
    main()
