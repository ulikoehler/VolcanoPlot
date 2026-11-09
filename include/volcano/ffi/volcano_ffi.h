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

/// Grid lines on both axes: 0 = off, 1 = on (default).
void vp_set_grid(VpFigure* fig, int on);

/// Background: 0 = dark (default mpl), 1 = white.
void vp_set_background(VpFigure* fig, int white);
/// Set the canvas+axes background to an exact color (egui panel_fill).
void vp_set_background_rgba(VpFigure* fig, float r, float g, float b);

/// Render one frame into `out` (must be width*height*4 bytes).
/// Returns width*height*4 on success, 0 on failure.
size_t vp_render_rgba(VpFigure* fig, uint8_t* out, size_t out_len);

/// Current size after create/resize.
int vp_figure_width(const VpFigure* fig);
int vp_figure_height(const VpFigure* fig);

#ifdef __cplusplus
}
#endif
