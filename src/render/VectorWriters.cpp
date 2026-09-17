// volcano/render/VectorWriters.cpp — SVG / PDF / EPS / PGF canvases
#include "volcano/render/VectorWriters.hpp"
#include "volcano/encode/ImageEncoder.hpp"
#include "volcano/encode/MovieWriter.hpp"  // base64Encode

#ifdef VOLCANO_HAS_ZLIB
#include <zlib.h>
#endif

#include <cmath>
#include <format>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace volcano::render {
namespace {

using plot::Color;
using plot::Point2D;
using plot::Rect2D;

// ─── shared helpers ─────────────────────────────────────────────────────────

std::string num(float v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::string cssColor(const Color& c) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  int(std::clamp(c.r, 0.f, 1.f) * 255 + 0.5f),
                  int(std::clamp(c.g, 0.f, 1.f) * 255 + 0.5f),
                  int(std::clamp(c.b, 0.f, 1.f) * 255 + 0.5f));
    return buf;
}

std::string pdfColor(const Color& c) {
    return std::format("{:.3f} {:.3f} {:.3f}", c.r, c.g, c.b);
}

/// XML-escape a string.
std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

/// UTF-8 → Latin-1 best effort (PDF base-14 / PostScript text).
/// Unsupported codepoints become '?'.
std::string toLatin1(std::string_view utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        auto c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) { out += char(c); ++i; continue; }
        uint32_t cp = 0; int n = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
        else { ++i; continue; }
        for (int k = 1; k <= n && i + k < utf8.size(); ++k)
            cp = (cp << 6) | (utf8[i + k] & 0x3F);
        i += n + 1;
        out += cp <= 0xFF ? char(cp) : '?';
    }
    return out;
}

/// Escape a string for a PDF/PS literal ( ... ).
std::string psEscape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

/// Encode RGBA8 as PNG bytes via the CPU encoder.
std::vector<uint8_t> pngBytes(std::span<const uint8_t> rgba,
                              uint32_t w, uint32_t h) {
    auto enc = encode::createCpuEncoder(encode::ImageFormat::Png);
    if (!enc) return {};
    return enc->encode(rgba, w, h).bytes;
}

std::vector<uint8_t> deflate(std::span<const uint8_t> in) {
#ifdef VOLCANO_HAS_ZLIB
    uLong bound = compressBound(uLong(in.size()));
    std::vector<uint8_t> out(bound);
    uLongf n = bound;
    if (compress2(out.data(), &n, in.data(), uLong(in.size()), 6) == Z_OK) {
        out.resize(n);
        return out;
    }
#endif
    return {in.begin(), in.end()};
}

bool zlibAvailable() {
#ifdef VOLCANO_HAS_ZLIB
    return true;
#else
    return false;
#endif
}

/// Write whole file, returns success.
bool writeFile(const std::filesystem::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(s.data(), std::streamsize(s.size()));
    return f.good();
}
bool writeFile(const std::filesystem::path& p,
               const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()),
            std::streamsize(b.size()));
    return f.good();
}

// ─── SVG ────────────────────────────────────────────────────────────────────

class SvgCanvas : public VectorCanvas {
public:
    SvgCanvas(float w, float h, VectorOptions o)
        : w_(w), h_(h), opts_(std::move(o)) {}

    void polyline(std::span<const Point2D> pts, const Pen& pen) override {
        if (pts.size() < 2) return;
        body_ += "<path d=\"" + pathData(pts, false) + "\" fill=\"none\" "
               + penAttrs(pen) + "/>\n";
    }

    void polygon(std::span<const Point2D> pts, Color fill,
                 const Pen* stroke) override {
        if (pts.size() < 3) return;
        body_ += "<path d=\"" + pathData(pts, true) + "\" fill=\""
               + cssColor(fill) + "\" fill-opacity=\"" + num(fill.a) + "\"";
        body_ += stroke ? " " + penAttrs(*stroke) : " stroke=\"none\"";
        body_ += "/>\n";
    }

    void text(Point2D base, std::string_view utf8, float sizePx,
              Color color, float rot) override {
        if (utf8.empty()) return;
        // rot: radians CW-positive in Y-down — same as SVG rotate(deg).
        float deg = rot * 180.0f / float(M_PI);
        body_ += "<text x=\"" + num(base.x) + "\" y=\"" + num(base.y)
               + "\" font-family=\"DejaVu Sans\" font-size=\""
               + num(sizePx) + "\" fill=\"" + cssColor(color)
               + "\" fill-opacity=\"" + num(color.a) + "\"";
        if (rot != 0.0f)
            body_ += " transform=\"rotate(" + num(deg) + " " + num(base.x)
                   + " " + num(base.y) + ")\"";
        body_ += ">" + xmlEscape(utf8) + "</text>\n";
    }

    void image(Rect2D r, uint32_t w, uint32_t h,
               std::span<const uint8_t> rgba) override {
        auto png = pngBytes(rgba, w, h);
        if (png.empty()) return;
        body_ += "<image x=\"" + num(float(r.x)) + "\" y=\"" + num(float(r.y))
               + "\" width=\"" + num(float(r.width)) + "\" height=\""
               + num(float(r.height))
               + "\" preserveAspectRatio=\"none\" href=\"data:image/png;"
                 "base64," + encode::base64Encode(png) + "\"/>\n";
    }

    void pushClip(Rect2D r) override {
        clipDefs_ += "<clipPath id=\"c" + std::to_string(nClips_)
                   + "\"><rect x=\"" + num(float(r.x)) + "\" y=\""
                   + num(float(r.y)) + "\" width=\"" + num(float(r.width))
                   + "\" height=\"" + num(float(r.height)) + "\"/></clipPath>\n";
        body_ += "<g clip-path=\"url(#c" + std::to_string(nClips_++)
               + ")\">\n";
    }
    void popClip() override { body_ += "</g>\n"; }

    bool finish(const std::filesystem::path& path) override {
        std::string doc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg"
            " xmlns=\"http://www.w3.org/2000/svg\""
            " xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\""
            + num(w_) + "\" height=\"" + num(h_) + "\" viewBox=\"0 0 "
            + num(w_) + " " + num(h_) + "\">\n";
        for (auto& [k, v] : opts_.metadata)
            doc += "<!-- " + k + ": " + xmlEscape(v) + " -->\n";
        if (!clipDefs_.empty()) doc += "<defs>\n" + clipDefs_ + "</defs>\n";
        if (opts_.facecolor.a > 0)
            doc += "<rect width=\"100%\" height=\"100%\" fill=\""
                 + cssColor(opts_.facecolor) + "\"/>\n";
        doc += body_ + "</svg>\n";
        return writeFile(path, doc);
    }

private:
    std::string pathData(std::span<const Point2D> pts, bool closed) {
        std::string d = "M " + num(pts[0].x) + " " + num(pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i)
            d += " L " + num(pts[i].x) + " " + num(pts[i].y);
        if (closed) d += " Z";
        return d;
    }
    std::string penAttrs(const Pen& p) {
        std::string s = "stroke=\"" + cssColor(p.color)
            + "\" stroke-opacity=\"" + num(p.color.a)
            + "\" stroke-width=\"" + num(p.width) + "\"";
        if (!p.dashes.empty()) {
            s += " stroke-dasharray=\"";
            for (size_t i = 0; i < p.dashes.size(); ++i)
                s += (i ? "," : "") + num(p.dashes[i]);
            s += "\"";
            if (p.dashOffset != 0)
                s += " stroke-dashoffset=\"" + num(p.dashOffset) + "\"";
        }
        if (p.cap == plot::CapStyle::Round) s += " stroke-linecap=\"round\"";
        else if (p.cap == plot::CapStyle::Projecting)
            s += " stroke-linecap=\"square\"";
        if (p.join == plot::JoinStyle::Round) s += " stroke-linejoin=\"round\"";
        else if (p.join == plot::JoinStyle::Bevel)
            s += " stroke-linejoin=\"bevel\"";
        return s;
    }

    float w_, h_;
    VectorOptions opts_;
    std::string body_, clipDefs_;
    int nClips_ = 0;
};

// ─── PDF ────────────────────────────────────────────────────────────────────

/// Minimal single-page PDF. All coordinates converted to PDF y-up points
/// (1 px = 1 pt at 100dpi ≈ figure-space units; we use px directly).
class PdfCanvas : public VectorCanvas {
public:
    PdfCanvas(float w, float h, VectorOptions o)
        : w_(w), h_(h), opts_(std::move(o)) {}

    void polyline(std::span<const Point2D> pts, const Pen& pen) override {
        if (pts.size() < 2) return;
        setPen(pen);
        content_ += num(pts[0].x) + " " + num(yUp(pts[0].y)) + " m\n";
        for (size_t i = 1; i < pts.size(); ++i)
            content_ += num(pts[i].x) + " " + num(yUp(pts[i].y)) + " l\n";
        content_ += "S\n";
    }

    void polygon(std::span<const Point2D> pts, Color fill,
                 const Pen* stroke) override {
        if (pts.size() < 3) return;
        content_ += "q\n" + pdfColor(fill) + " rg\n";
        setAlpha(fill.a);
        content_ += num(pts[0].x) + " " + num(yUp(pts[0].y)) + " m\n";
        for (size_t i = 1; i < pts.size(); ++i)
            content_ += num(pts[i].x) + " " + num(yUp(pts[i].y)) + " l\n";
        content_ += "h\n";
        if (stroke) {
            setPen(*stroke);
            content_ += "B\n";  // fill + stroke
        } else {
            content_ += "f\n";
        }
        content_ += "Q\n";
    }

    void text(Point2D base, std::string_view utf8, float sizePx,
              Color color, float rot) override {
        if (utf8.empty()) return;
        // rot: radians CW-positive in Y-down. Viewed in PDF's y-up device
        // space that is CCW-negative → θ_pdf = -rot, giving Tm =
        // [cosθ sinθ -sinθ cosθ x y] = [cos rot, -sin rot, sin rot, cos rot].
        float c = std::cos(rot), s = std::sin(rot);
        content_ += "BT /F1 " + num(sizePx) + " Tf " + pdfColor(color)
            + " rg\n" + num(c) + " " + num(-s) + " " + num(s) + " "
            + num(c) + " " + num(base.x) + " " + num(yUp(base.y))
            + " Tm\n(" + psEscape(toLatin1(utf8)) + ") Tj\nET\n";
    }

    void image(Rect2D r, uint32_t w, uint32_t h,
               std::span<const uint8_t> rgba) override {
        // Split RGB + alpha; register XObject (+ SMask).
        std::vector<uint8_t> rgb(size_t(w) * h * 3), alpha(size_t(w) * h);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            rgb[i * 3 + 0] = rgba[i * 4 + 0];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
            alpha[i] = rgba[i * 4 + 3];
        }
        // Image rows are bottom-up in PDF when we place via cm; flip rows.
        std::vector<uint8_t> rgbF(rgb.size()), aF(alpha.size());
        for (uint32_t y = 0; y < h; ++y) {
            auto* src = rgb.data() + size_t(y) * w * 3;
            auto* dst = rgbF.data() + size_t(h - 1 - y) * w * 3;
            std::copy(src, src + size_t(w) * 3, dst);
            auto* sa = alpha.data() + size_t(y) * w;
            auto* da = aF.data() + size_t(h - 1 - y) * w;
            std::copy(sa, sa + w, da);
        }
        int smask = addImageObj(aF, w, h, "/DeviceGray");
        int img = addImageObj(rgbF, w, h, "/DeviceRGB", smask);
        float x = float(r.x), yTop = float(r.y);
        content_ += "q\n" + num(float(r.width)) + " 0 0 " + num(float(r.height))
            + " " + num(x) + " " + num(yUp(yTop + float(r.height)))
            + " cm\n/Im" + std::to_string(img) + " Do\nQ\n";
    }

    void pushClip(Rect2D r) override {
        content_ += "q\n" + num(float(r.x)) + " " + num(yUp(float(r.y + r.height)))
            + " " + num(float(r.width)) + " " + num(float(r.height))
            + " re\nW\nn\n";
    }
    void popClip() override { content_ += "Q\n"; }

    bool finish(const std::filesystem::path& path) override {
        // Object numbering: 1 Catalog, 2 Pages, 3 Page, 4 Contents,
        // 5 Font, 6.. image objects.
        auto stream = deflate({reinterpret_cast<const uint8_t*>(content_.data()),
                               content_.size()});
        std::vector<std::string> objs;
        objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");
        objs.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
        std::string xobjs;
        for (size_t i = 0; i < imgObjects_.size(); ++i)
            xobjs += "/Im" + std::to_string(6 + i) + " "
                   + std::to_string(6 + i) + " 0 R ";
        objs.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
            + num(w_) + " " + num(h_) + "] /Contents 4 0 R /Resources << "
            "/Font << /F1 5 0 R >> /XObject << " + xobjs
            + ">> /ExtGState << /GS0 << /ca 1 /CA 1 >> >> >> >>");
        objs.push_back("<< /Length " + std::to_string(stream.size())
            + (zlibAvailable() ? " /Filter /FlateDecode" : "")
            + " >>\nstream\n");
        objs.push_back("<< /Type /Font /Subtype /Type1 "
                       "/BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        for (auto& o : imgObjects_) objs.push_back(o);

        std::string pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<size_t> offsets;
        for (size_t i = 0; i < objs.size(); ++i) {
            offsets.push_back(pdf.size());
            pdf += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\n";
            if (i == 3) {  // contents stream body
                pdf.append(reinterpret_cast<const char*>(stream.data()),
                           stream.size());
                pdf += "\nendstream\n";
            }
            pdf += "endobj\n";
        }
        size_t xref = pdf.size();
        pdf += "xref\n0 " + std::to_string(objs.size() + 1)
             + "\n0000000000 65535 f \n";
        for (auto off : offsets) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", off);
            pdf += buf;
        }
        pdf += "trailer\n<< /Size " + std::to_string(objs.size() + 1)
             + " /Root 1 0 R /Info << /Producer (VolcanoPlot)";
        for (auto& [k, v] : opts_.metadata)
            pdf += " /" + k + " (" + psEscape(toLatin1(v)) + ")";
        pdf += " >> >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
        return writeFile(path, pdf);
    }

private:
    float yUp(float y) const { return h_ - y; }
    void setPen(const Pen& p) {
        content_ += "q\n" + pdfColor(p.color) + " RG\n" + num(p.width) + " w\n";
        setAlpha(p.color.a);
        int cap = p.cap == plot::CapStyle::Round ? 1
                : p.cap == plot::CapStyle::Projecting ? 2 : 0;
        int join = p.join == plot::JoinStyle::Round ? 1
                 : p.join == plot::JoinStyle::Bevel ? 2 : 0;
        content_ += std::to_string(cap) + " J " + std::to_string(join) + " j\n";
        if (!p.dashes.empty()) {
            content_ += "[";
            for (float d : p.dashes) content_ += num(d) + " ";
            content_ += "] " + num(p.dashOffset) + " d\n";
        }
    }
    void setAlpha(float) {}  // GS0 placeholder (uniform alpha default 1)
    int addImageObj(const std::vector<uint8_t>& data, uint32_t w, uint32_t h,
                    const char* cs, int smask = -1) {
        auto enc = deflate(data);
        std::string obj = "<< /Type /XObject /Subtype /Image /Width "
            + std::to_string(w) + " /Height " + std::to_string(h)
            + " /ColorSpace " + cs + " /BitsPerComponent 8"
            + (zlibAvailable() ? " /Filter /FlateDecode" : "")
            + (smask >= 0 ? " /SMask " + std::to_string(smask) + " 0 R" : "")
            + " /Length " + std::to_string(enc.size()) + " >>\nstream\n";
        obj.append(reinterpret_cast<const char*>(enc.data()), enc.size());
        obj += "\nendstream";
        imgObjects_.push_back(std::move(obj));
        return int(6 + imgObjects_.size() - 1);  // object number
    }

    float w_, h_;
    VectorOptions opts_;
    std::string content_;
    std::vector<std::string> imgObjects_;
};

// ─── EPS ────────────────────────────────────────────────────────────────────

class EpsCanvas : public VectorCanvas {
public:
    EpsCanvas(float w, float h, VectorOptions o)
        : w_(w), h_(h), opts_(std::move(o)) {
        // Flip: work in y-down space to match the raster renderer.
        prologue_ = "0 " + num(h_) + " translate 1 -1 scale\n";
    }

    void polyline(std::span<const Point2D> pts, const Pen& pen) override {
        if (pts.size() < 2) return;
        setPen(pen);
        body_ += "newpath " + num(pts[0].x) + " " + num(pts[0].y) + " moveto\n";
        for (size_t i = 1; i < pts.size(); ++i)
            body_ += num(pts[i].x) + " " + num(pts[i].y) + " lineto\n";
        body_ += "stroke\n";
    }

    void polygon(std::span<const Point2D> pts, Color fill,
                 const Pen* stroke) override {
        if (pts.size() < 3) return;
        body_ += "gsave " + pdfColor(fill) + " setrgbcolor\nnewpath "
            + num(pts[0].x) + " " + num(pts[0].y) + " moveto\n";
        for (size_t i = 1; i < pts.size(); ++i)
            body_ += num(pts[i].x) + " " + num(pts[i].y) + " lineto\n";
        body_ += "closepath ";
        if (stroke) { body_ += "gsave fill grestore\n"; setPen(*stroke); body_ += "stroke\n"; }
        else body_ += "fill\n";
        body_ += "grestore\n";
    }

    void text(Point2D base, std::string_view utf8, float sizePx,
              Color color, float rot) override {
        if (utf8.empty()) return;
        // rot: radians CW-positive in Y-down. In the y-flipped user space
        // `rotate(deg)` turns the baseline the same way; the inner
        // `1 -1 scale` un-mirrors glyph shapes around the baseline.
        float deg = rot * 180.0f / float(M_PI);
        body_ += "gsave /Helvetica findfont " + num(sizePx)
            + " scalefont setfont " + pdfColor(color) + " setrgbcolor\n"
            + num(base.x) + " " + num(base.y) + " translate "
            + num(deg) + " rotate 1 -1 scale\n0 0 moveto ("
            + psEscape(toLatin1(utf8)) + ") show\ngrestore\n";
    }

    void image(Rect2D r, uint32_t w, uint32_t h,
               std::span<const uint8_t> rgba) override {
        // Composite onto the figure facecolor (EPS has no alpha).
        const Color& bg = opts_.facecolor;
        body_ += "gsave\n" + num(float(r.x)) + " " + num(float(r.y))
            + " translate " + num(float(r.width)) + " " + num(float(r.height))
            + " scale\n" + std::to_string(w) + " " + std::to_string(h)
            + " 8 [" + std::to_string(w) + " 0 0 -" + std::to_string(h)
            + " 0 " + std::to_string(h)
            + "] {<currentfile> 3 string readhexstring pop} false 3 colorimage\n";
        int perLine = 0;
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            char buf[8];
            float a = rgba[i * 4 + 3] / 255.0f;
            std::snprintf(buf, sizeof(buf), "%02x%02x%02x",
                int((rgba[i*4+0] * a + bg.r * (1 - a) * 255) + 0.5f),
                int((rgba[i*4+1] * a + bg.g * (1 - a) * 255) + 0.5f),
                int((rgba[i*4+2] * a + bg.b * (1 - a) * 255) + 0.5f));
            body_ += buf;
            if (++perLine == 12) { body_ += '\n'; perLine = 0; }
        }
        body_ += "\ngrestore\n";
    }

    void pushClip(Rect2D r) override {
        body_ += "gsave newpath " + num(float(r.x)) + " " + num(float(r.y))
            + " moveto " + num(float(r.width)) + " 0 rlineto 0 "
            + num(float(r.height)) + " rlineto " + num(-float(r.width))
            + " 0 rlineto closepath clip\n";
    }
    void popClip() override { body_ += "grestore\n"; }

    bool finish(const std::filesystem::path& path) override {
        std::string doc = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 "
            + std::to_string(int(std::ceil(w_))) + " "
            + std::to_string(int(std::ceil(h_)))
            + "\n%%Creator: VolcanoPlot\n";
        for (auto& [k, v] : opts_.metadata)
            doc += "%%" + k + ": " + toLatin1(v) + "\n";
        doc += "%%EndComments\n" + prologue_;
        if (opts_.facecolor.a > 0)
            doc += pdfColor(opts_.facecolor) + " setrgbcolor\n0 0 " + num(w_)
                 + " " + num(h_) + " rectfill\n";
        doc += body_ + "showpage\n%%EOF\n";
        return writeFile(path, doc);
    }

private:
    void setPen(const Pen& p) {
        body_ += pdfColor(p.color) + " setrgbcolor " + num(p.width)
            + " setlinewidth\n";
        int cap = p.cap == plot::CapStyle::Round ? 1
                : p.cap == plot::CapStyle::Projecting ? 2 : 0;
        int join = p.join == plot::JoinStyle::Round ? 1
                 : p.join == plot::JoinStyle::Bevel ? 2 : 0;
        body_ += std::to_string(cap) + " setlinecap " + std::to_string(join)
            + " setlinejoin\n";
        if (!p.dashes.empty()) {
            body_ += "[";
            for (float d : p.dashes) body_ += num(d) + " ";
            body_ += "] " + num(p.dashOffset) + " setdash\n";
        } else body_ += "[] 0 setdash\n";
    }

    float w_, h_;
    VectorOptions opts_;
    std::string prologue_, body_;
};

// ─── PGF ────────────────────────────────────────────────────────────────────

class PgfCanvas : public VectorCanvas {
public:
    PgfCanvas(float w, float h, VectorOptions o)
        : w_(w), h_(h), opts_(std::move(o)) {}

    void polyline(std::span<const Point2D> pts, const Pen& pen) override {
        if (pts.size() < 2) return;
        setPen(pen);
        body_ += "\\pgfpathmoveto{" + pt(pts[0]) + "}\n";
        for (size_t i = 1; i < pts.size(); ++i)
            body_ += "\\pgfpathlineto{" + pt(pts[i]) + "}\n";
        body_ += "\\pgfusepath{stroke}\n";
    }

    void polygon(std::span<const Point2D> pts, Color fill,
                 const Pen* stroke) override {
        if (pts.size() < 3) return;
        body_ += colorCmd(fill, "fill") + "\\pgfpathmoveto{" + pt(pts[0]) + "}\n";
        for (size_t i = 1; i < pts.size(); ++i)
            body_ += "\\pgfpathlineto{" + pt(pts[i]) + "}\n";
        body_ += "\\pgfpathclose\n";
        if (stroke) { setPen(*stroke); body_ += "\\pgfusepath{fill,stroke}\n"; }
        else body_ += "\\pgfusepath{fill}\n";
    }

    void text(Point2D base, std::string_view utf8, float sizePx,
              Color color, float rot) override {
        if (utf8.empty()) return;
        // rot: radians CW-positive in Y-down → pgf rotate is CCW in y-up
        // → negate (and convert to degrees).
        body_ += colorCmd(color, "text")
            + "\\pgftext[base,left,at={\\pgfpoint{" + num(base.x * kPt)
            + "pt}{" + num((h_ - base.y) * kPt) + "pt}}";
        if (rot != 0.0f) body_ += ",rotate=" + num(-rot * 180.0f / float(M_PI));
        body_ += "]{\\fontsize{" + num(sizePx * kPt) + "}{"
            + num(sizePx * kPt * 1.2f) + "}\\selectfont "
            + pgfEscape(utf8) + "}\n";
    }

    void image(Rect2D r, uint32_t w, uint32_t h,
               std::span<const uint8_t> rgba) override {
        pendingImages_.push_back({r, w, h,
                                  {rgba.begin(), rgba.end()}});
    }

    void pushClip(Rect2D r) override {
        body_ += "\\begin{pgfscope}\\pgfpathrectangle{\\pgfpoint{"
            + num(float(r.x) * kPt) + "pt}{" + num((h_ - r.y - r.height) * kPt)
            + "pt}}{\\pgfpoint{" + num(float(r.width) * kPt) + "pt}{"
            + num(float(r.height) * kPt) + "pt}}\n"
            + "\\pgfusepath{clip}\n";
    }
    void popClip() override { body_ += "\\end{pgfscope}\n"; }

    bool finish(const std::filesystem::path& path) override {
        std::string base = path.stem().string();
        std::string doc = "\\begingroup\n";
        for (auto& [k, v] : opts_.metadata)
            doc += "%% " + k + ": " + v + "\n";
        if (opts_.facecolor.a > 0)
            doc += colorCmd(opts_.facecolor, "fill")
                + "\\pgfpathrectangle{\\pgfpoint{0pt}{0pt}}{\\pgfpoint{"
                + num(w_ * kPt) + "pt}{" + num(h_ * kPt) + "pt}}\n"
                + "\\pgfusepath{fill}\n";
        doc += body_;
        // Emit deferred images + \pgfdeclareimage declarations.
        std::string decls;
        int idx = 0;
        for (auto& im : pendingImages_) {
            std::string name = base + "-img" + std::to_string(idx++) + ".png";
            auto png = pngBytes(im.rgba, im.w, im.h);
            if (png.empty()) continue;
            writeFile(path.parent_path() / name, png);
            decls += "\\pgfdeclareimage[width=" + num(float(im.rect.width) * kPt)
                + "pt,height=" + num(float(im.rect.height) * kPt)
                + "pt]{" + name + "}{" + name + "}\n";
            doc += "\\pgftext[at={\\pgfpoint{" + num(float(im.rect.x) * kPt)
                + "pt}{" + num((h_ - im.rect.y - im.rect.height) * kPt)
                + "pt}},left,bottom]{\\pgfuseimage{" + name + "}}\n";
        }
        doc = decls + doc + "\\endgroup\n";
        return writeFile(path, doc);
    }

private:
    static constexpr float kPt = 0.72f;  // px(100dpi) → pt
    std::string pt(Point2D p) const {
        return "\\pgfpoint{" + num(p.x * kPt) + "pt}{"
             + num((h_ - p.y) * kPt) + "pt}";
    }
    std::string colorCmd(Color c, const char* kind) {
        std::string name = "vc" + std::to_string(colorIdx_++);
        return "\\definecolor{" + name + "}{rgb}{" + num(c.r) + "," + num(c.g)
            + "," + num(c.b) + "}\\pgfset" + kind + "color{" + name + "}\n";
    }
    void setPen(const Pen& p) {
        body_ += colorCmd(p.color, "stroke")
            + "\\pgfsetlinewidth{" + num(p.width * kPt) + "pt}\n";
        if (!p.dashes.empty()) {
            body_ += "\\pgfsetdash{{";
            for (size_t i = 0; i < p.dashes.size(); ++i)
                body_ += (i ? "}{" : "") + num(p.dashes[i] * kPt) + "pt";
            body_ += "}}{" + num(p.dashOffset * kPt) + "pt}\n";
        }
    }
    static std::string pgfEscape(std::string_view s) {
        std::string out;
        for (char c : s) {
            if (c == '%' || c == '#' || c == '&' || c == '{' || c == '}')
                out += '\\';
            out += c;
        }
        return out;
    }

    struct PendingImage { Rect2D rect; uint32_t w, h; std::vector<uint8_t> rgba; };
    float w_, h_;
    VectorOptions opts_;
    std::string body_;
    int colorIdx_ = 0;
    std::vector<PendingImage> pendingImages_;
};

} // namespace

std::unique_ptr<VectorCanvas>
vectorCanvas(VectorFormat fmt, float width, float height,
             const VectorOptions& opts) {
    switch (fmt) {
    case VectorFormat::Svg: return std::make_unique<SvgCanvas>(width, height, opts);
    case VectorFormat::Pdf: return std::make_unique<PdfCanvas>(width, height, opts);
    case VectorFormat::Eps: return std::make_unique<EpsCanvas>(width, height, opts);
    case VectorFormat::Pgf: return std::make_unique<PgfCanvas>(width, height, opts);
    }
    return nullptr;
}

} // namespace volcano::render
