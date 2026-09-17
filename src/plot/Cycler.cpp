// volcano/plot/Cycler.cpp — property cycler implementation
#include <volcano/plot/Cycler.hpp>

namespace volcano::plot {

Cycler Cycler::ofEntries(std::vector<CycleProps> props) {
    Cycler c;
    c.props_ = std::move(props);
    return c;
}

Cycler Cycler::ofColors(std::vector<Color> colors) {
    Cycler c;
    c.props_.reserve(colors.size());
    for (auto col : colors) {
        CycleProps p; p.color = col; c.props_.push_back(p);
    }
    return c;
}

Cycler Cycler::ofLineStyles(std::vector<LineStyle> styles) {
    Cycler c;
    c.props_.reserve(styles.size());
    for (auto s : styles) {
        CycleProps p; p.lineStyle = s; c.props_.push_back(p);
    }
    return c;
}

Cycler Cycler::ofLineWidths(std::vector<float> widths) {
    Cycler c;
    c.props_.reserve(widths.size());
    for (float w : widths) {
        CycleProps p; p.lineWidth = w; c.props_.push_back(p);
    }
    return c;
}

Cycler Cycler::ofMarkers(std::vector<MarkerStyle> markers) {
    Cycler c;
    c.props_.reserve(markers.size());
    for (auto m : markers) {
        CycleProps p; p.marker = m; c.props_.push_back(p);
    }
    return c;
}

Cycler Cycler::operator+(const Cycler& o) const {
    Cycler r;
    r.props_.reserve(props_.size() + o.props_.size());
    r.props_.insert(r.props_.end(), props_.begin(), props_.end());
    r.props_.insert(r.props_.end(), o.props_.begin(), o.props_.end());
    return r;
}

Cycler Cycler::operator*(const Cycler& o) const {
    Cycler r;
    if (props_.empty() || o.props_.empty()) return r;
    r.props_.reserve(props_.size() * o.props_.size());
    for (const auto& a : props_) {
        for (const auto& b : o.props_) {
            CycleProps m;
            m.color = a.color ? a.color : b.color;
            m.lineStyle = a.lineStyle ? a.lineStyle : b.lineStyle;
            m.lineWidth = a.lineWidth ? a.lineWidth : b.lineWidth;
            m.marker = a.marker ? a.marker : b.marker;
            r.props_.push_back(m);
        }
    }
    return r;
}

} // namespace volcano::plot
