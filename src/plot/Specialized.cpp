// volcano/plot/Specialized.cpp — §15: TablePlot, Sankey, squarify/treemap
#include "volcano/plot/Specialized.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/text/TextRenderer.hpp"
#include "volcano/backend/Backend.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace volcano::plot {

// ═══ TablePlot ════════════════════════════════════════════════════════════

void TablePlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                     const Axes&, Rect2D rect) {
    if (cellText.empty()) return;
    size_t rows = cellText.size() + (rowLabels.empty() ? 0 : 0);
    size_t cols = 0;
    for (const auto& row : cellText) cols = std::max(cols, row.size());
    bool hasRowLabels = !rowLabels.empty();
    bool hasColLabels = !colLabels.empty();
    size_t totalCols = cols + (hasRowLabels ? 1 : 0);
    size_t totalRows = cellText.size() + (hasColLabels ? 1 : 0);
    if (totalCols == 0 || totalRows == 0) return;
    rows = totalRows;

    float cellW = float(rect.width) / float(totalCols);
    float cellH = heightFrac > 0
                      ? float(rect.height) * heightFrac / float(rows)
                      : std::min(float(rect.height) / float(rows), 28.0f);
    float tableH = cellH * float(rows);
    // Y-down pixels: "bottom" → table at bottom of axes rect.
    float y0 = (loc == "top") ? float(rect.y)
                              : float(rect.y) + float(rect.height) - tableH;
    float x0 = float(rect.x);

    auto& spine = r.spineRenderer();
    auto& text = r.textRenderer();
    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};

    auto cellRect = [&](size_t row, size_t col) {
        return plot::Rect2D{int32_t(x0 + float(col) * cellW),
                            int32_t(y0 + float(row) * cellH),
                            uint32_t(std::max(1.0f, cellW)),
                            uint32_t(std::max(1.0f, cellH))};
    };
    auto drawCell = [&](size_t row, size_t col, const std::string& str,
                        Color bg) {
        auto cr = cellRect(row, col);
        if (bg.a > 0) spine.drawFilledRect(cmd, clip, cr, bg);
        // Cell border.
        spine.drawRect(cmd, clip, cr, edgeColor, 1.0f);
        // Centered text.
        auto m = text.measureText(str, cellFontScale);
        float tx = float(cr.x) + (cellW - m.width) * 0.5f;
        float ty = float(cr.y) + (cellH + m.height) * 0.5f - m.height;
        text.draw(cmd, clip, str, tx, ty, textColor, cellFontScale);
    };

    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < totalCols; ++col) {
            bool labelCol = hasRowLabels && col == 0;
            bool labelRow = hasColLabels && row == 0;
            if (labelRow && labelCol) {
                drawCell(row, col, "", labelColor);
            } else if (labelRow) {
                size_t ci = col - (hasRowLabels ? 1 : 0);
                drawCell(row, col,
                         ci < colLabels.size() ? colLabels[ci] : "",
                         labelColor);
            } else if (labelCol) {
                size_t ri = row - (hasColLabels ? 1 : 0);
                drawCell(row, col,
                         ri < rowLabels.size() ? rowLabels[ri] : "",
                         labelColor);
            } else {
                size_t ri = row - (hasColLabels ? 1 : 0);
                size_t ci = col - (hasRowLabels ? 1 : 0);
                Color bg{0, 0, 0, 0};
                if (ri < cellColors.size() && ci < cellColors[ri].size())
                    bg = cellColors[ri][ci];
                std::string s = (ri < cellText.size() && ci < cellText[ri].size())
                                    ? cellText[ri][ci] : "";
                drawCell(row, col, s, bg);
            }
        }
    }
}

// ═══ Sankey ═══════════════════════════════════════════════════════════════

Sankey& Sankey::add(std::vector<float> flows,
                    std::vector<std::string> labels,
                    std::vector<int> orientations, Color color) {
    sets_.push_back({std::move(flows), std::move(labels),
                     std::move(orientations), color});
    return *this;
}

void Sankey::finish() {
    // Single-stage horizontal sankey in data space [0,1]×[0,1]:
    //   - trunk: vertical rectangle centered at x≈0.5 with height ∝ sum
    //     of all |flows|.
    //   - positive flows: ribbons entering the trunk's left edge from x=0.
    //   - negative flows: ribbons leaving the trunk's right edge to x=1.
    // Ribbons bend smoothly via cubic Bézier vertical transitions.
    float total = 0;
    for (const auto& s : sets_)
        for (float f : s.flows) total += std::abs(f);
    if (total <= 0) return;

    const float trunkX0 = 0.45f, trunkX1 = 0.55f;
    const float scale = 0.8f / total; // trunk height = 0.8 of axes
    float trunkTop = 0.5f + total * scale * 0.5f;

    // Trunk body.
    Patch trunk = patch::Rectangle(trunkX0, 0.5f - total * scale * 0.5f,
                                   trunkX1 - trunkX0, total * scale);
    trunk.style.face = Color{0.4f, 0.4f, 0.4f, 0.6f};
    trunk.style.edge = Color{0.2f, 0.2f, 0.2f, 1.0f};
    axes_->addPatch(std::move(trunk));

    // Stack ribbons: positives along the trunk top edge, negatives bottom.
    float inY = trunkTop;    // current top edge cursor
    float outY = 0.5f - total * scale * 0.5f; // bottom edge cursor
    float inSrcY = 0.95f;    // outer endpoints fan from top-left
    float outSrcY = 0.05f;   // outputs fan to bottom-right

    for (const auto& s : sets_) {
        for (size_t i = 0; i < s.flows.size(); ++i) {
            float f = s.flows[i];
            float w = std::abs(f) * scale;
            Path p;
            if (f >= 0) {
                // Ribbon: outer (0, inSrcY-w)..(0,inSrcY) → trunk left edge
                // (trunkX0, inY-w)..(trunkX0, inY).
                Point2D a0{0.0f, inSrcY}, a1{0.0f, inSrcY - w};
                Point2D b0{trunkX0, inY}, b1{trunkX0, inY - w};
                p.moveTo(a1);
                p.curve4({trunkX0 * 0.5f, a1.y}, {trunkX0 * 0.5f, b1.y}, b1);
                p.lineTo(b0);
                p.curve4({trunkX0 * 0.5f, b0.y}, {trunkX0 * 0.5f, a0.y}, a0);
                p.close();
                if (i < s.labels.size() && !s.labels[i].empty()) {
                    auto* t = axes_->text(0.02f, inSrcY - w * 0.5f,
                                          s.labels[i], CoordSystem::Data);
                    t->valign = VAlign::Center;
                    t->fontSize = 0.6f;
                }
                inSrcY -= w + gap_;
                inY -= w;
            } else {
                Point2D a0{1.0f, outSrcY + w}, a1{1.0f, outSrcY};
                Point2D b0{trunkX1, outY + w}, b1{trunkX1, outY};
                p.moveTo(b1);
                p.curve4({1.0f - (1.0f - trunkX1) * 0.5f, b1.y},
                         {1.0f - (1.0f - trunkX1) * 0.5f, a1.y}, a1);
                p.lineTo(a0);
                p.curve4({1.0f - (1.0f - trunkX1) * 0.5f, a0.y},
                         {1.0f - (1.0f - trunkX1) * 0.5f, b0.y}, b0);
                p.close();
                if (i < s.labels.size() && !s.labels[i].empty()) {
                    auto* t = axes_->text(0.98f, outSrcY + w * 0.5f,
                                          s.labels[i], CoordSystem::Data);
                    t->valign = VAlign::Center;
                    t->halign = HAlign::Right;
                    t->fontSize = 0.6f;
                }
                outSrcY += w + gap_;
                outY += w;
            }
            Patch rib{std::move(p), {}};
            rib.style.face = s.color;
            rib.style.edge = s.color;
            rib.style.edge.a = std::min(1.0f, s.color.a + 0.3f);
            rib.style.lineWidth = 0.5f;
            axes_->addPatch(std::move(rib));
        }
    }
}

// ═══ squarify / treemap ═══════════════════════════════════════════════════

namespace {

float worstAspect(std::span<const float> row, float w) {
    float s = 0, mn = 1e30f, mx = 0;
    for (float v : row) { s += v; mn = std::min(mn, v); mx = std::max(mx, v); }
    if (w <= 0 || s <= 0) return 1e30f;
    float w2 = w * w;
    return std::max(w2 * mx / (s * s), s * s / (w2 * mn));
}

} // namespace

std::vector<Rect2Df> squarify(std::span<const float> sizesIn,
                              float x, float y, float w, float h) {
    std::vector<Rect2Df> out;
    float total = 0;
    for (float s : sizesIn) total += std::max(0.0f, s);
    if (total <= 0 || w <= 0 || h <= 0) return out;

    // Normalize sizes to the rectangle's area.
    std::vector<float> sizes(sizesIn.begin(), sizesIn.end());
    float k = w * h / total;
    for (auto& s : sizes) s = std::max(0.0f, s) * k;

    out.reserve(sizes.size());
    std::vector<float> row;
    float rx = x, ry = y, rw = w, rh = h;

    auto layoutRow = [&]() {
        // Lay `row` along the shorter side of the remaining rect.
        float rowSum = 0;
        for (float v : row) rowSum += v;
        if (rw >= rh) {
            // Wider than tall → vertical strip (column) on the left.
            float cw = rowSum / rh;
            float cy = ry;
            for (float v : row) {
                float ch = v / cw;
                out.push_back({rx, cy, cw, ch});
                cy += ch;
            }
            rx += cw; rw -= cw;
        } else {
            // Taller than wide → horizontal strip (row) on the bottom.
            float ch = rowSum / rw;
            float cx = rx;
            for (float v : row) {
                float cw = v / ch;
                out.push_back({cx, ry, cw, ch});
                cx += cw;
            }
            ry += ch; rh -= ch;
        }
        row.clear();
    };

    for (float s : sizes) {
        float side = std::min(rw, rh);
        std::vector<float> trial = row;
        trial.push_back(s);
        if (!row.empty() && worstAspect(trial, side) > worstAspect(row, side))
            layoutRow();
        row.push_back(s);
    }
    if (!row.empty()) layoutRow();
    return out;
}

void treemap(Axes& axes, std::span<const float> sizes,
             const std::vector<std::string>& labels,
             const std::vector<Color>& colors) {
    auto rects = squarify(sizes, 0.0f, 0.0f, 1.0f, 1.0f);
    for (size_t i = 0; i < rects.size(); ++i) {
        auto& rc = rects[i];
        Patch p = patch::Rectangle(rc.x, rc.y, rc.w, rc.h);
        p.style.face = i < colors.size()
                           ? colors[i]
                           : colormaps::hsv().sample(
                                 std::fmod(float(i) * 0.61803f, 1.0f));
        p.style.edge = Color::white();
        p.style.lineWidth = 1.5f;
        axes.addPatch(std::move(p));
        if (i < labels.size() && !labels[i].empty() &&
            rc.w > 0.06f && rc.h > 0.06f) {
            auto* t = axes.text(rc.x + rc.w * 0.5f, rc.y + rc.h * 0.5f,
                                labels[i], CoordSystem::Data);
            t->halign = HAlign::Center;
            t->valign = VAlign::Center;
            t->fontSize = std::min(1.0f, rc.h * 0.9f);
            t->color = Color::white();
        }
    }
    axes.setViewport({{0, 1}, {0, 1}});
}

} // namespace volcano::plot
