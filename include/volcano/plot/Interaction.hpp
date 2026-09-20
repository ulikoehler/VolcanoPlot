// volcano/plot/Interaction.hpp — navigation toolbar + key bindings
#pragma once

#include "volcano/plot/Events.hpp"
#include "volcano/plot/Transform.hpp"
#include "volcano/plot/Types.hpp"

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace volcano::plot {

class Axes;
class Figure;

/// matplotlib NavigationToolbar equivalent: pan/zoom modes, viewport
/// history (home/back/forward), zoom-to-rectangle, key bindings, and
/// cursor readout. Feed it canvas events; it mutates axes viewports.
class Navigation {
public:
    enum class Mode { None, Pan, ZoomRect };

    explicit Navigation(Figure& fig) : figure_(&fig) {}

    // --- Toolbar state ---
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    /// mpl toolbar.pan()/zoom(): toggle the mode.
    void pan() { mode_ = (mode_ == Mode::Pan) ? Mode::None : Mode::Pan; }
    void zoom() { mode_ = (mode_ == Mode::ZoomRect) ? Mode::None : Mode::ZoomRect; }

    // --- History ---
    void home();
    void back();
    void forward();
    /// Capture the current viewport state as a history entry (called
    /// automatically on the first drag).
    void pushHistory();
    [[nodiscard]] bool canBack() const { return histPos_ > 0; }
    [[nodiscard]] bool canForward() const {
        return histPos_ + 1 < history_.size();
    }

    // --- Event handling ---
    /// Returns true if the event was consumed (mode active / binding hit).
    bool handleEvent(const Event& e);

    /// Cursor readout: "x=0.123, y=0.456" in data coords.
    [[nodiscard]] virtual std::string formatCoord(const Axes& ax,
                                                Point2D data) const;

    /// Callbacks (wired by the host — e.g. 's' save, 'f' fullscreen, 'q' quit).
    std::function<void()> onSaveRequest;
    std::function<void()> onFullscreenToggle;
    std::function<void()> onQuitRequest;
    /// Cursor data readout (mpl's toolbar message): called on motion over
    /// an axes with `formatCoord` output.
    std::function<void(std::string_view)> onCursorMove;

    /// Optional x/y axis constraint for zoom-rect (mpl's 'x'/'y' zoom mode).
    bool constrainX = false, constrainY = false;
    /// Scroll-wheel zoom about the cursor (off by default like mpl's base
    /// toolbar; the default mpl keymap doesn't bind scroll to zoom).
    bool scrollZoom = false;

private:
    struct ViewSnapshot { Axes* axes; Viewport vp; };
    /// One history entry: a viewport snapshot for every axes.
    using Snapshot = std::vector<ViewSnapshot>;

    void applySnapshot(const Snapshot& s);
    void startDrag(const Event& e);
    void dragTo(const Event& e);
    void endDrag(const Event& e);
    bool handleKey(const Event& e);
    /// mpl Axes3D mouse handling: left-drag rotates (elev/azim),
    /// right-drag zooms (camera distance).
    void startDrag3D(const Event& e);
    void dragTo3D(const Event& e);

    Figure* figure_;
    Mode mode_ = Mode::None;
    std::deque<Snapshot> history_;
    size_t histPos_ = 0;

    // Drag state.
    bool dragging_ = false;
    Axes* dragAxes_ = nullptr;
    Point2D dragStartCanvas_{0, 0}; // canvas px
    Point2D dragStartData_{0, 0};
    Viewport dragStartVp_{};
    // 3D camera drag state: per-camera initial angles/distance captured
    // at drag start (mpl Axes3D._on_move semantics).
    struct Cam3DState {
        Camera3D* cam;
        float elev, azim, roll, dist;
    };
    std::vector<Cam3DState> cams3D_;
    int drag3DButton_ = 0;
    // Zoom-rect selection (canvas px), drawn as rubber band.
    Point2D rectEnd_{0, 0};
public:
    /// Current zoom-rectangle in canvas pixels (valid while dragging in
    /// ZoomRect mode).
    [[nodiscard]] bool hasZoomRect() const {
        return mode_ == Mode::ZoomRect && dragging_;
    }
    [[nodiscard]] std::pair<Point2D, Point2D> zoomRect() const {
        return {dragStartCanvas_, rectEnd_};
    }
};

} // namespace volcano::plot
