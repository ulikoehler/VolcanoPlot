// volcano/plot/GridSpec.cpp
#include "volcano/plot/GridSpec.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace volcano::plot {

namespace {

/// Subdivide `length` pixels into n parts by ratios with `space` fraction
/// (of mean part) gaps between them. Returns offset+size pairs.
std::vector<std::pair<float, float>>
subdivide(float origin, float length, uint32_t n,
          const std::vector<float>& ratios, float space) {
    std::vector<float> r(n, 1.0f);
    for (uint32_t i = 0; i < n && i < ratios.size(); ++i)
        r[i] = std::max(ratios[i], 0.0f);
    float sum = std::accumulate(r.begin(), r.end(), 0.0f);
    float mean = sum / n;
    // Solve: sum(parts) + gaps = length, gap = space * meanPart where
    // meanPart = mean * unit. unit * (sum + space*(n-1)*mean) = length.
    float unit = (n > 0 && sum + space * (n - 1) * mean > 0.0f)
                     ? length / (sum + space * (n - 1) * mean)
                     : 0.0f;
    float gap = space * mean * unit;
    std::vector<std::pair<float, float>> out;
    float pos = origin;
    for (uint32_t i = 0; i < n; ++i) {
        float sz = r[i] * unit;
        out.emplace_back(pos, sz);
        pos += sz + gap;
    }
    return out;
}

} // namespace

const GridSpec* GridSpec::nestedAt(const SubplotSpec& s) const {
    for (const auto& n : nested_)
        if (n.spec.row == s.row && n.spec.col == s.col &&
            n.spec.rowSpan == s.rowSpan && n.spec.colSpan == s.colSpan)
            return n.grid.get();
    return nullptr;
}

Rect2D GridSpec::region(Rect2D outer) const {
    Rect2D r;
    r.x = outer.x + static_cast<int32_t>(std::lround(left * outer.width));
    r.y = outer.y + static_cast<int32_t>(std::lround((1.0f - top) * outer.height));
    r.width = static_cast<uint32_t>(std::lround((right - left) * outer.width));
    r.height = static_cast<uint32_t>(std::lround((top - bottom) * outer.height));
    return r;
}

Rect2D GridSpec::resolveRegion(Rect2D fig) const {
    if (!parent_ || !parent_->grid)
        return region(fig);
    // Nested: our containing rect is the parent grid's cell rect.
    Rect2D parentRegion = parent_->grid->resolveRegion(fig);
    Rect2D cell = parent_->grid->cellRect(parentRegion, *parent_);
    return region(cell);
}

Rect2D GridSpec::cellRect(Rect2D outer, const SubplotSpec& s) const {
    auto rows = subdivide(static_cast<float>(outer.y),
                          static_cast<float>(outer.height),
                          rows_, heightRatios, hspace);
    auto cols = subdivide(static_cast<float>(outer.x),
                          static_cast<float>(outer.width),
                          cols_, widthRatios, wspace);
    uint32_t r1 = std::min(s.row + s.rowSpan, rows_) - 1;
    uint32_t c1 = std::min(s.col + s.colSpan, cols_) - 1;
    Rect2D out;
    out.x = static_cast<int32_t>(std::lround(cols[s.col].first));
    out.width = static_cast<uint32_t>(std::lround(
        cols[c1].first + cols[c1].second - cols[s.col].first));
    out.y = static_cast<int32_t>(std::lround(rows[s.row].first));
    out.height = static_cast<uint32_t>(std::lround(
        rows[r1].first + rows[r1].second - rows[s.row].first));
    return out;
}

} // namespace volcano::plot
