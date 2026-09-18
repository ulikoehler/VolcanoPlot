// volcano/render/Renderer.hpp — top-level renderer orchestrating all primitives
#pragma once

#include <volcano/backend/Backend.hpp>
#include <volcano/core/PipelineCache.hpp>
#include <volcano/core/DescriptorPool.hpp>
#include <volcano/render/primitives/SpineRenderer.hpp>
#include <volcano/render/primitives/ReduceRenderer.hpp>
#include <volcano/text/TextRenderer.hpp>

#include <volcano/plot/Plot.hpp>
#include <volcano/encode/ImageEncoder.hpp>

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <memory>

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

    [[nodiscard]] backend::IBackend& backend() noexcept { return backend_; }
    [[nodiscard]] core::PipelineCache& pipelineCache() noexcept { return *pipelineCache_; }
    [[nodiscard]] core::DescriptorPool& descriptorPool() noexcept { return *descriptorPool_; }
    [[nodiscard]] text::TextRenderer& textRenderer() noexcept { return textRenderer_; }
    [[nodiscard]] primitives::ReduceRenderer& reduceRenderer() noexcept { return reduceRenderer_; }
    [[nodiscard]] primitives::SpineRenderer& spineRenderer() noexcept { return spineRenderer_; }
    /// True when the text renderer pipeline + atlas are ready to draw.
    [[nodiscard]] bool textReady() const noexcept { return textInited_ && textReady_; }
    /// True when the spine renderer pipeline is ready.
    [[nodiscard]] bool spineReady() const noexcept { return spineInited_; }

    /// Draw UTF-8 text with mathtext (`$…$`) support at a pixel position
    /// (baseline origin). Used internally and by plot layers drawing
    /// rich-text elements (TeX markers, contour labels).
    void drawRichText(vk::CommandBuffer cmd, vk::Rect2D scissor,
                      std::string_view text, float x, float y,
                      plot::Color color, float scale = 1.0f,
                      float rotation = 0.0f,
                      plot::HAlign lineAlign = plot::HAlign::Left);
    /// Measure rich text at `scale` (mathtext-aware).
    [[nodiscard]] text::TextRenderer::TextMetrics
    measureRichText(std::string_view text, float scale = 1.0f);

private:
    /// Which plot subset a frame draws (blit modes).
    enum class DrawSubset { All, StaticOnly, AnimatedOnly };
    void renderFrameSubset(plot::Figure& figure, DrawSubset subset);

    backend::IBackend& backend_;
    std::unique_ptr<core::PipelineCache> pipelineCache_;
    std::unique_ptr<core::DescriptorPool> descriptorPool_;
    primitives::SpineRenderer spineRenderer_;
    primitives::ReduceRenderer reduceRenderer_;
    text::TextRenderer textRenderer_;
    bool textInited_ = false;
    bool spineInited_ = false;
    bool reduceInited_ = false;
    bool textReady_ = false;
    bool prepared_ = false;

    /// Draw axis labels, tick labels, and title for one axes.
    void drawText(vk::CommandBuffer cmd, const plot::Axes& axes,
                  plot::Rect2D rect);

    /// Draw axis spines (border lines) and tick marks for one axes.
    void drawSpines(vk::CommandBuffer cmd, const plot::Axes& axes,
                    plot::Rect2D rect);

    /// Draw tick-aligned grid lines for one axes (xAxis.grid → vertical
    /// lines at x ticks, yAxis.grid → horizontal lines at y ticks;
    /// gridWhich selects major/minor/both).
    void drawGrid(vk::CommandBuffer cmd, const plot::Axes& axes,
                  plot::Rect2D rect);

    /// Draw a legend for the axes (if enabled in style).
    void drawLegend(vk::CommandBuffer cmd, const plot::Axes& axes,
                    plot::Rect2D rect);

    /// Draw a colorbar for the axes (if enabled in style).
    void drawColorbar(vk::CommandBuffer cmd, const plot::Axes& axes,
                      plot::Rect2D rect);

    /// Draw text annotations and arrow annotations for one axes.
    void drawAnnotations(vk::CommandBuffer cmd, const plot::Axes& axes,
                         plot::Rect2D rect);

    /// Vector-backend savefig (pdf/svg/eps/pgf). See savefig.
    [[nodiscard]] bool savefigVector(plot::Figure& figure,
                                     const std::filesystem::path& path,
                                     const encode::SaveOptions& options,
                                     encode::ImageFormat fmt);
};

} // namespace volcano::render
