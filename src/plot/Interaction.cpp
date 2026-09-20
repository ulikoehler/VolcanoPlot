// volcano/plot/Interaction.cpp — navigation toolbar logic
#include "volcano/plot/Interaction.hpp"
#include "volcano/plot/Axes.hpp"
#include "volcano/plot/Plot.hpp"

#include <cmath>
#include <format>

namespace volcano::plot {

std::string Navigation::formatCoord(const Axes& ax, Point2D data) const {
    (void)ax;
    return std::format("x={:.4g}, y={:.4g}", data.x, data.y);
}

void Navigation::pushHistory() {
    if (!figure_) return;
    // Drop forward history (truncate only — never grow with empties).
    if (histPos_ + 1 < history_.size()) history_.resize(histPos_ + 1);
    Snapshot snap;
    for (auto* ax : figure_->allAxes())
        snap.push_back({ax, ax->viewport()});
    history_.push_back(std::move(snap));
    histPos_ = history_.size() - 1;
}

void Navigation::applySnapshot(const Snapshot& s) {
    for (auto& [ax, vp] : s)
        if (ax) ax->setViewport(vp);
}

void Navigation::home() {
    if (history_.empty()) return;
    histPos_ = 0;
    applySnapshot(history_[0]);
}

void Navigation::back() {
    if (!canBack()) return;
    --histPos_;
    applySnapshot(history_[histPos_]);
}

void Navigation::forward() {
    if (!canForward()) return;
    ++histPos_;
    applySnapshot(history_[histPos_]);
}

void Navigation::startDrag3D(const Event& e) {
    // Collect every 3D camera on the axes (mpl rotates the whole scene).
    cams3D_.clear();
    for (auto& p : e.inaxes->plots()) {
        if (Camera3D* cam = p->camera3D()) {
            Cam3DState s{cam, cam->elevDeg, cam->azimDeg, cam->rollDeg, 0};
            if (std::isnan(s.elev) || std::isnan(s.azim)) {
                // Camera built from explicit eye: derive spherical angles.
                float dx = cam->eye.x - cam->target.x;
                float dy = cam->eye.y - cam->target.y;
                float dz = cam->eye.z - cam->target.z;
                float r = std::sqrt(dx*dx + dy*dy + dz*dz);
                s.dist = r;
                s.elev = std::asin(dz / std::max(r, 1e-9f)) * 180.0f
                         / float(M_PI);
                s.azim = std::atan2(dy, dx) * 180.0f / float(M_PI);
            } else {
                float dx = cam->eye.x - cam->target.x;
                float dy = cam->eye.y - cam->target.y;
                float dz = cam->eye.z - cam->target.z;
                s.dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            }
            cams3D_.push_back(s);
        }
    }
    if (cams3D_.empty()) return;
    dragging_ = true;
    dragAxes_ = e.inaxes;
    dragStartCanvas_ = {e.x, e.y};
    drag3DButton_ = e.button;
}

void Navigation::dragTo3D(const Event& e) {
    float dx = e.x - dragStartCanvas_.x;
    float dy = e.y - dragStartCanvas_.y;  // canvas px, y-down
    float w = float(std::max(dragAxes_->rect.width, 1u));
    float h = float(std::max(dragAxes_->rect.height, 1u));
    for (auto& s : cams3D_) {
        Camera3D& cam = *s.cam;
        float elev = s.elev, azim = s.azim, dist = s.dist;
        if (drag3DButton_ == 3) {
            // Right-drag: zoom — dolly in/out (drag up zooms in).
            dist = s.dist * std::exp(dy * 0.01f);
        } else {
            // Left-drag: rotate — mpl Axes3D._on_move:
            // azim -= dx/w*360, elev += dy_up/h*180 (y-down → -dy).
            azim = s.azim - dx / w * 360.0f;
            elev = std::clamp(s.elev - dy / h * 180.0f, -180.0f, 180.0f);
        }
        Camera3D n = Camera3D::viewInit(elev, azim, s.roll, cam.target, dist);
        // Preserve projection/box-normalization settings.
        n.fov = cam.fov; n.aspect = cam.aspect;
        n.nearZ = cam.nearZ; n.farZ = cam.farZ;
        n.dataMin = cam.dataMin; n.dataMax = cam.dataMax;
        cam = n;
    }
}

void Navigation::startDrag(const Event& e) {
    if (mode_ == Mode::None || !e.inaxes) return;
    dragging_ = true;
    dragAxes_ = e.inaxes;
    dragStartCanvas_ = {e.x, e.y};
    dragStartData_ = e.dataPos;
    dragStartVp_ = e.inaxes->viewport();
    rectEnd_ = {e.x, e.y};
    pushHistory();
}

void Navigation::dragTo(const Event& e) {
    if (!dragging_) return;
    rectEnd_ = {e.x, e.y};
    if (mode_ == Mode::Pan && dragAxes_) {
        // Pan: translate the viewport by the axes-fraction drag delta.
        auto f0 = dragAxes_->canvasToFraction(dragStartCanvas_);
        auto f1 = dragAxes_->canvasToFraction({e.x, e.y});
        float dx = f0.x - f1.x, dy = f1.y - f0.y;
        auto vp = dragStartVp_;
        float xs = vp.x.span(), ys = vp.y.span();
        vp.x.min += dx * xs;
        vp.x.max += dx * xs;
        vp.y.min += dy * ys;
        vp.y.max += dy * ys;
        dragAxes_->setViewport(vp);
    }
}

void Navigation::endDrag(const Event& e) {
    if (!dragging_) return;
    dragging_ = false;
    if (mode_ == Mode::ZoomRect && dragAxes_) {
        // Zoom to the dragged rectangle (canvas px → axes fraction → data).
        auto f0 = dragAxes_->canvasToFraction(dragStartCanvas_);
        auto f1 = dragAxes_->canvasToFraction({e.x, e.y});
        auto& vp = dragStartVp_;
        auto newVp = vp;
        // Work in display (scaled) space, then invert back to data space.
        auto zoomRange = [](const AxisScale& s, float lo, float hi,
                            float f0, float f1, Range& out) {
            float d0 = s.forward(lo), d1 = s.forward(hi);
            float span = d1 - d0;
            out.min = s.inverse(d0 + std::min(f0, f1) * span);
            out.max = s.inverse(d0 + std::max(f0, f1) * span);
        };
        if (!constrainY) zoomRange(dragAxes_->xscale(), vp.x.min, vp.x.max,
                                  f0.x, f1.x, newVp.x);
        if (!constrainX) zoomRange(dragAxes_->yscale(), vp.y.min, vp.y.max,
                                  f0.y, f1.y, newVp.y);
        // Ignore degenerate (<4px) zoom rects (mpl rubberband threshold).
        if (std::abs(e.x - dragStartCanvas_.x) > 4.0f &&
            std::abs(e.y - dragStartCanvas_.y) > 4.0f)
            dragAxes_->setViewport(newVp);
    }
    dragAxes_ = nullptr;
    // mpl pushes the post-gesture state so back/forward can revisit it.
    pushHistory();
}

bool Navigation::handleKey(const Event& e) {
    if (e.key.empty()) return false;
    char k = e.key[0];
    switch (k) {
        case 'p': pan(); return true;
        case 'o': zoom(); return true;
        case 'h': case 'r': home(); return true;
        case 'g': {
            for (auto* ax : figure_->allAxes()) {
                auto& st = ax->style();
                st.xAxis.grid = st.yAxis.grid = !st.xAxis.grid;
            }
            return true;
        }
        case 'l': {
            for (auto* ax : figure_->allAxes()) {
                ax->yscale().kind = ax->yscale().kind == ScaleKind::Log
                    ? ScaleKind::Linear : ScaleKind::Log;
            }
            return true;
        }
        case 'k': {
            for (auto* ax : figure_->allAxes()) {
                ax->xscale().kind = ax->xscale().kind == ScaleKind::Log
                    ? ScaleKind::Linear : ScaleKind::Log;
            }
            return true;
        }
        case 's': if (onSaveRequest) onSaveRequest(); return true;
        case 'f': if (onFullscreenToggle) onFullscreenToggle(); return true;
        case 'q': if (onQuitRequest) onQuitRequest(); return true;
        case 'x': constrainX = !constrainX; return true;
        case 'y': constrainY = !constrainY; return true;
        default: return false;
    }
}

bool Navigation::handleEvent(const Event& e) {
    switch (e.type) {
        case Event::Type::KeyPress:
            return handleKey(e);
        case Event::Type::ButtonPress:
            if (e.button == 1 || e.button == 3) {
                // mpl Axes3D: plain drag rotates (button1) / zooms
                // (button3) when the axes holds 3D plots.
                if (e.inaxes && std::ranges::any_of(e.inaxes->plots(),
                        [](const auto& p) { return p->camera3D(); })) {
                    startDrag3D(e);
                    return true;
                }
                startDrag(e);
                return dragging_;
            }
            return false;
        case Event::Type::MotionNotify:
            if (dragging_ && !cams3D_.empty()) { dragTo3D(e); return true; }
            if (dragging_) { dragTo(e); return true; }
            // Cursor data readout (toolbar coordinate display).
            if (onCursorMove && e.inaxes) {
                onCursorMove(formatCoord(*e.inaxes, e.dataPos));
            }
            return false;
        case Event::Type::ButtonRelease:
            if (dragging_) {
                endDrag(e);
                cams3D_.clear();
                drag3DButton_ = 0;
                return true;
            }
            return false;
        case Event::Type::Scroll: {
            // Scroll zoom: scale both view ranges by 1.2^-step about the
            // cursor's data position (scale-aware via forward/inverse).
            if (!scrollZoom || !e.inaxes || e.step == 0.0f) return false;
            pushHistory();
            auto vp = e.inaxes->viewport();
            float f = std::pow(1.2f, -e.step);
            auto zoomAbout = [](const AxisScale& s, Range& r, float c,
                                float f) {
                float d0 = s.forward(r.min), d1 = s.forward(r.max);
                float dc = s.forward(c);
                r.min = s.inverse(dc + (d0 - dc) * f);
                r.max = s.inverse(dc + (d1 - dc) * f);
            };
            if (!constrainY) zoomAbout(e.inaxes->xscale(), vp.x,
                                       e.dataPos.x, f);
            if (!constrainX) zoomAbout(e.inaxes->yscale(), vp.y,
                                       e.dataPos.y, f);
            e.inaxes->setViewport(vp);
            pushHistory();
            return true;
        }
        default:
            return false;
    }
}

} // namespace volcano::plot
