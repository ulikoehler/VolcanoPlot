// volcano/plot/GridSpec.hpp — GridSpec/SubplotSpec layout engine
//
// Mirrors matplotlib's gridspec module:
//   GridSpec(nrows, ncols, left, right, bottom, top, wspace, hspace,
//            width_ratios, height_ratios)
//   SubplotSpec — a cell range within a GridSpec
//   GridSpecFromSubplotSpec — a nested GridSpec inside a SubplotSpec
#pragma once

#include "volcano/plot/Types.hpp"

#include <memory>
#include <vector>

namespace volcano::plot {

class GridSpec;

/// A rectangular cell range within a GridSpec.
struct SubplotSpec {
    const GridSpec* grid = nullptr;
    uint32_t row = 0, col = 0, rowSpan = 1, colSpan = 1;

    /// Create a nested GridSpec inside this cell
    /// (matplotlib GridSpecFromSubplotSpec).
    [[nodiscard]] std::shared_ptr<GridSpec> nested(uint32_t rows,
                                                   uint32_t cols) const;
};

/// GridSpec describes an nrows×ncols grid inside a rectangular region
/// (figure fractions for a top-level grid, or the parent cell rect for a
/// nested grid).
class GridSpec : public std::enable_shared_from_this<GridSpec> {
public:
    GridSpec(uint32_t rows = 1, uint32_t cols = 1)
        : rows_(rows ? rows : 1), cols_(cols ? cols : 1) {}

    [[nodiscard]] uint32_t rows() const noexcept { return rows_; }
    [[nodiscard]] uint32_t cols() const noexcept { return cols_; }

    // --- Geometry parameters (fractions of the containing region) ---
    // left/right/bottom/top bound the grid area; wspace/hspace are gaps
    // between cells expressed as a fraction of the mean cell size.
    // matplotlib rcParams figure.subplot.* defaults.
    float left = 0.125f, right = 0.9f, bottom = 0.11f, top = 0.88f;
    // matplotlib defaults: wspace/hspace ≈ 0.2 of the mean cell size.
    float wspace = 0.2f, hspace = 0.2f;
    /// Per-column/row relative sizes. Empty = uniform.
    std::vector<float> widthRatios, heightRatios;

    /// A SubplotSpec addressing cells [row, row+rowSpan) × [col, col+colSpan).
    [[nodiscard]] SubplotSpec at(uint32_t row, uint32_t col,
                                 uint32_t rowSpan = 1,
                                 uint32_t colSpan = 1) const {
        return {this, row, col, rowSpan, colSpan};
    }

    /// Pixel rect of a cell range inside `outer`.
    [[nodiscard]] Rect2D cellRect(Rect2D outer, const SubplotSpec& s) const;

    /// Pixel rect covered by this whole grid inside `outer`.
    [[nodiscard]] Rect2D region(Rect2D outer) const;

    /// The nested GridSpec occupying `s`, or nullptr.
    [[nodiscard]] const GridSpec* nestedAt(const SubplotSpec& s) const;

    /// The region this grid occupies inside figure rect `fig`, resolving
    /// the parent chain for nested grids.
    [[nodiscard]] Rect2D resolveRegion(Rect2D fig) const;

    /// The parent SubplotSpec this grid lives in (null for top-level).
    [[nodiscard]] const SubplotSpec* parentSpec() const { return parent_.get(); }

private:
    struct Nested { SubplotSpec spec; std::shared_ptr<GridSpec> grid; };
    uint32_t rows_, cols_;
    /// For nested grids: the parent cell we live inside (owned copy).
    std::shared_ptr<SubplotSpec> parent_;
    std::vector<Nested> nested_;

    friend class Figure;
    friend struct SubplotSpec;
};

inline std::shared_ptr<GridSpec> SubplotSpec::nested(uint32_t rows,
                                                   uint32_t cols) const {
    auto g = std::make_shared<GridSpec>(rows, cols);
    g->parent_ = std::make_shared<SubplotSpec>(*this);
    // mpl GridSpecFromSubplotSpec: left/right/bottom/top default to
    // None, meaning the nested grid fills the whole parent cell (the
    // params are fractions of the cell, not of the figure).
    g->left = 0.0f; g->right = 1.0f;
    g->bottom = 0.0f; g->top = 1.0f;
    return g;
}

} // namespace volcano::plot
