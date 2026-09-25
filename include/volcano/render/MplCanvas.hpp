// volcano/render/MplCanvas.hpp — generic Vulkan canvas for the
// matplotlib backend bridge. Records display-space draw ops (paths,
// images, text) during a frame and replays them into one Vulkan
// submission on endFrame, then serves the framebuffer as RGBA8
// (headless) or presents it (GLFW window).
//
// Pixel space is top-left origin, Y-down — identical to mpl's display
// buffer convention, so callers hand in already-transformed mpl
// display coordinates.
#pragma once

#include <volcano/backend/Backend.hpp>
#include <volcano/core/Buffer.hpp>
#include <volcano/core/ShaderModule.hpp>
#include <volcano/plot/Path.hpp>
#include <volcano/plot/Types.hpp>
#include <volcano/render/primitives/HeatmapRenderer.hpp>
#include <volcano/render/primitives/SpineRenderer.hpp>
#include <volcano/text/TextRenderer.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::render {

class MplCanvas {
public:
    /// Headless offscreen canvas (savefig path).
    static std::unique_ptr<MplCanvas>
    headless(uint32_t width, uint32_t height,
             vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e4);
    /// GLFW window canvas (interactive plt.show() path). Returns nullptr
    /// when no display/GLFW is available.
    static std::unique_ptr<MplCanvas>
    windowed(uint32_t width, uint32_t height, std::string title,
             vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e4);
    ~MplCanvas();

    MplCanvas(const MplCanvas&) = delete;
    MplCanvas& operator=(const MplCanvas&) = delete;

    [[nodiscard]] vk::Extent2D extent() const { return backend_->extent(); }
    [[nodiscard]] bool isWindow() const noexcept { return windowed_; }
    /// Resize the render target (mpl FigureCanvas.resize).
    void resize(uint32_t w, uint32_t h);

    // ── frame lifecycle ─────────────────────────────────────────────
    /// Start a frame. All draw ops are recorded until endFrame().
    void beginFrame();
    /// Replay the recorded ops into one Vulkan submission (and present
    /// for windowed canvases). Replays once more when the text atlas
    /// gained lazily-rasterized glyphs during the first pass.
    void endFrame();
    /// Headless only: RGBA8 readback of the last frame.
    [[nodiscard]] std::vector<uint8_t> readback() const;

    // ── draw ops (Y-down pixel space) ───────────────────────────────
    /// Clear-override for the frame background. Default: opaque white
    /// is replaced by the first clear() call's color — callers issue
    /// clear(figure face) themselves when a face is wanted.
    void clear(plot::Color c) { clear_ = c; }

    /// mpl draw_path: subpaths are flattened in the canvas (curves
    /// subdivided). Closed subpaths fill via ear-clipping (opposite-
    /// winding contained subpaths bridge-merge as holes); open subpaths
    /// stroke with joins/caps/dashes. `hatch` replicates mpl hatch
    /// pattern fills clipped to the filled region. `clip` is the scissor
    /// rect (canvas px); `clipRing` additionally clips emitted geometry
    /// to a polygon (mpl clip_path).
    void path(std::span<const float> verts,
              std::span<const uint8_t> codes,
              plot::Color face, bool fillEvenOdd,
              plot::Color edge, float lwPx,
              float dashOffset, std::span<const float> dashSeq,
              plot::JoinStyle join, plot::CapStyle cap,
              std::string_view hatch, plot::Color hatchColor,
              std::optional<plot::Rect2D> clip,
              std::span<const plot::Point2D> clipRing);

    /// mpl draw_image: RGBA8 bitmap blitted into dst px rect. interp:
    /// 0 = nearest, 1 = bilinear, 2 = bicubic.
    void image(std::span<const uint8_t> rgba, uint32_t w, uint32_t h,
               plot::Rect2D dstPx, int interp,
               std::optional<plot::Rect2D> clip);

    /// mpl draw_text (ismath=False): `sizePx` is the font size in
    /// canvas pixels; `rotation` in degrees, clockwise (Y-down). The
    /// (x,y) anchor is the baseline origin. `faceKey` carries the
    /// resolved glyb face ("family/style/weight" description — the
    /// canvas resolves the closest available face itself).
    void text(float x, float y, std::string utf8, float sizePx,
              plot::Color color, float rotDeg,
              std::string family, std::string style,
              std::string weight, std::optional<plot::Rect2D> clip);

    /// mpl draw_text (ismath=True): full mathtext layout (runs + rules).
    void mathText(float x, float y, std::string utf8, float sizePx,
                  plot::Color color, float rotDeg, std::string fontset,
                  std::string family, std::string style,
                  std::string weight, std::optional<plot::Rect2D> clip);

    /// mpl draw_gouraud_triangles: 3 vertices (Y-down px) + RGBA per
    /// vertex. Currently approximated with the average vertex color.
    void gouraud(float x0, float y0, float x1, float y1, float x2,
                 float y2, plot::Color c0, plot::Color c1, plot::Color c2,
                 std::optional<plot::Rect2D> clip);

    // ── metrics (mpl get_text_width_height_descent / layout) ────────
    struct TextMetrics { float width, height, ascent; };
    [[nodiscard]] TextMetrics measureText(std::string_view utf8,
                                          float sizePx,
                                          const std::string& family,
                                          const std::string& style,
                                          const std::string& weight);
    /// w / h / descent for mathtext (layoutMathText on the native engine).
    [[nodiscard]] TextMetrics measureMath(std::string_view utf8,
                                          float sizePx,
                                          const std::string& fontset,
                                          const std::string& family,
                                          const std::string& style,
                                          const std::string& weight);

    // ── window interaction (windowed canvases only) ─────────────────
    /// Pump window events into the queue. Returns false once the window
    /// should close (mpl: emit close_event, then stop the loop).
    bool pollEvents();
    [[nodiscard]] std::vector<backend::InputEvent> takeEvents();
    void toggleFullscreen();
    void setTitle(std::string_view title);

private:
    explicit MplCanvas(std::unique_ptr<backend::IBackend> backend,
                       bool windowed);

    struct PathOp {
        std::vector<float> verts;
        std::vector<uint8_t> codes;
        plot::Color face;
        bool fillEvenOdd = true;
        plot::Color edge;
        float lwPx = 0;
        float dashOffset = 0;
        std::vector<float> dashSeq;
        plot::JoinStyle join = plot::JoinStyle::Round;
        plot::CapStyle cap = plot::CapStyle::Butt;
        std::string hatch;
        plot::Color hatchColor{0, 0, 0, 1};
        std::optional<plot::Rect2D> clip;
        std::vector<plot::Point2D> clipRing;
    };
    struct ImageOp {
        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0;
        plot::Rect2D dst{0, 0, 0, 0};
        int interp = 0;
        std::optional<plot::Rect2D> clip;
    };
    struct TextOp {
        float x = 0, y = 0;
        std::string text;
        float sizePx = 0;
        plot::Color color;
        float rotDeg = 0;
        std::string family, style, weight;
        bool math = false;
        std::string fontset;
        std::optional<plot::Rect2D> clip;
    };
    struct GouraudOp {
        float x[3], y[3];
        plot::Color c[3];
        std::optional<plot::Rect2D> clip;
    };
    using Op = std::variant<PathOp, ImageOp, TextOp, GouraudOp>;

    void initRenderers();
    void execute(vk::CommandBuffer cmd);
    void execPath(vk::CommandBuffer cmd, const PathOp& op);
    void execImage(vk::CommandBuffer cmd, const ImageOp& op);
    void execText(vk::CommandBuffer cmd, const TextOp& op);
    void execGouraud(vk::CommandBuffer cmd, const GouraudOp& op);
    /// Resolve family/style/weight → glyb face + faux-style flags.
    font_face* faceFor(const TextOp& op);

    std::unique_ptr<backend::IBackend> backend_;
    bool windowed_ = false;
    bool inited_ = false;

    std::unique_ptr<core::PipelineCache> pipelineCache_;
    std::unique_ptr<core::DescriptorPool> descPool_;
    primitives::SpineRenderer spine_;
    primitives::HeatmapRenderer heat_;
    text::TextRenderer text_;

    std::vector<Op> ops_;
    plot::Color clear_{1, 1, 1, 1};
    /// Scratch fill/stroke work buffers reused across ops.
    std::vector<plot::Point2D> trisScratch_;
};

} // namespace volcano::render
