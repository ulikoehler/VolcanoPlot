// volcano/plot/PhaseDecomposition.hpp — f32 phase decomposition for deep-zoom
#pragma once

#include <cmath>
#include <cstdint>

namespace volcano::plot {

/// f32 phase decomposition for deep-zoom chirp plots.
///
/// When evaluating oscillatory functions like sin(f*x) at high frequencies,
/// the phase f*x can become very large (e.g. 1e10), causing f32 precision
/// loss: the mantissa has only 23 bits (~7 decimal digits), so small changes
/// in x produce no change in the computed phase.
///
/// The solution is to decompose the phase into a large constant part and a
/// small delta part:
///   phase = freq * x_center + freq * (x - x_center)
///         = phase_base + phase_delta
///
/// where phase_base is computed once (using f64) and phase_delta is small
/// enough to be represented accurately in f32. Then:
///   sin(phase) = sin(phase_base) * cos(phase_delta)
///              + cos(phase_base) * sin(phase_delta)
///   cos(phase) = cos(phase_base) * cos(phase_delta)
///              - sin(phase_base) * sin(phase_delta)
///
/// This preserves full f32 precision for the oscillation even when the total
/// phase is very large, enabling correct rendering of chirp plots at deep
/// zoom levels.
class PhaseDecomposer {
public:
    /// Construct a phase decomposer for frequency `freq` centered at `xCenter`.
    /// The decomposer precomputes sin/cos of the base phase using f64.
    PhaseDecomposer(double freq, double xCenter)
        : freq_(freq), xCenter_(xCenter) {
        double phaseBase = freq * xCenter;
        // Reduce phase_base to [-pi, pi] using f64 for precision.
        double reduced = std::remainder(phaseBase, 2.0 * M_PI);
        sinBase_ = static_cast<float>(std::sin(reduced));
        cosBase_ = static_cast<float>(std::cos(reduced));
    }

    /// Compute sin(freq * x) using phase decomposition.
    /// `x` is the evaluation point. The delta (x - xCenter) should be small
    /// relative to the period for best precision.
    [[nodiscard]] float sin(float x) const noexcept {
        // phase_delta = freq * (x - xCenter), computed in f32.
        float delta = static_cast<float>(freq_) * (x - static_cast<float>(xCenter_));
        return sinBase_ * std::cos(delta) + cosBase_ * std::sin(delta);
    }

    /// Compute cos(freq * x) using phase decomposition.
    [[nodiscard]] float cos(float x) const noexcept {
        float delta = static_cast<float>(freq_) * (x - static_cast<float>(xCenter_));
        return cosBase_ * std::cos(delta) - sinBase_ * std::sin(delta);
    }

    /// Compute the raw phase (freq * x) using decomposition.
    /// Returns the phase as a float (may lose precision for very large phases,
    /// but sin/cos are still correct via the decomposition formulas).
    [[nodiscard]] float phase(float x) const noexcept {
        // This is approximate — the whole point is that the raw phase
        // loses precision. Use it only for non-oscillatory purposes.
        return static_cast<float>(freq_) * x;
    }

    /// The frequency.
    [[nodiscard]] double frequency() const noexcept { return freq_; }

    /// The center point.
    [[nodiscard]] double center() const noexcept { return xCenter_; }

    /// Precomputed sin(phase_base).
    [[nodiscard]] float sinBase() const noexcept { return sinBase_; }

    /// Precomputed cos(phase_base).
    [[nodiscard]] float cosBase() const noexcept { return cosBase_; }

private:
    double freq_;
    double xCenter_;
    float sinBase_;
    float cosBase_;
};

/// Evaluate a linear chirp signal at point x.
/// A linear chirp sweeps frequency from f0 at t=0 to f1 at t=duration.
/// The instantaneous frequency at time t is: f(t) = f0 + (f1-f0)*t/duration
/// The phase is the integral of frequency: phase(t) = 2*pi*(f0*t + 0.5*(f1-f0)*t^2/duration)
///
/// For deep-zoom precision, this uses f64 for the phase computation and
/// phase decomposition for the oscillatory evaluation.
struct LinearChirp {
    double f0;       ///< Start frequency (Hz)
    double f1;       ///< End frequency (Hz)
    double duration; ///< Total duration (seconds)

    /// Evaluate the chirp signal at time t.
    [[nodiscard]] float evaluate(float t) const noexcept {
        // Compute phase in f64 for precision.
        double phase = 2.0 * M_PI * (f0 * t + 0.5 * (f1 - f0) * t * t / duration);
        // Reduce to [-pi, pi] in f64.
        double reduced = std::remainder(phase, 2.0 * M_PI);
        return static_cast<float>(std::sin(reduced));
    }

    /// Evaluate the chirp signal at time t using phase decomposition
    /// centered at tCenter. This is more precise when zoomed in around tCenter
    /// because the delta phase is small.
    [[nodiscard]] float evaluateDecomposed(float t, float tCenter) const noexcept {
        // Base phase at center (f64).
        double phaseBase = 2.0 * M_PI * (f0 * tCenter + 0.5 * (f1 - f0) * tCenter * tCenter / duration);
        double reducedBase = std::remainder(phaseBase, 2.0 * M_PI);
        float sinBase = static_cast<float>(std::sin(reducedBase));
        float cosBase = static_cast<float>(std::cos(reducedBase));

        // Delta phase: integral of instantaneous frequency from tCenter to t.
        // inst_freq(t) = f0 + (f1-f0)*t/duration
        // phase_delta = integral from tCenter to t of 2*pi*inst_freq(tau) dtau
        //             = 2*pi * [f0*(t-tCenter) + 0.5*(f1-f0)/duration * (t^2 - tCenter^2)]
        // But (t^2 - tCenter^2) = (t - tCenter)*(t + tCenter), so:
        // phase_delta = 2*pi * (t - tCenter) * [f0 + 0.5*(f1-f0)/duration * (t + tCenter)]
        double dt = t - tCenter;
        double avgFreq = f0 + 0.5 * (f1 - f0) / duration * (t + tCenter);
        float delta = static_cast<float>(2.0 * M_PI * dt * avgFreq);

        return sinBase * std::cos(delta) + cosBase * std::sin(delta);
    }
};

} // namespace volcano::plot
