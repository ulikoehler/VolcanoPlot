// volcano/plot/Rc.cpp — runtime configuration implementation
#include "volcano/plot/Rc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace volcano::plot {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool parseBool(std::string_view v) {
    v = trim(v);
    if (v == "true" || v == "True" || v == "on" || v == "On" ||
        v == "yes" || v == "1") return true;
    return false;
}

bool isBoolValue(std::string_view v) {
    v = trim(v);
    return v == "true" || v == "True" || v == "false" || v == "False" ||
           v == "on" || v == "On" || v == "off" || v == "Off" ||
           v == "yes" || v == "no" || v == "1" || v == "0" ||
           v == "line" || v == "none" || v == "None";
}

bool parseFloat(std::string_view v, float& out) {
    v = trim(v);
    std::string s(v);
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end == s.c_str() + s.size() && end != s.c_str();
}

/// matplotlib named font sizes are relative to font.size (default 10).
bool parseFontSize(std::string_view v, float base, float& out) {
    v = trim(v);
    static const std::pair<std::string_view, float> named[] = {
        {"xx-small", 0.579f}, {"x-small", 0.694f}, {"small", 0.833f},
        {"medium", 1.0f},     {"large", 1.2f},     {"x-large", 1.44f},
        {"xx-large", 1.728f}, {"larger", 1.2f},    {"smaller", 0.833f},
    };
    for (auto [name, scale] : named) {
        if (v == name) { out = base * scale; return true; }
    }
    return parseFloat(v, out);
}

std::optional<Color> parseColor(std::string_view v) {
    v = trim(v);
    if (v.empty()) return std::nullopt;
    if (auto c = Color::parse(v)) return c;
    // .mplstyle files use bare hex without '#'.
    bool hexDigits = std::all_of(v.begin(), v.end(), [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c));
    });
    if (hexDigits && (v.size() == 6 || v.size() == 8 || v.size() == 3)) {
        std::string s = "#" + std::string(v);
        return Color::parse(s);
    }
    return std::nullopt;
}

/// Extract atoms (quoted strings or bare tokens) from a "[a, b, c]" list.
std::vector<std::string> listAtoms(std::string_view list) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < list.size()) {
        char c = list[i];
        if (c == '\'' || c == '"') {
            auto q2 = list.find(c, i + 1);
            if (q2 == std::string_view::npos) break;
            out.emplace_back(list.substr(i + 1, q2 - i - 1));
            i = q2 + 1;
        } else if (!std::isspace(static_cast<unsigned char>(c)) && c != ',') {
            size_t e = i;
            while (e < list.size() && list[e] != ',') ++e;
            auto tok = trim(list.substr(i, e - i));
            if (!tok.empty()) out.emplace_back(tok);
            i = e;
        } else ++i;
    }
    return out;
}

void applyCycleKey(CycleProps& p, std::string_view key, std::string_view val) {
    if (key == "color" || key == "c") {
        if (auto c = parseColor(val)) p.color = *c;
    } else if (key == "linestyle" || key == "ls") {
        if (auto s = lineStyleFromString(val)) p.lineStyle = *s;
    } else if (key == "linewidth" || key == "lw") {
        float w;
        if (parseFloat(val, w)) p.lineWidth = w;
    } else if (key == "marker") {
        if (val.size() >= 2 && val.front() == '$' && val.back() == '$')
            p.markerTex = std::string(val.substr(1, val.size() - 2));
        else if (val.size() == 1)
            if (auto m = markerFromChar(val[0])) p.marker = *m;
    }
}

/// Parse one "cycler(...)" term supporting both
/// cycler('color', [...]) and cycler(color=[...], ls=[...]) key forms.
/// Equal-length lists zip; a length-1 list broadcasts (matplotlib).
std::vector<CycleProps> parseCyclerTerm(std::string_view term) {
    auto lp = term.find('(');
    auto rp = term.rfind(')');
    if (lp == std::string_view::npos || rp <= lp) return {};
    std::string_view body = term.substr(lp + 1, rp - lp - 1);

    struct KeyList { std::string key; std::vector<std::string> atoms; };
    std::vector<KeyList> lists;
    size_t i = 0;
    while (i < body.size()) {
        auto lb = body.find('[', i);
        if (lb == std::string_view::npos) break;
        auto rb = body.find(']', lb);
        if (rb == std::string_view::npos) break;
        // Key precedes the list: skip spaces/commas, then either a quoted
        // name ('color', [..]) or an identifier followed by '=' (key=[..]).
        size_t e = lb;
        while (e > 0 && (std::isspace(static_cast<unsigned char>(body[e - 1])) ||
                         body[e - 1] == ',')) --e;
        std::string key;
        if (e > 0 && (body[e - 1] == '\'' || body[e - 1] == '"')) {
            size_t qc = e - 1;                      // closing quote
            auto qo = body.rfind(body[qc], qc - 1); // opening quote
            if (qo != std::string_view::npos)
                key = std::string(body.substr(qo + 1, qc - qo - 1));
        } else if (e > 0 && body[e - 1] == '=') {
            size_t ke = e - 1;
            while (ke > 0 && std::isspace(static_cast<unsigned char>(body[ke - 1]))) --ke;
            size_t ks = ke;
            while (ks > 0 && (std::isalnum(static_cast<unsigned char>(body[ks - 1])) ||
                              body[ks - 1] == '_')) --ks;
            key = std::string(body.substr(ks, ke - ks));
        }
        if (!key.empty()) {
            for (auto& ch : key) ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
            lists.push_back({std::move(key),
                             listAtoms(body.substr(lb + 1, rb - lb - 1))});
        }
        i = rb + 1;
    }
    if (lists.empty()) return {};

    size_t len = 0;
    for (auto& kl : lists) len = std::max(len, kl.atoms.size());
    if (len == 0) return {};
    for (auto& kl : lists)
        if (kl.atoms.size() != 1 && kl.atoms.size() != len) return {};

    std::vector<CycleProps> out(len);
    for (auto& kl : lists) {
        for (size_t j = 0; j < len; ++j) {
            const auto& atom = kl.atoms[kl.atoms.size() == 1 ? 0 : j];
            applyCycleKey(out[j], kl.key, atom);
        }
    }
    return out;
}

/// Parse a prop_cycle expression into a Cycler. Supports:
///   cycler('color', ['a1b2c3', '#ff0000', 'red'])
///   cycler(color=[...], linestyle=[...])
///   cycler('color',[...]) * cycler('linestyle',[...])   (outer product)
///   cycler(...) + cycler(...)                           (concatenation)
///   ['#ff0000', ...]                                    (color list)
Cycler parsePropCycle(std::string_view v) {
    if (v.find("cycler") == std::string_view::npos) {
        // Plain color list.
        auto lb = v.find('[');
        auto rb = v.rfind(']');
        if (lb == std::string_view::npos || rb <= lb) return {};
        std::vector<Color> colors;
        for (auto& a : listAtoms(v.substr(lb + 1, rb - lb - 1)))
            if (auto c = parseColor(a)) colors.push_back(*c);
        return Cycler::ofColors(std::move(colors));
    }

    // Split on top-level '*'/'+' operators.
    Cycler result;
    char op = 0;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= v.size(); ++i) {
        char c = i < v.size() ? v[i] : '\0';
        if (c == '(' || c == '[') ++depth;
        else if (c == ')' || c == ']') --depth;
        if (i == v.size() || (depth == 0 && (c == '*' || c == '+'))) {
            auto term = parseCyclerTerm(trim(v.substr(start, i - start)));
            auto tc = Cycler::ofEntries(std::move(term));
            if (op == '*') result = result * tc;
            else result = result + tc; // first term or '+'
            if (i < v.size()) op = c;
            start = i + 1;
        }
    }
    return result;
}

bool applyColor(AxisStyle& ax, std::string_view v) {
    auto c = parseColor(v);
    if (!c) return false;
    ax.color = *c;
    ax.labelColor = *c;
    return true;
}

} // namespace

bool applyRcParam(FigureStyle& s, std::string_view key, std::string_view value) {
    key = trim(key);
    value = trim(value);
    if (key.empty() || value.empty()) return false;

    // Strip inline comments after the value ('#') — but not a leading '#'
    // (hex color) or '#' inside quotes, so only strip when preceded by
    // whitespace.
    auto hash = value.find('#');
    while (hash != std::string_view::npos) {
        if (hash > 0 && std::isspace(static_cast<unsigned char>(value[hash - 1]))) {
            value = trim(value.substr(0, hash));
            break;
        }
        hash = value.find('#', hash + 1);
    }

    // ── figure ──
    if (key == "figure.facecolor" || key == "axes.facecolor") {
        auto c = parseColor(value); if (!c) return false; s.faceColor = *c; return true;
    }
    if (key == "figure.edgecolor") {
        auto c = parseColor(value); if (!c) return false; s.edgeColor = *c; return true;
    }
    if (key == "figure.dpi" || key == "savefig.dpi") {
        float f; if (!parseFloat(value, f)) return false; s.dpi = f; return true;
    }

    // ── axes ──
    if (key == "axes.edgecolor" || key == "axes.spines.color") {
        auto c = parseColor(value); if (!c) return false;
        s.xAxis.color = *c; s.yAxis.color = *c; return true;
    }
    if (key == "axes.linewidth") {
        float f; if (!parseFloat(value, f)) return false;
        s.xAxis.lineWidth = f; s.yAxis.lineWidth = f; return true;
    }
    if (key == "axes.grid") {
        if (!isBoolValue(value)) return false;
        s.xAxis.grid = parseBool(value); s.yAxis.grid = s.xAxis.grid; return true;
    }
    if (key == "axes.axisbelow") {
        // 'line' maps to the default grid-below behavior.
        s.axisBelow = (value == "true" || value == "True" ||
                       value == "line" || value == "1"); return true;
    }
    if (key == "axes.labelcolor") {
        auto c = parseColor(value); if (!c) return false;
        s.xAxis.labelColor = *c; s.yAxis.labelColor = *c; return true;
    }
    if (key == "axes.labelsize") {
        float f; if (!parseFontSize(value, s.fontSize, f)) return false;
        s.xAxis.labelFont.size = f; s.yAxis.labelFont.size = f; return true;
    }
    if (key == "axes.prop_cycle") {
        auto cyc = parsePropCycle(value);
        if (cyc.empty()) return false;
        // Mirror color entries into colorCycle for compat.
        s.colorCycle.colors.clear();
        s.propCycle.clear();
        s.propCycle.reserve(cyc.length());
        for (size_t i = 0; i < cyc.length(); ++i) {
            const auto& p = cyc.at(i);
            s.propCycle.push_back(p);
            if (p.color) s.colorCycle.colors.push_back(*p.color);
        }
        return true;
    }
    if (key == "axes.titlesize") {
        float f; if (!parseFontSize(value, s.fontSize, f)) return false;
        s.title.font.size = f; return true;
    }
    if (key == "axes.titleweight" || key == "figure.titleweight") {
        s.title.weight = std::string(value);
        s.title.font.weight = std::string(value); return true;
    }

    // ── text / font ──
    if (key == "text.color") {
        auto c = parseColor(value); if (!c) return false; s.textColor = *c; return true;
    }
    if (key == "font.family") {
        std::string f(value);
        // font.family may be a list: ['sans-serif'] — take first quoted item.
        auto q1 = f.find_first_of("'\"");
        if (q1 != std::string::npos) {
            auto q2 = f.find(f[q1], q1 + 1);
            if (q2 != std::string::npos) f = f.substr(q1 + 1, q2 - q1 - 1);
        }
        s.fontFamily = f; return true;
    }
    if (key == "font.size") {
        float f; if (!parseFloat(value, f)) return false; s.fontSize = f; return true;
    }

    // ── ticks ──
    if (key == "xtick.direction" || key == "ytick.direction") {
        auto& a = (key[0] == 'x') ? s.xAxis : s.yAxis;
        a.ticks.direction = std::string(value); return true;
    }
    if (key == "xtick.major.size" || key == "ytick.major.size") {
        float f; if (!parseFloat(value, f)) return false;
        (key[0] == 'x' ? s.xAxis : s.yAxis).ticks.majorSize = f; return true;
    }
    if (key == "xtick.minor.size" || key == "ytick.minor.size") {
        float f; if (!parseFloat(value, f)) return false;
        (key[0] == 'x' ? s.xAxis : s.yAxis).ticks.minorSize = f; return true;
    }
    if (key == "xtick.major.width" || key == "ytick.major.width") {
        float f; if (!parseFloat(value, f)) return false;
        (key[0] == 'x' ? s.xAxis : s.yAxis).ticks.majorWidth = f; return true;
    }
    if (key == "xtick.minor.width" || key == "ytick.minor.width") {
        float f; if (!parseFloat(value, f)) return false;
        (key[0] == 'x' ? s.xAxis : s.yAxis).ticks.minorWidth = f; return true;
    }
    if (key == "xtick.labelsize" || key == "ytick.labelsize") {
        float f; if (!parseFontSize(value, s.fontSize, f)) return false;
        (key[0] == 'x' ? s.xAxis : s.yAxis).tickFont.size = f; return true;
    }
    if (key == "xtick.color" || key == "ytick.color") {
        auto c = parseColor(value); if (!c) return false;
        (key[0] == 'x' ? s.xAxis : s.yAxis).labelColor = *c; return true;
    }
    if (key == "xtick.minor.visible" || key == "ytick.minor.visible") {
        bool on = value == "True" || value == "true" || value == "1" ||
                  value == "on";
        (key[0] == 'x' ? s.xAxis : s.yAxis).ticks.minor = on; return true;
    }
    if (key == "axes.grid.which") {
        s.xAxis.gridWhich = std::string(value);
        s.yAxis.gridWhich = std::string(value); return true;
    }

    // ── ScalarFormatter defaults (axes.formatter.*) ──
    if (key == "axes.formatter.limits") {
        // Value: "[lo, hi]" or "lo, hi" (e.g. "[-7, 7]").
        std::string v(value);
        v.erase(std::remove_if(v.begin(), v.end(),
                    [](char c) { return c == '[' || c == ']' || c == ' '; }),
                v.end());
        auto comma = v.find(',');
        if (comma == std::string::npos) return false;
        s.formatterLimits = {std::stoi(v.substr(0, comma)),
                             std::stoi(v.substr(comma + 1))};
        return true;
    }
    if (key == "axes.formatter.use_mathtext") {
        s.formatterUseMathText = value == "True" || value == "true" ||
                                 value == "1"; return true;
    }
    if (key == "axes.formatter.useoffset") {
        s.formatterUseOffset = value != "False" && value != "false" &&
                               value != "0"; return true;
    }

    // ── grid ──
    if (key == "grid.color") {
        auto c = parseColor(value); if (!c) return false;
        s.xAxis.gridColor = *c; s.yAxis.gridColor = *c; return true;
    }
    if (key == "grid.linewidth") {
        float f; if (!parseFloat(value, f)) return false;
        s.xAxis.gridLineWidth = f; s.yAxis.gridLineWidth = f; return true;
    }
    if (key == "grid.linestyle") {
        s.xAxis.gridLineStyle = std::string(value);
        s.yAxis.gridLineStyle = std::string(value); return true;
    }
    if (key == "grid.alpha") {
        float f; if (!parseFloat(value, f)) return false;
        s.xAxis.gridColor.a = f; s.yAxis.gridColor.a = f; return true;
    }

    // ── lines ──
    if (key == "lines.linewidth") {
        float f; if (!parseFloat(value, f)) return false;
        s.lines.lineWidth = f; return true;
    }
    if (key == "lines.solid_capstyle") {
        s.lines.solidCapStyle = std::string(value); return true;
    }
    if (key == "lines.dash_capstyle") {
        s.lines.dashCapStyle = std::string(value); return true;
    }
    if (key == "lines.solid_joinstyle") {
        s.lines.solidJoinStyle = std::string(value); return true;
    }
    if (key == "lines.dash_joinstyle") {
        s.lines.dashJoinStyle = std::string(value); return true;
    }

    // ── patch ──
    if (key == "patch.linewidth") {
        float f; if (!parseFloat(value, f)) return false;
        s.patch.lineWidth = f; return true;
    }
    if (key == "patch.facecolor") {
        auto c = parseColor(value); if (!c) return false;
        s.patch.faceColor = *c; return true;
    }
    if (key == "patch.edgecolor") {
        auto c = parseColor(value); if (!c) return false;
        s.patch.edgeColor = *c; return true;
    }
    if (key == "patch.force_edgecolor") {
        if (!isBoolValue(value)) return false;
        s.patch.forceEdgeColor = parseBool(value); return true;
    }

    // ── legend ──
    if (key == "legend.frameon") {
        if (!isBoolValue(value)) return false;
        s.legend.frameOn = parseBool(value); return true;
    }
    if (key == "legend.framealpha") {
        float f; if (!parseFloat(value, f)) return false;
        s.legend.frameAlpha = f; s.legend.faceColor.a = f; return true;
    }
    if (key == "legend.facecolor") {
        auto c = parseColor(value); if (!c) return false;
        s.legend.faceColor = *c; return true;
    }
    if (key == "legend.edgecolor") {
        auto c = parseColor(value); if (!c) return false;
        s.legend.edgeColor = *c; return true;
    }
    if (key == "legend.fontsize") {
        float f; if (!parseFontSize(value, s.fontSize, f)) return false;
        s.legend.font.size = f; return true;
    }
    if (key == "legend.loc") {
        s.legend.location = std::string(value); return true;
    }
    if (key == "legend.ncols" || key == "legend.ncol") {
        float f; if (!parseFloat(value, f) || f < 1) return false;
        s.legend.ncols = static_cast<int>(f); return true;
    }
    if (key == "legend.title_fontsize") {
        float f; if (!parseFontSize(value, s.fontSize, f)) return false;
        s.legend.titleFont.size = f; return true;
    }
    if (key == "legend.labelcolor") {
        if (value == "inherit" || value == "None" || value == "none") {
            s.legend.labelColor.reset(); return true;
        }
        auto c = parseColor(value); if (!c) return false;
        s.legend.labelColor = *c; return true;
    }
    if (key == "legend.fancybox") {
        if (!isBoolValue(value)) return false;
        s.legend.fancyBox = parseBool(value); return true;
    }
    if (key == "legend.shadow") {
        if (!isBoolValue(value)) return false;
        s.legend.shadow = parseBool(value); return true;
    }
    if (key == "legend.handlelength" || key == "legend.handletextpad" ||
        key == "legend.borderpad" || key == "legend.columnspacing" ||
        key == "legend.borderaxespad") {
        float f; if (!parseFloat(value, f)) return false;
        if (key == "legend.handlelength")      s.legend.handleLength = f;
        else if (key == "legend.handletextpad")   s.legend.handleTextPad = f;
        else if (key == "legend.borderpad")       s.legend.borderPad = f;
        else if (key == "legend.columnspacing")   s.legend.columnSpacing = f;
        else s.legend.borderAxesPad = f;
        return true;
    }

    // ── image ──
    if (key == "image.cmap") {
        s.colorbar.colormap = std::string(value); return true;
    }

    return false;
}

size_t applyRcText(FigureStyle& style, std::string_view text) {
    size_t applied = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        auto nl = text.find('\n', pos);
        auto line = text.substr(pos, nl == std::string_view::npos
                                     ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;

        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        if (applyRcParam(style, line.substr(0, colon), line.substr(colon + 1)))
            ++applied;
    }
    return applied;
}

// ─── rc ───────────────────────────────────────────────────────────────────

namespace rc {

FigureStyle& params() {
    static FigureStyle p = styles::defaultStyle();
    return p;
}

void rcdefaults() {
    params() = styles::defaultStyle();
}

bool set(std::string_view key, std::string_view value) {
    return applyRcParam(params(), key, value);
}

bool loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    return applyRcText(params(), content) > 0;
}

Context::Context() : saved_(params()) {}
Context::Context(const FigureStyle& style) : saved_(params()) {
    params() = style;
}
Context::~Context() { if (active_) params() = saved_; }

} // namespace rc

// ─── style ────────────────────────────────────────────────────────────────

namespace style {

const std::vector<std::string>& available() {
    static const std::vector<std::string> names = {
        "default", "classic", "ggplot", "bmh", "fivethirtyeight",
        "dark_background", "grayscale", "Solarize_Light2", "fast", "xkcd",
        "seaborn-v0_8", "seaborn-v0_8-bright", "seaborn-v0_8-colorblind",
        "seaborn-v0_8-dark", "seaborn-v0_8-darkgrid", "seaborn-v0_8-deep",
        "seaborn-v0_8-muted", "seaborn-v0_8-notebook", "seaborn-v0_8-paper",
        "seaborn-v0_8-pastel", "seaborn-v0_8-poster", "seaborn-v0_8-talk",
        "seaborn-v0_8-ticks", "seaborn-v0_8-white", "seaborn-v0_8-whitegrid",
        "tableau-colorblind10", "petroff6", "petroff8", "petroff10",
    };
    return names;
}

bool use(const std::string& name) {
    if (auto* fn = styles::byName(name)) {
        rc::params() = fn();
        return true;
    }
    // Not a builtin name — treat as a path to a .mplstyle/matplotlibrc file.
    return rc::loadFile(name);
}

bool use(const std::vector<std::string>& names) {
    // matplotlib applies composable styles on top of the current params,
    // in order. Builtin sheets replace the whole style; .mplstyle files
    // apply only the params they set.
    bool ok = true;
    for (const auto& n : names) {
        if (auto* fn = styles::byName(n)) {
            // Preserve nothing from prior builtins (they're complete styles),
            // but custom file params applied earlier are lost — match
            // matplotlib semantics where each sheet fully replaces.
            rc::params() = fn();
        } else if (!rc::loadFile(n)) {
            ok = false;
        }
    }
    return ok;
}

bool use(std::initializer_list<std::string> names) {
    return use(std::vector<std::string>(names));
}

rc::Context context(const std::string& name) {
    rc::Context ctx;
    use(name);
    return ctx;
}

rc::Context context(const std::vector<std::string>& names) {
    rc::Context ctx;
    use(names);
    return ctx;
}

} // namespace style

} // namespace volcano::plot
