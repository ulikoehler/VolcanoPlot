// volcano/ffi/volcano_ffi.cpp — C ABI implementation.
// One VpFigure owns a headless backend + renderer + figure/axes.
// Traces are LinePlots keyed by a monotonically increasing id; their
// data is kept CPU-side so the whole figure can be rebuilt on resize
// (plots hold GPU buffers tied to the backend — a backend rebuild
// without rebuilding the plots crashes in updatePoints).
#include "volcano/ffi/volcano_ffi.h"

#include <volcano/backend/Backend.hpp>
#include <volcano/backend/HeadlessBackend.hpp>
#include <volcano/plot/Axes.hpp>
#include <volcano/plot/Style.hpp>
#include <volcano/plot/plots/LinePlot.hpp>
#include <volcano/render/Offload.hpp>
#include <volcano/render/Renderer.hpp>

#include <cmath>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace volcano;

struct VpFigure {
    backend::BackendDesc desc{};
    std::unique_ptr<backend::IBackend> backend;
    std::unique_ptr<render::Renderer> renderer;
    std::unique_ptr<plot::Figure> figure;
    float margin_l = 0.07f, margin_r = 0.985f, margin_b = 0.20f, margin_t = 0.985f;
    bool grid_on = true;
    plot::Color custom_bg{0,0,0,0};
    bool has_custom_bg = false;
    plot::Axes* axes = nullptr;

    struct TraceData {
        // Interleaved [x,y] pairs — populated on figure rebuild by
        // pulling the live plot's raw f64 store back out (takeRawF64),
        // so it costs nothing while the figure is intact.
        std::vector<double> xy;
        uint32_t rgba8 = 0xFFFFFFFF;
        float width = 1.5f;
        int style = 0;
        int marker = 40;        // plot::MarkerStyle::None
        float marker_size = 6.0f;
        int xy_mode = 0;      // XY pair — no column-envelope decimation
    };
    std::unordered_map<uint64_t, TraceData> trace_data;
    std::unordered_map<uint64_t, plot::LinePlot*> traces;
    uint64_t next_id = 1;

    std::string x_label, y_label;
    bool white_bg = false;
    int w = 0, h = 0;

    // Explicit tick positions + labels per axis (the caller mirrors
    // its own locator/formatter — e.g. liveplot's egui-style clock
    // labels). Empty `ticks_set` = automatic locator/formatter.
    struct TickOverride {
        std::vector<float> positions;
        std::vector<std::string> labels;
    };
    std::optional<TickOverride> ticks[2];

    // Explicit minor grid lines per axis: data-space positions plus a
    // per-line strength (0..1 alpha scale, egui_plot's decade-level
    // fade). Empty positions = no minor grid on that axis.
    struct MinorGrid {
        std::vector<float> positions;
        std::vector<float> strengths;
    };
    MinorGrid minor_grid[2];

    // Scratch for vp_render_rgba_view when the staging rows are pitched
    // (non-contiguous view) — readback lands here and the pointer is
    // valid until the next render on this figure.
    std::vector<uint8_t> rgba_view_scratch;
};

/// Apply the stored minor-grid override to one axis' style: explicit
/// positions+strengths ride in AxisStyle and Renderer::drawGrid
/// buckets them into per-alpha draw sets. Minors keep the majors' own
/// `gridColor` family so they can never out-contrast them — but the raw
/// egui spacing strengths are remapped into [0.40, 0.70]: below ~0.40
/// dense marks fade under the visibility floor (the original
/// invisible-sub-grid bug), and the ceiling keeps even the strongest
/// minor clearly under major intensity (the inverted-intensity bug).
/// The gradient is preserved, just compressed.
static void apply_minor_grid(const VpFigure::MinorGrid& g,
                             plot::AxisStyle& as) {
    as.minorGridPositions = g.positions;
    as.minorGridStrengths = g.strengths;
    for (float& s : as.minorGridStrengths) {
        s = std::clamp(0.40f + 0.30f * s, 0.0f, 1.0f);
    }
    if (g.positions.empty()) {
        if (as.gridWhich == "both") as.gridWhich = "major";
    } else {
        as.gridWhich = "both";
        as.minorGridColor = as.gridColor;
        as.minorGridLineWidth = as.gridLineWidth;
        // Minors share the majors' color family, so with the subtle
        // default major alpha (~110/255) the strongest minor reads
        // almost as bright as a major. Lift the major alpha so majors
        // stay clearly dominant, and pin the minor base to the
        // original alpha — both are idempotent (this runs on every
        // minor-grid upload, after gridColor may already be lifted).
        as.minorGridColor.a = std::min(as.minorGridColor.a, 110.0f / 255.0f);
        as.gridColor.a = std::max(as.gridColor.a, 200.0f / 255.0f);
    }
}

static void apply_trace_style(const VpFigure::TraceData& td,
                              plot::Series2D& s) {
    s.color = plot::Color::fromRgba8(
        (td.rgba8 >> 24) & 0xFF, (td.rgba8 >> 16) & 0xFF,
        (td.rgba8 >> 8) & 0xFF, td.rgba8 & 0xFF);
    s.lineWidth = td.width;
    s.lineStyle = td.style == 1 ? plot::LineStyle::Dashed
                  : td.style == 2 ? plot::LineStyle::Dotted
                                  : plot::LineStyle::Solid;
    // style==3 (legacy points flag) forces a Circle marker when the
    // dedicated marker field is still at its default.
    s.marker = td.marker != 40
                   ? static_cast<plot::MarkerStyle>(td.marker)
                   : (td.style == 3 ? plot::MarkerStyle::Circle
                                    : plot::MarkerStyle::None);
    if (td.marker != 40) s.size = td.marker_size;
    // XY pairs are parametric curves — not x-monotonic — so the
    // column-envelope decimation (a per-column min/max fill built for
    // time traces) must not run on them; the egui backend draws the
    // full polyline for XY too.
    s.envelopeDecimate = !td.xy_mode;
}

static void rebuild_figure(VpFigure* f) {
    // Recover raw point data from live plots before the old figure is
    // destroyed — the rebuilt plots take the same buffers back, so a
    // rebuild doesn't cost a data copy.
    for (auto& [id, lp] : f->traces)
        if (lp) f->trace_data[id].xy = lp->takeRawF64();
    f->traces.clear();
    f->figure = std::make_unique<plot::Figure>(1, 1);
    // Fill the whole canvas — the texture must align with the egui
    // plot transform underneath it (default margins are matplotlib's
    // 0.125/0.9/0.11/0.88 which would squeeze the data area).
    // Leave room for VolcanoPlot's own axes/ticks/labels — liveplot
    // computes the same axes rect (left .11, right .985, bottom .14,
    // top .985) to align overlay coordinates.
    f->figure->grid().left = f->margin_l;
    f->figure->grid().right = f->margin_r;
    f->figure->grid().bottom = f->margin_b;
    f->figure->grid().top = f->margin_t;
    f->axes = f->figure->addAxes();
    auto st = f->white_bg ? plot::styles::defaultStyle()
                          : plot::styles::darkBackground();
    st.xAxis.grid = f->grid_on;
    st.yAxis.grid = f->grid_on;
    // Subtle grid — muted against the plot bg.
    auto gc = f->white_bg ? plot::Color::fromRgba8(200, 200, 200, 110)
                          : plot::Color::fromRgba8(80, 80, 80, 110);
    st.xAxis.gridColor = gc;
    st.yAxis.gridColor = gc;
    st.xAxis.gridLineWidth = 0.6f;
    st.yAxis.gridLineWidth = 0.6f;
    // egui parity: only the labeled (override) tick positions get
    // marks — mpl's AutoMinorLocator would add unlabeled stubs
    // between them that read as misaligned ticks.
    st.xAxis.ticks.minor = false;
    st.yAxis.ticks.minor = false;
    f->figure->style() = st;   // canvas/margins adopt the theme too
    f->axes->setStyle(st);
    // egui parity: subtle frame border, not the mpl styles' bright
    // solid spine — must match vp_set_background, or every figure
    // rebuild (resize / backend re-init) brings the white bar back.
    const auto spineGray = plot::Color::fromRgba8(
        f->white_bg ? 160 : 72, f->white_bg ? 160 : 72,
        f->white_bg ? 160 : 72, 255);
    for (const char* side : {"left", "right", "bottom", "top"})
        f->axes->spine(side).color = spineGray;
    if (!f->x_label.empty()) f->axes->style().xAxis.label = f->x_label;
    if (!f->y_label.empty()) f->axes->style().yAxis.label = f->y_label;
    // style()=st above reset faceColor to the theme default — re-apply
    // the embedder's panel bg or every rebuild paints white margins.
    if (f->has_custom_bg) {
        f->figure->style().faceColor = f->custom_bg;
        f->figure->style().edgeColor = f->custom_bg;
        f->axes->style().faceColor = f->custom_bg;
    }
    for (int a = 0; a < 2; ++a) {
        if (!f->ticks[a]) continue;
        auto& tc = a == 0 ? f->axes->style().xAxis.ticks
                          : f->axes->style().yAxis.ticks;
        tc.positions = f->ticks[a]->positions;
        tc.labels = f->ticks[a]->labels;
    }
    apply_minor_grid(f->minor_grid[0], f->axes->style().xAxis);
    apply_minor_grid(f->minor_grid[1], f->axes->style().yAxis);
    for (auto& [id, td] : f->trace_data) {
        if (td.xy.empty()) continue; // empty LinePlot crashes renderFrame
        auto lp = std::make_shared<plot::LinePlot>(plot::Series2D{});
        auto* raw = lp.get();
        f->axes->addPlot(std::move(lp));
        raw->setDataF64(std::move(td.xy));
        apply_trace_style(td, raw->series());
        f->traces[id] = raw;
    }
}

static bool init(VpFigure* f, int w, int h) {
    // Tear down the old figure *before* the backend: plots own GPU
    // buffers tied to the old device — destroying them after the
    // backend is gone trips vkDestroyBuffer on an invalid device.
    // Pull the raw point data out of the live plots first so the
    // rebuilt figure keeps it.
    for (auto& [id, lp] : f->traces)
        if (lp) f->trace_data[id].xy = lp->takeRawF64();
    f->traces.clear();
    f->figure = std::make_unique<plot::Figure>(1, 1);
    f->axes = nullptr;
    f->renderer.reset();
    f->desc.width = static_cast<uint32_t>(w);
    f->desc.height = static_cast<uint32_t>(h);
    f->desc.samples = vk::SampleCountFlagBits::e4;
    // Cost knob: liveplot's dense live scopes rasterize a full
    // envelope fill every frame — at 4x MSAA that is the dominant GPU
    // cost. VP_MSAA={1,2,4} trades line smoothness for throughput.
    if (const char* msaa = std::getenv("VP_MSAA")) {
        f->desc.samples = msaa[0] == '1' ? vk::SampleCountFlagBits::e1
                          : msaa[0] == '2' ? vk::SampleCountFlagBits::e2
                                           : vk::SampleCountFlagBits::e4;
    }
    // Shared GPU context: one instance/device/allocator process-wide —
    // N figures no longer cost N devices (VRAM + driver threads), and
    // parallel FFI tests stop tripping over per-device creation.
    f->backend = backend::createHeadlessBackend(
        f->desc, backend::sharedGpuContext());
    if (!f->backend) return false;
    f->renderer = std::make_unique<render::Renderer>(*f->backend);
    f->w = w;
    f->h = h;
    rebuild_figure(f);
    return true;
}

extern "C" {

namespace {
/// NVIDIA's driver (620.x series, observed via NVRM Xid 32 "corrupted
/// push buffer stream" + eglcore faults) is not stable under
/// concurrent device-level calls from many worker threads, even
/// though the usage is spec-legal (submits are already mutex'd, pools
/// are per-backend). Mesa/llvmpipe runs the same suite cleanly. This
/// gate serializes every GPU-touching entry point — render, figure
/// create/resize/destroy — so the driver only ever sees one caller.
/// Trace/style setters are pure CPU (they only mutate plot state the
/// next render reads) and stay parallel.
std::mutex& renderGate() {
    static std::mutex m;
    return m;
}
} // namespace

VpFigure* vp_figure_create(int width, int height) {
    std::lock_guard gate(renderGate());
    try {
        // The column-envelope decimation is fully device-resident
        // (reduce + polyline emit stay in the frame's compute stream —
        // no readback round trip), so Auto lets huge traces use it.
        // An explicit envelope= choice in VOLCANO_GPU_OFFLOAD still
        // wins; the legacy submit+readback envelope remains as the
        // fallback behind VOLCANO_ENVDEV=0.
        static const bool userEnv =
            [] {
                const char* v = std::getenv("VOLCANO_GPU_OFFLOAD");
                return v &&
                       std::string_view{v}.find("envelope") !=
                           std::string_view::npos;
            }();
        if (!userEnv)
            render::OffloadConfig::global().envelope =
                render::OffloadMode::Auto;
        auto* f = new VpFigure();
        if (!init(f, width, height)) {
            delete f;
            return nullptr;
        }
        return f;
    } catch (...) {
        return nullptr;
    }
}

void vp_figure_destroy(VpFigure* fig) {
    std::lock_guard gate(renderGate());
    try { delete fig; } catch (...) {}
}

int vp_shared_gpu_info(VpSharedGpuInfo* out) {
    std::lock_guard gate(renderGate());
    try {
        if (!out) return 0;
        auto ctx = backend::sharedGpuContextExtended();
        if (!ctx) return 0;
        out->vk_instance = reinterpret_cast<uint64_t>(
            static_cast<VkInstance>(ctx->instance.handle()));
        out->vk_physical_device = reinterpret_cast<uint64_t>(
            static_cast<VkPhysicalDevice>(ctx->physical.handle()));
        out->vk_device = reinterpret_cast<uint64_t>(
            static_cast<VkDevice>(ctx->device.handle()));
        out->api_version = ctx->apiVersion;
        out->graphics_family = ctx->device.graphicsFamily();
        out->shared_capable = ctx->extendedSharing ? 1u : 0u;
        return 1;
    } catch (...) {
        return 0;
    }
}

int vp_shared_device_exts(const char*** names_out) {
    try {
        if (!names_out) return 0;
        auto ctx = backend::sharedGpuContextExtended();
        if (!ctx) return 0;
        static std::vector<const char*> names;
        if (names.empty())
            for (const auto& e : ctx->device.enabledExtensions())
                names.push_back(e.c_str());
        *names_out = names.data();
        return static_cast<int>(names.size());
    } catch (...) {
        return 0;
    }
}

int vp_shared_instance_exts(const char*** names_out) {
    try {
        if (!names_out) return 0;
        auto ctx = backend::sharedGpuContextExtended();
        if (!ctx) return 0;
        static std::vector<const char*> names;
        if (names.empty())
            for (const auto& e : ctx->instance.enabledExtensions())
                names.push_back(e.c_str());
        *names_out = names.data();
        return static_cast<int>(names.size());
    } catch (...) {
        return 0;
    }
}

int vp_render_shm(VpFigure* fig, VpShmFrame* out) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || !out) return 0;
        auto* hb = dynamic_cast<backend::HeadlessBackend*>(
            fig->backend.get());
        if (!hb) return 0;
        // Enable the shm copy barrier + consumer-release gating.
        hb->setShmMode(true);
        fig->renderer->prepare(*fig->figure);
        fig->renderer->renderFrame(*fig->figure);
        const int slot = hb->lastSubmittedSlot();
        if (slot < 0) return 0;
        out->buffer = reinterpret_cast<uint64_t>(
            static_cast<VkBuffer>(hb->stagingBuffer(
                static_cast<uint32_t>(slot))));
        out->slot = static_cast<uint32_t>(slot);
        out->generation = hb->stagingGeneration(
            static_cast<uint32_t>(slot));
        out->row_pitch = static_cast<uint64_t>(hb->rowPitch());
        out->width = static_cast<uint32_t>(fig->w);
        out->height = static_cast<uint32_t>(fig->h);
        out->byte_size = out->row_pitch * out->height;
        return 1;
    } catch (...) {
        return 0;
    }
}

void vp_shm_release(VpFigure* fig, uint32_t slot) {
    try {
        if (!fig) return;
        auto* hb = dynamic_cast<backend::HeadlessBackend*>(
            fig->backend.get());
        if (hb) hb->shmRelease(slot);
    } catch (...) {
    }
}

int vp_figure_resize(VpFigure* fig, int width, int height) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || width <= 0 || height <= 0) return 0;
        if (width == fig->w && height == fig->h) return 1;
        return init(fig, width, height) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

uint64_t vp_trace_add(VpFigure* fig) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return 0;
        // Skip ids already registered — vp_trace_set auto-registers
        // unknown ids, which may be numerically ahead of next_id.
        uint64_t id;
        do { id = fig->next_id++; } while (fig->trace_data.count(id));
        fig->trace_data[id] = VpFigure::TraceData{};
        auto lp = std::make_shared<plot::LinePlot>(plot::Series2D{});
        auto* raw = lp.get();
        fig->axes->addPlot(std::move(lp));
        fig->traces[id] = raw;
        return id;
    } catch (...) {
        return 0;
    }
}

void vp_trace_set(VpFigure* fig, uint64_t id, const double* xy, size_t n,
                  uint32_t rgba8, float width, int style) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        // Auto-register unknown ids instead of silently dropping the
        // upload — a stale id after a figure rebuild used to no-op
        // here while vp_trace_slide reported failure forever, leaving
        // the scope permanently blank.
        auto& td = fig->trace_data[id];
        td.rgba8 = rgba8;
        td.width = width;
        td.style = style;

        auto it = fig->traces.find(id);
        // An empty LinePlot crashes renderFrame — drop it until data
        // arrives again.
        if (n == 0) {
            td.xy.clear();
            if (it != fig->traces.end()) {
                fig->axes->removePlot(it->second);
                fig->traces.erase(it);
            }
            return;
        }
        if (it == fig->traces.end()) {
            auto lp2 = std::make_shared<plot::LinePlot>(plot::Series2D{});
            auto* raw = lp2.get();
            fig->axes->addPlot(std::move(lp2));
            it = fig->traces.emplace(id, raw).first;
        }
        auto* lp = it->second;
        // One memcpy into the plot's recycled f64 buffer — the f64→f32
        // narrowing runs on the GPU, so there's no host-side
        // de-interleave + re-interleave pass at all.
        lp->setDataF64(std::span<const double>{xy, 2 * n});
        apply_trace_style(td, lp->series());
    } catch (...) {}
}

int vp_trace_slide(VpFigure* fig, uint64_t id, uint32_t drop,
                   const double* tail, size_t tail_n, uint32_t rgba8,
                   float width, int style) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return 0;
        auto td_it = fig->trace_data.find(id);
        if (td_it == fig->trace_data.end()) return 0;
        auto it = fig->traces.find(id);
        // The plot is gone (e.g. removed by an empty upload) — the
        // caller must retry with a full vp_trace_set; reporting
        // success here would silently lose the trace forever.
        if (it == fig->traces.end()) return 0;
        auto& td = td_it->second;
        td.rgba8 = rgba8;
        td.width = width;
        td.style = style;
        auto* lp = it->second;
        // tail_n == 0 is legal (look-only change) — slideDataF64 still
        // applies the eviction; the upload falls back to a full set.
        lp->slideDataF64(
            drop, std::span<const double>{tail, 2 * tail_n});
        apply_trace_style(td, lp->series());
        return 1;
    } catch (...) {
        return 0;
    }
}

void vp_trace_set_marker(VpFigure* fig, uint64_t id, int32_t marker,
                         float size) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        auto td_it = fig->trace_data.find(id);
        if (td_it == fig->trace_data.end()) return;
        auto& td = td_it->second;
        td.marker = marker;
        if (size > 0.0f) td.marker_size = size;
        auto it = fig->traces.find(id);
        if (it != fig->traces.end()) {
            apply_trace_style(td, it->second->series());
        }
    } catch (...) {}
}

void vp_trace_set_xy(VpFigure* fig, uint64_t id, int32_t on) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        auto td_it = fig->trace_data.find(id);
        if (td_it == fig->trace_data.end()) return;
        td_it->second.xy_mode = on ? 1 : 0;
        auto it = fig->traces.find(id);
        if (it != fig->traces.end()) {
            it->second->series().envelopeDecimate = !on;
        }
    } catch (...) {}
}

void vp_trace_remove(VpFigure* fig, uint64_t id) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        auto it = fig->traces.find(id);
        if (it != fig->traces.end()) {
            fig->axes->removePlot(it->second);
            fig->traces.erase(it);
        }
        fig->trace_data.erase(id);
    } catch (...) {}
}

void vp_set_viewport(VpFigure* fig, double xmin, double xmax, double ymin,
                     double ymax) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || !fig->axes) return;
        if (!(std::isnan(xmin) || std::isnan(xmax)))
            fig->axes->setXlim(static_cast<float>(xmin), static_cast<float>(xmax));
        if (!(std::isnan(ymin) || std::isnan(ymax)))
            fig->axes->setYlim(static_cast<float>(ymin), static_cast<float>(ymax));
    } catch (...) {}
}

void vp_set_margins(VpFigure* f, float left, float right, float bottom, float top) {
    std::lock_guard gate(renderGate());
    try {
        if (!f) return;
        f->margin_l = left; f->margin_r = right;
        f->margin_b = bottom; f->margin_t = top;
        if (f->figure) {
            f->figure->grid().left = left;
            f->figure->grid().right = right;
            f->figure->grid().bottom = bottom;
            f->figure->grid().top = top;
        }
    } catch (...) {}
}

void vp_set_background_rgba(VpFigure* fig, float r, float g, float b) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        fig->custom_bg = plot::Color::fromRgba8(
            int(r * 255), int(g * 255), int(b * 255), 255);
        fig->has_custom_bg = true;
        if (fig->figure) {
            fig->figure->style().faceColor = fig->custom_bg;
            fig->figure->style().edgeColor = fig->custom_bg;
        }
        if (fig->axes) fig->axes->style().faceColor = fig->custom_bg;
        // The default clear is WHITE — any rows the figure patch
        // misses (driver quirk, missed rasterization) read back as a
        // bright band. Clearing to the panel color makes stray
        // unpainted rows invisible instead.
        if (fig->backend) fig->backend->setClearColor(r, g, b, 1.0f);
    } catch (...) {}
}

void vp_set_axis_labels(VpFigure* fig, const char* x_label,
                        const char* y_label) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        if (x_label) fig->x_label = x_label;
        if (y_label) fig->y_label = y_label;
        if (fig->axes) {
            if (x_label) fig->axes->style().xAxis.label = x_label;
            if (y_label) fig->axes->style().yAxis.label = y_label;
        }
    } catch (...) {}
}

void vp_set_ticks(VpFigure* fig, int axis, int count,
                  const float* positions, const char* const* labels) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || axis < 0 || axis > 1) return;
        if (count <= 0 || !positions) {
            fig->ticks[axis].reset();
        } else {
            VpFigure::TickOverride t;
            t.positions.assign(positions, positions + count);
            t.labels.reserve(count);
            for (int i = 0; i < count; ++i)
                t.labels.emplace_back(labels && labels[i] ? labels[i] : "");
            fig->ticks[axis] = std::move(t);
        }
        if (fig->axes) {
            auto& tc = axis == 0 ? fig->axes->style().xAxis.ticks
                                 : fig->axes->style().yAxis.ticks;
            if (fig->ticks[axis]) {
                tc.positions = fig->ticks[axis]->positions;
                tc.labels = fig->ticks[axis]->labels;
            } else {
                tc.positions.reset();
                tc.labels.reset();
            }
        }
    } catch (...) {}
}

void vp_set_grid(VpFigure* fig, int on) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        fig->grid_on = on != 0;
        if (fig->axes) {
            fig->axes->style().xAxis.grid = fig->grid_on;
            fig->axes->style().yAxis.grid = fig->grid_on;
        }
    } catch (...) {}
}

void vp_set_minor_grid(VpFigure* fig, int axis, int count,
                       const float* positions, const float* strengths) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || axis < 0 || axis > 1) return;
        auto& g = fig->minor_grid[axis];
        if (count > 0 && positions) {
            g.positions.assign(positions, positions + count);
            if (strengths) {
                g.strengths.assign(strengths, strengths + count);
            } else {
                g.strengths.assign(count, 1.0f);
            }
        } else {
            g.positions.clear();
            g.strengths.clear();
        }
        if (fig->axes) {
            apply_minor_grid(g, axis == 0 ? fig->axes->style().xAxis
                                        : fig->axes->style().yAxis);
        }
    } catch (...) {}
}

void vp_set_background(VpFigure* fig, int white) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig) return;
        fig->white_bg = white != 0;
        if (fig->axes) {
            auto st = white ? plot::styles::defaultStyle()
                            : plot::styles::darkBackground();
            st.xAxis.grid = fig->grid_on;
            st.yAxis.grid = fig->grid_on;
            auto gc = white ? plot::Color::fromRgba8(200, 200, 200, 110)
                            : plot::Color::fromRgba8(80, 80, 80, 110);
            st.xAxis.gridColor = gc;
            st.yAxis.gridColor = gc;
            st.xAxis.gridLineWidth = 0.6f;
            st.yAxis.gridLineWidth = 0.6f;
            // egui parity: no unlabeled minor stubs between the
            // labeled (override) tick positions.
            st.xAxis.ticks.minor = false;
            st.yAxis.ticks.minor = false;
            if (fig->figure) fig->figure->style() = st;
            fig->axes->setStyle(st);
            // egui parity: its plot frame stroke is a subtle border —
            // the mpl styles' solid spine color reads as a bright bar
            // across the top of each scope (esp. dark mode's white).
            const auto spineGray = plot::Color::fromRgba8(
                white ? 160 : 72, white ? 160 : 72, white ? 160 : 72, 255);
            for (const char* side : {"left", "right", "bottom", "top"})
                fig->axes->spine(side).color = spineGray;
            // setStyle wipes the labels — re-apply them.
            if (!fig->x_label.empty())
                fig->axes->style().xAxis.label = fig->x_label;
            if (!fig->y_label.empty())
                fig->axes->style().yAxis.label = fig->y_label;
            // … and the explicit minor-grid override.
            apply_minor_grid(fig->minor_grid[0], fig->axes->style().xAxis);
            apply_minor_grid(fig->minor_grid[1], fig->axes->style().yAxis);
        }
    } catch (...) {}
}

size_t vp_render_rgba(VpFigure* fig, uint8_t* out, size_t out_len) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || !out) return 0;
        size_t need = static_cast<size_t>(fig->w) * fig->h * 4;
        if (out_len < need) return 0;
        // CPU-readback renders don't produce a shm frame — without
        // this they'd still bump shmRenders_ and permanently lag the
        // consumer-release counters behind.
        if (auto* hb = dynamic_cast<backend::HeadlessBackend*>(
                fig->backend.get()))
            hb->setShmMode(false);
        fig->renderer->prepare(*fig->figure);
        fig->renderer->renderFrame(*fig->figure);
        return fig->backend->readbackRgba8Into(out, need) == need ? need : 0;
    } catch (...) {
        return 0;
    }
}

const uint8_t* vp_render_rgba_view(VpFigure* fig, size_t* out_len) {
    std::lock_guard gate(renderGate());
    try {
        if (!fig || !out_len) return nullptr;
        *out_len = 0;
        const bool dbgPerf = [] {
            static const bool on = std::getenv("VP_DEBUG_PERF") != nullptr;
            return on;
        }();
        if (auto* hb = dynamic_cast<backend::HeadlessBackend*>(
                fig->backend.get()))
            hb->setShmMode(false);
        auto t0 = std::chrono::steady_clock::now();
        fig->renderer->prepare(*fig->figure);
        auto t1 = std::chrono::steady_clock::now();
        fig->renderer->renderFrame(*fig->figure);
        auto t2 = std::chrono::steady_clock::now();
        const uint8_t* v = fig->backend->rgba8View(out_len);
        if (!v) {
            // Pitched staging rows → no contiguous view; un-pitch into
            // the figure scratch and borrow that instead.
            auto& px = fig->rgba_view_scratch;
            px.resize(fig->w > 0 && fig->h > 0 ? size_t(fig->w) * fig->h * 4 : 0);
            const size_t got = fig->backend->readbackRgba8Into(px.data(), px.size());
            px.resize(got);
            if (got > 0) { v = px.data(); *out_len = got; }
        }
        auto t3 = std::chrono::steady_clock::now();
        if (dbgPerf) {
            static int n = 0;
            static double s0 = 0, s1 = 0, s2 = 0;
            s0 += std::chrono::duration<double, std::milli>(t1 - t0).count();
            s1 += std::chrono::duration<double, std::milli>(t2 - t1).count();
            s2 += std::chrono::duration<double, std::milli>(t3 - t2).count();
            if (++n % 120 == 0)
                std::fprintf(stderr,
                             "perf: prepare=%.2fms render+wait=%.2fms readback=%.2fms\n",
                             s0 / n, s1 / n, s2 / n);
        }
        return v;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "volcano: render failed: %s\n", e.what());
        return nullptr;
    } catch (...) {
        std::fprintf(stderr, "volcano: render failed (unknown)\n");
        return nullptr;
    }
}

void vp_figure_set_deferred_present(VpFigure* fig, int on) {
    std::lock_guard gate(renderGate());
    if (fig && fig->backend) fig->backend->setDeferredReadback(on != 0);
}

int vp_figure_width(const VpFigure* fig) { return fig ? fig->w : 0; }
int vp_figure_height(const VpFigure* fig) { return fig ? fig->h : 0; }

int vp_debug_xlim(const VpFigure* fig, float out_xlim[2],
                  float out_ylim[2]) {
    std::lock_guard gate(renderGate());
    if (!fig || !fig->axes || !out_xlim || !out_ylim) return 0;
    out_xlim[0] = fig->axes->xlim().min;
    out_xlim[1] = fig->axes->xlim().max;
    out_ylim[0] = fig->axes->ylim().min;
    out_ylim[1] = fig->axes->ylim().max;
    return 1;
}

int vp_debug_axes_rect(const VpFigure* fig, float out_rect[4]) {
    std::lock_guard gate(renderGate());
    if (!fig || !fig->axes || !out_rect) return 0;
    const auto& r = fig->axes->rect;
    out_rect[0] = float(r.x);
    out_rect[1] = float(r.y);
    out_rect[2] = float(r.x + r.width);
    out_rect[3] = float(r.y + r.height);
    return 1;
}

} // extern "C"
