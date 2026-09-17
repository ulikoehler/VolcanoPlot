// volcano/plot/Ticks.hpp — matplotlib-style tick locators and formatters
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace volcano::plot {

/// Tick locator: computes tick positions for a data range [vmin, vmax].
/// Positions are in *data* coordinates.
class Locator {
public:
    virtual ~Locator() = default;
    [[nodiscard]] virtual std::vector<float> tickValues(float vmin,
                                                       float vmax) const = 0;
};

/// Tick formatter: produces the label for a tick value.
class Formatter {
public:
    virtual ~Formatter() = default;
    /// Called once per axis draw with the full set of located ticks
    /// (matplotlib set_locs). Used by offset/scientific formatters.
    virtual void setLocs(std::span<const float> locs) { (void)locs; }
    [[nodiscard]] virtual std::string format(float v, int pos) const = 0;
    /// Optional offset text drawn at the axis end (e.g. "+1e5").
    [[nodiscard]] virtual std::string offsetText() const { return {}; }
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

/// Nice-number locator targeting at most `nbins` ticks
/// (matplotlib MaxNLocator; AutoLocator = nbins 9).
class MaxNLocator : public Locator {
public:
    explicit MaxNLocator(int nbins = 9) : nbins_(nbins) {}
    [[nodiscard]] std::vector<float> tickValues(float vmin,
                                                float vmax) const override;
private:
    int nbins_;
};

class AutoLocator : public MaxNLocator {
public:
    AutoLocator() : MaxNLocator(9) {}
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
};

/// Explicit label per tick index.
class FixedFormatter : public Formatter {
public:
    explicit FixedFormatter(std::vector<std::string> labels)
        : labels_(std::move(labels)) {}
    [[nodiscard]] std::string format(float, int pos) const override;
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
private:
    std::function<std::string(float, int)> fn_;
};

/// printf-style format string (matplotlib FormatStrFormatter): "%.2f".
class FormatStrFormatter : public Formatter {
public:
    explicit FormatStrFormatter(std::string fmt) : fmt_(std::move(fmt)) {}
    [[nodiscard]] std::string format(float v, int pos) const override;
private:
    std::string fmt_;
};

/// "{x}" / "{pos}" template (matplotlib StrMethodFormatter).
class StrMethodFormatter : public Formatter {
public:
    explicit StrMethodFormatter(std::string tmpl) : tmpl_(std::move(tmpl)) {}
    [[nodiscard]] std::string format(float v, int pos) const override;
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
    bool useSci_ = false;
    std::string offsetText_;
};

/// Log-axis formatter: "$10^{k}$" for exact powers of the base,
/// plain "%g" otherwise (matplotlib LogFormatter family).
class LogFormatter : public Formatter {
public:
    explicit LogFormatter(float base = 10.0f) : base_(base) {}
    [[nodiscard]] std::string format(float v, int pos) const override;
protected:
    float base_;
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
    explicit EngFormatter(std::string unit = "", int places = 1,
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
    [[nodiscard]] std::string format(float v, int pos) const override;
private:
    float xmax_;
    int decimals_; // -1 → auto
    std::string symbol_;
};

} // namespace volcano::plot
