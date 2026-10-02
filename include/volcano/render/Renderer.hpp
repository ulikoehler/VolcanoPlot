// volcano/render/Renderer.hpp — top-level renderer orchestrating all primitives
#pragma once

#include <volcano/backend/IBackend.hpp>
#include <volcano/render/Cmd.hpp>
#include <volcano/render/GpuServices.hpp>
#include <volcano/render/primitives/SpineRenderer.hpp>
#include <volcano/render/primitives/InstancedPathRenderer.hpp>
#include <volcano/render/primitives/GpuLineRenderer.hpp>
#include <volcano/render/primitives/PointRenderer.hpp>
#include <volcano/render/primitives/ReduceRenderer.hpp>
#include <volcano/text/TextRenderer.hpp>
#include <volcano/text/MathText.hpp>

#include <volcano/plot/Plot.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <future>

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <optional>

namespace volcano::plot { class Animation; }

namespace volcano::render {

class Renderer {
public:
    explicit Renderer(backend::IBackend& backend);
    ~Renderer();

    /// Prepare (upload GPU resources) for a figure.
    void prepare(plot::Figure& figure);

    /// Render one frame of the figure.
    void renderFrame(plot::Figure& figure);

    /// Partial-redraw entry point (mpl `fig.stale` semantics): re-renders
    /// only when the figure or an axes was mutated since the last draw.
    /// Returns true when a frame was actually recorded; false means the
    /// previous framebuffer is still current and can be reused as-is.
    bool renderIfStale(plot::Figure& figure);

    /// Blitting (mpl canvas.copy_from_bbox + restore_region + draw_artist):
    /// render the frame *without* animated artists and snapshot the
    /// result as the restorable background. Returns false when the
    /// backend can't blit (MSAA, swapchain).
    bool blitCaptureBackground(plot::Figure& figure);
    /// Restore the captured background and draw only artists flagged
    /// `animated` (mpl canvas.restore_region + draw_artist + blit).
    void blitDrawAnimated(plot::Figure& figure);

    /// Drain backend input events and dispatch them into the figure's
    /// interaction system (canvas callbacks, widgets, navigation).
    /// Returns false when a quit event was received.
    bool processInput(plot::Figure& figure);

    /// matplotlib anim.save: render every frame and encode via a
    /// MovieWriter inferred from the extension (.apng/.gif/.mp4/...) or
    /// an explicit writer name ("apng","pillow"/"gif","ffmpeg",
    /// "imagemagick").
    [[nodiscard]] bool saveAnimation(plot::Animation& anim,
                                     const std::filesystem::path& path,
                                     double fps = 10.0,
                                     std::string_view writer = {});

    /// anim.to_jshtml: render all frames → standalone HTML page with
    /// embedded base64 PNG frames and a JS player.
    [[nodiscard]] std::string toJsHtml(plot::Animation& anim,
                                       double fps = 10.0);

    /// anim.to_html5_video: mp4 via ffmpeg (if available) wrapped in a
    /// <video> tag; falls back to the jshtml player.
    [[nodiscard]] std::string toHtml5Video(plot::Animation& anim,
                                           double fps = 10.0);

    /// matplotlib figure.savefig: prepare, render (with transparent
    /// clear if requested), read back, and encode to `path` — format
    /// inferred from the extension (png/webp/bmp/raw/jpg/tiff/pdf/svg/
    /// svgz/eps/ps/pgf) or options.format. Headless backends only.
    /// Vector formats (pdf/svg/eps/ps/pgf) go through the vector
    /// backend: layers with canEmitVector() write native geometry,
    /// others are rasterized and embedded (matplotlib rasterized).
    [[nodiscard]] bool savefig(plot::Figure& figure,
                               const std::filesystem::path& path,
                               const encode::SaveOptions& options = {});

    /// savefig variant that runs the CPU encode + file write on a worker
    /// thread while the caller is free to render the next figure —
    /// overlaps encode(N) with render(N+1) in multi-figure workloads.
    /// The file is complete once the returned future resolves. Always
    /// uses a per-call CPU encoder (GPU encoders are not thread-safe).
    /// Vector formats run synchronously; the future is already ready.
    [[nodiscard]] std::future<bool> savefigAsync(
        plot::Figure& figure, const std::filesystem::path& path,
        const encode::SaveOptions& options = {});

    [[nodiscard]] backend::IBackend& backend() noexcept { return backend_; }
    /// GPU services of the active backend (factories + shared renderers).
    [[nodiscard]] render::GpuServices& gpu() noexcept { return backend_.gpu(); }
    [[nodiscard]] text::TextRenderer& textRenderer() noexcept { return gpu().text(); }
    [[nodiscard]] primitives::ReduceRenderer& reduceRenderer() noexcept { return gpu().reduce(); }
    [[nodiscard]] primitives::SpineRenderer& spineRenderer() noexcept { return gpu().spine(); }
    /// Instanced path renderer — lazily inited with the spine renderer.
    [[nodiscard]] primitives::InstancedPathRenderer& instancedPathRenderer() noexcept {
        return gpu().instancedPath();
    }
    [[nodiscard]] primitives::GpuLineRenderer& gpuLineRenderer() noexcept {
        return gpu().gpuLine();
    }
    /// SDF point-sprite renderer — lazily inited with the spine renderer.
    [[nodiscard]] primitives::PointRenderer& pointRenderer() noexcept {
        return gpu().sharedPoints();
    }
    /// Monotonically increasing draw-cycle counter — plots use it to
    /// invalidate GPU meshes produced in a previous frame's preDraw.
    [[nodiscard]] uint64_t frameSeq() const noexcept { return frameSeq_; }
    /// True when the text renderer pipeline + atlas are ready to draw.
    [[nodiscard]] bool textReady() const noexcept { return textReady_; }
    /// True when the spine renderer pipeline is ready.
    [[nodiscard]] bool spineReady() const noexcept { return graphicsReady_; }

    /// Draw UTF-8 text with mathtext (`$…$`) support at a pixel position
    /// (baseline origin). Used internally and by plot layers drawing
    /// rich-text elements (TeX markers, contour labels).
    /// `font` selects the face (family/style/weight) via
    /// TextRenderer::faceFor; nullptr uses the primary face.
    void drawRichText(Cmd& cmd, plot::Rect2D scissor,
                      std::string_view text, float x, float y,
                      plot::Color color, float scale = 1.0f,
                      float rotation = 0.0f,
                      plot::HAlign lineAlign = plot::HAlign::Left,
                      const plot::FontProperties* font = nullptr);
    /// Measure rich text at `scale` (mathtext-aware).
    [[nodiscard]] text::TextRenderer::TextMetrics
    measureRichText(std::string_view text, float scale = 1.0f);
    /// mpl fontproperties → font face resolution (family/style/weight).
    /// Returns the primary face when no better match exists; `.bold`
    /// tells callers whether the face is genuinely bold (skip faux-bold).
    [[nodiscard]] text::TextRenderer::FaceMatch
    richTextFace(const plot::FontProperties& font);
    /// Draw rich text honoring mpl `path_effects`: Stroke passes draw
    /// the text at a disk of pixel offsets in the foreground color
    /// (approximating the glyph outline), shadow passes draw one offset
    /// copy, Normal/thenNormal run the plain draw at their position in
    /// the list.
    void drawRichTextFx(Cmd& cmd, plot::Rect2D scissor,
                        std::span<const plot::PathEffect> fxs,
                        std::string_view text, float x, float y,
                        plot::Color color, float scale = 1.0f,
                        float rotation = 0.0f,
                        plot::HAlign lineAlign = plot::HAlign::Left,
                        float dpi = 96.0f,
                        const plot::FontProperties* font = nullptr);

private:
    /// Which plot subset a frame draws (blit modes).
    enum class DrawSubset { All, StaticOnly, AnimatedOnly };
    void renderFrameSubset(plot::Figure& figure, DrawSubset subset);

    /// Outward distance (px) from each axis edge to the axis label's far
    /// edge — tick marks + tick labels + labelpad + label text.
    struct AxisLabelDepths { float x = 0.0f, y = 0.0f; };
    [[nodiscard]] AxisLabelDepths measureAxisLabelDepths(
        const plot::Axes& axes, plot::Rect2D rect);
    /// mpl Figure.align_labels: equalize axis-label depth across each
    /// subplot row/column group by writing Axes::xLabelShiftPx/yLabelShiftPx.
    void alignAxesLabels(plot::Figure& figure);

    backend::IBackend& backend_;
    /// mpl mathtext.fontset — set from figure.style().mathFontset at the
    /// top of every frame; consumed by drawRichText/measureRichText.
    text::MathFontset mathFontset_ = text::MathFontset::DejaVuSans;
    uint64_t frameSeq_ = 0;
    bool graphicsReady_ = false;
    bool textReady_ = false;
    bool prepared_ = false;
    bool frameValid_ = false;

    /// Cached GPU encoders for savefig (PNG filtering / YUV conversion on
    /// the GPU + fast CPU bitstream). Keyed by ImageFormat; a null entry
    /// marks a failed creation attempt (CPU fallback).
    std::map<encode::ImageFormat, std::unique_ptr<encode::IImageEncoder>>
        gpuEncs_;
    std::set<encode::ImageFormat> gpuEncTried_;

    /// Draw axis labels, tick labels, and title for one axes.
    void drawText(Cmd& cmd, const plot::Axes& axes,
                  plot::Rect2D rect);

    /// Draw axis spines (border lines) and tick marks for one axes.
    void drawSpines(Cmd& cmd, const plot::Axes& axes,
                    plot::Rect2D rect);

    /// Draw tick-aligned grid lines for one axes (xAxis.grid → vertical
    /// lines at x ticks, yAxis.grid → horizontal lines at y ticks;
    /// gridWhich selects major/minor/both).
    void drawGrid(Cmd& cmd, const plot::Axes& axes,
                  plot::Rect2D rect);

    /// Polar furniture: radial "thetagrid" spokes + concentric r-grid
    /// circles (matplotlib projection="polar"), plus the circular outer
    /// spine, degree theta labels, and r tick labels.
    void drawPolarGrid(Cmd& cmd, const plot::Axes& axes,
                       plot::Rect2D rect);
    void drawPolarSpineAndLabels(Cmd& cmd,
                                 const plot::Axes& axes,
                                 plot::Rect2D rect);

    /// Geo furniture (aitoff/hammer/lambert/mollweide): projected-domain
    /// boundary frame, graticule grid (meridians + parallels), degree
    /// longitude labels on the equator and latitude labels on the left
    /// limb (matplotlib GeoAxes layout).
    void drawGeoGrid(Cmd& cmd, const plot::Axes& axes,
                     plot::Rect2D rect);
    void drawGeoFrameAndLabels(Cmd& cmd,
                               const plot::Axes& axes,
                               plot::Rect2D rect);

    /// Draw a legend for the axes (if enabled in style).
    void drawLegend(Cmd& cmd, const plot::Axes& axes,
                    plot::Rect2D rect);
    /// mpl fig.legend — figure-level legend collecting handles from all
    /// axes, anchored in figure (canvas) coordinates.
    void drawFigureLegend(Cmd& cmd, const plot::Figure& fig);

    /// Draw a colorbar for the axes (if enabled in style).
    void drawColorbar(Cmd& cmd, const plot::Axes& axes,
                      plot::Rect2D rect);

    /// Draw text annotations and arrow annotations for one axes.
    void drawAnnotations(Cmd& cmd, const plot::Axes& axes,
                         plot::Rect2D rect);

    /// Draw anchored scale bars (mpl AnchoredSizeBar) for one axes.
    void drawSizeBars(Cmd& cmd, const plot::Axes& axes,
                      plot::Rect2D rect);
    /// Draw anchored text boxes (mpl AnchoredText) for one axes.
    void drawAnchoredTexts(Cmd& cmd, const plot::Axes& axes,
                           plot::Rect2D rect);
    /// Draw inset-zoom indicator rectangles + connectors (mpl
    /// indicate_inset_zoom) for one axes.
    void drawInsetIndicators(Cmd& cmd,
                             const plot::Axes& axes,
                             plot::Rect2D rect);

    /// Vector-backend savefig (pdf/svg/eps/pgf). See savefig.
    [[nodiscard]] bool savefigVector(plot::Figure& figure,
                                     const std::filesystem::path& path,
                                     const encode::SaveOptions& options,
                                     encode::ImageFormat fmt);

    /// savefig raster prefix: transparent/facecolor juggling, prepare,
    /// renderFrame, restore, readback. Returns the RGBA8 framebuffer
    /// (empty on failure) and writes the pixel extent into w/h.
    [[nodiscard]] std::vector<uint8_t> savefigPixels(
        plot::Figure& figure, const encode::SaveOptions& opts,
        uint32_t& w, uint32_t& h);

    /// Apply the savefig PNG compression-level default (level 3, env
    /// VOLCANO_PNG_LEVEL overrides) — shared by savefig/savefigAsync.
    static encode::SaveOptions resolveSaveOpts(encode::SaveOptions opts,
                                               encode::ImageFormat fmt);

    // --- legend internals (shared by axes + figure legends) ---
    /// One legend row: label + handle color/shape.
    struct LegendEntry {
        std::string label;
        plot::Color color;
        plot::LegendMarker marker;
        int points = -1;  ///< LegendHandle::points (handle marker count)
    };
    /// Measured legend layout (mpl VPacker/HPacker packing).
    struct LegendLayout {
        float boxW = 0, boxH = 0;
        float scale = 1, fontPx = 0, pad = 0;
        float handleW = 0, textGap = 0, colGap = 0, rowSep = 0;
        float hAbove = 0, hBelow = 0, hBoxH = 0;
        float titleH = 0, titleScale = 1;
        float firstAbove = 0;
        int rows = 0, cols = 0;
        std::vector<float> colW;
        std::vector<float> itemW, itemAbove, itemBelow;
    };
    static std::vector<LegendEntry> collectLegendEntries(const plot::Axes& axes);
    LegendLayout measureLegend(const std::vector<LegendEntry>& entries,
                               const plot::LegendStyle& lg, float dpi);
    /// forceW > 0 (mpl mode="expand" / 4-tuple bbox_to_anchor): the box
    /// grows to that width, spreading columns to fill it.
    plot::Rect2D paintLegendBox(Cmd& cmd,
                                const std::vector<LegendEntry>& entries,
                                const plot::LegendStyle& lg,
                                const LegendLayout& L,
                                plot::Color textColor,
                                plot::Point2D anchor, float bx, float by,
                                float forceW = -1.0f);
};

} // namespace volcano::render
