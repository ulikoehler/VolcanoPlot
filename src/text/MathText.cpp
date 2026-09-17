// volcano/text/MathText.cpp — matplotlib-style MathText (TeX subset)
#include "volcano/text/MathText.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <string>

namespace volcano::text {

// The renderer's base font size in pixels at scale 1.0.
static constexpr float kBaseFontPx = 16.0f;

bool containsMath(std::string_view s) noexcept {
    return s.find('$') != std::string_view::npos;
}

// ─── Symbol tables ─────────────────────────────────────────────────────────

namespace {

const std::map<std::string_view, std::string_view>& symbolMap() {
    static const std::map<std::string_view, std::string_view> m = {
        // Greek lowercase
        {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"},
        {"epsilon", "ϵ"}, {"varepsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"},
        {"theta", "θ"}, {"vartheta", "ϑ"}, {"iota", "ι"}, {"kappa", "κ"},
        {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"},
        {"pi", "π"}, {"varpi", "ϖ"}, {"rho", "ρ"}, {"varrho", "ϱ"},
        {"sigma", "σ"}, {"varsigma", "ς"}, {"tau", "τ"}, {"upsilon", "υ"},
        {"phi", "ϕ"}, {"varphi", "φ"}, {"chi", "χ"}, {"psi", "ψ"},
        {"omega", "ω"},
        // Greek uppercase
        {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"},
        {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"}, {"Upsilon", "Υ"},
        {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"},
        // Operators & relations
        {"times", "×"}, {"div", "÷"}, {"pm", "±"}, {"mp", "∓"},
        {"cdot", "⋅"}, {"cdots", "⋯"}, {"ldots", "…"}, {"dots", "…"},
        {"leq", "≤"}, {"le", "≤"}, {"geq", "≥"}, {"ge", "≥"},
        {"neq", "≠"}, {"ne", "≠"}, {"approx", "≈"}, {"simeq", "≃"},
        {"cong", "≅"}, {"equiv", "≡"}, {"sim", "∼"}, {"propto", "∝"},
        {"ll", "≪"}, {"gg", "≫"},
        // Sets & logic
        {"in", "∈"}, {"notin", "∉"}, {"ni", "∋"},
        {"subset", "⊂"}, {"supset", "⊃"}, {"subseteq", "⊆"},
        {"supseteq", "⊇"}, {"cup", "∪"}, {"cap", "∩"},
        {"setminus", "∖"}, {"emptyset", "∅"}, {"forall", "∀"},
        {"exists", "∃"}, {"nexists", "∄"}, {"neg", "¬"}, {"land", "∧"},
        {"wedge", "∧"}, {"lor", "∨"}, {"vee", "∨"},
        // Arrows
        {"rightarrow", "→"}, {"to", "→"}, {"leftarrow", "←"},
        {"gets", "←"}, {"Rightarrow", "⇒"}, {"Leftarrow", "⇐"},
        {"leftrightarrow", "↔"}, {"Leftrightarrow", "⇔"},
        {"mapsto", "↦"}, {"uparrow", "↑"}, {"downarrow", "↓"},
        // Big operators & misc
        {"sum", "∑"}, {"prod", "∏"}, {"coprod", "∐"}, {"int", "∫"},
        {"iint", "∬"}, {"iiint", "∭"}, {"oint", "∮"},
        {"bigcup", "⋃"}, {"bigcap", "⋂"}, {"bigoplus", "⨁"},
        {"bigotimes", "⨂"}, {"bigodot", "⨀"}, {"biguplus", "⨄"},
        {"bigsqcup", "⨆"}, {"bigvee", "⋁"}, {"bigwedge", "⋀"},
        {"partial", "∂"}, {"nabla", "∇"}, {"infty", "∞"},
        {"hbar", "ℏ"}, {"ell", "ℓ"}, {"Re", "ℜ"}, {"Im", "ℑ"},
        {"wp", "℘"}, {"aleph", "ℵ"}, {"imath", "ı"}, {"jmath", "ȷ"},
        {"degree", "°"}, {"circ", "∘"}, {"bullet", "•"},
        {"star", "⋆"}, {"ast", "∗"}, {"prime", "′"},
        {"oplus", "⊕"}, {"ominus", "⊖"}, {"otimes", "⊗"},
        {"oslash", "⊘"}, {"odot", "⊙"}, {"perp", "⊥"},
        {"parallel", "∥"}, {"angle", "∠"}, {"triangle", "△"},
        {"square", "□"}, {"blacksquare", "■"}, {"diamond", "⋄"},
        {"langle", "⟨"}, {"rangle", "⟩"}, {"lceil", "⌈"},
        {"rceil", "⌉"}, {"lfloor", "⌊"}, {"rfloor", "⌋"},
        {"lbrace", "{"}, {"rbrace", "}"}, {"lvert", "|"}, {"rvert", "|"},
        {"mid", "∣"}, {"top", "⊤"}, {"bot", "⊥"}, {"vdash", "⊢"},
        {"dashv", "⊣"}, {"models", "⊨"}, {"therefore", "∴"},
        {"because", "∵"}, {"%", "%"}, {"#", "#"}, {"&", "&"}, {"_", "_"},
    };
    return m;
}

// Combining accent marks (Unicode combining diacriticals).
const std::map<std::string_view, std::string_view>& accentMap() {
    static const std::map<std::string_view, std::string_view> m = {
        {"hat", "̂"}, {"widehat", "̂"},
        {"tilde", "̃"}, {"widetilde", "̃"},
        {"bar", "̄"}, {"dot", "̇"}, {"ddot", "̈"},
        {"vec", "⃗"}, {"breve", "̆"}, {"check", "̌"},
        {"acute", "́"}, {"grave", "̀"},
    };
    return m;
}

// Spacing commands → em widths.
const std::map<std::string_view, float>& spaceMap() {
    static const std::map<std::string_view, float> m = {
        {",", 3.0f/18.0f}, {":", 4.0f/18.0f}, {";", 5.0f/18.0f},
        {"!", -3.0f/18.0f}, {" ", 0.25f}, {"quad", 1.0f},
        {"qquad", 2.0f},
    };
    return m;
}

} // namespace

// ─── Box model ─────────────────────────────────────────────────────────────
//
// All box metrics (w/h/d) and stored offsets are absolute pixels, computed
// at parse time. `rel` is the font scale factor relative to baseScale —
// scripts measure runs at rel*0.7 etc.

namespace {

struct Ctx {
    const MeasureFn& measure;
    float baseScale;
};

struct Box {
    float w = 0, h = 0, d = 0;  // width, height(above base), depth(below)
    virtual ~Box() = default;
    virtual void emit(MathLayout& out, float x, float baseline) const = 0;
};

using BoxPtr = std::unique_ptr<Box>;

struct SpaceBox : Box {
    void emit(MathLayout&, float, float) const override {}
};

struct RunBox : Box {
    std::string text;
    float rel = 1.0f;  // render scale relative to baseScale
    void emit(MathLayout& out, float x, float baseline) const override {
        if (text.empty()) return;
        out.runs.push_back({text, x, baseline, rel});
    }
};

struct HBox : Box {
    std::vector<BoxPtr> kids;
    void emit(MathLayout& out, float x, float baseline) const override {
        float cx = x;
        for (const auto& k : kids) {
            k->emit(out, cx, baseline);
            cx += k->w;
        }
    }
};

struct ScriptBox : Box {
    BoxPtr base, sup, sub;
    float supShift = 0, subShift = 0;  // baseline offsets (px)
    void emit(MathLayout& out, float x, float baseline) const override {
        base->emit(out, x, baseline);
        float sx = x + base->w;
        if (sup) sup->emit(out, sx, baseline + supShift);
        if (sub) sub->emit(out, sx, baseline + subShift);
    }
};

struct FracBox : Box {
    BoxPtr num, den;
    float ruleY = 0;                 // rule offset rel. to baseline (px, -up)
    float thick = 1.0f;
    float numBase = 0, denBase = 0;  // baseline offsets (px)
    void emit(MathLayout& out, float x, float baseline) const override {
        float cx = x + w / 2.0f;
        num->emit(out, cx - num->w / 2.0f, baseline + numBase);
        den->emit(out, cx - den->w / 2.0f, baseline + denBase);
        out.rules.push_back({x, baseline + ruleY, x + w, thick});
    }
};

struct SqrtBox : Box {
    BoxPtr inner;
    std::string sign;       // "√" run (empty for \overline)
    float rel = 1.0f;
    float signW = 0;
    float thick = 1.0f;
    void emit(MathLayout& out, float x, float baseline) const override {
        if (!sign.empty())
            out.runs.push_back({sign, x, baseline, rel});
        inner->emit(out, x + signW, baseline);
        // Overline rule above the radicand.
        out.rules.push_back({x + signW * 0.6f, baseline - h, x + w, thick});
    }
};

/// A box with a rule drawn above or below it (\overline, \underline).
struct RuledBox : Box {
    BoxPtr inner;
    float ruleOff = 0;   // rule y offset rel. to baseline (px)
    float thick = 1.0f;
    void emit(MathLayout& out, float x, float baseline) const override {
        inner->emit(out, x, baseline);
        out.rules.push_back({x, baseline + ruleOff, x + w, thick});
    }
};

// ─── Parser ────────────────────────────────────────────────────────────────

struct Parser {
    std::string_view s;
    size_t pos = 0;
    const Ctx& ctx;

    char peek() const { return pos < s.size() ? s[pos] : '\0'; }
    bool eof() const { return pos >= s.size(); }

    void skipSpaces() { while (peek() == ' ' || peek() == '\t') ++pos; }

    // One UTF-8 codepoint length starting at pos.
    size_t cpLen() const {
        unsigned char c = static_cast<unsigned char>(s[pos]);
        size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        return std::min(len, s.size() - pos);
    }

    float em(float rel) const { return kBaseFontPx * ctx.baseScale * rel; }

    BoxPtr runBox(std::string text, float rel) {
        auto b = std::make_unique<RunBox>();
        b->text = std::move(text);
        b->rel = rel;
        auto m = ctx.measure(b->text, ctx.baseScale * rel);
        b->w = m.width;
        b->h = m.ascent;
        b->d = m.height - m.ascent;
        return b;
    }

    BoxPtr spaceBox(float emWidth, float rel) {
        auto b = std::make_unique<SpaceBox>();
        b->w = emWidth * em(rel);
        return b;
    }

    // Parse a required argument: {group} or a single token.
    BoxPtr parseArg(float rel) {
        skipSpaces();
        if (peek() == '{') { ++pos; return parseGroup('}', rel); }
        if (peek() == '\\') return parseCommand(rel);
        if (eof()) return spaceBox(0.0f, rel);
        size_t start = pos;
        pos += cpLen();
        return runBox(std::string(s.substr(start, pos - start)), rel);
    }

    void finishHBox(HBox& h) {
        h.w = 0; h.h = 0; h.d = 0;
        for (const auto& k : h.kids) {
            h.w += k->w;
            h.h = std::max(h.h, k->h);
            h.d = std::max(h.d, k->d);
        }
    }

    // Parse until `close` (consumed) or end of input.
    BoxPtr parseGroup(char close, float rel) {
        auto h = std::make_unique<HBox>();
        while (!eof() && peek() != close) {
            auto atom = parseAtom(rel);
            if (!atom) break;
            h->kids.push_back(std::move(atom));
        }
        if (peek() == close) ++pos;
        finishHBox(*h);
        return h;
    }

    // Parse one atom plus any trailing ^/_ scripts.
    BoxPtr parseAtom(float rel) {
        BoxPtr base;
        if (peek() == '{') {
            ++pos;
            base = parseGroup('}', rel);
        } else if (peek() == '\\') {
            base = parseCommand(rel);
        } else if (peek() == '^' || peek() == '_') {
            base = spaceBox(0.0f, rel);  // script with empty base
        } else {
            size_t start = pos;
            while (!eof() && peek() != '^' && peek() != '_' &&
                   peek() != '{' && peek() != '}' && peek() != '\\' &&
                   peek() != ' ' && peek() != '\t' && peek() != '$')
                pos += cpLen();
            if (pos == start) { ++pos; return spaceBox(0.0f, rel); }
            base = runBox(std::string(s.substr(start, pos - start)), rel);
        }
        // Trailing scripts (sup and/or sub, either order).
        BoxPtr sup, sub;
        for (;;) {
            if (peek() == '^') { ++pos; sup = parseArg(rel * 0.7f); }
            else if (peek() == '_') { ++pos; sub = parseArg(rel * 0.7f); }
            else break;
        }
        if (!sup && !sub) return base;
        auto sb = std::make_unique<ScriptBox>();
        float e = em(rel);
        sb->supShift = -0.45f * e;   // up
        sb->subShift = +0.30f * e;   // down
        sb->base = std::move(base);
        sb->sup = std::move(sup);
        sb->sub = std::move(sub);
        float supW = sb->sup ? sb->sup->w : 0.0f;
        float subW = sb->sub ? sb->sub->w : 0.0f;
        sb->w = sb->base->w + std::max(supW, subW);
        sb->h = sb->base->h;
        sb->d = sb->base->d;
        if (sb->sup) sb->h = std::max(sb->h, -sb->supShift + sb->sup->h);
        if (sb->sub) sb->d = std::max(sb->d, sb->subShift + sb->sub->d);
        return sb;
    }

    BoxPtr parseCommand(float rel) {
        ++pos;  // consume '\'
        // Command name: [a-zA-Z]+ or a single non-letter char.
        size_t start = pos;
        if (std::isalpha(static_cast<unsigned char>(peek()))) {
            while (std::isalpha(static_cast<unsigned char>(peek()))) ++pos;
        } else if (!eof()) {
            ++pos;
        }
        std::string_view name = s.substr(start, pos - start);

        if (name == "frac" || name == "dfrac" || name == "tfrac") {
            auto num = parseArg(rel);
            auto den = parseArg(rel);
            auto f = std::make_unique<FracBox>();
            float e = em(rel);
            f->thick = std::max(1.0f, 0.05f * e);
            float axis = 0.28f * e;      // math axis height above baseline
            float gap = 0.10f * e + f->thick;
            f->ruleY = -axis;
            f->numBase = -axis - gap - num->d;
            f->denBase = -axis + gap + den->h;
            f->num = std::move(num);
            f->den = std::move(den);
            f->w = std::max(f->num->w, f->den->w) + 0.2f * e;
            f->h = -f->numBase + f->num->h;
            f->d = f->denBase + f->den->d;
            return f;
        }
        if (name == "sqrt") {
            skipSpaces();
            if (peek() == '[') {  // \sqrt[n]{x} — skip the index
                while (!eof() && peek() != ']') ++pos;
                if (peek() == ']') ++pos;
            }
            auto inner = parseArg(rel);
            auto b = std::make_unique<SqrtBox>();
            auto m = ctx.measure("√", ctx.baseScale * rel);
            b->sign = "√";
            b->rel = rel;
            b->signW = m.width + 0.10f * em(rel);
            b->thick = std::max(1.0f, 0.05f * em(rel));
            b->inner = std::move(inner);
            b->w = b->signW + b->inner->w;
            b->h = b->inner->h + 0.15f * em(rel) + b->thick;
            b->d = b->inner->d;
            return b;
        }
        if (name == "overline" || name == "overbar" || name == "underline") {
            auto inner = parseArg(rel);
            auto b = std::make_unique<RuledBox>();
            b->thick = std::max(1.0f, 0.05f * em(rel));
            bool over = (name != "underline");
            b->ruleOff = over ? -(inner->h + 0.10f * em(rel))
                              : (inner->d + 0.10f * em(rel));
            b->inner = std::move(inner);
            b->w = b->inner->w;
            b->h = over ? -b->ruleOff + b->thick : b->inner->h;
            b->d = over ? b->inner->d : b->ruleOff + b->thick;
            return b;
        }
        // Accents: \hat{x} → x + combining mark.
        if (auto it = accentMap().find(name); it != accentMap().end()) {
            auto inner = parseArg(rel);
            if (auto* rb = dynamic_cast<RunBox*>(inner.get());
                rb && !rb->text.empty()) {
                rb->text += it->second;
                auto m = ctx.measure(rb->text, ctx.baseScale * rel);
                rb->w = m.width; rb->h = m.ascent; rb->d = m.height - m.ascent;
                return inner;
            }
            auto h = std::make_unique<HBox>();
            h->kids.push_back(std::move(inner));
            h->kids.push_back(runBox(std::string(it->second), rel));
            finishHBox(*h);
            return h;
        }
        // \left \right \big … — render the following delimiter plainly.
        if (name == "left" || name == "right" || name == "big" ||
            name == "Big" || name == "bigg" || name == "Bigg" ||
            name == "bigl" || name == "bigr" || name == "Bigl" ||
            name == "Bigr") {
            skipSpaces();
            if (peek() == '\\') return parseCommand(rel);  // e.g. \langle
            if (peek() == '.') { ++pos; return spaceBox(0.0f, rel); }
            return parseArg(rel);
        }
        // Font-variant groups: render contents plainly.
        if (name == "mathrm" || name == "mathbf" || name == "mathit" ||
            name == "mathsf" || name == "mathtt" || name == "mathcal" ||
            name == "mathbb" || name == "mathfrak" || name == "rm" ||
            name == "bf" || name == "it" || name == "sf" || name == "tt" ||
            name == "text" || name == "operatorname" || name == "boldsymbol") {
            return parseArg(rel);
        }
        // Spacing commands.
        if (auto it = spaceMap().find(name); it != spaceMap().end())
            return spaceBox(it->second, rel);
        // Symbol lookup.
        if (auto it = symbolMap().find(name); it != symbolMap().end())
            return runBox(std::string(it->second), rel);
        // Escaped chars / unknown commands render literally.
        if (name.size() == 1)
            return runBox(std::string(name), rel);
        return runBox("\\" + std::string(name), rel);
    }
};

// Lay out one segment (plain or math) into a box.
BoxPtr layoutSegment(std::string_view s, bool math, const Ctx& ctx) {
    if (!math) {
        Parser p{s, 0, ctx};
        return p.runBox(std::string(s), 1.0f);
    }
    Parser p{s, 0, ctx};
    auto h = std::make_unique<HBox>();
    while (!p.eof()) {
        auto atom = p.parseAtom(1.0f);
        if (!atom) break;
        h->kids.push_back(std::move(atom));
    }
    p.finishHBox(*h);
    return h;
}

} // namespace

// ─── Public API ────────────────────────────────────────────────────────────

MathLayout layoutMathText(std::string_view text, float baseScale,
                          const MeasureFn& measure) {
    MathLayout out;
    Ctx ctx{measure, baseScale};
    auto h = std::make_unique<HBox>();

    // Split into plain and $...$ math segments.
    size_t pos = 0;
    while (pos < text.size()) {
        size_t dollar = text.find('$', pos);
        if (dollar == std::string_view::npos) {
            auto seg = text.substr(pos);
            if (!seg.empty())
                h->kids.push_back(layoutSegment(seg, false, ctx));
            break;
        }
        if (dollar > pos) {
            auto seg = text.substr(pos, dollar - pos);
            if (!seg.empty())
                h->kids.push_back(layoutSegment(seg, false, ctx));
        }
        size_t close = text.find('$', dollar + 1);
        if (close == std::string_view::npos) {
            // Unmatched '$' — render the rest literally.
            h->kids.push_back(layoutSegment(text.substr(dollar), false, ctx));
            break;
        }
        auto seg = text.substr(dollar + 1, close - dollar - 1);
        h->kids.push_back(layoutSegment(seg, true, ctx));
        pos = close + 1;
    }

    float w = 0, asc = 0, desc = 0;
    for (const auto& k : h->kids) {
        w += k->w;
        asc = std::max(asc, k->h);
        desc = std::max(desc, k->d);
    }
    h->w = w; h->h = asc; h->d = desc;

    h->emit(out, 0.0f, 0.0f);
    out.width = w;
    out.ascent = asc;
    out.descent = desc;
    return out;
}

std::string mathTextToUnicode(std::string_view text) {
    std::string out;
    bool inMath = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == '$') { inMath = !inMath; continue; }
        if (inMath && ch == '\\') {
            size_t j = i + 1;
            while (j < text.size() &&
                   std::isalpha(static_cast<unsigned char>(text[j])))
                ++j;
            auto name = text.substr(i + 1, j - i - 1);
            if (auto it = symbolMap().find(name); it != symbolMap().end())
                out += it->second;
            else
                out += name;          // unknown command → literal name
            i = j - 1;
            continue;
        }
        // Drop grouping braces and script operators (layout concerns).
        if (inMath && (ch == '{' || ch == '}' || ch == '_' || ch == '^'))
            continue;
        out += ch;
    }
    return out;
}

} // namespace volcano::text
