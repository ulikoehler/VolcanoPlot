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

    /// Lookup a named colormap (viridis, plasma, inferno, magma, cividis,
    /// turbo, jet, coolwarm, RdBu, etc.).
    static const Colormap& byName(std::string_view name);

    /// List of available colormap names.
    static std::vector<std::string> availableNames();
};

namespace colormaps {
    const Colormap& viridis();
    const Colormap& plasma();
    const Colormap& inferno();
    const Colormap& magma();
    const Colormap& cividis();
    const Colormap& turbo();
    const Colormap& jet();
    const Colormap& coolwarm();
    const Colormap& RdBu();
    const Colormap& grayscale();
    const Colormap& seismic();
} // namespace colormaps

} // namespace volcano::plot
