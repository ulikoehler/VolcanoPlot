// volcano/plot/LineMarker.cpp — marker/line-style parsing and helpers
#include <volcano/plot/Types.hpp>

#include <algorithm>
#include <cctype>

namespace volcano::plot {

namespace {

std::string toLower(std::string_view s) {
    std::string r(s);
    std::ranges::transform(r, r.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
    return r;
}

} // namespace

std::optional<MarkerStyle> markerFromChar(char c) {
    switch (c) {
    case '.': return MarkerStyle::Point;
    case ',': return MarkerStyle::Pixel;
    case 'o': return MarkerStyle::Circle;
    case 's': return MarkerStyle::Square;
    case 'D': return MarkerStyle::Diamond;
    case 'd': return MarkerStyle::ThinDiamond;
    case '^': return MarkerStyle::Triangle;
    case 'v': return MarkerStyle::TriDown;
    case '<': return MarkerStyle::TriLeft;
    case '>': return MarkerStyle::TriRight;
    case 'p': return MarkerStyle::Pentagon;
    case 'P': return MarkerStyle::PlusFilled;
    case 'h': return MarkerStyle::Hexagon1;
    case 'H': return MarkerStyle::Hexagon2;
    case '8': return MarkerStyle::Octagon;
    case '+': return MarkerStyle::Plus;
    case 'x': return MarkerStyle::X;
    case 'X': return MarkerStyle::XFilled;
    case '*': return MarkerStyle::Star;
    case '|': return MarkerStyle::VLine;
    case '_': return MarkerStyle::HLine;
    case '1': return MarkerStyle::Tri1;
    case '2': return MarkerStyle::Tri2;
    case '3': return MarkerStyle::Tri3;
    case '4': return MarkerStyle::Tri4;
    default:  return std::nullopt;
    }
}

std::optional<MarkerFill> markerFillFromString(std::string_view s) {
    auto l = toLower(s);
    if (l == "full")   return MarkerFill::Full;
    if (l == "left")   return MarkerFill::Left;
    if (l == "right")  return MarkerFill::Right;
    if (l == "bottom") return MarkerFill::Bottom;
    if (l == "top")    return MarkerFill::Top;
    if (l == "none")   return MarkerFill::None;
    return std::nullopt;
}

std::optional<LineStyle> lineStyleFromString(std::string_view s) {
    auto l = toLower(s);
    if (l == "-" || l == "solid")    return LineStyle::Solid;
    if (l == "--" || l == "dashed")  return LineStyle::Dashed;
    if (l == "-." || l == "dashdot") return LineStyle::DashDot;
    if (l == ":" || l == "dotted")   return LineStyle::Dotted;
    if (l == "none" || l.empty())    return LineStyle::None;
    return std::nullopt;
}

std::vector<float> dashPattern(LineStyle style, float width) {
    // matplotlib dash tuples (in points, scaled by linewidth).
    float w = width > 0.0f ? width : 1.0f;
    switch (style) {
    case LineStyle::Dashed:  return {3.7f * w, 1.6f * w};
    case LineStyle::Dotted:  return {1.0f * w, 1.65f * w};
    case LineStyle::DashDot: return {6.4f * w, 1.6f * w, 1.0f * w, 1.6f * w};
    default: return {};
    }
}

std::optional<JoinStyle> joinStyleFromString(std::string_view s) {
    auto l = toLower(s);
    if (l == "miter") return JoinStyle::Miter;
    if (l == "round") return JoinStyle::Round;
    if (l == "bevel") return JoinStyle::Bevel;
    return std::nullopt;
}

std::optional<CapStyle> capStyleFromString(std::string_view s) {
    auto l = toLower(s);
    if (l == "butt")       return CapStyle::Butt;
    if (l == "round")      return CapStyle::Round;
    if (l == "projecting") return CapStyle::Projecting;
    return std::nullopt;
}

std::optional<DrawStyle> drawStyleFromString(std::string_view s) {
    auto l = toLower(s);
    if (l == "default" || l == "line") return DrawStyle::Default;
    if (l == "steps" || l == "steps-pre")  return DrawStyle::StepsPre;
    if (l == "steps-mid")  return DrawStyle::StepsMid;
    if (l == "steps-post") return DrawStyle::StepsPost;
    return std::nullopt;
}

std::vector<Point2D> applyDrawStyle(std::span<const Point2D> points,
                                    DrawStyle style) {
    if (style == DrawStyle::Default || points.size() < 2)
        return {points.begin(), points.end()};

    std::vector<Point2D> out;
    out.reserve(points.size() * 2);
    out.push_back(points[0]);
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& a = points[i - 1];
        const auto& b = points[i];
        switch (style) {
        case DrawStyle::StepsPre:   // horizontal then vertical
            out.push_back({a.x, b.y});
            break;
        case DrawStyle::StepsMid: { // step at midpoint x
            float mx = (a.x + b.x) * 0.5f;
            out.push_back({mx, a.y});
            out.push_back({mx, b.y});
            break;
        }
        case DrawStyle::StepsPost:  // vertical then horizontal
            out.push_back({b.x, a.y});
            break;
        default: break;
        }
        out.push_back(b);
    }
    return out;
}

} // namespace volcano::plot
