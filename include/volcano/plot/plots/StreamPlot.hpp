// volcano/plot/plots/StreamPlot.hpp — streamplot (matplotlib `streamplot`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/DataSeries.hpp"
#include "volcano/plot/Colormap.hpp"
#include "volcano/render/primitives/LineSegmentRenderer.hpp"
#include "volcano/render/primitives/FillRenderer.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for StreamPlot.
struct StreamConfig {
    /// Density of streamlines (seeds per cell). 1.0 = one seed per cell.
    float density = 1.0f;
    /// Line width for streamlines.
    float lineWidth = 1.0f;
    /// Color of streamlines. If alpha=0, uses colormap based on velocity.
    Color color = Color::black();
    /// Colormap for velocity-based coloring (used when color.alpha == 0).
    const Colormap* cmap = nullptr;
    /// Maximum number of points per streamline.
    uint32_t maxPoints = 200;
    /// Integration step size in data units (fraction of cell size).
    float stepSize = 0.2f;
    /// Draw arrowheads on streamlines.
    bool arrows = true;
    /// mpl `arrowsize`: scale factor for arrowhead length/width.
    float arrowsize = 1.0f;
    /// mpl `broken_streamlines`: when true (default) a streamline ends
    /// where the field vanishes or is masked (NaN); when false, the
    /// integration coasts through invalid holes using the last valid
    /// velocity so lines stay continuous.
    bool brokenStreamlines = true;
    /// Arrowhead length in pixels.
    float arrowLength = 10.0f;
    /// Arrowhead width in pixels.
    float arrowWidth = 7.0f;
    std::string label;
};

/// Streamplot — draws streamlines of a 2D vector field.
/// Equivalent to matplotlib's `streamplot(X, Y, U, V)`.
///
/// Uses RK4 integration to trace streamlines from seed points distributed
/// across the grid. Streamlines are rendered as line strips via LineRenderer.
/// Optional arrowheads are rendered as filled triangles via FillRenderer.
class StreamPlot : public IPlot {
public:
    /// Construct from a Grid2D for U and V components.
    /// The grid xRange/yRange define the field domain.
    /// U and V are sampled at grid positions (width × height, row-major).
    StreamPlot(Grid2D gridU, Grid2D gridV,
               StreamConfig config = {});
    /// mpl StreamplotSet config access (color/lw/arrows mutators).
    [[nodiscard]] StreamConfig& mutableConfig() noexcept {
        return config_;
    }
    [[nodiscard]] const StreamConfig& config() const noexcept {
        return config_;
    }

    void prepare(render::Renderer& r) override;
    void draw(render::Cmd& cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return config_.label; }
    [[nodiscard]] Color legendColor() const override { return config_.color; }
    [[nodiscard]] LegendMarker legendMarker() const override { return LegendMarker::Line; }

private:
    Grid2D gridU_, gridV_;
    StreamConfig config_;

    // Streamlines as polylines (each polyline is a contiguous run of points).
    std::vector<Point2D> streamlinePoints_;  // all points concatenated
    std::vector<uint32_t> streamlineStarts_; // start index of each streamline
    std::vector<uint32_t> streamlineLengths_;

    // Arrowhead triangles (in data space, computed in draw()).
    std::vector<Point2D> arrowPositions_;
    std::vector<Color> arrowColors_;

    std::unique_ptr<render::primitives::LineSegmentRenderer> lineRenderer_;
    std::unique_ptr<render::primitives::FillRenderer> arrowRenderer_;
    bool prepared_ = false;

    // GPU streamline tracing ('streamlines' offload): the RK4 traces run
    // on the device while the order-dependent seed accept/reject loop
    // (mpl seeds against already-accepted lines) replays on the CPU.
    std::vector<float> seeds_;          // candidate seeds, (x, y) pairs
    bool gpuTrace_ = false;
    bool gpuTraced_ = false;
    void buildSeeds();
    void appendLine(const std::vector<Point2D>& line);
    bool adoptGpuTraces(render::Renderer& r);
    void uploadSegments(render::Renderer& r);

    /// Sample the vector field at (x, y) via bilinear interpolation.
    /// Returns (u, v). Returns (0, 0) if outside the grid.
    std::pair<float, float> sampleField(float x, float y) const;

    /// Integrate a streamline from (x0, y0) in direction `dir` (+1 or -1).
    /// Appends points to `points`.
    void integrateStreamline(float x0, float y0, int dir,
                             std::vector<Point2D>& points) const;

    /// Generate seed points and trace all streamlines.
    void generateStreamlines();

    /// Check if a point is too close to an existing streamline.
    bool tooCloseToExisting(float x, float y, float minDist) const;
};

} // namespace volcano::plot
