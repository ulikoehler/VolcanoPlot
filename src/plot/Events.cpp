// volcano/plot/Events.cpp
#include "volcano/plot/Events.hpp"

namespace volcano::plot {

std::string_view Event::name() const noexcept {
    switch (type) {
        case Type::ButtonPress: return "button_press_event";
        case Type::ButtonRelease: return "button_release_event";
        case Type::MotionNotify: return "motion_notify_event";
        case Type::Scroll: return "scroll_event";
        case Type::KeyPress: return "key_press_event";
        case Type::KeyRelease: return "key_release_event";
        case Type::Pick: return "pick_event";
        case Type::Resize: return "resize_event";
        case Type::Draw: return "draw_event";
        case Type::FigureEnter: return "figure_enter_event";
        case Type::FigureLeave: return "figure_leave_event";
        case Type::AxesEnter: return "axes_enter_event";
        case Type::AxesLeave: return "axes_leave_event";
    }
    return "unknown";
}

int EventCanvas::connect(std::string_view name, Callback cb) {
    int id = nextId_++;
    callbacks_[std::string(name)].push_back({id, std::move(cb)});
    return id;
}

void EventCanvas::disconnect(int id) {
    for (auto& [name, cbs] : callbacks_)
        std::erase_if(cbs, [id](auto& p) { return p.first == id; });
}

void EventCanvas::emit(const Event& e) {
    auto it = callbacks_.find(std::string(e.name()));
    if (it == callbacks_.end()) return;
    for (auto& [id, cb] : it->second) cb(e);
}

size_t EventCanvas::connectionCount() const {
    size_t n = 0;
    for (auto& [_, cbs] : callbacks_) n += cbs.size();
    return n;
}

} // namespace volcano::plot
