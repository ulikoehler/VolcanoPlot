// volcano/plot/Animation.cpp
#include "volcano/plot/Animation.hpp"

namespace volcano::plot {

// ---------------------------------------------------------------- TimedAnimation
bool TimedAnimation::advance(uint64_t nowMs) {
    if (finished_) return false;
    if (!started_) {
        started_ = true;
        lastMs_ = nowMs;
        return true; // draw frame 0
    }
    if (nowMs - lastMs_ < uint64_t(interval)) return false;
    lastMs_ = nowMs;

    size_t n = frameCount();
    if (n == 0) { finished_ = true; return false; }
    ++frame_;
    if (frame_ >= n) {
        if (!repeat) { finished_ = true; frame_ = n - 1; return false; }
        // repeatDelay: pause once per loop (approximated — the delay folds
        // into the next interval deadline).
        frame_ = 0;
        lastMs_ = nowMs + uint64_t(repeatDelay);
    }
    return true;
}

void TimedAnimation::restart() {
    frame_ = 0;
    started_ = false;
    finished_ = false;
    lastMs_ = 0;
}

// ---------------------------------------------------------------- FuncAnimation
FuncAnimation::FuncAnimation(Figure& fig, std::function<void(size_t)> func,
                             size_t frames, std::function<void()> init,
                             int intervalMs, bool blit, bool repeat_)
    : TimedAnimation(fig), func_(std::move(func)), init_(std::move(init)),
      frames_(frames) {
    interval = intervalMs;
    this->blit = blit;
    this->repeat = repeat_;
}

void FuncAnimation::drawFrame(size_t i) {
    if (!initialized_ && init_) {
        init_();
        initialized_ = true;
    }
    if (func_) func_(i);
}

// ---------------------------------------------------------------- ArtistAnimation
ArtistAnimation::ArtistAnimation(Figure& fig,
                                 std::vector<std::function<void()>> appliers,
                                 int intervalMs, bool blit, bool repeat_)
    : TimedAnimation(fig), appliers_(std::move(appliers)) {
    interval = intervalMs;
    this->blit = blit;
    this->repeat = repeat_;
}

void ArtistAnimation::drawFrame(size_t i) {
    if (i < appliers_.size() && appliers_[i]) appliers_[i]();
}

} // namespace volcano::plot
