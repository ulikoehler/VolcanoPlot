// volcano/plot/Serialize.cpp — Figure ↔ JSON round-trip
//
// Hand-rolled minimal JSON writer/parser (no external dependency).
// Covers: figure grid + dpi + title, grid-placement axes with viewport,
// axis labels/title, scales, legend visibility, axis grids, and the
// series layers line / scatter / bar / errorbar.

#include "volcano/plot/Serialize.hpp"

#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/plots/LinePlot.hpp"
#include "volcano/plot/plots/ScatterPlot.hpp"
#include "volcano/plot/plots/BarPlot.hpp"
#include "volcano/plot/plots/ErrorbarPlot.hpp"

#include <charconv>
#include <format>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <variant>

namespace volcano::plot {
namespace {

// ─── Writer helpers ─────────────────────────────────────────────────────────

void esc(std::string& out, std::string_view s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out += std::format("\\u{:04x}", c);
            else out += c;
        }
    }
    out += '"';
}

void num(std::string& out, double v) {
    if (!std::isfinite(v)) { out += "null"; return; }
    char buf[40];
    auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v,
                                 std::chars_format::general);
    out.append(buf, p);
}

std::string hexOf(Color c) {
    auto ch = [](float f) {
        return std::clamp(int(std::lround(f * 255.0f)), 0, 255);
    };
    char buf[10];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
                  ch(c.r), ch(c.g), ch(c.b), ch(c.a));
    return buf;
}

std::optional<Color> colorOf(std::string_view s) {
    if (s.size() != 9 || s[0] != '#') return std::nullopt;
    auto byte = [&](size_t i) -> int {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = s[i + k];
            v <<= 4;
            v |= (c >= '0' && c <= '9') ? c - '0'
               : (c >= 'a' && c <= 'f') ? c - 'a' + 10
               : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1000;
        }
        return v;
    };
    int r = byte(1), g = byte(3), b = byte(5), a = byte(7);
    if (r < 0 || g < 0 || b < 0 || a < 0) return std::nullopt;
    return Color::fromRgba8(uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a));
}

const char* scaleName(ScaleKind k) {
    switch (k) {
    case ScaleKind::Linear:   return "linear";
    case ScaleKind::Log:      return "log";
    case ScaleKind::Symlog:   return "symlog";
    case ScaleKind::Logit:    return "logit";
    case ScaleKind::Asinh:    return "asinh";
    case ScaleKind::Mercator: return "mercator";
    default:                  return "linear";
    }
}

const char* lineStyleName(LineStyle ls) {
    switch (ls) {
    case LineStyle::Dashed:  return "dashed";
    case LineStyle::Dotted:  return "dotted";
    case LineStyle::DashDot: return "dashdot";
    case LineStyle::None:    return "none";
    default:                 return "solid";
    }
}

const char* drawStyleName(DrawStyle d) {
    switch (d) {
    case DrawStyle::StepsPre:  return "steps-pre";
    case DrawStyle::StepsMid:  return "steps-mid";
    case DrawStyle::StepsPost: return "steps-post";
    default:                   return "default";
    }
}

void writeColor(std::string& out, const char* key, Color c, bool skipAuto) {
    if (skipAuto && c.a == 0.0f) return;   // autoColor sentinel
    out += ",\""; out += key; out += "\":\""; out += hexOf(c); out += '"';
}

void writeSeries(std::string& out, const Series2D& s) {
    out += "\"points\":[";
    for (size_t i = 0; i < s.points.size(); ++i) {
        if (i) out += ',';
        out += '['; num(out, s.points[i].x); out += ','; num(out, s.points[i].y);
        out += ']';
    }
    out += ']';
    if (!s.label.empty()) { out += ",\"label\":"; esc(out, s.label); }
    writeColor(out, "color", s.color, true);
    if (s.size != 6.0f) { out += ",\"size\":"; num(out, s.size); }
    if (!s.markerTex.empty()) { out += ",\"markerTex\":"; esc(out, s.markerTex); }
    else {
        out += ",\"marker\":"; num(out, static_cast<int>(s.marker));
        if (s.markerFill != MarkerFill::Full)
            { out += ",\"markerFill\":"; num(out, int(s.markerFill)); }
        if (s.markerNumsides != 5)
            { out += ",\"markerNumsides\":"; num(out, s.markerNumsides); }
        if (s.markerAngle != 0.0f)
            { out += ",\"markerAngle\":"; num(out, s.markerAngle); }
    }
    if (s.lineStyle != LineStyle::Solid)
        { out += ",\"lineStyle\":\""; out += lineStyleName(s.lineStyle); out += '"'; }
    if (!s.dashes.empty()) {
        out += ",\"dashes\":[";
        for (size_t i = 0; i < s.dashes.size(); ++i) {
            if (i) out += ','; num(out, s.dashes[i]);
        }
        out += ']';
    }
    if (s.lineWidth != 1.5f) { out += ",\"lineWidth\":"; num(out, s.lineWidth); }
    if (s.drawStyle != DrawStyle::Default)
        { out += ",\"drawStyle\":\""; out += drawStyleName(s.drawStyle); out += '"'; }
    if (!s.usePropCycle) out += ",\"usePropCycle\":false";
}

std::string plotToJson(const IPlot& p) {
    std::string out;
    if (auto* l = dynamic_cast<const LinePlot*>(&p)) {
        out += "{\"type\":\"line\",";
        writeSeries(out, l->series());
    } else if (auto* sc = dynamic_cast<const ScatterPlot*>(&p)) {
        out += "{\"type\":\"scatter\",";
        writeSeries(out, sc->series());
    } else if (auto* b = dynamic_cast<const BarPlot*>(&p)) {
        const auto& d = b->data();
        out += "{\"type\":\"bar\",\"heights\":[";
        for (size_t i = 0; i < d.heights.size(); ++i) {
            if (i) out += ','; num(out, d.heights[i]);
        }
        out += ']';
        if (d.width != 0.8f) { out += ",\"width\":"; num(out, d.width); }
        if (d.horizontal) out += ",\"horizontal\":true";
        if (!d.labels.empty()) {
            out += ",\"labels\":[";
            for (size_t i = 0; i < d.labels.size(); ++i) {
                if (i) out += ','; esc(out, d.labels[i]);
            }
            out += ']';
        }
        if (!d.colors.empty()) {
            out += ",\"colors\":[";
            for (size_t i = 0; i < d.colors.size(); ++i) {
                if (i) out += ','; out += '"'; out += hexOf(d.colors[i]); out += '"';
            }
            out += ']';
        }
    } else if (auto* e = dynamic_cast<const ErrorbarPlot*>(&p)) {
        const auto& c = e->config();
        out += "{\"type\":\"errorbar\",\"x\":[";
        for (size_t i = 0; i < e->xs().size(); ++i) {
            if (i) out += ','; num(out, e->xs()[i]);
        }
        out += "],\"y\":[";
        for (size_t i = 0; i < e->ys().size(); ++i) {
            if (i) out += ','; num(out, e->ys()[i]);
        }
        out += ']';
        auto arr = [&](const char* k, const std::vector<float>& v) {
            if (v.empty()) return;
            out += ",\""; out += k; out += "\":[";
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) out += ','; num(out, v[i]);
            }
            out += ']';
        };
        arr("xerr", c.xerr); arr("yerr", c.yerr);
        arr("xerrLower", c.xerrLower); arr("xerrUpper", c.xerrUpper);
        arr("yerrLower", c.yerrLower); arr("yerrUpper", c.yerrUpper);
        writeColor(out, "color", c.color, false);
        writeColor(out, "errorbarColor", c.errorbarColor, false);
        if (!c.drawLine) out += ",\"drawLine\":false";
        if (!c.drawMarker) out += ",\"drawMarker\":false";
        if (!c.drawCaps) out += ",\"drawCaps\":false";
        if (c.capSize != 3.0f) { out += ",\"capSize\":"; num(out, c.capSize); }
        if (c.errorevery != 1) { out += ",\"errorevery\":"; num(out, c.errorevery); }
        if (!c.label.empty()) { out += ",\"label\":"; esc(out, c.label); }
    } else {
        return {};
    }
    out += '}';
    return out;
}

// ─── Parser ─────────────────────────────────────────────────────────────────

struct JVal {
    std::variant<std::nullptr_t, bool, double, std::string,
                 std::vector<JVal>, std::vector<std::pair<std::string, JVal>>> v;
    bool isObj() const { return std::holds_alternative<std::vector<std::pair<std::string, JVal>>>(v); }
    bool isArr() const { return std::holds_alternative<std::vector<JVal>>(v); }
    const JVal* find(std::string_view key) const {
        if (!isObj()) return nullptr;
        for (auto& [k, val] : std::get<std::vector<std::pair<std::string, JVal>>>(v))
            if (k == key) return &val;
        return nullptr;
    }
    double num(double def = 0.0) const {
        return std::holds_alternative<double>(v) ? std::get<double>(v) : def;
    }
    std::string_view str() const {
        if (!std::holds_alternative<std::string>(v)) return {};
        return std::get<std::string>(v);
    }
    bool boolean(bool def = false) const {
        return std::holds_alternative<bool>(v) ? std::get<bool>(v) : def;
    }
};

struct Parser {
    std::string_view s;
    size_t i = 0;
    bool fail = false;

    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) == w) { i += w.size(); return true; }
        return false;
    }

    JVal value() {
        ws();
        if (i >= s.size() || fail) { fail = true; return {}; }
        char c = s[i];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return JVal{string()};
        if (c == 't') { if (!lit("true")) fail = true; return JVal{true}; }
        if (c == 'f') { if (!lit("false")) fail = true; return JVal{false}; }
        if (c == 'n') { if (!lit("null")) fail = true; return JVal{nullptr}; }
        return number();
    }

    JVal object() {
        JVal o{std::vector<std::pair<std::string, JVal>>{}};
        ++i; ws();
        if (eat('}')) return o;
        while (!fail) {
            ws();
            if (i >= s.size() || s[i] != '"') { fail = true; return {}; }
            auto key = string();
            if (!eat(':')) { fail = true; return {}; }
            auto val = value();
            std::get<std::vector<std::pair<std::string, JVal>>>(o.v)
                .emplace_back(std::move(key), std::move(val));
            if (eat(',')) continue;
            if (eat('}')) return o;
            fail = true; return {};
        }
        return {};
    }

    JVal array() {
        JVal a{std::vector<JVal>{}};
        ++i; ws();
        if (eat(']')) return a;
        while (!fail) {
            std::get<std::vector<JVal>>(a.v).push_back(value());
            if (fail) return {};
            if (eat(',')) continue;
            if (eat(']')) return a;
            fail = true; return {};
        }
        return {};
    }

    std::string string() {
        std::string out;
        ++i; // opening quote
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'u': {
                    if (i + 4 > s.size()) { fail = true; return {}; }
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        cp <<= 4;
                        cp |= (h >= '0' && h <= '9') ? h - '0'
                            : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                            : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : 0;
                    }
                    // UTF-8 encode (BMP only).
                    if (cp < 0x80) out += char(cp);
                    else if (cp < 0x800) {
                        out += char(0xC0 | (cp >> 6));
                        out += char(0x80 | (cp & 0x3F));
                    } else {
                        out += char(0xE0 | (cp >> 12));
                        out += char(0x80 | ((cp >> 6) & 0x3F));
                        out += char(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e;
                }
            } else out += c;
        }
        fail = true;
        return out;
    }

    JVal number() {
        size_t start = i;
        while (i < s.size() &&
               (std::isdigit((unsigned char)s[i]) || s[i] == '-' ||
                s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
            ++i;
        if (i == start) { fail = true; return {}; }
        double v = 0;
        auto [p, ec] = std::from_chars(s.data() + start, s.data() + i, v);
        if (ec != std::errc{}) { fail = true; return {}; }
        return JVal{v};
    }
};

std::vector<float> floatArr(const JVal* v) {
    std::vector<float> out;
    if (!v || !v->isArr()) return out;
    for (auto& e : std::get<std::vector<JVal>>(v->v))
        out.push_back(float(e.num()));
    return out;
}

void readSeries(const JVal& j, Series2D& s) {
    if (auto* pts = j.find("points")) {
        for (auto& e : std::get<std::vector<JVal>>(pts->v)) {
            if (!e.isArr()) continue;
            auto& pair = std::get<std::vector<JVal>>(e.v);
            if (pair.size() >= 2)
                s.points.push_back({float(pair[0].num()), float(pair[1].num())});
        }
    }
    s.label = std::string(j.find("label") ? j.find("label")->str() : "");
    if (auto* c = j.find("color"))
        if (auto col = colorOf(c->str())) s.color = *col;
    if (auto* v = j.find("size")) s.size = float(v->num(6.0));
    if (auto* v = j.find("markerTex")) s.markerTex = std::string(v->str());
    if (auto* v = j.find("marker")) s.marker = MarkerStyle(int(v->num()));
    if (auto* v = j.find("markerFill")) s.markerFill = MarkerFill(int(v->num()));
    if (auto* v = j.find("markerNumsides")) s.markerNumsides = int(v->num(5));
    if (auto* v = j.find("markerAngle")) s.markerAngle = float(v->num());
    if (auto* v = j.find("lineStyle"))
        if (auto ls = lineStyleFromString(v->str())) s.lineStyle = *ls;
    s.dashes = floatArr(j.find("dashes"));
    if (auto* v = j.find("lineWidth")) s.lineWidth = float(v->num(1.5));
    if (auto* v = j.find("drawStyle"))
        if (auto d = drawStyleFromString(v->str())) s.drawStyle = *d;
    if (auto* v = j.find("usePropCycle")) s.usePropCycle = v->boolean(true);
}

} // namespace

std::string figureToJson(const Figure& fig) {
    std::string out = "{\"version\":1,\"grid\":{\"rows\":";
    out += std::to_string(fig.grid().rows());
    out += ",\"cols\":" + std::to_string(fig.grid().cols()) + '}';
    const auto& fs = fig.style();
    if (fs.dpi != 100.0f) { out += ",\"dpi\":"; num(out, fs.dpi); }
    if (!fs.title.text.empty()) { out += ",\"title\":"; esc(out, fs.title.text); }
    if (fs.faceColor.a != 1.0f || fs.faceColor.r != 1.0f ||
        fs.faceColor.g != 1.0f || fs.faceColor.b != 1.0f)
        { out += ",\"faceColor\":\""; out += hexOf(fs.faceColor); out += '"'; }
    out += ",\"axes\":[";
    bool first = true;
    for (const auto& pl : fig.placements()) {
        if (pl.mode != PlacementMode::Grid) continue;
        if (!first) out += ',';
        first = false;
        const Axes& ax = *pl.axes;
        const auto& st = ax.style();
        out += "{\"row\":" + std::to_string(pl.spec.row)
             + ",\"col\":" + std::to_string(pl.spec.col)
             + ",\"rowSpan\":" + std::to_string(pl.spec.rowSpan)
             + ",\"colSpan\":" + std::to_string(pl.spec.colSpan);
        const auto& vp = ax.viewport();
        out += ",\"viewport\":{\"xMin\":"; num(out, vp.x.min);
        out += ",\"xMax\":"; num(out, vp.x.max);
        out += ",\"yMin\":"; num(out, vp.y.min);
        out += ",\"yMax\":"; num(out, vp.y.max); out += '}';
        if (!st.xAxis.label.empty()) { out += ",\"xlabel\":"; esc(out, st.xAxis.label); }
        if (!st.yAxis.label.empty()) { out += ",\"ylabel\":"; esc(out, st.yAxis.label); }
        if (!st.title.text.empty()) { out += ",\"title\":"; esc(out, st.title.text); }
        if (!ax.xscale().isLinear())
            { out += ",\"xscale\":\""; out += scaleName(ax.xscale().kind); out += '"'; }
        if (!ax.yscale().isLinear())
            { out += ",\"yscale\":\""; out += scaleName(ax.yscale().kind); out += '"'; }
        if (st.legend.visible) out += ",\"legend\":true";
        if (st.xAxis.grid) out += ",\"xgrid\":true";
        if (st.yAxis.grid) out += ",\"ygrid\":true";
        out += ",\"plots\":[";
        bool fp = true;
        for (const auto& p : ax.plots()) {
            auto j = plotToJson(*p);
            if (j.empty()) continue;
            if (!fp) out += ',';
            fp = false;
            out += j;
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

std::unique_ptr<Figure> figureFromJson(std::string_view json) {
    Parser p{json};
    auto root = p.value();
    if (p.fail || !root.isObj()) return nullptr;
    auto* grid = root.find("grid");
    uint32_t rows = 1, cols = 1;
    if (grid && grid->isObj()) {
        rows = uint32_t(grid->find("rows")->num(1));
        cols = uint32_t(grid->find("cols")->num(1));
    }
    auto fig = std::make_unique<Figure>(rows, cols);
    if (auto* v = root.find("dpi")) fig->style().dpi = float(v->num(100));
    if (auto* v = root.find("title")) fig->setTitle(std::string(v->str()));
    if (auto* v = root.find("faceColor"))
        if (auto c = colorOf(v->str())) fig->style().faceColor = *c;
    auto* axes = root.find("axes");
    if (!axes || !axes->isArr()) return fig;
    for (auto& aj : std::get<std::vector<JVal>>(axes->v)) {
        uint32_t r = uint32_t(aj.find("row") ? aj.find("row")->num() : 0);
        uint32_t c = uint32_t(aj.find("col") ? aj.find("col")->num() : 0);
        uint32_t rs = uint32_t(aj.find("rowSpan") ? aj.find("rowSpan")->num(1) : 1);
        uint32_t cs = uint32_t(aj.find("colSpan") ? aj.find("colSpan")->num(1) : 1);
        Axes* ax = fig->addAxes(r, c, rs, cs);
        if (!ax) continue;
        if (auto* v = aj.find("viewport")) {
            ax->setXlim(float(v->find("xMin")->num()),
                        float(v->find("xMax")->num(1)));
            ax->setYlim(float(v->find("yMin")->num()),
                        float(v->find("yMax")->num(1)));
        }
        if (auto* v = aj.find("xlabel")) ax->style().xAxis.label = std::string(v->str());
        if (auto* v = aj.find("ylabel")) ax->style().yAxis.label = std::string(v->str());
        if (auto* v = aj.find("title")) ax->setTitle(std::string(v->str()));
        if (auto* v = aj.find("xscale")) ax->setXscale(v->str());
        if (auto* v = aj.find("yscale")) ax->setYscale(v->str());
        if (aj.find("legend") && aj.find("legend")->boolean()) ax->legend();
        if (auto* v = aj.find("xgrid")) ax->style().xAxis.grid = v->boolean();
        if (auto* v = aj.find("ygrid")) ax->style().yAxis.grid = v->boolean();
        auto* plots = aj.find("plots");
        if (!plots || !plots->isArr()) continue;
        for (auto& pj : std::get<std::vector<JVal>>(plots->v)) {
            auto type = pj.find("type") ? pj.find("type")->str() : "";
            if (type == "line") {
                Series2D s; readSeries(pj, s);
                ax->addPlot(std::make_unique<LinePlot>(std::move(s)));
            } else if (type == "scatter") {
                Series2D s; readSeries(pj, s);
                ax->addPlot(std::make_unique<ScatterPlot>(std::move(s)));
            } else if (type == "bar") {
                BarData d;
                d.heights = floatArr(pj.find("heights"));
                if (auto* v = pj.find("width")) d.width = float(v->num(0.8));
                d.horizontal = pj.find("horizontal") &&
                               pj.find("horizontal")->boolean();
                if (auto* v = pj.find("labels"))
                    for (auto& e : std::get<std::vector<JVal>>(v->v))
                        d.labels.push_back(std::string(e.str()));
                if (auto* v = pj.find("colors"))
                    for (auto& e : std::get<std::vector<JVal>>(v->v))
                        if (auto col = colorOf(e.str())) d.colors.push_back(*col);
                ax->addPlot(std::make_unique<BarPlot>(std::move(d)));
            } else if (type == "errorbar") {
                ErrorbarConfig c;
                c.xerr = floatArr(pj.find("xerr"));
                c.yerr = floatArr(pj.find("yerr"));
                c.xerrLower = floatArr(pj.find("xerrLower"));
                c.xerrUpper = floatArr(pj.find("xerrUpper"));
                c.yerrLower = floatArr(pj.find("yerrLower"));
                c.yerrUpper = floatArr(pj.find("yerrUpper"));
                if (auto* v = pj.find("color"))
                    if (auto col = colorOf(v->str())) c.color = *col;
                if (auto* v = pj.find("errorbarColor"))
                    if (auto col = colorOf(v->str())) c.errorbarColor = *col;
                if (auto* v = pj.find("drawLine")) c.drawLine = v->boolean(true);
                if (auto* v = pj.find("drawMarker")) c.drawMarker = v->boolean(true);
                if (auto* v = pj.find("drawCaps")) c.drawCaps = v->boolean(true);
                if (auto* v = pj.find("capSize")) c.capSize = float(v->num(3));
                if (auto* v = pj.find("errorevery"))
                    c.errorevery = uint32_t(v->num(1));
                if (auto* v = pj.find("label")) c.label = std::string(v->str());
                ax->addPlot(std::make_unique<ErrorbarPlot>(
                    floatArr(pj.find("x")), floatArr(pj.find("y")),
                    std::move(c)));
            }
            // Unknown types are skipped (forward compatibility).
        }
    }
    return fig;
}

bool saveFigureJson(const Figure& fig, const std::filesystem::path& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << figureToJson(fig);
    return f.good();
}

std::unique_ptr<Figure> loadFigureJson(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    std::stringstream ss;
    ss << f.rdbuf();
    return figureFromJson(ss.str());
}

} // namespace volcano::plot
