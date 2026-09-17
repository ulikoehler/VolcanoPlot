// volcano/plot/Animation.hpp — matplotlib-style animation model
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace volcano::plot {

class Figure;

/// Base animation: produces figure state changes per frame.
/// Frame i mutates the figure via drawFrame(i); the host re-renders.
class Animation {
public:
    explicit Animation(Figure& fig) : fig_(&fig) {}
    virtual ~Animation() = default;

    /// Number of frames (may be SIZE_MAX for infinite generators).
    [[nodiscard]] virtual size_t frameCount() const = 0;
    /// Apply frame i to the figure (mpl's _draw_next_frame).
    virtual void drawFrame(size_t i) = 0;
    /// The figure being animated.
    [[nodiscard]] Figure& figure() const noexcept { return *fig_; }

    /// ms between frames (mpl interval).
    int interval = 50;
    /// Restart after the last frame (mpl repeat).
    bool repeat = true;
    /// ms pause between repeats (mpl repeat_delay).
    int repeatDelay = 0;
    /// Redraw only animated artists (blit). When true, the renderer may
    /// cache the background; semantically the flag marks animated layers.
    bool blit = false;

private:
    Figure* fig_;
};

/// TimedAnimation: wall-clock pacing for live playback (mpl's
/// TimedAnimation). Call advance(nowMs) each event-loop iteration;
/// it returns true when drawFrame should be called + re-rendered.
class TimedAnimation : public Animation {
public:
    using Animation::Animation;

    /// Advance the playback clock. Returns true when the current frame
    /// changed (or the sequence finished/repeated).
    bool advance(uint64_t nowMs);
    [[nodiscard]] size_t currentFrame() const noexcept { return frame_; }
    /// True once all frames played and repeat is off.
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    /// Reset to frame 0.
    void restart();

private:
    size_t frame_ = 0;
    uint64_t lastMs_ = 0;
    bool started_ = false;
    bool finished_ = false;
};

/// FuncAnimation: calls func(frameIndex) each frame (mpl API).
class FuncAnimation : public TimedAnimation {
public:
    FuncAnimation(Figure& fig, std::function<void(size_t)> func,
                  size_t frames, std::function<void()> init = {},
                  int intervalMs = 50, bool blit = false, bool repeat = true);

    [[nodiscard]] size_t frameCount() const override { return frames_; }
    void drawFrame(size_t i) override;

private:
    std::function<void(size_t)> func_;
    std::function<void()> init_;
    size_t frames_;
    bool initialized_ = false;
};

/// ArtistAnimation: steps through a list of pre-baked frame appliers
/// (mpl's ArtistAnimation takes lists of artists; here each entry
/// is a callable that configures the figure for that frame).
class ArtistAnimation : public TimedAnimation {
public:
    ArtistAnimation(Figure& fig,
                    std::vector<std::function<void()>> frameAppliers,
                    int intervalMs = 50, bool blit = false,
                    bool repeat = true);

    [[nodiscard]] size_t frameCount() const override { return appliers_.size(); }
    void drawFrame(size_t i) override;

private:
    std::vector<std::function<void()>> appliers_;
};

} // namespace volcano::plot
