// volcano/plot/Colormap.hpp — matplotlib-style colormaps
#pragma once

#include "volcano/plot/Types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

/// A colormap is a list of control points sampled linearly.
struct Colormap {
    std::string name;
    std::vector<Color> stops;

    /// When true, sample() picks the discrete stop at floor(t*N) instead of
    /// interpolating — ListedColormap behavior.
    bool discrete = false;

    /// Colors for special values (matplotlib set_bad/set_under/set_over):
    ///   bad:   NaN input (default: transparent — i.e. not drawn)
    ///   under: t < 0 (default: clamp to stops.front())
    ///   over:  t > 1 (default: clamp to stops.back())
    std::optional<Color> bad, under, over;

    /// Sample the colormap at t in [0,1]. Honors bad/under/over for
    /// NaN / t<0 / t>1 inputs, and `discrete` for ListedColormap-style
    /// bin sampling.
    [[nodiscard]] Color sample(float t) const;

    /// Sample the reversed colormap at t in [0,1] (i.e., sample(1-t)).
    [[nodiscard]] Color sampleReversed(float t) const { return sample(1.0f - t); }

    /// Return a copy with the bad color set (matplotlib set_bad).
    [[nodiscard]] Colormap withBad(Color c) const { Colormap m = *this; m.bad = c; return m; }
    /// Return a copy with the under color set (matplotlib set_under).
    [[nodiscard]] Colormap withUnder(Color c) const { Colormap m = *this; m.under = c; return m; }
    /// Return a copy with the over color set (matplotlib set_over).
    [[nodiscard]] Colormap withOver(Color c) const { Colormap m = *this; m.over = c; return m; }
    /// Return a reversed copy (sample(1-t) equivalent as a real map).
    [[nodiscard]] Colormap reversed() const;

    /// Lookup a named colormap (viridis, plasma, inferno, magma, cividis,
    /// turbo, jet, coolwarm, RdBu, etc.). Supports reversed variants
    /// (e.g., "viridis_r") by stripping the "_r" suffix and reversing.
    /// Returns a reference to a static Colormap (for reversed variants,
    /// a static is created on first lookup).
    static const Colormap& byName(std::string_view name);

    /// mpl `matplotlib.cm.register_cmap` — add `cm` to the registry so
    /// byName() resolves it. The returned reference lives in the
    /// registry (stable address — safe to hold on plots). Registering
    /// a builtin name requires `overrideBuiltin` (mpl's
    /// override_builtin=False raises ValueError on builtin names).
    static const Colormap& registerCmap(Colormap cm,
                                        bool overrideBuiltin = false);

    /// List of available colormap names (without the "_r" suffix),
    /// including registered colormaps.
    static std::vector<std::string> availableNames();

    /// ListedColormap equivalent — a colormap from an explicit color list.
    /// When `discreteSampling` is true (default), sample() picks
    /// stops[floor(t*N)] like matplotlib's ListedColormap.
    [[nodiscard]] static Colormap listed(std::string name,
                                         std::vector<Color> colors,
                                         bool discreteSampling = true) {
        Colormap cm;
        cm.name = std::move(name);
        cm.stops = std::move(colors);
        cm.discrete = discreteSampling;
        return cm;
    }

    /// LinearSegmentedColormap equivalent — per-channel segment lists.
    /// Each channel is a list of (x, y0, y1) rows: x is the segment
    /// position in [0,1], y0 the value left of x, y1 right of x.
    /// Rasterized to `n` uniform stops (matplotlib uses a 256-entry LUT).
    struct SegPoint { float x, y0, y1; };
    [[nodiscard]] static Colormap segmented(std::string name,
                                            std::vector<SegPoint> r,
                                            std::vector<SegPoint> g,
                                            std::vector<SegPoint> b,
                                            size_t n = 256);
};

namespace colormaps {
    // Perceptually uniform sequential
    const Colormap& viridis();
    const Colormap& plasma();
    const Colormap& inferno();
    const Colormap& magma();
    const Colormap& cividis();
    // Miscellaneous (already implemented)
    const Colormap& turbo();
    const Colormap& jet();
    // Diverging (already implemented)
    const Colormap& coolwarm();
    const Colormap& RdBu();
    const Colormap& seismic();
    const Colormap& grayscale();

    // ─── Sequential (§3.2) ───────────────────────────────────────────
    const Colormap& Greys();
    const Colormap& Purples();
    const Colormap& Blues();
    const Colormap& Greens();
    const Colormap& Oranges();
    const Colormap& Reds();
    const Colormap& YlOrBr();
    const Colormap& YlOrRd();
    const Colormap& OrRd();
    const Colormap& PuRd();
    const Colormap& RdPu();
    const Colormap& BuPu();
    const Colormap& GnBu();
    const Colormap& PuBu();
    const Colormap& YlGnBu();
    const Colormap& PuBuGn();
    const Colormap& BuGn();
    const Colormap& YlGn();
    const Colormap& gray();
    const Colormap& bone();
    const Colormap& pink();
    const Colormap& spring();
    const Colormap& summer();
    const Colormap& autumn();
    const Colormap& winter();
    const Colormap& cool();
    const Colormap& Wistia();
    const Colormap& hot();
    const Colormap& afmhot();
    const Colormap& gist_heat();
    const Colormap& copper();

    // ─── Diverging (§3.3) ────────────────────────────────────────────
    const Colormap& PiYG();
    const Colormap& PRGn();
    const Colormap& BrBG();
    const Colormap& PuOr();
    const Colormap& RdGy();
    const Colormap& RdYlBu();
    const Colormap& RdYlGn();
    const Colormap& Spectral();
    const Colormap& bwr();
    const Colormap& berlin();
    const Colormap& managua();
    const Colormap& vanimo();

    // ─── Cyclic (§3.4) ───────────────────────────────────────────────
    const Colormap& twilight();
    const Colormap& twilight_shifted();
    const Colormap& hsv();

    // ─── Qualitative (§3.5) ──────────────────────────────────────────
    const Colormap& Pastel1();
    const Colormap& Pastel2();
    const Colormap& Paired();
    const Colormap& Accent();
    const Colormap& Dark2();
    const Colormap& Set1();
    const Colormap& Set2();
    const Colormap& Set3();
    const Colormap& tab10();
    const Colormap& tab20();
    const Colormap& tab20b();
    const Colormap& tab20c();
    const Colormap& okabe_ito();

    // ─── Miscellaneous (§3.6) ────────────────────────────────────────
    const Colormap& flag();
    const Colormap& prism();
    const Colormap& ocean();
    const Colormap& gist_earth();
    const Colormap& terrain();
    const Colormap& gist_stern();
    const Colormap& gnuplot();
    const Colormap& gnuplot2();
    const Colormap& CMRmap();
    const Colormap& cubehelix();
    const Colormap& brg();
    const Colormap& gist_rainbow();
    const Colormap& rainbow();
    const Colormap& nipy_spectral();
    const Colormap& gist_ncar();
} // namespace colormaps

} // namespace volcano::plot
