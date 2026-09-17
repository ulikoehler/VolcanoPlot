// volcano/text/MathText.hpp — matplotlib-style MathText (TeX subset)
//
// Parses strings containing `$...$` math segments and lays them out into
// positioned text runs plus horizontal rules (fraction bars, overlines).
// Pure CPU layout — measurement is delegated to a callback so the engine
// is usable with any text renderer and testable without a GPU.
//
// Supported subset:
//   $...$                  math segments (mixable with plain text)
//   x^2, x_i, x^{a+b}      super/subscripts (both on one base allowed)
//   \frac{a}{b}            fractions with rule
//   \sqrt{x}, \sqrt[n]{x}  radicals with overline
//   \alpha..\Omega         Greek letters (Unicode)
//   \times \pm \leq \sum \int ...  common math symbols (Unicode)
//   \hat \bar \tilde \dot \ddot \vec \overline  accents/overlines
//   \left( \right) \left| ...  delimiters (rendered at normal size)
//   \mathrm \mathbf \mathit \mathsf \mathtt \mathcal  font groups
//   \, \; \: \  \quad \qquad   spacing
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::text {

/// True if `s` contains a `$...$` math segment.
[[nodiscard]] bool containsMath(std::string_view s) noexcept;

/// Flatten a (possibly math-containing) string to plain Unicode: `$…$`
/// delimiters and grouping braces are stripped and `\command` symbols
/// are replaced by their Unicode equivalents. Used where math layout
/// isn't available (vector text backends, TeX markers).
[[nodiscard]] std::string mathTextToUnicode(std::string_view text);

/// A positioned run of UTF-8 text within a laid-out block.
/// Coordinates are pixels relative to the block origin (0, baseline).
struct MathRun {
    std::string text;
    float x = 0.0f;        ///< left edge, px from block left
    float baseline = 0.0f; ///< baseline y offset, px (+down)
    float scale = 1.0f;    ///< font scale relative to base
};

/// A horizontal rule (fraction bar, overline, sqrt overbar).
struct MathRule {
    float x0 = 0, y0 = 0, x1 = 0;  ///< x range + y position, px
    float thickness = 1.0f;        ///< rule height, px
};

/// Result of laying out a (possibly math-containing) text string.
struct MathLayout {
    std::vector<MathRun> runs;
    std::vector<MathRule> rules;
    float width = 0.0f;    ///< total width, px
    float ascent = 0.0f;   ///< extent above the baseline, px
    float descent = 0.0f;  ///< extent below the baseline, px
};

/// Font metrics for a run of text at a given scale (matches
/// TextRenderer::TextMetrics).
struct TextMeasure {
    float width = 0.0f;    ///< horizontal advance, px
    float height = 0.0f;   ///< ascent + descent, px
    float ascent = 0.0f;   ///< baseline to top, px
};
using MeasureFn = std::function<TextMeasure(std::string_view, float)>;

/// Parse and lay out `text`, which may mix plain text and `$...$` math.
/// `baseScale` is the font scale (1.0 = the renderer's default size).
/// Coordinates are relative to (0, baseline), y-down screen pixels.
[[nodiscard]] MathLayout layoutMathText(std::string_view text,
                                        float baseScale,
                                        const MeasureFn& measure);

} // namespace volcano::text
