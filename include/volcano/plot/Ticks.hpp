// volcano/plot/Ticks.hpp — matplotlib-style tick locators and formatters
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

/// Tick locator: computes tick positions for a data range [vmin, vmax].
/// Positions are in *data* coordinates.
class Locator {
public:
    virtual ~Locator() = default;
    [[nodiscard]] virtual std::vector<float> tickValues(float vmin,
                                                       float vmax) const = 0;
    /// Double-precision variant — date locators need float64 to
    /// resolve sub-day positions at epoch-day magnitudes (float32
    /// loses ~86 s at day 18000+).  Defaults to the float version.
    [[nodiscard]] virtual std::vector<double>
    tickValuesD(double vmin, double vmax) const {
        const auto f = tickValues(float(vmin), float(vmax));
        return {f.begin(), f.end()};
    }
};

/// Tick formatter: produces the label for a tick value.
class Formatter {
public:
    virtual ~Formatter() = default;
    /// Called once per axis draw with the full set of located ticks
    /// (matplotlib set_locs). Used by offset/scientific formatters.
    virtual void setLocs(std::span<const float> locs) { (void)locs; }
    /// Called once per axis draw with the axis view interval
    /// (matplotlib set_locs uses axis.get_view_interval()). Log
    /// formatters use it for minor_thresholds label suppression.
    virtual void setViewInterval(float vmin, float vmax) {
        (void)vmin; (void)vmax;
    }
    [[nodiscard]] virtual std::string format(float v, int pos) const = 0;
    /// Optional offset text drawn at the axis end (e.g. "+1e5").
    [[nodiscard]] virtual std::string offsetText() const { return {}; }
    /// mpl Formatter.__call__ wraps format() output in fix_minus
    /// (ASCII '-' → U+2212) for most formatters; NullFormatter,
    /// FormatStrFormatter, FuncFormatter and FixedFormatter bypass it,
    /// and StrMethodFormatter applies it per substituted value.
    [[nodiscard]] virtual bool unicodeMinus() const { return true; }
};

// ─── Locators ───────────────────────────────────────────────────────────────

/// No ticks at all.
class NullLocator : public Locator {
public:
    [[nodiscard]] std::vector<float> tickValues(float, float) const override {
        return {};
    }
};

/// Explicit tick positions.
class FixedLocator : public Locator {
public:
    explicit FixedLocator(std::vector<float> positions)
        : positions_(std::move(positions)) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    std::vector<float> positions_;
};

/// Evenly spaced ticks including the endpoints (matplotlib LinearLocator).
class LinearLocator : public Locator {
public:
    explicit LinearLocator(int numTicks = 0) : numTicks_(numTicks) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int numTicks_; // 0 → auto (9)
};

/// Ticks at multiples of `base` plus `offset` (matplotlib MultipleLocator).
class MultipleLocator : public Locator {
public:
    explicit MultipleLocator(float base, float offset = 0.0f)
        : base_(base), offset_(offset) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    float base_, offset_;
};

/// Ticks at integer multiples of `base` (matplotlib IndexLocator).
class IndexLocator : public Locator {
public:
    IndexLocator(float base, float offset = 0.0f) : base_(base), offset_(offset) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    float base_, offset_;
};

/// Nice-number locator targeting at most `nbins` intervals
/// (matplotlib MaxNLocator; mpl default nbins=10).
class MaxNLocator : public Locator {
public:
    explicit MaxNLocator(int nbins = 10) : nbins_(nbins) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;

    // ── mpl MaxNLocator.set_params (forwarded by Axes::locatorParams) ──
    [[nodiscard]] int nbins() const { return nbins_; }
    void setNbins(int n) { nbins_ = n < 1 ? 1 : n; }
    /// mpl nbins='auto' (AutoLocator): the caller derives nbins from
    /// the axis' tick space (see axisTicks) instead of a fixed count.
    void setNbinsAuto() { nbins_ = 0; }
    [[nodiscard]] bool autoNbins() const { return nbins_ <= 0; }
    /// mpl `steps`: acceptable step multiples in [1, 10]; the extended
    /// staircase (0.1·steps, steps, 10·steps[1]) is derived from it.
    void setSteps(std::vector<float> steps);
    [[nodiscard]] const std::vector<float>& steps() const { return steps_; }
    /// mpl `integer`: ticks take only integer values.
    void setInteger(bool v) { integer_ = v; }
    /// mpl `symmetric`: the located range is symmetrized about zero.
    void setSymmetric(bool v) { symmetric_ = v; }
    /// mpl `prune`: "lower"/"upper"/"both" drop the edge tick(s);
    /// "none"/"" keeps them.
    void setPrune(std::string_view p);
    /// mpl `min_n_ticks`: relax the step until ≥ n ticks are emitted.
    void setMinNTicks(int n) { minNTicks_ = n < 1 ? 1 : n; }
    [[nodiscard]] int minNTicks() const { return minNTicks_; }

private:
    /// mpl _raw_ticks: edge-inclusive ticks spanning [vmin, vmax].
    std::vector<float> rawTicks(double vmin, double vmax) const;
    int nbins_;
    /// mpl _raw_steps default: [1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10].
    std::vector<float> steps_{1, 1.5f, 2, 2.5f, 3, 4, 5, 6, 8, 10};
    bool integer_ = false;
    bool symmetric_ = false;
    std::string prune_;   ///< "", "lower", "upper" or "both"
    int minNTicks_ = 2;
};

/// mpl AutoLocator: MaxNLocator with nbins='auto' (derived from the
/// axis tick space by axisTicks) and the narrower staircase
/// steps=[1, 2, 2.5, 5, 10].
class AutoLocator : public MaxNLocator {
public:
    AutoLocator() : MaxNLocator(0) {
        setSteps({1.0f, 2.0f, 2.5f, 5.0f, 10.0f});
    }
};

/// Decade ticks for log axes (matplotlib LogLocator). `subs` are the
/// integer multipliers within each decade used by `minorValues()`
/// (default {2..base-1}).
class LogLocator : public Locator {
public:
    explicit LogLocator(float base = 10.0f, int numticks = 9)
        : base_(base), numticks_(numticks) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
    /// Minor positions: subs × decade within the range (2,3,...,9 × 10^e).
    [[nodiscard]] std::vector<float> minorValues(float vmin, float vmax,
                                                 std::span<const float> subs = {}) const;
private:
    float base_;
    int numticks_;
};

/// Ticks for symlog axes (matplotlib SymmetricalLogLocator).
class SymmetricalLogLocator : public Locator {
public:
    explicit SymmetricalLogLocator(float linthresh = 2.0f)
        : linthresh_(linthresh) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    float linthresh_;
};

/// Ticks for logit axes (matplotlib LogitLocator).
class LogitLocator : public Locator {
public:
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
};

/// Ticks spaced evenly on an inverse-sinh scale (matplotlib
/// AsinhLocator — used with asinh axes). `linearWidth` is the
/// quasi-linear region extent; `subs` are base multiples for minor
/// ticks.
class AsinhLocator : public Locator {
public:
    // Params are doubles: the asinh→sinh roundtrip is ulp-sensitive
    // (matplotlib computes in float64; float32 params visibly shift
    // the generated ticks at decade boundaries).
    explicit AsinhLocator(double linearWidth, int numticks = 11,
                          double symthresh = 0.2, double base = 10.0,
                          std::vector<double> subs = {})
        : linearWidth_(linearWidth), numticks_(numticks),
          symthresh_(symthresh), base_(base), subs_(std::move(subs)) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    double linearWidth_, symthresh_, base_;
    int numticks_;
    std::vector<double> subs_;
};

/// Minor ticks between major ticks (matplotlib AutoMinorLocator).
/// tickValues() returns the *minor* positions. `ndivs` = subdivisions
/// per major interval (0 → auto: 5, or 4 when the step divides unevenly).
class AutoMinorLocator : public Locator {
public:
    explicit AutoMinorLocator(int ndivs = 0) : ndivs_(ndivs) {}
    /// Minor positions between the given major ticks (clipped to range).
    [[nodiscard]] std::vector<float> between(std::span<const float> majors,
                                             float vmin, float vmax) const;
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int ndivs_;
};

// ─── Formatters ─────────────────────────────────────────────────────────────

/// Empty labels.
class NullFormatter : public Formatter {
public:
    [[nodiscard]] std::string format(float, int) const override { return {}; }
    [[nodiscard]] bool unicodeMinus() const override { return false; }
};

/// Explicit label per tick index.
class FixedFormatter : public Formatter {
public:
    explicit FixedFormatter(std::vector<std::string> labels)
        : labels_(std::move(labels)) {}
    [[nodiscard]] std::string format(float, int pos) const override;
    [[nodiscard]] bool unicodeMinus() const override { return false; }
private:
    std::vector<std::string> labels_;
};

/// Callback formatter (matplotlib FuncFormatter): func(value, pos).
class FuncFormatter : public Formatter {
public:
    explicit FuncFormatter(std::function<std::string(float, int)> fn)
        : fn_(std::move(fn)) {}
    [[nodiscard]] std::string format(float v, int pos) const override {
        return fn_ ? fn_(v, pos) : std::string{};
    }
    [[nodiscard]] bool unicodeMinus() const override { return false; }
private:
    std::function<std::string(float, int)> fn_;
};

/// printf-style format string (matplotlib FormatStrFormatter): "%.2f".
class FormatStrFormatter : public Formatter {
public:
    explicit FormatStrFormatter(std::string fmt) : fmt_(std::move(fmt)) {}
    [[nodiscard]] std::string format(float v, int pos) const override;
    [[nodiscard]] bool unicodeMinus() const override { return false; }
private:
    std::string fmt_;
};

/// "{x}" / "{pos}" template (matplotlib StrMethodFormatter).
class StrMethodFormatter : public Formatter {
public:
    explicit StrMethodFormatter(std::string tmpl) : tmpl_(std::move(tmpl)) {}
    [[nodiscard]] std::string format(float v, int pos) const override;
    // mpl applies fix_minus to each substituted value, not literals.
    [[nodiscard]] bool unicodeMinus() const override { return false; }
private:
    std::string tmpl_;
};

/// Default scalar formatter with matplotlib ticklabel_format options:
/// scilimits, useMathText, useOffset. Produces an offsetText like
/// "+1e4" (or "×10^4" / mathtext "$1\times10^{4}$") for the axis end.
class ScalarFormatter : public Formatter {
public:
    std::pair<int, int> scilimits{-5, 6};  // axes.formatter.limits
    bool useMathText = false;
    bool useOffset = true;
    /// Force scientific/plain style (ticklabel_format style="sci"/"plain").
    bool forceSci = false;

    void setLocs(std::span<const float> locs) override;
    [[nodiscard]] std::string format(float v, int pos) const override;
    [[nodiscard]] std::string offsetText() const override { return offsetText_; }
    /// Exponent/offset applied to labels (for tests).
    [[nodiscard]] float offset() const noexcept { return offset_; }
    [[nodiscard]] int sciExponent() const noexcept { return sciExp_; }
private:
    float offset_ = 0.0f;
    int sciExp_ = 0;
    int decimals_ = -1;   // common precision across locs (-1 = per-value %g)
    bool useSci_ = false;
    std::string offsetText_;
};

/// Log-axis formatter: "$10^{k}$" for exact powers of the base,
/// plain "%g" otherwise (matplotlib LogFormatter family).
class LogFormatter : public Formatter {
public:
    /// matplotlib labelOnlyBase: only powers of the base get labels.
    bool labelOnlyBase = false;
    /// matplotlib minor_thresholds (subset, all): based on the number
    /// of decades in the view interval, controls which non-decade
    /// coefficients get labels — numdec > subset → bases only,
    /// numdec > all → log-spaced subset ({1,2,3,4,6,10} for base 10),
    /// else all integer multiples. Default (1, 0.4).
    std::pair<float, float> minorThresholds{1.0f, 0.4f};

    explicit LogFormatter(float base = 10.0f) : base_(base) {}
    /// Computes the allowed coefficient set from the view interval
    /// (matplotlib LogFormatter.set_locs).
    void setViewInterval(float vmin, float vmax) override;
    [[nodiscard]] std::string format(float v, int pos) const override;
protected:
    float base_;
    /// mpl __call__ filter: false → the label is suppressed for v.
    [[nodiscard]] bool passesSublabels(float v) const;
private:
    std::optional<std::vector<int>> sublabels_;
};

/// Always "$10^{k}$" exponent form.
class LogFormatterExponent : public LogFormatter {
public:
    using LogFormatter::LogFormatter;
    [[nodiscard]] std::string format(float v, int pos) const override;
};

/// "$b^{k}$" with the base shown (matplotlib LogFormatterMathtext).
class LogFormatterMathtext : public LogFormatter {
public:
    using LogFormatter::LogFormatter;
    [[nodiscard]] std::string format(float v, int pos) const override;
};

/// "$m\times10^{k}$" mantissa × exponent (matplotlib SciNotation).
class LogFormatterSciNotation : public LogFormatter {
public:
    using LogFormatter::LogFormatter;
    [[nodiscard]] std::string format(float v, int pos) const override;
};

/// Probability formatter for logit axes (matplotlib LogitFormatter).
class LogitFormatter : public Formatter {
public:
    [[nodiscard]] std::string format(float v, int pos) const override;
};

/// Engineering notation with SI prefixes (matplotlib EngFormatter):
/// 1234 → "1.234 k", 0.001 → "1 m". `unit` is appended (e.g. "Hz").
class EngFormatter : public Formatter {
public:
    /// mpl places=None (default) → "%g" mantissa; an int → "%.Nf".
    /// Pass -1 for mpl's None.
    explicit EngFormatter(std::string unit = "", int places = -1,
                          std::string sep = " ")
        : unit_(std::move(unit)), places_(places), sep_(std::move(sep)) {}
    [[nodiscard]] std::string format(float v, int pos) const override;
private:
    std::string unit_;
    int places_;
    std::string sep_;
};

/// Percent labels (matplotlib PercentFormatter): v/xmax → "50%".
class PercentFormatter : public Formatter {
public:
    explicit PercentFormatter(float xmax = 100.0f, int decimals = -1,
                              std::string symbol = "%")
        : xmax_(xmax), decimals_(decimals), symbol_(std::move(symbol)) {}
    void setViewInterval(float vmin, float vmax) override {
        vmin_ = vmin; vmax_ = vmax;
    }
    [[nodiscard]] std::string format(float v, int pos) const override;
private:
    float xmax_;
    int decimals_; // -1 → auto (mpl: derived from the display range)
    std::string symbol_;
    float vmin_ = 0.0f, vmax_ = 0.0f;
};

} // namespace volcano::plot
