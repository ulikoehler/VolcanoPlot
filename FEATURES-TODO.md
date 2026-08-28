# FEATURES-TODO.md — Matplotlib Feature Parity Tracking

This file tracks VolcanoPlot's progress toward matplotlib feature parity.
Status legend: `[ ]` not started · `[-]` in progress · `[x]` done · `[~]` won't fix

## 1. Plot Types

### 1.1 2D — Pairwise / functional
- [x] `plot` — line and/or marker plots (LinePlot)
- [ ] `errorbar` — points/lines with x/y error bars
- [x] `scatter` — scatter with size/color mapping (ScatterPlot)
- [ ] `step` — step plots (`where='pre'|'mid'|'post'`)
- [ ] `loglog`, `semilogx`, `semilogy` (log scale supported via Axes::setLogX/Y)
- [ ] `fill` — filled polygons
- [ ] `fill_between`, `fill_betweenx`
- [ ] `vlines`, `hlines`
- [ ] `axhline`, `axvline`, `axhspan`, `axvspan`
- [ ] `broken_barh`

### 1.2 2D — Categorical / proportional
- [x] `bar`, `barh` (BarPlot — stub pipeline)
- [ ] `bar_label`
- [ ] `grouped_bar`
- [ ] `stackplot`
- [x] `pie`, `pie_label` (PiePlot — stub pipeline, 2D + donut)
- [ ] `hist` (1D and 2D `hist2d`)
- [ ] `stairs`
- [ ] `ecdf`

### 1.3 2D — Distributions / statistical
- [ ] `boxplot`
- [ ] `violinplot`
- [ ] `hexbin`
- [x] `hist`, `hist2d` (via KDEPlot — GPU-side KDE)
- [x] `volcano` (genomics — VolcanoPlot)

### 1.4 2D — Arrays / images
- [x] `imshow` (HeatmapPlot — stub)
- [ ] `matshow`
- [ ] `pcolor`, `pcolormesh`
- [ ] `pcolorfast`
- [ ] `spy`
- [ ] `figimage`

### 1.5 2D — Contours / triangulations
- [ ] `contour`, `contourf`
- [ ] `tricontour`, `tricontourf`
- [ ] `tripcolor`
- [ ] `triplot`
- [ ] `streamplot`
- [ ] `quiver`
- [ ] `barbs`

### 1.6 Signal / spectral (1-D)
- [ ] `xcorr`, `acorr`
- [ ] `psd` (power spectral density)
- [ ] `csd` (cross-spectral density)
- [ ] `cohere`
- [ ] `specgram`
- [ ] `magnitude_spectrum`
- [ ] `phase_spectrum`
- [ ] `angle_spectrum`

### 1.7 3D — `mpl_toolkits.mplot3d`
- [ ] `plot` / `plot3D`
- [ ] `scatter` / `scatter3D`
- [ ] `bar` (2D bars in 3D), `bar3d`
- [ ] `stem`
- [ ] `errorbar`
- [x] `plot_surface` (SurfacePlot — stub)
- [ ] `plot_wireframe`
- [ ] `plot_trisurf`
- [ ] `contour`, `contourf`
- [ ] `tricontour`, `tricontourf`
- [ ] `quiver`
- [ ] `voxels`
- [ ] 3D text (`Text3D`)
- [ ] 3D collections (`Line3DCollection`, `Poly3DCollection`, etc.)

### 1.8 VolcanoPlot-specific (GPU-native)
- [x] GPU-side function evaluation (FunctionPlot — CPU fallback, GPU compute TODO)
- [x] GPU-side KDE (KDEPlot — CPU fallback, GPU compute TODO)
- [ ] GPU autoscale (parallel min/max reduce)
- [x] Infinite zoom (FunctionPlot::reevaluate — framework in place)
- [ ] f32 phase decomposition for deep-zoom chirp plots
- [ ] 3D Mexican hat wavelet plot
- [ ] 3D dynamic grid (fwidth-based)
- [ ] Navigation cube/triad (3D)

---

## 2. Style System
- [ ] `matplotlib.rcParams` global runtime config
- [ ] `matplotlib.rc_context` temporary rc context manager
- [ ] `matplotlib.rcdefaults()`, `matplotlib.rc()`
- [ ] `matplotlibrc` file support
- [ ] Style sheets (`*.mplstyle`)
- [ ] `plt.style.use()`, `plt.style.context()`, `plt.style.available`
- [x] Built-in styles: `default`, `ggplot`, `seaborn`, `dark_background`, `grayscale`
- [ ] Built-in styles: `classic`, `fast`, `bmh`, `fivethirtyeight`, `Solarize_Light2`
- [ ] Seaborn variants: `seaborn-v0_8-*` (bright, colorblind, dark, darkgrid, deep, muted, ...)
- [ ] `tableau-colorblind10`, `petroff6`, `petroff8`, `petroff10`
- [ ] Composable style lists
- [ ] XKCD sketch style (`plt.xkcd()` context manager)

---

## 3. Colormaps

### 3.1 Perceptually Uniform Sequential
- [x] `viridis`, `plasma`, `inferno`, `magma`, `cividis`

### 3.2 Sequential
- [ ] `Greys`, `Purples`, `Blues`, `Greens`, `Oranges`, `Reds`
- [ ] `YlOrBr`, `YlOrRd`, `OrRd`, `PuRd`, `RdPu`, `BuPu`
- [ ] `GnBu`, `PuBu`, `YlGnBu`, `PuBuGn`, `BuGn`, `YlGn`
- [ ] `gray`, `bone`, `pink`, `spring`, `summer`, `autumn`, `winter`, `cool`, `Wistia`
- [ ] `hot`, `afmhot`, `gist_heat`, `copper`

### 3.3 Diverging
- [x] `coolwarm`, `RdBu`, `seismic`
- [ ] `PiYG`, `PRGn`, `BrBG`, `PuOr`, `RdGy`
- [ ] `RdYlBu`, `RdYlGn`, `Spectral`, `bwr`
- [ ] `berlin`, `managua`, `vanimo`

### 3.4 Cyclic
- [ ] `twilight`, `twilight_shifted`, `hsv`

### 3.5 Qualitative
- [ ] `Pastel1`, `Pastel2`, `Paired`, `Accent`
- [ ] `okabe_ito`, `Dark2`, `Set1`, `Set2`, `Set3`
- [ ] `tab10`, `tab20`, `tab20b`, `tab20c`

### 3.6 Miscellaneous
- [x] `turbo`, `jet`
- [ ] `flag`, `prism`, `ocean`, `gist_earth`, `terrain`, `gist_stern`
- [ ] `gnuplot`, `gnuplot2`, `CMRmap`, `cubehelix`, `brg`
- [ ] `gist_rainbow`, `rainbow`, `nipy_spectral`, `gist_ncar`

### 3.7 Colormap API
- [ ] `LinearSegmentedColormap`
- [ ] `ListedColormap`
- [ ] Reversed colormaps (`name + '_r'`)
- [ ] `Colormap` `bad`, `under`, `over` colors

---

## 4. Axes and Figure Features
- [x] `plt.figure` / `Figure`
- [x] `plt.subplots`, `plt.subplot` (Figure(rows, cols) + addAxes)
- [ ] `subplot_mosaic`
- [ ] `subplot2grid`
- [ ] `GridSpec`, `SubplotSpec`, `GridSpecFromSubplotSpec`
- [ ] `subfigures`
- [ ] `sharedx`, `sharedy`
- [ ] `twinx`, `twiny`
- [ ] `secondary_xaxis`, `secondary_yaxis`
- [ ] `inset_axes` (`mpl_toolkits.axes_grid1.inset_locator`)
- [ ] `make_axes_locatable`
- [ ] `constrained_layout`, `tight_layout`, `subplots_adjust`
- [ ] `colorbar` (inset and standalone)
- [ ] Projections: `rectilinear`, `polar`, `aitoff`, `hammer`, `lambert`, `mollweide`, `3d`
- [ ] Scales: `linear`, `log`, `symlog`, `logit`, `asinh`, `function`, `functionlog`, `mercator`
- [ ] Polar: `set_rgrids`, `set_thetagrids`, `set_theta_offset`, `set_theta_direction`
- [ ] Aspect ratio, equal axis, invert axis, set limits, autoscale (autoscale: [x])

---

## 5. Text and Annotations
- [ ] `title`, `suptitle`, `figtext`, `xlabel`, `ylabel` (data model: [x], rendering: [ ])
- [ ] `text`, `figtext`, `annotate`
- [ ] `tick_params`, `set_xticklabels`, `set_yticklabels`
- [ ] `Annotation` with `arrowprops` / `FancyArrowPatch`
- [ ] Coordinate systems: `data`, `axes`, `figure`, `display`, `offset points`
- [ ] MathText (TeX-like subset): sub/sup, fractions, radicals, Greek, accents, calligraphic, etc.
- [ ] MathText fontsets: `dejavusans` (default), `dejavuserif`, `cm`, `stix`, `stixsans`
- [ ] `text.usetex` full LaTeX rendering (requires external TeX)
- [x] Font properties: family, weight, style, size, color (FontProperties struct)
- [ ] Font properties: rotation, alignment

---

## 6. Markers and Line Styles

### 6.1 Markers
- [x] `'.'` point, `'o'` circle, `'s'` square, `'D' 'd'` diamond, `'^'` triangle, `'+'` plus, `'x'` X, `'*'` star (MarkerStyle enum)
- [ ] `'v' '<' '>'` triangles, `'1' '2' '3' '4'` tri arrows
- [ ] `'p'` pentagon, `'P'` plus filled
- [ ] `'h' 'H'` hexagons, `'X'`, `'|'` vline, `'_'` hline, `'8'` octagon
- [ ] TICK/CARET variants (`0`–`11`)
- [ ] TeX glyph markers (`'$...$'`)
- [ ] Custom `Path` markers and `(numsides, style, angle)` regular polygons
- [ ] Fill styles: `full`, `left`, `right`, `bottom`, `top`, `none`

### 6.2 Line styles
- [x] `'-'` / `solid`, `'--'` / `dashed`, `'-.'` / `dashdot`, `':'` / `dotted` (LineStyle enum)
- [ ] Custom dash tuple: `(offset, (on, off, on, off, ...))`
- [ ] `drawstyle`: `default`, `steps`, `steps-pre`, `steps-mid`, `steps-post`
- [ ] Cap styles: `butt`, `round`, `projecting`
- [ ] Join styles: `miter`, `round`, `bevel`
- [ ] `gapcolor` support

---

## 7. Color Handling
- [ ] Single-letter shorthands: `bgrcmykw`
- [ ] Named colors (X11/CSS4, `tab:...`, `C0`–`C9` cycle)
- [ ] Hex (`#RGB`, `#RRGGBB`, `#RGBA`, `#RRGGBBAA`)
- [x] RGB/RGBA tuples (0–1 float) (Color struct)
- [ ] Grayscale string (`'0.5'`)
- [ ] `CN` index colors (`C0`–`C9`)
- [ ] `xkcd:` color names
- [ ] Color cycles / `axes.prop_cycle`
- [ ] `cycler` library integration (color + linestyle + marker + linewidth cycling)
- [ ] Normalization:
  - [ ] `Normalize`, `NoNorm`
  - [ ] `LogNorm`, `PowerNorm`, `SymLogNorm`, `AsinhNorm`
  - [ ] `BoundaryNorm`, `CenteredNorm`, `TwoSlopeNorm`, `FuncNorm`, `MultiNorm`

---

## 8. Legend Features
- [x] `legend` with auto or explicit `handles`/`labels` (data model: LegendStyle struct)
- [x] Locations: `best`, `upper right`, `upper left`, `lower left`, `lower right`, `right`, `center left`, `center right`, `lower center`, `upper center`, `center` (location string field)
- [ ] `loc`, `bbox_to_anchor`, `bbox_transform`
- [ ] `ncols` / `ncol`, `nrows`
- [ ] `title`, `title_fontproperties`
- [x] `frameon`, `framealpha`, `facecolor`, `edgecolor`, `shadow`, `fancybox` (fields in LegendStyle)
- [ ] `labelcolor`
- [ ] `handlelength`, `handletextpad`, `borderpad`, `columnspacing`
- [ ] `draggable`
- [ ] `handler_map` / legend handlers

---

## 9. Grid and Ticks

### 9.1 Ticks
- [ ] Major and minor ticks (TickConfig::minor field)
- [x] `ax.grid` (major/minor, x/y/both, color, linestyle, linewidth) (AxisStyle fields + GridRenderer stub)
- [ ] `tick_params`

### 9.2 Locators
- [x] `AutoLocator`, `MaxNLocator` (TickConfig::nbins — framework only)
- [ ] `LinearLocator`, `MultipleLocator`, `FixedLocator`, `IndexLocator`
- [ ] `LogLocator`, `LogitLocator`, `AutoMinorLocator`
- [ ] `NullLocator`, `SymmetricalLogLocator`

### 9.3 Formatters
- [ ] `NullFormatter`, `FixedFormatter`
- [ ] `FuncFormatter`, `StrMethodFormatter`, `FormatStrFormatter`
- [ ] `ScalarFormatter`, `LogFormatter`, `LogFormatterExponent`, `LogFormatterMathtext`, `LogFormatterSciNotation`
- [ ] `LogitFormatter`, `EngFormatter`, `PercentFormatter`
- [ ] `ticklabel_format` (scilimits, useMathText, useOffset)

---

## 10. Savefig Formats

- [x] `png` (CPU via libpng; GPU encoder stub)
- [x] `webp` (CPU via libwebp)
- [x] `bmp` (CPU, no dependency)
- [x] `raw` (no encoding)
- [ ] `jpg` / `jpeg`
- [ ] `tiff` / `tif`
- [ ] `pdf` (vector)
- [ ] `svg`, `svgz` (vector)
- [ ] `eps`, `ps` (vector)
- [ ] `pgf` (LaTeX/PGF backend)
- [ ] Metadata support per format
- [ ] `transparent`, `dpi`, `bbox_inches`, `pad_inches`

---

## 11. Interactive Features
- [x] Pan, zoom (basic SDL3 event polling)
- [ ] Navigation toolbar: Home / Back / Forward
- [ ] Zoom (x/y constrained), zoom-to-rectangle
- [ ] Save figure button, configure subplots
- [ ] Cursor data readout
- [ ] Key bindings: `p` pan, `o` zoom, `h`/`r` home, `s` save, `g` grid, `l` y log, `k` x log, `f` fullscreen, `q` quit
- [ ] Event system: `button_press_event`, `motion_notify_event`, `key_press_event`, `pick_event`, `scroll_event`, etc.
- [ ] Widgets: Slider, RangeSlider, Button, CheckButtons, RadioButtons, TextBox, SpanSelector, RectangleSelector, EllipseSelector, LassoSelector, Lasso, PolygonSelector, Cursor, MultiCursor, SubplotTool

---

## 12. Animation
- [ ] `FuncAnimation`
- [ ] `ArtistAnimation`
- [ ] `TimedAnimation` base
- [ ] Blitting support for efficient updates
- [ ] Writers: `PillowWriter`, `FFMpegWriter`, `ImageMagickWriter`, `HTMLWriter`
- [ ] `animation.to_jshtml`, `animation.to_html5_video`

---

## 13. Transformations
- [x] `transData` (data → display) (Transform2D)
- [ ] `transAxes` (axes fraction)
- [ ] `transFigure` (figure fraction)
- [ ] `transDisplay`
- [ ] `CompositeGenericTransform`, `CompositeAffine2D`
- [ ] `BlendedAffine2D`, `BlendedGenericTransform`
- [ ] `blended_transform_factory`
- [ ] `offset_copy`
- [ ] `Affine2D` (scale, rotate, translate, skew)
- [ ] Custom `Transform` / `TransformNode` hierarchy
- [x] 3D camera (view + projection matrices) (Camera3D)

---

## 14. Path and Patch Collections
- [ ] `Path`, `PathPatch`
- [ ] `PatchCollection`
- [ ] `PathCollection`
- [ ] `LineCollection`
- [ ] `PolyCollection`
- [ ] `QuadMesh`
- [ ] `TriMesh`
- [ ] `CircleCollection`
- [ ] `RegularPolyCollection`, `AsteriskPolygonCollection`
- [ ] `Patch` primitives: `Circle`, `Ellipse`, `Rectangle`, `Polygon`, `Wedge`, `FancyBboxPatch`, `FancyArrowPatch`
- [ ] Hatch patterns
- [ ] `offsets` and `offset_transform` for instanced rendering

---

## 15. Specialized Plots / Extensions
- [ ] `Sankey` diagrams (`matplotlib.sankey.Sankey`)
- [ ] XKCD-style sketching (`plt.xkcd()`)
- [ ] Radar / spider charts via `polar` projection
- [ ] Treemaps (requires third-party `squarify` or similar)
- [ ] Word clouds (requires third-party `wordcloud`)
- [ ] Network/graph drawing (requires third-party `networkx`)
- [ ] `Table` / `ax.table` (tabular data overlays)
- [ ] `matshow` / `spy` matrix visualizations
- [ ] `broken_barh` gantt-like intervals

---

## 16. Additional Matplotlib Capabilities (cross-cutting)
- [ ] Colorbars with `extend` arrows and custom norms
- [ ] Spines / axis styling (hide individual spines, `spines.set_visible`)
- [ ] `zorder` compositing
- [ ] Picking / hit testing
- [ ] Rasterization (`rasterized=True`) for vector backends
- [ ] `clabel` for contour labels
- [ ] `hist` histogram types (bar, barstacked, step, stepfilled)
- [ ] `boxplot` notched, bootstrap, median/MU, cap/join styles
- [ ] `violinplot` with custom positions, widths, bodies
- [ ] `errorbar` continuous vs per-point error styles

---

## Priority Tiers

### Core (must-have for v0.1)
- Line plots with MSAA ✓ (pipeline done)
- Scatter plots with markers ✓ (pipeline done)
- Bar charts (stub — needs pipeline)
- Heatmap/imshow (stub — needs pipeline)
- PNG export (CPU ✓, GPU stub)
- ggplot style ✓
- Basic grid (stub — needs static VBO)
- Axis labels (data model ✓, rendering TODO)

### Advanced (v0.2+)
- GPU-side function evaluation (compute shader)
- GPU autoscale (parallel reduce)
- Infinite zoom interaction
- KDE on GPU
- 3D surface plot
- Volcano plot per-point coloring
- Text rendering (SDF atlas)
- Legend rendering

### Long-term
- Full matplotlib style sheet parser
- MathText/LaTeX rendering
- Vector export (PDF/SVG)
- Animation
- Interactive widgets
- All colormaps
- All plot types
