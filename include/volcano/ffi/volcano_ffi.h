// volcano/ffi/volcano_ffi.h — C ABI for embedding VolcanoPlot headless
// rendering into non-C++ hosts (e.g. liveplot-rs). All functions are
// thread-confined to the caller's render thread; one VpFigure per scope.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VpFigure VpFigure;

/// Create a headless figure surface. Returns NULL on failure.
VpFigure* vp_figure_create(int width, int height);
void vp_figure_destroy(VpFigure* fig);

/// Resize the offscreen surface (recreates the backend).
/// Returns 1 on success, 0 on failure.
int vp_figure_resize(VpFigure* fig, int width, int height);

/// Add a line trace; returns an opaque trace id (>=1), 0 on failure.
uint64_t vp_trace_add(VpFigure* fig);

/// Replace a trace's data + style. `xy` is interleaved x0,y0,x1,y1,...
/// `rgba8` is 0xRRGGBBAA. `style`: 0=solid, 1=dashed, 2=dotted,
/// 3=points-only.
void vp_trace_set(VpFigure* fig, uint64_t id, const double* xy, size_t n,
                  uint32_t rgba8, float width, int style);

/// Sliding-window update: evict `drop` points from the front of the
/// trace's data and append `tail`/`tail_n` (interleaved f64) at the
/// back, plus the same style args as vp_trace_set. Only the tail is
/// uploaded — O(new) instead of O(total). The caller MUST guarantee
/// the retained suffix is identical to the previous data (slide
/// detection is the caller's job); any other change needs
/// vp_trace_set.
/// Returns 1 when the slide was applied, 0 when it could not be (no
/// live plot for `id` — the caller must then send a full
/// vp_trace_set; a silent drop would lose the trace permanently).
int vp_trace_slide(VpFigure* fig, uint64_t id, uint32_t drop,
                   const double* tail, size_t tail_n, uint32_t rgba8,
                   float width, int style);

/// Marker glyph at each data point — independent of `style`, so any
/// line style can be combined with markers. `marker` is a
/// plot::MarkerStyle ordinal (0..41; 40 = None). `size` is the marker
/// size in pixels; <= 0 keeps the previous size.
void vp_trace_set_marker(VpFigure* fig, uint64_t id, int32_t marker,
                         float size);

void vp_trace_remove(VpFigure* fig, uint64_t id);

/// Explicit viewport (data coords). Any axis may be left at autoscale by
/// passing NAN for both bounds.
void vp_set_viewport(VpFigure* fig, double xmin, double xmax,
                     double ymin, double ymax);

/// Axis labels (NULL keeps current).
void vp_set_axis_labels(VpFigure* fig, const char* x_label,
                        const char* y_label);

/// Explicit major-tick positions (data coords) and UTF-8 labels.
/// `axis`: 0 = x, 1 = y. `count <= 0` or NULL positions restores the
/// automatic locator/formatter. Labels must be a parallel array of
/// `count` C strings (a NULL entry renders an unlabeled tick).
void vp_set_ticks(VpFigure* fig, int axis, int count,
                  const float* positions, const char* const* labels);

/// Grid lines on both axes: 0 = off, 1 = on (default).
void vp_set_grid(VpFigure* fig, int on);

/// Background: 0 = dark (default mpl), 1 = white.
void vp_set_background(VpFigure* fig, int white);
/// Set the canvas+axes background to an exact color (egui panel_fill).
void vp_set_background_rgba(VpFigure* fig, float r, float g, float b);

/// Render one frame into `out` (must be width*height*4 bytes).
/// Returns width*height*4 on success, 0 on failure.
size_t vp_render_rgba(VpFigure* fig, uint8_t* out, size_t out_len);

/// Render one frame and return a borrowed pointer into the backend's
/// readback staging buffer — skips the staging→caller memcpy. The
/// pointer stays valid until the next render/readback on `fig`.
/// Returns nullptr (and *out_len = 0) on failure or when the backend
/// has no view support; fall back to vp_render_rgba.
const uint8_t* vp_render_rgba_view(VpFigure* fig, size_t* out_len);

/// Deferred readback (default off): renderFrame submits without
/// blocking, and vp_render_rgba* returns the newest *completed* frame
/// — frame N-1 while N is still on the GPU. One frame of present
/// latency in exchange for never stalling the caller on the fence.
/// The first frame renders synchronously so callers always get a
/// valid image.
void vp_figure_set_deferred_present(VpFigure* fig, int on);

/// Current size after create/resize.
int vp_figure_width(const VpFigure* fig);
int vp_figure_height(const VpFigure* fig);

#ifdef __cplusplus
}
#endif
