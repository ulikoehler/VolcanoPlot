// volcano/plot/Colormap.hpp — matplotlib-style colormaps
#pragma once

#include "volcano/plot/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

/// A colormap is a list of control points sampled linearly.
struct Colormap {
    std::string name;
    std::vector<Color> stops;

    /// Sample the colormap at t in [0,1].
    [[nodiscard]] Color sample(float t) const;

    /// Sample the reversed colormap at t in [0,1] (i.e., sample(1-t)).
    [[nodiscard]] Color sampleReversed(float t) const { return sample(1.0f - t); }

    /// Lookup a named colormap (viridis, plasma, inferno, magma, cividis,
    /// turbo, jet, coolwarm, RdBu, etc.). Supports reversed variants
    /// (e.g., "viridis_r") by stripping the "_r" suffix and reversing.
    /// Returns a reference to a static Colormap (for reversed variants,
    /// a static is created on first lookup).
    static const Colormap& byName(std::string_view name);

    /// List of available colormap names (without the "_r" suffix).
    static std::vector<std::string> availableNames();
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
