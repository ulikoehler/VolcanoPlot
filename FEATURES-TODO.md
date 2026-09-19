# FEATURES-TODO.md — Matplotlib Feature Parity Tracking

This file tracks VolcanoPlot's progress toward matplotlib feature parity.
Status legend: `[ ]` not started · `[-]` in progress · `[x]` done · `[~]` won't fix

## 1. Plot Types

### 1.1 2D — Pairwise / functional
- [x] `plot` — line and/or marker plots (LinePlot)
- [x] `errorbar` — points/lines with x/y error bars (ErrorbarPlot — symmetric
      and asymmetric errors, caps, optional connecting line and markers)
- [x] `scatter` — scatter with size/color mapping (ScatterPlot)
- [x] `step` — step plots (StepPlot — `where='pre'|'mid'|'post'`, staircase
      expansion of x/y data via LineRenderer)
- [x] `loglog`, `semilogx`, `semilogy` (Axes::loglog/semilogx/semilogy
      convenience methods — set log scale on both/x/y axes; GPU vertex
      shader applies log10 transform; tested with exponential, power-law,
      and small-value data)
- [x] `fill` — filled polygons (FillPlot — triangle-fan tessellation)
- [x] `fill_between`, `fill_betweenx` (FillBetweenPlot — triangle-strip between
      two curves or curve and baseline, with alpha blending)
- [x] `vlines`, `hlines` (Vlines/Hlines — vertical/horizontal line segment
      collections via LineSegmentRenderer with eLineList topology)
- [x] `axhline`, `axvline`, `axhspan`, `axvspan` (ReferenceLines —
      axis-spanning lines drawn via SpineRenderer pixel-space line strips
      to avoid GPU guard-band clipping; spans use FillRenderer with
      kAxisSpan triangles that clip correctly at viewport boundaries)
- [x] `broken_barh` (BrokenBarHPlot — collection of horizontal rectangles
      at various y positions, per-segment colors, Gantt chart support,
      overlapping bars, negative x values, FillRenderer triangle tessellation)

### 1.2 2D — Categorical / proportional
- [x] `bar`, `barh` (BarPlot — stub pipeline)
- [x] `bar_label` (BarLabelPlot — value labels on bars with Edge/Center
      position, auto-generated or custom labels, format strings (%d, %.1f,
      %.2f, %.1g), horizontal/vertical mode, negative height support,
      TextRenderer pixel-space rendering)
- [x] `grouped_bar` (GroupedBarPlot — side-by-side bar chart with multiple
      series per group, configurable bar width, per-series colors from
      tab10 palette, horizontal/vertical orientation, negative height
      support, FillRenderer triangle tessellation)
- [x] `stackplot` (StackPlot — stacked area plot with cumulative series,
      per-series colors, FillRenderer triangle tessellation)
- [x] `pie`, `pie_label` (PiePlot — stub pipeline, 2D + donut)
- [x] `hist` (HistPlot — auto/fixed/FD/Sturges/Rice/Square bins, count/density/
      probability/cumulative normalization, horizontal mode, alpha blending)
- [x] `hist2d` (Hist2DPlot — 2D histogram with auto/fixed/explicit bins,
      count/density/probability normalization, colormap coloring via
      FillRenderer, empty bin skipping)
- [x] `stairs` (StairsPlot — step function from explicit bin edges and
      values, optional fill, LineRenderer + FillRenderer)
- [x] `ecdf` (ECDFPlot — empirical CDF step function, complementary CDF
      (survival function) mode, optional fill, repeated value handling,
      LineRenderer + FillRenderer)

### 1.3 2D — Distributions / statistical
- [x] `boxplot` (BoxPlot — quartiles, whiskers, outliers, multiple groups,
      IQR/MinMax/Percentile whisker types, configurable colors and fill)
- [x] `violinplot` (ViolinPlot — CPU KDE with Gaussian kernel and Silverman
      bandwidth, mirrored body via FillRenderer, optional inner box/whisker/median)
- [x] `hexbin` (HexbinPlot — hexagonal binning with pointy-top/flat-top
      orientation, axial coordinate binning, count/density normalization,
      minCount filtering, colormap coloring via FillRenderer fan
      triangulation)
- [x] `hist` (via HistPlot — CPU binning + FillRenderer), `hist2d` (via KDEPlot — GPU-side KDE)
- [x] `volcano` (genomics — VolcanoPlot)

### 1.4 2D — Arrays / images
- [x] `imshow` (HeatmapPlot — GPU texture path with `interpolation=`:
      "nearest"/"none", "bilinear" (linear sampler), "bicubic"
      (Catmull-Rom texelFetch in shader), "antialiased"/"hanning"
      approximated by bilinear)
- [x] `matshow` (MatshowPlot — matrix display with row-0-at-top convention,
      colormap coloring, NaN cell skipping, explicit value range, nearest-
      neighbor display via FillRenderer triangle tessellation)
- [x] `pcolor`, `pcolormesh` (PcolormeshPlot — rectangular cells with per-cell
      colormap colors via FillRenderer, non-uniform cell edges, NaN skipping,
      explicit value range support, `shading="flat"/"gouraud"` — gouraud
      uses mpl's 4-triangle center-averaged quad split with per-corner
      colors interpolated by the rasterizer)
- [x] `pcolorfast` (PcolorfastPlot — fast pseudocolor with extent-based
      regular grid constructor and explicit edges constructor, colormap
      coloring, NaN skipping, explicit value range, FillRenderer tessellation)
- [x] `spy` (SpyPlot — sparsity pattern visualization with configurable
      non-zero/zero colors, precision threshold, marker size control,
      row-0-at-top convention, FillRenderer triangle tessellation)
- [x] `figimage` (FigImagePlot — figure-level RGBA8 image overlay in pixel
      space, row-0-at-top convention, configurable position/scale, transparent
      pixel skipping, bypasses axes data coordinates, FillRenderer with
      inverted-Y pixel-space viewport transform)

### 1.5 2D — Contours / triangulations
- [x] `contour`, `contourf` (ContourPlot/ContourfPlot — CPU marching squares
      with saddle disambiguation; contour lines via LineSegmentRenderer,
      filled bands via FillRenderer with per-vertex colormap colors and
      Sutherland-Hodgman polygon clipping)
- [x] `tricontour`, `tricontourf` (TriContourPlot/TriContourfPlot — Delaunay
      triangulation via Bowyer-Watson, marching triangles for isoline
      extraction, Sutherland-Hodgman polygon clipping for filled bands,
      auto/explicit levels, colormap coloring for filled bands,
      LineSegmentRenderer/FillRenderer)
- [x] `tripcolor` (TripcolorPlot — pseudocolor on unstructured triangular grids,
      flat and gouraud shading modes, per-vertex or per-face values, Delaunay
      triangulation or explicit triangles, colormap coloring, NaN skipping,
      explicit value range, FillRenderer)
- [x] `triplot` (TriplotPlot — draws triangulation edges via LineSegmentRenderer,
      Delaunay triangulation or explicit triangles, optional vertex markers via
      PointRenderer, configurable color/width/marker settings)
- [x] `streamplot` (StreamPlot — RK4 streamline integration with bilinear
      field interpolation, seed point distribution with density control,
      streamline proximity deduplication, optional arrowheads via
      FillRenderer, LineSegmentRenderer for streamline segments)
- [x] `quiver` (QuiverPlot — 2D vector field with arrow shafts via
      LineSegmentRenderer and filled arrowheads via FillRenderer,
      auto-scaling, pixel-space arrowhead geometry)
- [x] `barbs` (BarbsPlot — wind barb symbols with shaft, flags (50 kt),
      full barbs (10 kt), half barbs (5 kt), meteorological convention
      (barbs point FROM wind direction), flip option, pixel-space rendering
      via LineSegmentRenderer, configurable color/width/length)

### 1.6 Signal / spectral (1-D)
- [x] `xcorr`, `acorr` (XCorrPlot — autocorrelation and cross-correlation
      with normalized/unnormalized modes, configurable max lags, stem plot
      rendering via LineSegmentRenderer + PointRenderer, symmetric
      autocorrelation, zero-lag peak for identical signals)
- [x] `psd` (PsdPlot — power spectral density via radix-2 FFT, |FFT(x)|^2
      normalized by sampleRate * windowPower, one-sided spectrum with DC/Nyquist
      correction, 10*log10 dB output, Hann/Hamming/Blackman/Rectangular windows,
      configurable nfft, LineRenderer)
- [x] `csd` (CsdPlot — cross-spectral density via radix-2 FFT,
      |FFT(x) * conj(FFT(y))| normalized by sampleRate * sqrt(windowPower_x * windowPower_y),
      one-sided spectrum with DC/Nyquist correction, 10*log10 dB output,
      Hann/Hamming/Blackman/Rectangular windows, configurable nfft, LineRenderer)
- [x] `cohere` (CoherePlot — magnitude-squared coherence Cxy = |Pxy|^2 / (Pxx * Pyy),
      auto/cross power spectral densities via radix-2 FFT, one-sided spectrum,
      output clamped to [0, 1], Hann/Hamming/Blackman/Rectangular windows,
      configurable nfft, LineRenderer)
- [x] `specgram` (SpecgramPlot — STFT spectrogram via sliding-window FFT,
      configurable nfft/noverlap, Hann/Hamming/Blackman/Rectangular windows,
      one-sided spectrum, 10*log10 dB magnitude, colormap coloring via
      FillRenderer, time/frequency axes, explicit value range, chirp support)
- [x] `magnitude_spectrum` (SpectrumPlot — radix-2 Cooley-Tukey FFT, linear/dB
      scale, windowing (Rectangular/Hann/Hamming/Blackman), one-sided spectrum,
      LineRenderer)
- [x] `phase_spectrum` (SpectrumPlot — phase = atan2(im, re) of FFT, wrapped
      to [-pi, pi], magnitude thresholding for noise suppression)
- [x] `angle_spectrum` (SpectrumPlot — same as phase spectrum, alias)

### 1.7 3D — `mpl_toolkits.mplot3d`
- [x] `plot` / `plot3D` (Plot3D — 3D line plot via CPU projection of Point3D
      through Camera3D view-projection matrix to NDC, rendered as line strip
      via LineRenderer with identity viewport, optional markers via
      PointRenderer, configurable color/width/markers, perspective divide)
- [x] `scatter` / `scatter3D` (Scatter3D — 3D scatter via CPU projection of
      Point3D through Camera3D view-projection matrix to NDC, rendered as
      markers via PointRenderer with identity viewport, per-point colors/sizes,
      configurable marker style/size/color, perspective divide)
- [x] `bar` (2D bars in 3D), `bar3d` (Bar3D — 3D rectangular bars via CPU
      projection of 8-corner boxes through Camera3D, 6 faces per bar rendered
      as filled triangles via FillRenderer with painter's algorithm depth
      sorting, per-face shading for simple lighting, optional edge outlines
      via LineSegmentRenderer, configurable color/edge color/edge width)
- [x] `stem` (StemPlot — vertical lines from baseline to data points with
      optional markers, configurable baseline, custom line/marker colors,
      LineSegmentRenderer + PointRenderer, auto x from index)
- [x] `errorbar` (Errorbar3D — 3D error bars along x/y/z axes via CPU
      projection through Camera3D, symmetric/asymmetric errors, caps as
      perpendicular line segments, markers via PointRenderer, error bars
      via LineSegmentRenderer, configurable colors/sizes/caps)
- [x] `plot_surface` (SurfacePlot — stub)
- [x] `plot_wireframe` (WireframePlot — 3D wireframe via CPU projection of
      Grid2D surface through Camera3D, row/column line segments connecting
      adjacent grid points, configurable row/col stride, rendered as
      independent line segments via LineSegmentRenderer, configurable
      color/width)
- [x] `plot_trisurf` (TrisurfPlot — 3D triangulated surface via Delaunay
      triangulation of (x,y) with z as height, triangles projected through
      Camera3D to NDC, rendered as filled polygons via FillRenderer with
      painter's algorithm depth sorting, per-triangle colormap coloring from
      average z, optional edge outlines via LineSegmentRenderer, explicit
      triangle support, configurable colormap/value range)
- [x] `contour`, `contourf` (Contour3D / Contourf3D — 3D contour lines and
      filled bands at a fixed z-level, marching squares isoline extraction
      from Grid2D, projected through Camera3D to NDC, contour lines via
      LineSegmentRenderer, filled bands via FillRenderer with colormap
      coloring, configurable levels/colormap/z-level, saddle-case handling)
- [x] `tricontour`, `tricontourf` (TricontourPlot / TricontourfPlot — 3D
      contour lines and filled bands on scattered data via Delaunay
      triangulation of (x,y) with z as scalar field, per-triangle edge
      crossing extraction for contour lines, Sutherland-Hodgman polygon
      clipping for filled bands, projected through Camera3D at fixed
      z-level, LineSegmentRenderer for lines, FillRenderer for bands,
      configurable levels/colormap/z-level)
- [x] `quiver` (Quiver3D — 3D vector field via CPU projection of arrow
      base/tip through Camera3D, shafts as line segments via
      LineSegmentRenderer, arrowheads as filled triangles via FillRenderer
      with screen-space perpendicular orientation, configurable scale/color/
      head size/filled heads, autoscale includes arrow tip positions)
- [x] `voxels` (VoxelsPlot — 3D voxel grid via CPU projection of unit cubes
      through Camera3D, 6 faces per voxel rendered as filled triangles via
      FillRenderer with painter's algorithm depth sorting, per-face shading
      for simple lighting, optional edge outlines via LineSegmentRenderer,
      per-voxel or uniform colors, 3D boolean array input)
- [x] 3D text (`Text3D` — 3D text annotations via CPU projection of 3D
      positions through Camera3D to 2D pixel coordinates, rendered using
      existing TextRenderer/glyb bitmap atlas, configurable color/rotation,
      multiple text items, behind-camera culling, autoscale includes text
      positions)
- [x] 3D collections (`Line3DCollection`, `Poly3DCollection` —
      `Line3DCollection` renders independent 3D line segments via CPU
      projection through Camera3D and LineSegmentRenderer, supports flat
      array or point-pair constructors, per-segment or uniform colors;
      `Poly3DCollection` renders 3D polygons with arbitrary vertex counts
      via fan triangulation, painter's algorithm depth sorting, FillRenderer
      for faces and LineSegmentRenderer for edges, per-polygon or uniform
      face colors, configurable face/edge rendering)

### 1.8 VolcanoPlot-specific (GPU-native)
- [x] GPU-side function evaluation (FunctionPlot — user GLSL bodies compiled
      to a compute shader at runtime via EvalRenderer; output vec2 buffer is
      bound directly to LineRenderer — results never leave the GPU; CPU
      fallback when shaderc is unavailable or the body fails to compile)
- [x] GPU-side KDE (KDEPlot — KdeEvalRenderer compute shader writes the
      density grid; CPU fallback kept, Gaussian math corrected)
- [x] GPU autoscale (parallel min/max reduce)
- [x] Infinite zoom (FunctionPlot re-evaluates on manual x-range changes;
      autoscale-driven viewports don't trigger resampling — no feedback loop)
- [x] f32 phase decomposition for deep-zoom chirp plots
      (`PhaseDecomposer` utility — splits phase into large f64-computed
      constant + small f32 delta, uses sin(a+b)=sin(a)cos(b)+cos(a)sin(b)
      identity for accurate oscillatory evaluation at high frequencies;
      `LinearChirp` struct with evaluate/evaluateDecomposed methods;
      `ChirpPlot` plot type with per-viewport re-evaluation centered at
      viewport center for maximum precision, configurable phase decomposition
      on/off, infinite zoom support via reevaluate())
- [x] 3D Mexican hat wavelet plot (MexicanHatPlot — evaluates the 2D Ricker
      wavelet psi(x,y) = (2 - r^2/sigma^2) * exp(-r^2/(2*sigma^2)) on a
      regular grid, renders as a 3D surface via CPU projection through
      Camera3D with painter's algorithm depth sorting, FillRenderer for
      colormap-colored faces, optional LineSegmentRenderer wireframe overlay
      with configurable stride, configurable sigma/colormap/grid resolution)
- [x] 3D dynamic grid (fwidth-based) (Grid3DRenderer — fullscreen-quad
      fragment shader ray-casts against floor/wall planes using inverse
      view-projection matrix, computes grid line distances in world space
      with fwidth-based screen-space derivatives for anti-aliasing, lines
      never quantize under zoom, configurable floor/back-wall/side-wall
      planes, auto or custom grid step, alpha-blended overlay)
- [x] Navigation cube/triad (3D) (NavCubePlot — axis triad or wireframe
      cube overlay drawn in a corner of the axes rect, projects axis
      directions through the Camera3D view rotation only, colored X/Y/Z
      arrows with optional labels and negative half-axes, depth-sorted
      drawing, SpineRenderer pixel-space lines + TextRenderer labels)

---

## 2. Style System
- [x] `matplotlib.rcParams` global runtime config (`rc::params()` — global
      FigureStyle snapshotted by new Axes/Figure, `rc::set(key, value)`
      applies matplotlib rcParam names via `applyRcParam`)
- [x] `matplotlib.rc_context` temporary rc context manager (`rc::Context`
      RAII guard, moveable, restores on scope exit)
- [x] `matplotlib.rcdefaults()`, `matplotlib.rc()` (`rc::rcdefaults()`,
      `rc::set`)
- [x] `matplotlibrc` file support (`rc::loadFile` — key:value parser with
      comments, colors, bools, cycler prop_cycle, named font sizes)
- [x] Style sheets (`*.mplstyle`) (`style::use(path)` / `rc::loadFile` —
      ~40 rcParam keys mapped onto FigureStyle fields)
- [x] `plt.style.use()`, `plt.style.context()`, `plt.style.available`
      (`style::use`/`style::context`/`style::available` in Rc.hpp)
- [x] Built-in styles: `default`, `ggplot`, `seaborn`, `dark_background`, `grayscale`
- [x] Built-in styles: `classic`, `fast`, `bmh`, `fivethirtyeight`, `Solarize_Light2`
- [x] Seaborn variants: `seaborn-v0_8-*` (bright, colorblind, dark, darkgrid, deep, muted, pastel, white, whitegrid, ticks)
- [x] Seaborn contexts: `seaborn-v0_8-paper`, `seaborn-v0_8-notebook`, `seaborn-v0_8-talk`, `seaborn-v0_8-poster`
- [x] `tableau-colorblind10`, `petroff6`, `petroff8`
- [x] `styles::byName()` lookup function for all built-in styles
- [x] Color cycle per style (`ColorCycleStyle` with `axes.prop_cycle` colors)
- [x] Line style defaults per style (`LineStyleDefaults`: linewidth, capstyle, joinstyle)
- [x] Patch style defaults per style (`PatchStyleDefaults`: facecolor, edgecolor, linewidth)
- [x] Tick direction per style (`in`, `out`, `inout`)
- [x] Legend frame on/off per style (`LegendStyle::frameOn`)
- [x] Text color per style (`FigureStyle::textColor`)
- [x] Axis below per style (`FigureStyle::axisBelow`)
- [x] `petroff10` (petroff10Style — official 10-color Petroff cycle)
- [x] Composable style lists (`style::use({name, path, ...})` — applied in
      order, .mplstyle files overlay params on top of builtin sheets)
- [x] XKCD sketch style (`plt.xkcd()` context manager) (xkcdStyle —
      rcParams portion: Comic Sans-ish font, 14pt, thicker spines; the
      hand-drawn path wobble is the §15 sketching feature)

---

## 3. Colormaps

### 3.1 Perceptually Uniform Sequential
- [x] `viridis`, `plasma`, `inferno`, `magma`, `cividis`

### 3.2 Sequential
- [x] `Greys`, `Purples`, `Blues`, `Greens`, `Oranges`, `Reds`
- [x] `YlOrBr`, `YlOrRd`, `OrRd`, `PuRd`, `RdPu`, `BuPu`
- [x] `GnBu`, `PuBu`, `YlGnBu`, `PuBuGn`, `BuGn`, `YlGn`
- [x] `gray`, `bone`, `pink`, `spring`, `summer`, `autumn`, `winter`, `cool`, `Wistia`
- [x] `hot`, `afmhot`, `gist_heat`, `copper`

### 3.3 Diverging
- [x] `coolwarm`, `RdBu`, `seismic`
- [x] `PiYG`, `PRGn`, `BrBG`, `PuOr`, `RdGy`
- [x] `RdYlBu`, `RdYlGn`, `Spectral`, `bwr`
- [x] `berlin`, `managua`, `vanimo`

### 3.4 Cyclic
- [x] `twilight`, `twilight_shifted`, `hsv`

### 3.5 Qualitative
- [x] `Pastel1`, `Pastel2`, `Paired`, `Accent`
- [x] `okabe_ito` (added in matplotlib 3.11)
- [x] `Dark2`, `Set1`, `Set2`, `Set3`
- [x] `tab10`, `tab20`, `tab20b`, `tab20c`

### 3.6 Miscellaneous
- [x] `turbo`, `jet`
- [x] `flag`, `prism`, `ocean`, `gist_earth`, `terrain`, `gist_stern`
- [x] `gnuplot`, `gnuplot2`, `CMRmap`, `cubehelix`, `brg`
- [x] `gist_rainbow`, `rainbow`, `nipy_spectral`, `gist_ncar`

### 3.7 Colormap API
- [x] `LinearSegmentedColormap` (`Colormap::segmented()` — per-channel (x,y0,y1) segment lists rasterized to a LUT)
- [x] `ListedColormap` (`Colormap::listed()` — discrete bin sampling)
- [x] Reversed colormaps (`name + '_r'`, `Colormap::reversed()` — swaps under/over, preserves discrete/bad)
- [x] `Colormap` `bad`, `under`, `over` colors (`withBad`/`withUnder`/`withOver`; NaN → bad/transparent, t<0 → under, t>1 → over in all colormapped plots)

---

## 4. Axes and Figure Features
- [x] `plt.figure` / `Figure`
- [x] `plt.subplots`, `plt.subplot` (Figure(rows, cols) + addAxes)
- [x] `subplot_mosaic`
- [x] `subplot2grid`
- [x] `GridSpec`, `SubplotSpec`, `GridSpecFromSubplotSpec`
- [x] `subfigures`
- [x] `sharedx`, `sharedy`
- [x] `twinx`, `twiny`
- [x] `secondary_xaxis`, `secondary_yaxis`
- [x] `inset_axes` (`mpl_toolkits.axes_grid1.inset_locator`)
- [x] `make_axes_locatable`
- [x] `constrained_layout`, `tight_layout`, `subplots_adjust`
- [x] `colorbar` (inset and standalone)
- [x] `colorbar` (vertical color strip + tick labels, right of axes)
- [x] Projections: `rectilinear`, `polar`, `aitoff`, `hammer`, `lambert`, `mollweide`, `3d`
- [x] Scales: `linear`, `log`, `symlog`, `logit`, `asinh`, `function`, `functionlog`, `mercator`
- [x] Polar: `set_rgrids`, `set_thetagrids`, `set_theta_offset`, `set_theta_direction`
- [x] Aspect ratio, equal axis, invert axis, set limits, autoscale (autoscale: [x])

---

## 5. Text and Annotations
- [x] `title`, `suptitle`, `figtext`, `xlabel`, `ylabel` (data model: [x], rendering: [x] — glyb bitmap atlas)
- [x] `text`, `annotate` (Axes::text(), Axes::annotate() with arrows)
- [x] `tick_params`, `set_xticklabels`, `set_yticklabels` (auto tick labels: [x])
- [x] `Annotation` with `arrowprops` (simple arrow shaft + arrowhead)
- [x] `arrowstyle` grammar (`ArrowStyleSpec` + `parseArrowStyle`):
      `-`, `<-`, `->`, `<->`, `-|>`, `<|-`, `<|-|>`, `-[`, `<-[`, `]-[`,
      `|-|`, `simple`, `fancy`, `wedge` + params (head_length/head_width/
      tail_width/widthA/widthB/lengthA/lengthB/shrink_factor/
      mutation_scale); geometry per mpl `patches.py` (pad_projected
      overshoot, stroked brackets, tapered wedge); raster + vector paths
- [x] Coordinate systems: `data`, `axes`, `figure`, `display`, `offset points`
- [x] Text alignment: left/center/right, top/center/bottom/baseline
- [x] Text background box (bbox face/edge color, padding)
- [x] Text rotation (per-annotation, radians)
- [x] Text color and font size scaling
- [x] `FancyArrowPatch` with curved shaft, connection styles (arc, arc3, angle, bar)
- [x] `Annotation` with `arrowprops` / `FancyArrowPatch`
- [x] MathText (TeX-like subset): sub/sup, fractions, radicals, Greek, accents, calligraphic, etc.
- [~] MathText fontsets: `dejavusans` (default), `dejavuserif`, `cm`, `stix`, `stixsans`
      (single-font atlas; DejaVu Sans covers the implemented glyph set)
- [~] `text.usetex` full LaTeX rendering (requires external TeX — not planned;
      MathText subset covers the common cases)
- [x] Font properties: family, weight, style, size, color (FontProperties struct)
- [x] Font properties: rotation, alignment

### 5.1 Text Rendering (glyb bitmap atlas)
- [x] Hole bridging for glyphs with holes (O, A, B, etc.) — resolved by
      switching from vectorized triangulation to glyb's FreeType span
      rasterizer, which correctly handles the even-odd fill rule.
- [x] HarfBuzz text shaping (kerning, ligatures, RTL/CJK ready)
- [x] Y-axis label rotation — 90° rotation via per-glyph vertex transform
      in TextRenderer::draw() (rotation parameter in radians).
- [x] Font rotation and alignment properties (FontProperties::rotation,
      horizontal/vertical alignment)
- [x] Text clipping to axes rect for `text()` / `annotate()` (data-space text,
      `clipOn` scissor)
- [x] Multi-line text (newline support, per-line alignment)
- [x] Text layout engine (multi-line layout with per-line alignment;
      word wrap not implemented — no wrap-width API)
- [x] Subpixel positioning (float positions preserved through layout and
      vertex transform; glyphs rasterized at 16px reference and scaled on GPU)
- [x] Font subsetting for large character sets (atlas holds a curated subset:
      ASCII + Greek + math symbols, not the full font)
- [x] CJK / RTL text shaping — HarfBuzz `hb_buffer_guess_segment_properties`
      gives correct direction/script per run; a broad-coverage fallback face
      is probed at init (coverage ranking over system fonts) and shares the
      glyph atlas (single texture); lazily rasterized glyphs flag the atlas
      dirty → re-upload + repaint once; UTF-8 run-splitting routes glyphs to
      the covering face; atlas enlarged to 2048²

---

## 6. Markers and Line Styles

### 6.1 Markers
- [x] `'.'` point, `'o'` circle, `'s'` square, `'D' 'd'` diamond, `'^'` triangle, `'+'` plus, `'x'` X, `'*'` star (MarkerStyle enum)
- [x] `'v' '<' '>'` triangles, `'1' '2' '3' '4'` tri arrows
- [x] `'p'` pentagon, `'P'` plus filled
- [x] `'h' 'H'` hexagons, `'X'`, `'|'` vline, `'_'` hline, `'8'` octagon
- [x] TICK/CARET variants (`0`–`11`)
- [~] TeX glyph markers (`'$...$'`) — MathText exists, but per-marker
      text draw calls aren't wired
- [x] Custom `Path` markers — `Series2D::markerPath` (any `Path` flattened
      via `markerGeom`, wired into ScatterPlot/LinePlot; feature
      `063_marker_path`); `(numsides, style, angle)` polygons via
      Polygon/StarN/AsteriskN/CircledN + `markerNumsides`/`markerAngle`
- [x] Fill styles: `full`, `left`, `right`, `bottom`, `top`, `none`
      (MarkerFill, SDF half-fill/outline in PointRenderer)

### 6.2 Line styles
- [x] `'-'` / `solid`, `'--'` / `dashed`, `'-.'` / `dashdot`, `':'` / `dotted`
      (LineStyle enum + `lineStyleFromString`, rendered via CPU stroker)
- [x] Custom dash tuple: `(offset, (on, off, on, off, ...))`
      (`Series2D::dashes` + `dashOffset`, pixel-space)
- [x] `drawstyle`: `default`, `steps`, `steps-pre`, `steps-mid`, `steps-post`
      (`Series2D::drawStyle`, `drawStyleFromString`, `applyDrawStyle`)
- [x] Cap styles: `butt`, `round`, `projecting` (stroker end caps)
- [x] Join styles: `miter`, `round`, `bevel` (stroker joins, miterLimit)
- [x] `gapcolor` support (`Series2D::gapColor` — solid underlay pass)

---

## 7. Color Handling
- [x] Single-letter shorthands: `bgrcmykw`
- [x] Named colors (X11/CSS4, `tab:...`, `C0`–`C9` cycle)
- [x] Hex (`#RGB`, `#RRGGBB`, `#RGBA`, `#RRGGBBAA`)
- [x] RGB/RGBA tuples (0–1 float) (Color struct)
- [x] Grayscale string (`'0.5'`)
- [x] `CN` index colors (`C0`–`C9`)
- [x] `xkcd:` color names
- [x] Color cycles / `axes.prop_cycle` (ColorCycle with tab10 palette)
- [x] `cycler` library integration (color + linestyle + marker + linewidth cycling)
- [x] Normalization:
  - [x] `Normalize`, `NoNorm`
  - [x] `LogNorm`, `PowerNorm`, `SymLogNorm`, `AsinhNorm`
  - [x] `BoundaryNorm`, `CenteredNorm`, `TwoSlopeNorm`, `FuncNorm`, `MultiNorm`
  - [x] Norm integration in colormapped plots (pcolormesh, matshow, hexbin,
        hist2d, tripcolor, pcolorfast, trisurf, specgram)
  - [x] Norm autoscale from data (vmin/vmax auto-computed if unset)
  - [x] Factory functions (`norms::linear`, `norms::log`, `norms::power`, etc.)
  - [x] `Colormap` `bad`, `under`, `over` colors

---

## 8. Legend Features
- [x] `legend` with auto or explicit `handles`/`labels` (data model: LegendStyle struct)
- [x] Legend rendering (colored markers + text labels, semi-transparent background + border)
- [x] Locations: `best`, `upper right`, `upper left`, `lower left`, `lower right`, `right`, `center left`, `center right`, `lower center`, `upper center`, `center` (location string field)
- [x] `loc`, `bbox_to_anchor`, `bbox_transform` (all named locs + numeric codes; anchor in axes/figure coords)
- [x] `ncols` / `ncol`, `nrows` (column-major multi-column layout)
- [x] `title`, `title_fontproperties`
- [x] `frameon`, `framealpha`, `facecolor`, `edgecolor`, `shadow`, `fancybox` (fields in LegendStyle)
- [x] `labelcolor`
- [x] `handlelength`, `handletextpad`, `borderpad`, `columnspacing` (+`borderaxespad`, rcParams)
- [ ] `draggable` — flag field exists; §11 event system + picking are
      done, but pick+drag wiring for legends/annotations is still missing
- [~] `handler_map` / legend handlers — `IPlot::legendMarker()` provides per-plot handle shapes; no arbitrary handler factory

---

## 9. Grid and Ticks

### 9.1 Ticks
- [x] Major and minor ticks (minorticksOn/Off, AutoMinorLocator, log-scale auto minors)
- [x] `ax.grid` (major/minor/both via gridWhich, x/y/both axis, color, linewidth) (AxisStyle fields + GridRenderer minor grid)
- [x] `tick_params` (Axes::tickParams: direction in/out/inout, major/minor size+width, per-axis; xtick./ytick. rcParams)
- [x] Auto tick computation (nice-number locator, tick label formatting)

### 9.2 Locators
- [x] `AutoLocator`, `MaxNLocator` (nice-number algorithm, TickConfig::nbins)
- [x] `LinearLocator`, `MultipleLocator`, `FixedLocator`, `IndexLocator` (Ticks.hpp; Axes::set*Locator)
- [x] `LogLocator`, `LogitLocator`, `AutoMinorLocator` (incl. LogLocator::minorValues subs)
- [x] `NullLocator`, `SymmetricalLogLocator`

### 9.3 Formatters
- [x] `ScalarFormatter` (setLocs offset detection, scilimits, useMathText, useOffset, forceSci)
- [x] `NullFormatter`, `FixedFormatter` (Ticks.hpp; Axes::set*Formatter)
- [x] `FuncFormatter`, `StrMethodFormatter`, `FormatStrFormatter`
- [x] `LogFormatter`, `LogFormatterExponent`, `LogFormatterMathtext`, `LogFormatterSciNotation`
- [x] `LogitFormatter`, `EngFormatter`, `PercentFormatter`
- [x] `ticklabel_format` (Axes::ticklabelFormat + axes.formatter.{limits,use_mathtext,useoffset} rcParams)

---

## 10. Savefig Formats

- [x] `png` (CPU via libpng; GPU encoder stub)
- [x] `webp` (CPU via libwebp)
- [x] `bmp` (CPU, no dependency)
- [x] `raw` (no encoding)
- [x] `jpg` / `jpeg` (CPU via libjpeg, VOLCANO_HAS_JPEG)
- [x] `tiff` / `tif` (uncompressed RGBA writer, no dependency)
- [x] `pdf` (raster-embedded XObject, FlateDecode via zlib / hex fallback)
- [x] `svg`, `svgz` (base64-PNG embedded raster; svgz gzip via zlib)
- [x] `eps`, `ps` (PostScript hex colorimage)
- [~] `pgf` (LaTeX/PGF backend — needs a true vector command stream; deferred)
- [x] Metadata support per format (PNG tEXt, JPEG COM, TIFF ImageDescription, PDF /Info, SVG dc:*, EPS %%Title)
- [x] `transparent`, `dpi`, `bbox_inches` (tight content crop), `pad_inches` (encode::SaveOptions + Renderer::savefig)

---

## 11. Interactive Features
- [x] Pan, zoom (basic SDL3 event polling)
- [x] Navigation toolbar: Home / Back / Forward (Navigation: viewport history)
- [x] Zoom (x/y constrained), zoom-to-rectangle (ZoomRect mode + rubber band)
- [x] Save figure button, configure subplots ('s' key → savefig; SubplotTool)
- [x] Cursor data readout (Navigation::formatCoord + onCursorMove)
- [x] Key bindings: `p` pan, `o` zoom, `h`/`r` home, `s` save, `g` grid, `l` y log, `k` x log, `f` fullscreen, `q` quit
- [x] Event system: `button_press_event`, `motion_notify_event`, `key_press_event`, `pick_event`, `scroll_event`, etc. (EventCanvas mpl_connect/disconnect; backend InputEvent → plot::Event)
- [x] Widgets: Slider, RangeSlider, Button, CheckButtons, RadioButtons, TextBox, SpanSelector, RectangleSelector, EllipseSelector, LassoSelector, Lasso, PolygonSelector, Cursor, MultiCursor, SubplotTool

---

## 12. Animation
- [x] `FuncAnimation`
- [x] `ArtistAnimation`
- [x] `TimedAnimation` base
- [x] Blitting support for efficient updates — `IPlot::animated` marks
      artists; `blitCaptureBackground()` renders static-only and snapshots
      the framebuffer; `blitDrawAnimated()` restores the snapshot via a
      loadOp=LOAD render pass and redraws only animated artists;
      saveAnimation/toJsHtml use it when `anim.blit` is set
- [x] Writers: `PillowWriter` (GIF89a built-in), `FFMpegWriter`, `ImageMagickWriter`, `ApngWriter` (built-in, zlib); HTML via `jsHtmlFromPngFrames`/`html5VideoFromMovie`
- [x] `animation.to_jshtml` (`Renderer::toJsHtml`), `animation.to_html5_video` (`Renderer::toHtml5Video`, falls back to JS HTML without ffmpeg)

---

## 13. Transformations
- [x] `transData` (data → display) (Transform2D + live-bound `transData(axes)`)
- [x] `transAxes` (axes fraction) — live-bound, `Axes::transAxes()`
- [x] `transFigure` (figure fraction) — live-bound, `Figure::transFigure()`
- [x] `transDisplay` (IdentityTransform singleton)
- [x] `CompositeGenericTransform`, `CompositeAffine2D` (`Transform::then`)
- [x] `BlendedAffine2D`, `BlendedGenericTransform`
- [x] `blended_transform_factory` (`blendedTransformFactory`)
- [x] `offset_copy` (`offsetCopy`: points/pixels/inches at figure dpi)
- [x] `Affine2D` (scale, rotate, rotateAround, translate, skew, concat, inverted)
- [x] Custom `Transform` / `TransformNode` hierarchy (abstract `Transform` with apply/inverted/clone)
- [x] 3D camera (view + projection matrices) (Camera3D)

---

## 14. Path and Patch Collections
- [x] `Path` (vertices+codes, curves, bounds, containsPoint, unit shapes), `patch::PathPatch`
- [x] `PatchCollection` (`Axes::addPatch`)
- [x] `PathCollection` (path instanced at offsets, per-item sizes/transforms)
- [x] `LineCollection`
- [x] `PolyCollection`
- [x] `QuadMesh`
- [x] `TriMesh` (`TriMeshCollection`)
- [x] `CircleCollection`
- [x] `RegularPolyCollection`, `AsteriskPolygonCollection`
- [x] `Patch` primitives: `Circle`, `Ellipse`, `Rectangle`, `Polygon`, `Wedge`, `FancyBboxPatch`, `FancyArrowPatch` (in `patch::` namespace)
- [x] `boxstyle` variants on `FancyBboxPatch` — full mpl `BoxStyle`
      grammar (`square`/`circle`/`ellipse`/`round`/`round4`/`sawtooth`/
      `roundtooth`/`larrow`/`rarrow`/`darrow` + `pad`/`rounding_size`/
      `tooth_size`/`mutation_scale`) via `parseBoxStyle`/`boxStylePath`;
      `Annotation::boxStyle` for `bbox=dict(boxstyle=...)`
- [x] `set_clip_path` for artists — `Collection::clipPath` and
      `Annotation::clipPath` (data-coord `Path`, CPU polygon clipping of
      fills/hatches/edges/lines, raster + vector)
- [x] Hatch patterns (`/ \ | - + x`, repeats increase density; `o`/`.`/`*` not supported)
- [x] `offsets` and `offset_transform` for instanced rendering (offsetTransform on Collection)

---

## 15. Specialized Plots / Extensions
- [x] `Sankey` diagrams (`matplotlib.sankey.Sankey`) — single-stage trunk +
      Bezier ribbons, `Sankey(ax).add(flows, labels).finish()`
- [x] XKCD-style sketching (`plt.xkcd()`) — `xkcdStyle` sets `sketchScale`;
      lines and collection edges wobble via deterministic `sketchPolyline`
- [x] Radar / spider charts via `polar` projection (lines + filled polygons
      map through `dataToFraction`'s projection stage)
- [x] Treemaps — built-in `squarify` layout + `treemap(axes, sizes, ...)`
      helper (patch rects + centered labels)
- [x] Word clouds — `WordCloudPlot` / `ax.wordcloud(words)`: Archimedean-
      spiral packing with pixel-space box collision via measureText,
      weight-scaled fonts (linear/log), seeded-deterministic, optional
      90° rotation ratio, colormap-ranked colors
- [x] Network/graph drawing — `NetworkPlot` / `ax.network(n, edges)`:
      Fruchterman–Reingold spring, circular, random, and given layouts;
      edges via LineSegmentRenderer, nodes via PointRenderer, optional
      per-node labels
- [x] `Table` / `ax.table` (tabular data overlays) — `TablePlot` IPlot with
      cell text/colors, row/col labels, `loc` placement
- [x] `matshow` / `spy` matrix visualizations (heatmap-backed, existing)
- [x] `broken_barh` gantt-like intervals (existing)

---

## 16. Additional Matplotlib Capabilities (cross-cutting)
- [x] Colorbars with `extend` arrows and custom norms
- [x] Colorbar rendering (vertical color strip with viridis colormap + tick labels)
- [x] Spines / axis styling (hide individual spines, `spines.set_visible`)
- [x] Spines / axis border rendering (rectangle border + tick marks around axes rect)
- [x] `zorder` compositing
- [x] Picking / hit testing
- [x] Rasterization (`rasterized=True`) for vector backends
- [x] `clabel` for contour labels
- [x] `hist` histogram types (bar, barstacked, step, stepfilled)
- [x] `boxplot` notched, bootstrap, median/MU, cap/join styles
- [x] `violinplot` with custom positions, widths, bodies
- [x] `errorbar` continuous vs per-point error styles

## 17. Units & Date/Categorical Axes

- [x] Units registry (`UnitsRegistry::instance()`, `UnitConverter`,
      `AxisInfo` — mpl `matplotlib.units`)
- [x] `dates::dateToNum`/`numToDate` (days since 1970-01-01 UTC),
      `civilFromNum`, `strfnum`
- [x] Date locators: `YearLocator`, `MonthLocator`, `WeekdayLocator`,
      `DayLocator`, `HourLocator`, `MinuteLocator`, `SecondLocator`,
      `MicrosecondLocator`, `AutoDateLocator`
- [x] Date formatters: `DateFormatter` (strftime), `AutoDateFormatter`,
      `ConciseDateFormatter` (offset text carries larger context)
- [x] `dates::DateConverter` (chrono sys_days/sys_seconds/...) and
      `dates::StrCategoryConverter` (order-of-appearance categories)
- [x] `Axes::plot(x, y)` unit-aware convenience (floats, chrono dates,
      string categories on either axis)
- [x] `ax.xaxis_date()`/`yaxis_date()`, `setXCategories`/`setYCategories`,
      `xCategoryIndex`/`yCategoryIndex` (mpl unit_data semantics)

---

## Priority Tiers

### Core (must-have for v0.1)
- Line plots with MSAA ✓ (pipeline done)
- Scatter plots with markers ✓ (pipeline done)
- Bar charts ✓ (pipeline done + regression tests)
- Heatmap/imshow ✓ (pipeline done + regression tests)
- PNG export (CPU ✓, GPU stub)
- ggplot style ✓
- Basic grid ✓ (fwidth-based GridRenderer)
- Axis labels ✓ (vectorized text rendering with FreeType outline decomposition)
- Tick labels ✓ (auto-locator + auto-formatter)
- Title ✓ (vectorized text rendering)

### Advanced (v0.2+)
- GPU-side function evaluation (compute shader)
- GPU autoscale (parallel reduce)
- Infinite zoom interaction
- KDE on GPU
- 3D surface plot (depth attachment + depth testing ✓)
- Volcano plot per-point coloring
- Text rendering (SDF atlas)
- Legend rendering ✓ (colored markers + text labels)
- Axis spines ✓ (border + tick marks)
- Colorbar rendering ✓ (color strip + tick labels)

### Long-term
- Full matplotlib style sheet parser
- MathText/LaTeX rendering
- Vector export (PDF/SVG)
- Animation
- Interactive widgets
- All colormaps
- All plot types

---

## 18. Roadmap — Next Major Features (priority order)

The microfeature-parity sweep is complete (see `docs/MICROFEATURES.md`).
Items below are roughly ordered by user-visible value vs. implementation
cost. Features that were once listed here and are now done (Path
infrastructure, contour/contourf, widgets, GridSpec/subfigures, polar,
tri_*) are checked off in the sections above.

### P0 — Correctness & robustness
- [ ] **Remaining parity deltas** — residual microfeature diffs: legend
      box sizing/spacing vs mpl in dense cases, colorbar `fraction`/
      `aspect` semantics (mpl `aspect=20` drives strip width; `fraction`
      drives axes shrink — `ColorbarStyle` only has pixel `width`),
      minor tick-label suppression on crowded axes, `labelpad`/
      `tick pad` fine tuning.
- [~] **Vector/raster parity pass** — secondary x/y axis tick labels,
      tick-label rotation anchoring, and native `TablePlot::emitVector`
      (cells/borders/text, may extend outside the axes rect) emit in
      vector now; legend shadow, faux-bold titles, and colorbar extend
      triangles remain raster-only.
- [x] **`draggable` legends/annotations** — `legend.draggable` and
      `TextAnnotation`/`Annotation.draggable` drag via `dragOffset`
      (pixel displacement, arrow follows the text end); hit-testing uses
      the text box recorded each draw (`Figure::artistDragEvent`).
- [x] **Deterministic vector regression tests** — golden-geometry tests
      parse `emitVector` path data and check coordinates against the
      data→pixel transform, byte-stability across runs, and native
      size-bar geometry (`VectorSvgDeterministic.*`).

### P1 — High-value features
- [x] **`set_clip_path` for artists** — done (CPU polygon clipping on
      `Collection`/`Annotation`, raster + vector; §14).
- [x] **`boxstyle` variants** — done (full mpl `BoxStyle` grammar; §14).
- [x] **Bezier-accurate arrow bodies** — `simple`/`fancy`/`wedge` now
      use mpl's `make_wedged_bezier2`/`get_parallels`/circle-split
      construction (quad-Bézier outlines flattened at draw time).
- [x] **MathText coverage** — `\sum`/`\prod`/big-op family enlarged
      with stacked limits, `\int`-family enlarged with side scripts,
      `\left…\right` auto-sized delimiters (+`\big`/`\Big`/`\bigg`/
      `\Bigg` fixed sizes), nested scripts verified (§5).
- [x] **`contour`/`clabel` inline label gaps** — labels exist; the
      contour line isn't broken under them.
- [x] **`pcolormesh shading="gouraud"`** and `imshow interpolation=`
      variants (`"bilinear"`, `"bicubic"`, `"antialiased"` — `"nearest"`
      done) in the heatmap shader.
- [x] **Log-scale data clipping** — non-positive points in log mode
      should be dropped at data-conversion time (currently clamps).

### P2 — Performance & GPU leverage
- [ ] **GPU-side marker/path tessellation** — marker quads currently
      use SDF; large `PathCollection`s should triangulate on GPU or use
      instanced glyph atlases.
- [ ] **MSDF text atlas** — replace the grayscale bitmap atlas with
      multi-channel SDF for crisp text at any DPI (glyb has an MSDF
      renderer stub already).
- [ ] **Multi-frame animation GPU encode** — APNG/GIF encoders are CPU;
      wire the compute-shader encoder into the animation path.
- [ ] **Partial redraw / damage tracking** — screen backend re-renders
      everything; cache the framebuffer and blit overlays.
- [ ] **GPU `pcolormesh`/`contour`** — move CPU tessellation to compute
      for million-cell meshes.

### P3 — Broader matplotlib surface
- [~] **`AxesImage` transforms** — `imshow` `extent` (`Grid2D.xRange`/
      `yRange`) and mpl `aspect=` ("equal" default letterboxes the axes;
      "auto" fills it) done; `transform=` non-affine (polar projection
      of images) remains.
- [x] **`mpl_toolkits.axes_grid1` extras** — `AnchoredSizeBar`
      (`Axes::addSizeBar`), zoom-effect inset (`Axes::indicateInset`/
      `indicateInsetZoom`, mpl auto connector visibility), and generic
      `AnchoredText` (`Axes::addAnchoredText` — loc names/codes,
      frameon, pad/borderpad) done, raster + native vector;
      `inset_axes`/`make_axes_locatable` done.
- [~] **3D polish** — `plot_surface` light-source shading done
      (`SurfacePlot::shade`, mpl LightSource azdeg=315/altdeg=45
      lambert via screen-space normals); `view_init` interactive
      rotation, 3D tick labels on pane edges, and `scatter` size/depth
      cueing remain.
- [x] **`quiver`/`streamplot` params** — quiver `scale`, `width`,
      `headwidth` (+ `headlength`/`headaxislength`/`pivot`); streamplot
      `arrowsize`/`broken_streamlines`.

### P4 — Ecosystem & ergonomics
- [ ] **pybind11 bindings (`volcanoplot` Python module)** — the single
      biggest adoption driver; keep the API mirroring `matplotlib.pyplot`.
- [ ] **`xarray`/`pandas` plotting hooks** — `__array__` + `plot`
      dispatch, index/date handling via existing unit converters.
- [ ] **Nix/Homebrew packaging + CI matrix** — prebuilt binaries,
      Vulkan ICD (lavapipe) in CI for headless tests.
- [ ] **Documentation site** — API reference (Doxygen/mkdocs), gallery
      browser from `gallery_micro`, migration guide from matplotlib.
- [ ] **Serialization round-trip** — save/load a `Figure` to JSON for
      reproducible test fixtures and headless replays.
- [ ] **Accessibility** — colorblind-safe default cycle option,
      alt-text metadata in vector output.
