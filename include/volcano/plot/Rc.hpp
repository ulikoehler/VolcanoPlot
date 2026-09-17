// volcano/plot/Rc.hpp — runtime configuration (matplotlib rcParams / pyplot.style)
//
// Global runtime style configuration. New Axes and Figures snapshot the
// global rc params at construction time, so `rc::set` / `style::use`
// affect subsequently created figures.
#pragma once

#include "volcano/plot/Style.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

/// Apply a single rcParam-style key/value pair to a FigureStyle.
/// Keys are matplotlib rcParams names ("axes.facecolor", "lines.linewidth",
/// "axes.prop_cycle", ...). Values are parsed as in .mplstyle files:
/// colors accept hex/named/CN/grayscale, booleans accept
/// true/false/on/off/1/0, prop_cycle accepts cycler('color', [...]).
/// Returns false for unknown keys or unparseable values.
bool applyRcParam(FigureStyle& style, std::string_view key,
                  std::string_view value);

/// Parse rcParam key/value lines (matplotlibrc / .mplstyle format:
/// "key : value  # comment") and apply them to `style`.
/// Returns the number of successfully applied params.
size_t applyRcText(FigureStyle& style, std::string_view text);

/// matplotlib.rcParams equivalent — global runtime parameters.
namespace rc {

    /// Access the global rc parameters (mutable). Equivalent to
    /// mutating matplotlib.rcParams entries.
    [[nodiscard]] FigureStyle& params();

    /// Reset all rcParams to the built-in defaults (matplotlib.rcdefaults).
    void rcdefaults();

    /// Set a single rcParam (matplotlib.rc). Returns false for unknown keys.
    bool set(std::string_view key, std::string_view value);

    /// Load a matplotlibrc or .mplstyle file into the global params.
    /// Returns false if the file cannot be read.
    bool loadFile(const std::string& path);

    /// RAII context manager (matplotlib.rc_context). Saves the current
    /// params on construction, restores them on destruction.
    /// Optionally applies key/value overrides for the context duration.
    class Context {
    public:
        Context();
        explicit Context(const FigureStyle& style);
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&& o) noexcept
            : saved_(std::move(o.saved_)), active_(o.active_) {
            o.active_ = false;
        }
        Context& operator=(Context&&) = delete;
    private:
        FigureStyle saved_;
        bool active_ = true;
    };

} // namespace rc

/// matplotlib.pyplot.style equivalent.
namespace style {

    /// Apply a style by name (e.g. "ggplot", "dark_background") or by
    /// path to a .mplstyle / matplotlibrc file. Returns false if the name
    /// is neither a known style nor a readable file.
    bool use(const std::string& name);

    /// Apply multiple styles in order (composable style list, like
    /// plt.style.use(['ggplot', './custom.mplstyle'])). Later styles
    /// override earlier ones. Returns false if any style fails to load.
    bool use(std::initializer_list<std::string> names);
    bool use(const std::vector<std::string>& names);

    /// Context manager (plt.style.context) — applies the style and
    /// restores the previous params when the returned guard goes out
    /// of scope.
    [[nodiscard]] rc::Context context(const std::string& name);
    [[nodiscard]] rc::Context context(const std::vector<std::string>& names);

    /// Names of all built-in style sheets (plt.style.available).
    [[nodiscard]] const std::vector<std::string>& available();

} // namespace style

} // namespace volcano::plot
