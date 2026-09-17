// volcano/plot/Events.hpp — matplotlib-style event system
#pragma once

#include "volcano/plot/Types.hpp"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace volcano::plot {

class Axes;

/// matplotlib canvas event: button_press_event, motion_notify_event, etc.
struct Event {
    enum class Type {
        ButtonPress, ButtonRelease, MotionNotify, Scroll,
        KeyPress, KeyRelease, Pick,
        Resize, Draw,
        FigureEnter, FigureLeave, AxesEnter, AxesLeave,
    };
    Type type;
    /// Canvas pixel position (top-left origin, Y-down).
    float x = 0.0f, y = 0.0f;
    /// Mouse button: 1=left, 2=middle, 3=right.
    int button = 0;
    /// Pressed-button bitmask for motion events (bit0=left, bit1=middle,
    /// bit2=right).
    int buttons = 0;
    /// Double-click flag on ButtonPress (mpl MouseEvent.dblclick).
    bool dblclick = false;
    /// Scroll steps (+up / -down).
    float step = 0.0f;
    /// Key name for key events ("p", "control", "shift", ...).
    std::string key;
    bool shift = false, ctrl = false, alt = false;
    /// Axes under the cursor (hit-tested), or nullptr.
    Axes* inaxes = nullptr;
    /// Data coordinates inside inaxes (valid when inaxes != nullptr).
    Point2D dataPos{0.0f, 0.0f};
    /// New canvas size for Resize events.
    uint32_t width = 0, height = 0;

    /// mpl_connect event name for this type.
    [[nodiscard]] std::string_view name() const noexcept;
};

/// Event canvas: named-signal connect/disconnect/emit, attached to a
/// Figure (mpl's `fig.canvas.mpl_connect(...)`).
class EventCanvas {
public:
    using Callback = std::function<void(const Event&)>;

    /// mpl_connect: subscribe to an event name. Returns a connection id.
    int connect(std::string_view name, Callback cb);
    /// mpl_disconnect.
    void disconnect(int id);
    /// Emit an event to all subscribers of its mpl name.
    void emit(const Event& e);
    /// Number of live connections (for tests).
    [[nodiscard]] size_t connectionCount() const;

private:
    std::map<std::string, std::vector<std::pair<int, Callback>>> callbacks_;
    int nextId_ = 1;
};

} // namespace volcano::plot
