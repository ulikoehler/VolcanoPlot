// volcano/plot/plots/Text3D.hpp — 3D text annotations (matplotlib `Axes3D.text`)
#pragma once
#include "volcano/plot/Plot.hpp"
#include "volcano/plot/Types.hpp"
#include "volcano/plot/Transform.hpp"
#include <vector>
#include <string>

namespace volcano::plot {

/// Configuration for a single 3D text annotation.
struct Text3DItem {
    float x, y, z;          ///< 3D position
    std::string text;       ///< Text content
    Color color = Color::black();
    float fontSize = 12.0f; ///< Font scale (relative to base size)
    float rotation = 0.0f;  ///< Rotation in radians (screen space)
};

/// 3D text annotation plot. Renders text labels at 3D positions,
/// projected through Camera3D to 2D screen space.
/// Equivalent to matplotlib's `Axes3D.text(x, y, z, s)`.
///
/// Each text item's 3D position is projected to 2D NDC using the Camera3D
/// view-projection matrix, then converted to pixel coordinates for rendering
/// via the existing TextRenderer. Text is drawn in screen space (not rotated
/// in 3D — only a 2D screen-space rotation is applied).
class Text3D : public IPlot {
public:
    /// Construct from a list of text items.
    explicit Text3D(std::vector<Text3DItem> items);

    /// Construct a single text annotation.
    Text3D(float x, float y, float z, std::string text,
           Color color = Color::black(), float fontSize = 12.0f);

    /// Set the camera for 3D projection. Must be called before prepare().
    void setCamera(const Camera3D& camera) { camera_ = camera; }

    void prepare(render::Renderer& r) override;
    void draw(vk::CommandBuffer cmd, render::Renderer& r,
              const Axes& axes, Rect2D rect) override;
    void contributeToAutoscale(Viewport& v) const override;
    [[nodiscard]] std::string label() const override { return ""; }
    [[nodiscard]] Color legendColor() const override { return Color::black(); }

private:
    std::vector<Text3DItem> items_;
    Camera3D camera_;

    // Projected pixel positions (computed in prepare).
    struct ProjectedText {
        float px, py;           ///< pixel position
        std::string text;
        Color color;
        float fontSize;
        float rotation;
        bool visible;           ///< false if behind camera
    };
    std::vector<ProjectedText> projected_;
    bool prepared_ = false;
};

} // namespace volcano::plot
