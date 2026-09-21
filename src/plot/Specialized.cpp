// volcano/plot/Specialized.cpp — §15: TablePlot, Sankey, squarify/treemap
#include "volcano/plot/Specialized.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/Renderer.hpp"
#include "volcano/render/primitives/SpineRenderer.hpp"
#include "volcano/text/TextRenderer.hpp"
#include "volcano/render/VectorCanvas.hpp"
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
                      : std::min(float(rect.height) / float(rows),
                                 cellFontScale * 16.0f * 1.5f);
    float tableH = cellH * float(rows);
    // mpl loc='bottom' attaches the table's top edge to the bottom spine,
    // extending below the axes; 'top' extends above; 'center' overlays
    // the table centered in the axes. Y-down pixels.
    float y0 = (loc == "top") ? float(rect.y) - tableH
               : (loc == "center" || loc == "centre")
                   ? float(rect.y) + (float(rect.height) - tableH) * 0.5f
                   : float(rect.y) + float(rect.height);
    float x0 = float(rect.x);

    auto& spine = r.spineRenderer();
    auto& text = r.textRenderer();
    // The table may extend outside the axes rect — clip to the canvas.
    auto ext = r.backend().extent();
    vk::Rect2D clip{vk::Offset2D{0, 0}, ext};

    auto cellRect = [&](size_t row, size_t col) {
        return plot::Rect2D{int32_t(x0 + float(col) * cellW),
                            int32_t(y0 + float(row) * cellH),
                            uint32_t(std::max(1.0f, cellW)),
                            uint32_t(std::max(1.0f, cellH))};
    };
    auto drawCell = [&](size_t row, size_t col, const std::string& str,
                        Color bg) {
        auto cr = cellRect(row, col);
        if (bg.a > 0)
            spine.drawFilledRect(cmd, clip, r.backend().extent(), cr, bg);
        // Cell border.
        spine.drawRect(cmd, clip, r.backend().extent(), cr, edgeColor, 1.0f);
        // Centered text — draw() takes the baseline origin, so place the
        // baseline at the cell's vertical center + (ascent - height/2).
        auto m = text.measureText(str, cellFontScale);
        float tx = float(cr.x) + (cellW - m.width) * 0.5f;
        float ty = float(cr.y) + (cellH - m.height) * 0.5f + m.ascent;
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

// ─── WordCloudPlot ──────────────────────────────────────────────────────────

namespace {

/// Deterministic PRNG for wordcloud/network layout reproducibility.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull;
                      return uint32_t(s >> 33); }
    float uniform() { return float(next()) / float(UINT32_MAX); }
};

bool boxesOverlap(float ax, float ay, float aw, float ah,
                  float bx, float by, float bw, float bh, float margin) {
    return ax - margin < bx + bw && ax + aw + margin > bx &&
           ay - margin < by + bh && ay + ah + margin > by;
}

} // namespace

void WordCloudPlot::layout(render::Renderer& r, Rect2D rect) {
    placed_.clear();
    laidOut_ = true;
    if (words.empty() || !r.textReady()) return;

    // Sort by weight descending, cap at maxWords.
    std::vector<size_t> order(words.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return words[a].weight > words[b].weight;
    });
    if (order.size() > maxWords) order.resize(maxWords);

    double wMin = words[order.back()].weight;
    double wMax = words[order.front()].weight;
    auto toSize = [&](double w) {
        double t = (wMax > wMin) ? (w - wMin) / (wMax - wMin) : 1.0;
        if (logScale) {
            double lw = std::log(std::max(w, 1e-12));
            double l0 = std::log(std::max(wMin, 1e-12));
            double l1 = std::log(std::max(wMax, 1e-12));
            t = (l1 > l0) ? (lw - l0) / (l1 - l0) : 1.0;
        }
        return minFontScale + float(t) * (maxFontScale - minFontScale);
    };

    const Colormap& cm = cmap ? *cmap : colormaps::viridis();
    Lcg rng(seed);
    const float cx = float(rect.x) + float(rect.width) * 0.5f;
    const float cy = float(rect.y) + float(rect.height) * 0.5f;

    struct Box { float x, y, w, h; };
    std::vector<Box> boxes;

    for (size_t rank = 0; rank < order.size(); ++rank) {
        size_t wi = order[rank];
        const auto& word = words[wi];
        float scale = toSize(word.weight);
        auto m = r.textRenderer().measureText(word.text, scale);
        float w = m.width, h = m.height;
        bool vertical = rotationRatio > 0.0f && rng.uniform() < rotationRatio;
        float bw = vertical ? h : w;
        float bh = vertical ? w : h;
        if (bw < 1.0f || bh < 1.0f) continue;
        if (bw > float(rect.width) * 0.95f || bh > float(rect.height) * 0.95f)
            continue;  // too big to ever fit

        // Archimedean spiral from the axes centre.
        bool placed = false;
        float px = 0, py = 0;
        constexpr uint32_t kMaxSteps = 1200;
        for (uint32_t s = 0; s < kMaxSteps; ++s) {
            float theta = float(s) * 0.25f;                 // radians
            float radius = 2.0f + theta * 3.0f;             // px growth
            px = cx + radius * std::cos(theta) - bw * 0.5f;
            py = cy + radius * std::sin(theta) - bh * 0.5f;
            // Must fit fully inside the axes rect.
            if (px < float(rect.x) || py < float(rect.y) ||
                px + bw > float(rect.x) + float(rect.width) ||
                py + bh > float(rect.y) + float(rect.height))
                continue;
            bool hit = false;
            for (const auto& b : boxes)
                if (boxesOverlap(px, py, bw, bh, b.x, b.y, b.w, b.h, margin)) {
                    hit = true; break;
                }
            if (!hit) { placed = true; break; }
        }
        if (!placed) continue;

        boxes.push_back({px, py, bw, bh});
        float t = order.size() > 1 ? float(rank) / float(order.size() - 1) : 0.0f;
        placed_.push_back({wi, px, py, w, h, scale, vertical,
                           cm.sample(0.15f + 0.7f * t)});
    }
}

void WordCloudPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                         const Axes&, Rect2D rect) {
    if (!laidOut_) layout(r, rect);
    if (placed_.empty() || !r.textReady()) return;

    vk::Rect2D clip{vk::Offset2D{rect.x, rect.y},
                    vk::Extent2D{rect.width, rect.height}};
    for (const auto& p : placed_) {
        const auto& word = words[p.word];
        auto m = r.textRenderer().measureText(word.text, p.scale);
        if (p.vertical) {
            // Rotated −90° (reads bottom→top): baseline origin at the
            // box's bottom-right corner.
            r.textRenderer().draw(cmd, clip, word.text,
                                  p.x + m.height, p.y + p.w,
                                  p.color, p.scale, -float(M_PI_2));
        } else {
            // (x, y) is the baseline origin: box top + ascent.
            r.textRenderer().draw(cmd, clip, word.text,
                                  p.x, p.y + m.ascent,
                                  p.color, p.scale);
        }
    }
}

// ─── NetworkPlot ────────────────────────────────────────────────────────────

NetworkPlot::NetworkPlot(uint32_t nodeCount,
                         std::vector<std::pair<uint32_t, uint32_t>> edges,
                         Options opts)
    : n_(nodeCount), edges_(std::move(edges)), opts_(std::move(opts)) {}

void NetworkPlot::computeLayout() {
    pos_.assign(n_, {0.5f, 0.5f});
    if (n_ == 0) { laidOut_ = true; return; }
    Lcg rng(opts_.seed);

    switch (opts_.layout) {
    case Layout::Given:
        if (opts_.positions.size() == n_) pos_ = opts_.positions;
        break;
    case Layout::Circular:
        for (uint32_t i = 0; i < n_; ++i) {
            float a = 2.0f * float(M_PI) * float(i) / float(n_);
            pos_[i] = {0.5f + 0.45f * std::cos(a),
                       0.5f + 0.45f * std::sin(a)};
        }
        break;
    case Layout::Random:
        for (auto& p : pos_) p = {0.05f + 0.9f * rng.uniform(),
                                  0.05f + 0.9f * rng.uniform()};
        break;
    case Layout::Spring: {
        // Fruchterman–Reingold: repulsion between all pairs, attraction
        // along edges, temperature-decayed displacement.
        for (auto& p : pos_) p = {0.1f + 0.8f * rng.uniform(),
                                  0.1f + 0.8f * rng.uniform()};
        const float k = std::sqrt(1.0f / float(std::max(n_, 1u)));
        std::vector<Point2D> disp(n_);
        int iters = std::max(opts_.iterations, 1);
        for (int it = 0; it < iters; ++it) {
            std::fill(disp.begin(), disp.end(), Point2D{0, 0});
            // Repulsion.
            for (uint32_t i = 0; i < n_; ++i) {
                for (uint32_t j = i + 1; j < n_; ++j) {
                    float dx = pos_[i].x - pos_[j].x;
                    float dy = pos_[i].y - pos_[j].y;
                    float d2 = dx * dx + dy * dy + 1e-6f;
                    float f = k * k / d2;
                    float fx = f * dx, fy = f * dy;
                    disp[i].x += fx; disp[i].y += fy;
                    disp[j].x -= fx; disp[j].y -= fy;
                }
            }
            // Attraction along edges.
            for (auto [a, b] : edges_) {
                if (a >= n_ || b >= n_) continue;
                float dx = pos_[a].x - pos_[b].x;
                float dy = pos_[a].y - pos_[b].y;
                float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
                float f = d * d / k / d;   // = d/k
                disp[a].x -= f * dx; disp[a].y -= f * dy;
                disp[b].x += f * dx; disp[b].y += f * dy;
            }
            float temp = 0.1f * (1.0f - float(it) / float(iters));
            for (uint32_t i = 0; i < n_; ++i) {
                float dl = std::sqrt(disp[i].x * disp[i].x +
                                     disp[i].y * disp[i].y);
                if (dl < 1e-6f) continue;
                float step = std::min(dl, temp) / dl;
                pos_[i].x += disp[i].x * step;
                pos_[i].y += disp[i].y * step;
                pos_[i].x = std::clamp(pos_[i].x, 0.0f, 1.0f);
                pos_[i].y = std::clamp(pos_[i].y, 0.0f, 1.0f);
            }
        }
        break;
    }
    }
    laidOut_ = true;
}

void NetworkPlot::prepare(render::Renderer& r) {
    auto& ctx = r.backend().context();
    if (!laidOut_) computeLayout();
    if (!prepared_) {
        edgesR_.init(ctx.device.handle(), r.backend().renderPass(),
                     r.backend().sampleCount(), r.pipelineCache());
        nodesR_.init(ctx.device.handle(), r.backend().renderPass(),
                     r.backend().sampleCount(), r.descriptorPool(),
                     r.pipelineCache());
        prepared_ = true;
    }
    // Edge endpoint pairs (a,b) per edge.
    std::vector<Point2D> segPts;
    segPts.reserve(edges_.size() * 2);
    for (auto [a, b] : edges_) {
        if (a >= n_ || b >= n_) continue;
        segPts.push_back(pos_[a]);
        segPts.push_back(pos_[b]);
    }
    if (!segPts.empty())
        edgesR_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                       ctx.graphicsPool.handle(), ctx.allocator.handle(),
                       std::span{segPts}, opts_.edgeColor, opts_.edgeWidth);
    if (!pos_.empty()) {
        std::vector<Color> colors(pos_.size(), opts_.nodeColor);
        std::vector<float> sizes(pos_.size(), opts_.nodeSize);
        nodesR_.upload(ctx.device.handle(), ctx.device.graphicsQueue(),
                       ctx.graphicsPool.handle(), ctx.allocator.handle(),
                       std::span{pos_}, std::span{colors}, std::span{sizes});
    }
}

void NetworkPlot::draw(vk::CommandBuffer cmd, render::Renderer& r,
                       const Axes& axes, Rect2D rect) {
    if (!prepared_) return;
    Transform2D t = axes.transform();
    vk::Rect2D vrect{vk::Offset2D{rect.x, rect.y},
                     vk::Extent2D{rect.width, rect.height}};
    if (edgesR_.pointCount() >= 2)
        edgesR_.draw(cmd, vrect, t, edgesR_.pointCount());
    if (nodesR_.pointCount() > 0)
        nodesR_.draw(cmd, vrect, t, nodesR_.pointCount());
    // Node labels (mpl `with_labels=True`).
    if (!opts_.labels.empty() && r.textReady()) {
        for (uint32_t i = 0; i < n_ && i < opts_.labels.size(); ++i) {
            if (opts_.labels[i].empty()) continue;
            Point2D f = axes.dataToFraction(pos_[i]);
            float px = rect.x + f.x * rect.width;
            float py = rect.y + (1.0f - f.y) * rect.height;
            r.textRenderer().draw(cmd, vrect, opts_.labels[i], px, py,
                                  Color::black(), opts_.fontScale, 0.0f,
                                  HAlign::Center);
        }
    }
}

void NetworkPlot::contributeToAutoscale(Viewport& v) const {
    for (const auto& p : pos_) {
        v.x.min = std::min(v.x.min, p.x); v.x.max = std::max(v.x.max, p.x);
        v.y.min = std::min(v.y.min, p.y); v.y.max = std::max(v.y.max, p.y);
    }
}


void TablePlot::emitVector(render::VectorCanvas& c, const Axes&,
                           Rect2D rect) {
    if (cellText.empty()) return;
    size_t cols = 0;
    for (const auto& row : cellText) cols = std::max(cols, row.size());
    bool hasRowLabels = !rowLabels.empty();
    bool hasColLabels = !colLabels.empty();
    size_t totalCols = cols + (hasRowLabels ? 1 : 0);
    size_t totalRows = cellText.size() + (hasColLabels ? 1 : 0);
    if (totalCols == 0 || totalRows == 0) return;

    float cellW = float(rect.width) / float(totalCols);
    float cellH = heightFrac > 0
                      ? float(rect.height) * heightFrac / float(totalRows)
                      : std::min(float(rect.height) / float(totalRows), 28.0f);
    float tableH = cellH * float(totalRows);
    float y0 = (loc == "top") ? float(rect.y) - tableH
               : (loc == "center" || loc == "centre")
                   ? float(rect.y) + (float(rect.height) - tableH) * 0.5f
                   : float(rect.y) + float(rect.height);
    float x0 = float(rect.x);
    const float sizePx = 16.0f * cellFontScale;

    auto emitCell = [&](size_t row, size_t col, const std::string& str,
                        Color bg) {
        float cx = x0 + float(col) * cellW, cy = y0 + float(row) * cellH;
        Point2D quad[4] = {{cx, cy}, {cx + cellW, cy},
                           {cx + cellW, cy + cellH}, {cx, cy + cellH}};
        if (bg.a > 0) c.polygon(std::span{quad}, bg);
        Point2D ring[5] = {quad[0], quad[1], quad[2], quad[3], quad[0]};
        render::VectorCanvas::Pen pen;
        pen.color = edgeColor; pen.width = 1.0f;
        c.polyline(std::span{ring}, pen);
        if (str.empty()) return;
        // Approximate glyph metrics (vector writers lack font metrics):
        // ~0.6em advance, baseline at ~70% of the cell height.
        float tw = 0.6f * sizePx * float(str.size());
        c.text({cx + (cellW - tw) * 0.5f, cy + cellH * 0.72f},
               str, sizePx, textColor);
    };

    for (size_t row = 0; row < totalRows; ++row) {
        for (size_t col = 0; col < totalCols; ++col) {
            bool labelCol = hasRowLabels && col == 0;
            bool labelRow = hasColLabels && row == 0;
            if (labelRow && labelCol) { emitCell(row, col, "", labelColor); continue; }
            if (labelRow) {
                size_t ci = col - (hasRowLabels ? 1 : 0);
                emitCell(row, col,
                         ci < colLabels.size() ? colLabels[ci] : "",
                         labelColor);
            } else if (labelCol) {
                size_t ri = row - (hasColLabels ? 1 : 0);
                emitCell(row, col,
                         ri < rowLabels.size() ? rowLabels[ri] : "",
                         labelColor);
            } else {
                size_t ri = row - (hasColLabels ? 1 : 0);
                size_t ci = col - (hasRowLabels ? 1 : 0);
                Color bg = Color::transparent();
                if (ri < cellColors.size() && ci < cellColors[ri].size())
                    bg = cellColors[ri][ci];
                emitCell(row, col,
                         ri < cellText.size() && ci < cellText[ri].size()
                             ? cellText[ri][ci] : "",
                         bg);
            }
        }
    }
}

} // namespace volcano::plot
