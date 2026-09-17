// volcano/plot/Cycler.hpp — matplotlib-style property cycler
#pragma once

#include <volcano/plot/Types.hpp>

#include <optional>
#include <vector>

namespace volcano::plot {

/// One entry yielded by a Cycler: optional per-property values.
/// A field is only applied to a plot when it has a value.
struct CycleProps {
    std::optional<Color> color;
    std::optional<LineStyle> lineStyle;
    std::optional<float> lineWidth;
    std::optional<MarkerStyle> marker;
};

/// matplotlib-style cycler: `cycler('color', [...])` produces a sequence of
/// CycleProps. `cyclerA + cyclerB` concatenates; `cyclerA * cyclerB` forms
/// the outer product (each entry of A combined with each entry of B), like
/// matplotlib's cycler arithmetic.
class Cycler {
public:
    Cycler() = default;

    /// Build a cycler from pre-combined entries (e.g. a parsed
    /// multi-key cycler expression).
    static Cycler ofEntries(std::vector<CycleProps> props);

    /// Build a single-key cycler.
    static Cycler ofColors(std::vector<Color> colors);
    static Cycler ofLineStyles(std::vector<LineStyle> styles);
    static Cycler ofLineWidths(std::vector<float> widths);
    static Cycler ofMarkers(std::vector<MarkerStyle> markers);

    /// Number of entries in the cycle.
    [[nodiscard]] size_t length() const noexcept { return props_.size(); }
    [[nodiscard]] bool empty() const noexcept { return props_.empty(); }

    /// Entry at index i (wraps modulo length).
    [[nodiscard]] const CycleProps& at(size_t i) const {
        return props_[i % props_.size()];
    }

    /// Yield the next entry (advances the internal position).
    const CycleProps& next() {
        const auto& p = props_[pos_ % props_.size()];
        ++pos_;
        return p;
    }

    /// Current entry without advancing (pair with advance()).
    [[nodiscard]] const CycleProps& peek() const {
        return props_[pos_ % props_.size()];
    }
    /// Advance the internal position by one entry.
    void advance() { ++pos_; }

    /// Reset the internal position to the first entry.
    void reset() noexcept { pos_ = 0; }

    /// Concatenation: A followed by B (matplotlib `+`).
    Cycler operator+(const Cycler& o) const;
    /// Outer product: every entry of A combined with every entry of B
    /// (matplotlib `*`). Merged fields prefer the left value when both set.
    Cycler operator*(const Cycler& o) const;

private:
    std::vector<CycleProps> props_;
    size_t pos_ = 0;
};

} // namespace volcano::plot
