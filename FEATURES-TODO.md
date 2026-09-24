# FEATURES-TODO.md — Matplotlib Feature Parity Tracking

This file tracks VolcanoPlot's progress toward matplotlib feature parity.
Status legend: `[ ]` not started · `[-]` in progress · `[x]` done · `[~]` won't fix

## 1. Plot Types

### 1.1 2D — Pairwise / functional
- [x] `plot` — line and/or marker plots (LinePlot), `markevery=` subsampling
      (int/(start,step)/fraction/(frac,frac)/index list/`first`/`last`/`all`/
      `none`, raster + vector, `Series2D::markevery`/`set_markevery`)
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
      two curves or curve and baseline, with alpha blending; `where=` boolean
      mask splits into contiguous regions and `interpolate=` extends each
      region to the y1==y2 root inside the gap, matching mpl
      `_make_verts_for_region`; masked points excluded from autoscale)
- [x] `vlines`, `hlines` (Vlines/Hlines — vertical/horizontal line segment
      collections via LineSegmentRenderer with eLineList topology)
- [x] `axhline`, `axvline`, `axhspan`, `axvspan` (ReferenceLines —
      axis-spanning lines drawn via SpineRenderer pixel-space line strips
      to avoid GPU guard-band clipping; spans use FillRenderer with
      kAxisSpan triangles that clip correctly at viewport boundaries)
- [x] `axline` (AxLine — infinite line through two points or point+slope,
      pixel-space Liang–Barsky clipping to the axes rect, raster + vector)
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
      per-series colors, FillRenderer triangle tessellation; `baseline=`
      `zero`/`sym`/`wiggle`/`weighted_wiggle` matching mpl's `stackplot`
      baseline math)
- [x] `pie`, `pie_label` (PiePlot — stub pipeline, 2D + donut; full mpl
      geometry: `startangle`/`counterclock`/`radius`/`center`/`normalize`,
      `autopct` fmt/callable percentage labels with `pctdistance`,
      `labeldistance` (None disables), `rotatelabels`, `frame`; mpl
      validation: negative x, normalize=False with sum>1, radius<=0 → error)
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
      approximated by bilinear; `origin=` "upper"/"lower" row flipping
      via shader flag, `Grid2D::origin`)
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
- [x] `psd` (PsdPlot — power spectral density via mpl `_spectral_helper`
      semantics: Welch segment averaging over NFFT segments with `noverlap`
      (mpl default 0), windowed radix-2 FFT, |FFT|^2 normalized by
      sampleRate * sum(w^2), one-sided spectrum with DC/Nyquist correction,
      10*log10 dB output, Hann/Hamming/Blackman/Rectangular windows,
      configurable nfft, LineRenderer)
- [x] `csd` (CsdPlot — cross-spectral density via Welch segment
      averaging (NFFT segments, `noverlap`, mpl default 0),
      |FFT(x) * conj(FFT(y))| normalized by sampleRate * sqrt(windowPower_x * windowPower_y),
      one-sided spectrum with DC/Nyquist correction, 10*log10 dB output,
      Hann/Hamming/Blackman/Rectangular windows, configurable nfft, LineRenderer)
- [x] `cohere` (CoherePlot — magnitude-squared coherence Cxy = |mean(Pxy)|^2
      / (mean(Pxx) * mean(Pyy)) averaged over NFFT segments (mpl `noverlap`
      default 0), one-sided spectrum,
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
- [x] `colorbar` (color strip + tick labels; `orientation` "vertical" →
      right of axes / "horizontal" → below axes, mpl fraction/pad/shrink/
      aspect/extend in layout + raster + vector)
- [x] Projections: `rectilinear`, `polar`, `aitoff`, `hammer`, `lambert`, `mollweide`, `3d`
- [x] Scales: `linear`, `log`, `symlog`, `logit`, `asinh`, `function`, `functionlog`, `mercator`
- [x] Polar: `set_rgrids`, `set_thetagrids`, `set_theta_offset`, `set_theta_direction`
      — plus full polar furniture: circular spine, radial thetagrid
      spokes, concentric rgrid circles, degree theta labels, r labels at
      22.5°, equal aspect (raster path)
- [x] Aspect ratio, equal axis, invert axis, set limits, autoscale (autoscale: [x])
- [x] `relim` / `autoscale_view` / `autoscale(enable, axis, tight)` —
      mpl autoscale state machine: `manualX_`/`manualY_` flags set by
      set_xlim/set_xbound/invert, `autoscale(enable=None)` leaves flags
      untouched and only updates autoscaling axes, `dataLim` holds raw
      limits (margins applied to the view only, GPU autoscale too),
      `set_autoscalex_on`/`get_autoscalex_on`, `relim(visible_only)`
      skips invisible artists, sticky edges (bar baseline pins y=0)
- [x] `set_xbound`/`set_ybound` (Axes::setXbound/setYbound — mpl sorted-bounds
      semantics: None sides keep old displayed bounds, pair sorted
      ascending/descending by inversion and applied atomically)
- [x] `axis("off"/"on")`, `set_axis_off`/`set_axis_on`, `set_frame_on`,
      `axison`, `frame_on` — mpl semantics: `axison` gates patch+spines+
      ticks+labels (title stays), `frame_on` gates patch+spines; wired in
      raster and vector renderers
- [x] `label_outer` (Axes::labelOuter — suppress inner tick labels/ticks on
      shared subplot grids by AxesPlacement spec position)

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
- [x] MathText fontsets: `dejavusans` (default) and `dejavuserif` via
      `style().mathFontset` — DejaVu Serif loads as a second face into the
      shared atlas, math runs carry a per-run face tag measured/shaped
      with the serif face. `cm`/`stix`/`stixsans`/`custom` are accepted
      and degrade to dejavusans (only DejaVu faces ship).
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
- [x] TeX glyph markers (`'$...$'`) — `Series2D::setMarker("$…$")`
      parses the mpl string spec into `markerTex`; raster
      (drawTexMarkersPx) + vector (unicode glyph) paths wired in
      ScatterPlot and LinePlot
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
- [x] `vp.colors` submodule — `is_color_like`, `to_rgba`/`to_rgb`/`to_hex`,
      `rgb2hex`, `hex2color`, `same_color`, `to_rgba_array`,
      `hsv_to_rgb`/`rgb_to_hsv`, scalar-float grayscale specs; all mpl
      `Normalize` classes bound (`Normalize`/`NoNorm`/`LogNorm`/
      `PowerNorm`/`SymLogNorm`/`AsinhNorm`/`BoundaryNorm`/`CenteredNorm`/
      `TwoSlopeNorm`) with `vmin`/`vmax`/`clip`/`scaled()`/`inverse`/
      `autoscale`/`autoscale_None`; `ListedColormap` +
      `LinearSegmentedColormap` (incl. `from_list`, segmentdata dicts,
      `gamma`) via Python classes over registry factories
- [x] `vp.cm` submodule — `get_cmap` (name/Colormap/None→viridis),
      `register_cmap` (custom names persist; builtin names protected
      unless `override_builtin`), `colormaps()` listing, `ScalarMappable`
      standalone object (norm/cmap/array/clim/to_rgba/autoscale)
- [x] `IPlot` ScalarMappable protocol — virtual `norm()`/`setNorm()`/
      `cmap()`/`setCmap()`/`setArray()`/`array()`/`setClim()`; artist
      handles (`PathCollection`, `AxesImage`) expose `set_cmap`,
      `get_cmap`, `set_norm`, `get_norm`, `set_clim`, `get_clim`,
      `set_array`, `get_array`, `to_rgba`, `autoscale`, `autoscale_None`,
      `norm`/`cmap` properties
- [x] `scatter(c=..., s=...)` — scalar `c` arrays colormapped through
      cmap+norm (mpl disambiguation: color spec vs color list vs value
      array), explicit per-point color lists, per-point `s` pt² sizes
      (scalar or array, `sqrt(s)·dpi/72` px), `cmap`/`norm`/`vmin`/`vmax`/
      `alpha`/`edgecolors`/`linewidths`/`marker` kwargs; raster + vector
- [x] `imshow(cmap=Colormap, norm=..., vmin=..., vmax=..., alpha=...)` —
      CPU norm pre-transform into the LUT upload, `set_array` reshape,
      `set_clim`/`set_cmap`/`set_norm` mutation, alpha baked into stops
- [x] `pcolormesh`/`matshow`/`hexbin`/`hist2d` accept Colormap objects and
      `norm`/`vmin`/`vmax` kwargs; colorbar resolves cmap+range from the
      `mappable` artist (`ColorbarStyle::mappable`)

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
- [x] `markerscale`, `numpoints`, `scatterpoints` — marker counts/sizes on
      handles (marked lines draw `numpoints` markers on the handle line)
- [x] `reverse`, `markerfirst` (label-first + right-aligned handle),
      `mode='expand'` (columns stretch to fill the anchor width),
      `alignment` ('left'/'center'/'right' — title + entry block align
      within the box)
- [x] `handles=`/`labels=` kwargs — explicit artist handles snapshot
      label/color/marker at legend() time; positional label override
- [x] `bbox_to_anchor` 2- and 4-tuples — (x, y, w, h) resolves the loc
      anchor inside the sub-box (mpl Bbox semantics)
- [x] `Legend` artist handle — `ax.legend()`/`fig.legend()`/`plt.legend()`
      return a `Legend` object (`set_visible`/`get_visible`/`remove`,
      `draggable()`/`set_draggable`/`get_draggable`, `get_texts`,
      `get_title`, `set_title`, `legend_handles`, `loc`/`_loc` introspection)
- [x] `draggable` — legend + annotation/text pick-and-drag via
      `Figure::artistDragEvent` (dragOffset applied in raster+vector)
- [~] `handler_map` / legend handlers — `IPlot::legendMarker()` provides per-plot handle shapes; no arbitrary handler factory

---

## 9. Grid and Ticks

### 9.1 Ticks
- [x] Major and minor ticks (minorticksOn/Off, AutoMinorLocator, log-scale auto minors)
- [x] `ax.grid` (major/minor/both via gridWhich, x/y/both axis, color, linewidth) (AxisStyle fields + GridRenderer minor grid)
- [x] `tick_params` (Axes::tickParams: direction in/out/inout, major/minor size+width, per-axis; xtick./ytick. rcParams)
- [x] Auto tick computation (nice-number locator, tick label formatting)

### 9.2 Locators
- [x] `AutoLocator`, `MaxNLocator` (mpl-faithful `_raw_ticks`: extended
      step staircase {1,1.5,2,2.5,3,4,5,6,8,10}×10^k, integer/symmetric/
      prune/min_n_ticks, auto-nbins from pixel space; TickConfig::nbins)
- [x] `locator_params` (Axes::locatorParams — nbins/steps/integer/symmetric/
      prune/min_n_ticks/tight forwarded to MaxNLocator; non-linear axes
      unaffected per mpl)
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
- [x] `savefig` kwargs: `dpi='figure'`/number, `transparent`, `facecolor`,
      `edgecolor`, `format`, `metadata`, `pad_inches`, `quality`,
      `bbox_inches='tight'` — raster crops the readback; vector output does
      a two-pass tight emit (BoundsCanvas probe → ShiftCanvas translate into
      a tight-sized canvas)

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
- [x] Artist `transform=` — `IPlot::transform` overrides data→pixel
      mapping on lines/scatter/fill_between/collections; `relim` honors
      `transform.contains_branch(transData)` via `feedsData` (blended
      transforms feed each axis independently); `vp.transforms` module
      (`Transform`, `Affine2D` chainable mpl mutators — premultiply
      semantics, `translate` adds to the column, `transform_point`,
      `transform`, `inverted`, `+` composition, `IdentityTransform`,
      `blended_transform_factory`, `offset_copy`); `ax.transData` /
      `transAxes`, `fig.transFigure` keep the owner alive; `ax.text`
      accepts a Transform object
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
- [x] Spine `set_position` / `set_bounds` / `set_color` / `set_linewidth` /
      `set_linestyle` / `set_dashes` — mpl `('outward', pts)` /
      `('axes', frac)` / `('data', v)` modes plus 'center'/'zero'
      shortcuts; tick marks + tick labels + axis labels follow the moved
      spine; raster quads + stroked dashes + vector lines
- [x] `quiverkey` (QuiverKeyPlot — mpl reference arrow at axes-fraction
      anchor with the quiver's effective scale, labelpos N/S/E/W,
      labelsep/angle/color/labelcolor; raster + vector)
- [x] `clip_on` (IPlot::clipOn — raster scissor per artist, vector
      push/pop clip; `visible` flag honored by both renderers and by
      relim(visible_only))
- [x] `plt.xkcd(scale, length, randomness)` context manager + 
      `ax.set_sketch_params` — mpl path.sketch rcParam semantics
      (scale=None disables, length/randomness default 128/16)
- [x] Spines / axis border rendering (rectangle border + tick marks around axes rect)
- [x] `zorder` compositing
- [x] Picking / hit testing
- [x] Rasterization (`rasterized=True`) for vector backends
- [x] `clabel` for contour labels
- [x] `hist` histogram types (bar, barstacked, step, stepfilled)
- [x] `boxplot` notched, bootstrap, median/MU, cap/join styles
- [x] `violinplot` with custom positions, widths, bodies
- [x] `errorbar` continuous vs per-point error styles
- [x] `matplotlib.patheffects` — `vp.patheffects` module:
      `Normal`/`Stroke`/`SimpleLineShadow`/`SimplePatchShadow` +
      `withStroke`/`withSimpleLineShadow`/`withSimplePatchShadow`
      (effect + Normal); ordered passes, optional gc overrides
      (linewidth/foreground/alpha/offset in points, +y up), mpl shadow
      defaults (rho=0.3, alpha=0.3, offset (2,-2)); on lines, scatter,
      collections, bars, text/annotations — raster + vector
- [x] Axis placement — `ax.xaxis/yaxis.set_ticks_position` /
      `get_ticks_position` ('top'/'bottom'/'both'/'default'/'none';
      mpl's "default" = both marks + near labels, "unknown" otherwise),
      `set_label_position`/`get_label_position`,
      `tick_top`/`tick_bottom`/`tick_left`/`tick_right` (preserve
      labels-off state); marks, tick labels, axis labels, and offset
      text follow per-side furniture state in raster + vector; tight
      margins account for far-side labels
- [x] `fig.colorbar`/`plt.colorbar` object API — returns a `Colorbar`
      handle (`set_label`, `set_ticks`, `set_ticklabels`, `get_ticks`,
      `minorticks_on`/`off`, `remove`, `ax`, `mappable`,
      `orientation`); `mappable` artist arg selects the value range;
      `extend` min/max/both triangles; minor ticks subdivide majors /5
      (AutoMinorLocator); raster + vector
- [x] `Artist.remove()` — `Axes::removePlot`/`Figure::removeAxes` detach
      ownership and return the `unique_ptr`; Python handles keep the
      removed artist alive (`detached` shared_ptr slots); `.remove()`
      on Line2D/PathCollection/AxesImage/Patch/Text/Annotation/Quiver/
      BarContainer/Colorbar/Legend/Axes handles; `ax.remove()` drops the
      axes from the figure
- [x] Text `bbox=dict(...)` — mpl `set_bbox` semantics on `ax.text`,
      `annotate`, `plt.text`, `fig.text`: absent boxstyle → 'square' with
      pad = 4pt/fontsize fraction, explicit boxstyle → pad=0.3 default;
      `fc`/`facecolor`, `ec`/`edgecolor`, `lw`/`linewidth`, `alpha`,
      `pad` keys; full BoxStyle grammar (round/sawtooth/round4/etc.);
      `set_bbox`/`get_bbox`/`get_bbox_patch` on Text handles; raster +
      vector
- [x] Figure geometry/color API — `set_size_inches`/`get_size_inches`
      (HeadlessBackend::resize re-creates the framebuffer + render pass),
      `set_dpi`/`get_dpi`, `set_figwidth`/`set_figheight`,
      `set_facecolor`/`get_facecolor`, `set_edgecolor`/`get_edgecolor`,
      `set_frameon`/`get_frameon` (frameless canvas keeps the transparent
      clear color), `fig.set_facecolor` honored by savefig clear + vector
      canvas op; `savefig(dpi=)` scales relative to `figure.dpi`

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
- [~] **Remaining parity deltas** — verified: colorbar `fraction`/`pad`/
      `aspect`/`shrink`/`extend` semantics (axes shrink by fraction+pad;
      strip width = min(region, height/aspect) like mpl set_box_aspect),
      minor tick-label suppression (LogFormatterSciNotation
      minor_thresholds on crowded axes), `labelPad`/`tick pad` all in
      place. Legend box sizing/spacing in dense cases remains.
- [x] **Vector/raster parity pass** — secondary x/y axis tick labels,
      tick-label rotation anchoring, native `TablePlot::emitVector`,
      legend shadow, colorbar extend triangles, and faux-bold titles
      (0.6px double-strike, axes title + suptitle) all emit in vector.
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
- [x] **GPU-side marker/path tessellation** — `PointRenderer` already
      draws point sprites (one vertex per marker, SDF shapes in the
      fragment shader); `PathCollection` now uses
      `InstancedPathRenderer` (template triangle mesh uploaded once,
      per-instance center/scale/color buffer, single instanced draw)
      for eligible collections — no per-item transforms/hatch/edges/
      clip/sketch, closed triangulatable template; conservative CPU
      fallback otherwise (CollectionRender tests).
- [x] **MSDF text atlas** — real `glyph_renderer_msdf` via bundled
      msdfgen core (FreeType outline → Shape → generateMSDF → RGBA
      atlas), `font_manager_ft::msdf_enabled`, R8G8B8A8 atlas texture,
      median-of-3 + fwidth fragment shader.
- [x] **Multi-frame animation GPU encode** — `GpuPngEncoder` computes
      adaptive PNG row filtering (all 5 filters, min sum-of-abs) in a
      compute shader (one workgroup/row); `saveAnimation` wires it into
      `ApngWriter` via `setFrameFilter` so APNG frames are GPU-filtered
      (zlib deflate stays on CPU; verified decoded output).
- [x] **Partial redraw / damage tracking** — mpl `figure.stale`/`axes.stale`
      dirty tracking: every Axes mutator calls `touch()` which propagates
      to `Figure::markStale()`; `Figure::stale()`/`setStale()` aggregate
      over all axes (incl. subfigures); `Renderer::renderFrame` clears the
      flags and new `Renderer::renderIfStale()` skips re-recording when
      nothing changed (StaleTracking tests). Blit-based overlay restore
      already existed via `blitCaptureBackground`/`blitDrawAnimated`.
- [x] **GPU `pcolormesh` tessellation** — compute shader expands the
      cell grid directly into `VertexStorage` position/color buffers
      (flat + Gouraud, 259-entry LUT incl. under/over/bad, NaN-skip),
      adopted by `FillRenderer::adoptBuffers` with compute→vertex barriers;
      `PcmShading::gpuTessellate` (-1 auto large meshes, 0 CPU, 1 force),
      CPU fallback on failure. `GpuTessellationMatchesCpu` verifies
      pixel-identical output. Contour remains CPU (per-level restroking
      needs host-side segments).

### P3 — Broader matplotlib surface
- [x] **`AxesImage` transforms** — `imshow` `extent` (`Grid2D.xRange`/
      `yRange`), mpl `aspect=`, and `transform=` non-affine projections
      all done: `HeatmapRenderer` draws a fullscreen quad and
      inverse-maps each fragment through `projInv`/`scaleInv` GLSL
      (analytic polar inverse + theta mod-2pi wrap, Newton solve for
      geo projections), so images curve correctly on polar axes
      (ProjectionRegression.PolarImshowCurves).
- [x] **`mpl_toolkits.axes_grid1` extras** — `AnchoredSizeBar`
      (`Axes::addSizeBar`), zoom-effect inset (`Axes::indicateInset`/
      `indicateInsetZoom`, mpl auto connector visibility), and generic
      `AnchoredText` (`Axes::addAnchoredText` — loc names/codes,
      frameon, pad/borderpad) done, raster + native vector;
      `inset_axes`/`make_axes_locatable` done.
- [~] **3D polish** — `plot_surface` light-source shading
      (`SurfacePlot::shade`, mpl LightSource azdeg=315/altdeg=45
      lambert via screen-space normals) and scatter `depthshade`
      (mpl art3d._zalpha: alpha *= 1 − norm(depth)·0.7) done;
      and `Camera3D::viewInit(elev, azim, roll)` (mpl axes3d u/v/w
      basis, roll rotation about the view axis) done; interactive
      mouse-drag rotation done (left-drag orbits elev/azim, right-drag
      dollies; `Navigation` detects 3D plot layers, `IPlot::camera3D()`,
      Navigation3D tests).
- [x] **`quiver`/`streamplot` params** — quiver `scale`, `width`,
      `headwidth` (+ `headlength`/`headaxislength`/`pivot`); streamplot
      `arrowsize`/`broken_streamlines`.

### P4 — Ecosystem & ergonomics
- [x] **pybind11 bindings (`volcanoplot` Python module)** — pybind11
      module (`python/volcanoplot.cpp`, `VOLCANO_BUILD_PYTHON`, built
      to build/python/): Figure(figsize=/width,height,dpi, .stale),
      Axes: plot/scatter/bar/imshow/hist/errorbar/stem/step/
      fill_between/contour/contourf/boxplot/pcolormesh,
      xlim/ylim/xscale/yscale/xlabel/ylabel/title/grid/legend/
      set_projection, suptitle/supxlabel/supylabel, savefig (all
      raster+vector formats). Keeps the API mirroring `matplotlib.pyplot`.
- [x] **`xarray`/`pandas` plotting hooks** — `toFloats()` in
      volcanoplot.cpp accepts numpy arrays (buffer fast-path via
      forcecast), pandas Series/Index and xarray via `.values`,
      datetime64/timedelta64 → days-since-epoch (date2num) with automatic
      `xaxis_date`/`yaxis_date` converter install; string x in `bar()`
      installs category ticks (mpl categorical semantics).
- [x] **CI test workflow** — `.github/workflows/ci.yml` runs the full
      `volcano_tests` suite headless on lavapipe
      (`VK_ICD_FILENAMES` + `LIBGL_ALWAYS_SOFTWARE`) in a
      Debug/Release matrix. Packaging (Nix/Homebrew) still open.
- [x] **Pyplot-style Python API** — stateful module functions in
      `volcanoplot`: figure()/gcf()/gca()/subplots(nrows,ncols),
      plot(y|x,y)/scatter, xlabel/ylabel/title/suptitle,
      xlim/ylim/xscale/yscale/grid/legend, savefig, cla/clf/close, show
      (no-op headless). `plot(y)` gets implicit x (mpl).
- [x] **In-place `set_data` fast path** — `LinePlot::setData/setXdata/
      setYdata` and `ScatterPlot::setData/setOffsets` update the series
      and mark the owner axes stale via a new `IPlot::owner()` back-pointer
      (set in `Axes::addPlot`, `IPlot::touch()` propagates). Point buffers
      are host-visible dynamic VBOs; `prepare()` memcpy's in place and
      only reallocs on growth — no staging round-trip per frame.
      Python: `Line2D.set_data/set_xdata/set_ydata`,
      `PathCollection.set_offsets`; `plot()`/`scatter()` return artist
      handles.
- [x] **`Figure::alignLabels/alignXlabels/alignYlabels`** — mpl label
      alignment: the renderer measures each grid axes' label depth
      (ticks + tick labels + labelpad + text) per frame, groups by
      rowspan.stop/start (xlabels) and colspan.start/stop (ylabels) with
      the tick-side as the mpl label position, and equalizes the deepest
      label via `Axes::xLabelShiftPx/yLabelShiftPx` applied at draw time.
      Non-SubplotSpec axes are skipped, same as mpl.
- [x] **`Axes::bxp()`** — box-and-whisker plot from precomputed stats
      (`BxpStats`: med/q1/q3/whislo/whishi + optional fliers/mean/
      cilo/cihi/label), with mpl options positions, widths, showbox,
      showcaps, showfliers, showmedians, showmeans, meanline,
      shownotches, patch_artist and manage_ticks (FixedLocator +
      FixedFormatter at the box positions). Python `ax.bxp(stats, ...)`
      accepts a list of dicts.
- [~] **Documentation site** — mkdocs site (`mkdocs.yml`,
      `docs/index.md`) with architecture/microfeatures pages and an
      auto-generated gallery browser (`scripts/build_docs.py`
      regenerates `docs/gallery.md` + assets from `gallery*/comparison`).
      Doxygen API reference and mpl migration guide still open.
- [x] **Serialization round-trip** — `Figure` ↔ JSON save/load
      (`Serialize.hpp`/`Serialize.cpp`); line plots preserve data,
      color, and line style; unknown plot types skipped; malformed
      input returns null; pixel-identical round-trip render
      (tests/test_serialize.cpp).
- [~] **Accessibility** — `FigureStyle::colorblindSafe()` swaps the
      prop_cycle to Okabe-Ito (named styles seaborn-v0_8-colorblind /
      tableau-colorblind10 already existed); SVG savefig metadata
      "Title"/"Description" keys emit <title>/<desc> elements for
      screen readers.
- [x] **GPU tessellated solid polylines** — `GpuLineRenderer` compute
      shader expands pixel-space points into a triangle soup (segment
      quads + miter/bevel/round joins + caps, degenerate tris for unused
      slots) into a device-local VB drawn through SpineRenderer's fill
      pipeline (`drawTrianglesGpu`). `IPlot::preDraw()` hook runs compute
      on a pre-pass command buffer submitted before `beginFrame` each
      `renderFrameSubset` (frame-seq tagged; vector raster fallback bumps
      the seq too). CPU `strokePolyline` remains for dashed/sketch lines.
- [x] **Figure-level legend (`fig.legend`)** — `Figure::legend(loc)` /
      `figureLegend()` collect entries across all axes and anchor in
      figure/canvas coordinates. Raster path reuses refactored
      `Renderer::collectLegendEntries`/`measureLegend`/`paintLegendBox`;
      vector path mirrors it via `VectorRenderer::collectVecLegendEntries`/
      `emitLegendBox`/`emitFigureLegend` (SVG/PDF/EPS/PGF parity).
      Python: `fig.legend(loc)` + `vp.figlegend(loc)`.
- [x] **Python `FuncAnimation`** — `vp.FuncAnimation(fig, func, frames,
      init_func, fargs, interval, blit, repeat, repeat_delay)` wraps
      `plot::FuncAnimation` with GIL-safe callbacks; int `frames` passes
      the index, iterables pass elements (mpl semantics), `fargs`
      appended to every call. `anim.save(path, writer=, fps=)` writes
      .apng/.gif/.mp4 via `Renderer::saveAnimation` (blit fast path
      included) and .html/.htm via `toJsHtml`; `to_jshtml()`/
      `to_html5_video()` exposed.
- [x] **Python reference lines + layout + rc** — `axhline/axvline/
      axline/axhspan/axvspan/hlines/vlines` on Axes and module level;
      `fig.tight_layout/constrained_layout/subplots_adjust/align_*labels`;
      `vp.tight_layout/subplots_adjust`; `vp.rc(group, **kwargs)`,
      `vp.rcdefaults()`, `vp.rc_context(**overrides)`, dict-like
      `vp.rcParams` (writes via `rc::set`, reads from a shadow map),
      `vp.style` submodule (use/available/context).
- [x] **Python bindings for remaining plot types** — `pie` (labels/
      colors/explode/donut), `stackplot`, `hexbin` (gridsize/cmap/mincnt),
      `quiver` (1D x,y + 2D u,v meshgrid expansion, scale/pivot),
      `streamplot` (density/arrows), `violinplot` (positions/vert/
      showmeans/showextrema), `hist2d` (bins int|[nx,ny]|auto, cmap),
      `eventplot` (orientation/colors/offsets/lengths/widths),
      `triplot`/`tripcolor`/`tricontour`/`tricontourf` (Delaunay or
      explicit (i,j,k) triangles, per-vertex or per-face values),
      `psd`/`csd`/`specgram`/`cohere`/`xcorr`/`acorr`/
      `magnitude_spectrum`/`phase_spectrum`/`angle_spectrum`
      (Fs/NFFT/noverlap/window/scale), `ecdf`, `spy`, `matshow`,
      `fill_betweenx` (broadcast x1/x2, FillPlot polygon).
- [x] **Python fmt-string `plot()`** — `ax.plot(y)` / `plot(y, "ro--")` /
      `plot(x, y)` / `plot(x, y, "ro--")` with mpl marker+color+linestyle
      spec parsing; explicit color/marker/linestyle/markersize/alpha/
      linewidth/label kwargs override the fmt string.
- [x] **Python tick/locator/formatter API** — `Locator`/`Formatter`
      class hierarchies exposed (Fixed/Multiple/MaxN/Auto/Log/AutoMinor
      locators; Fixed/Func/FormatStr/StrMethod/Scalar/Log/Eng/Percent
      formatters); `ax.xaxis`/`ax.yaxis` proxies with
      `set_{major,minor}_{locator,formatter}`; `set_xticks/set_yticks`
      (+labels), `xticks`/`yticks`, `tick_params`, `minorticks_on/off`,
      `grid(on, which, axis)`, `ticklabel_format`, `set_aspect`,
      `invert_xaxis/yaxis`, `margins` (new `Axes::margins` — autoscale
      padding replaces the hard-coded 5%), `axis("off"/"on"/"equal"/
      "auto"/"tight"/[xmin,xmax,ymin,ymax])`.
- [x] **Python 3D bindings** — `plot3D/scatter3D/plot_surface/
      plot_wireframe/bar3d/voxels/plot_trisurf/quiver3D` (+`3d` aliases),
      `view_init(elev, azim)`, `set_zlim/zlabel` etc. Each call manages
      an `Axes3DPlot` box + shared `Camera3D`; `Axes3DPlot` gained
      `setRange`/`setXLabel`/`setYLabel`/`setZLabel` (cache-invalidating,
      projected labels along the axis edges).
- [x] **Python text/annotation/legend/colorbar/axis helpers** —
      `ax.text`/`ax.annotate` (arrowstyle, connectionstyle, xytext,
      textcoords), module-level `text`/`annotate`, `fig.legend`/`ax.legend`
      kwargs passthrough, `fig.colorbar`/`vp.colorbar` with orientation/
      fraction/pad/shrink + rotated colorbar labels (`ColorbarStyle::label`).
- [x] **Python subplot/axes/sharing APIs** — `subplots(nrows, ncols,
      figsize, dpi, sharex, sharey)` ("all"/"none"/"row"/"col"/Axes),
      `subplot`, `subplot_mosaic` (string-grid or nested sequences),
      `add_subplot`/`add_axes`, `twinx`/`twiny`, `secondary_xaxis`/
      `secondary_yaxis` with `(fwd, inv)` transform functions.
- [x] **Python Patches API** — `vp.Path` (vertices+codes, `MOVETO`/
      `LINETO`/`CURVE3`/`CURVE4`/`CLOSEPOLY` constants, `bounds`,
      `contains_point`), `vp.patches` submodule (`Rectangle`, `Circle`,
      `Ellipse`, `Polygon`, `Wedge`, `FancyBboxPatch` with boxstyle,
      `FancyArrowPatch`, `PathPatch`) with mpl kwargs (fc/ec/lw/ls/alpha/
      hatch/fill/label), `ax.add_patch` (live handle into the axes'
      PatchCollection), `ax.add_collection`, `vp.collections`
      (`LineCollection`, `PolyCollection`, `PatchCollection`).
- [x] **Python date-axis API** — `vp.dates` submodule: `date2num`/
      `num2date`, `YearLocator`/`MonthLocator`/`WeekdayLocator`/
      `DayLocator`/`HourLocator`/`MinuteLocator`/`SecondLocator`/
      `MicrosecondLocator`/`AutoDateLocator`, `DateFormatter`/
      `AutoDateFormatter`/`ConciseDateFormatter`, `ax.plot_date`,
      `ax.xaxis_date`/`yaxis_date`, `fig.autofmt_xdate` (tick-label
      rotation + ha via `tickFont.rotation`); `tick_params` extended
      to full mpl kwargs (labelsize/labelrotation/colors/grid_*).
      numpy `datetime64`/`timedelta64` arrays auto-convert.
- [x] **Python plot types vol. 2** — `barbs`, `broken_barh`,
      `bar_label` (accepts the `BarContainer` returned by `bar`/`barh`;
      `bar`/`barh` now return mpl-style containers), `figimage`
      (scalar→cmap + RGB/RGBA arrays, vmin/vmax, xo/yo), `table`
      (rowLabels/colLabels/cellColours/rowColours/colColours; `loc`
      gains "center" in raster+vector), `wordcloud`, `network`
      (spring/circular/random/given layouts), `pcolorfast`,
      `errorbar3D`/`errorbar3d` (scalar-broadcast + asymmetric
      (2,N) errors), `text3D`, `loglog`/`semilogx`/`semilogy`.
- [x] **Python interactive widgets** — `vp.widgets` submodule:
      `Slider`/`RangeSlider`/`Button`/`CheckButtons`/`RadioButtons`/
      `TextBox`/`Cursor`/`MultiCursor`/`SpanSelector`/
      `RectangleSelector`/`EllipseSelector`/`PolygonSelector`/
      `SubplotTool`, mpl callback signatures (`on_changed(val)`/
      `(lo,hi)` tuple, `on_clicked(event)`/`(label)`), `set_val`/`val`,
      `fig.canvas` proxy with `mpl_connect`/`mpl_disconnect` and
      headless `dispatch(name, x, y, button, key, step)` for synthetic
      event injection; `fig.show()` runs the SDL event loop when a
      screen backend is available.
- [x] **Python polar + spines + cycler** — `projection=` kwarg on
      `subplots`/`subplot`/`add_subplot` (+ `subplot_kw` dict),
      `ax.set_rgrids`/`set_thetagrids`/`set_theta_zero_location`/
      `set_theta_direction`/`set_theta_offset`/`set_rmin`/`set_rmax`/
      `set_rorigin`/`set_rlabel_position` (new `Axes::rlabelPosition_`,
      default 22.5° per mpl, used by the polar renderer),
      `ax.spines['top'].set_visible()` dict proxy, `vp.cycler()`
      (color/c/fc, linestyle/ls, linewidth/lw, marker keys; `+` concat
      and `*` outer product) and `ax.set_prop_cycle` (cycler or kwargs).
- [x] **Python artist API surface** — `Line2D` full property set
      (set/get data, color, linestyle, linewidth, marker, markersize,
      markerface/edgecolor, markeredgewidth, markevery, alpha, label,
      visible, zorder, clip_on, generic `set(**kw)`/`update`); `Axes.set`
      (`xlim`/`ylim`/`xlabel`/`title`/`xscale`/`aspect`/`autoscale*_on`/
      `xmargin`/`xticks`/`frame_on`/`axison`/`axisbelow`, unknown props
      raise AttributeError) + `Axes.get`; module-level `vp.setp()`/
      `vp.getp()` (single or iterable, positional pairs, kwargs, query
      form); Axes getter batch (`get_xlim`/`get_xscale`/`get_aspect`/
      `get_autoscalex_on`/`get_frame_on`/`get_axisbelow`/…); Axes
      introspection (`lines`/`collections`/`patches`/`texts`/`images`/
      `artists`/`get_children()`/`get_legend_handles_labels()`/
      `findobj()`); artist handles for `text`/`quiver`/`imshow`/
      `stairs`/`pcolor`/`quiverkey`/`inset_axes`/`indicate_inset`/
      `indicate_inset_zoom` (Text/Quiver/Artist/AxesImage/
      InsetIndicator wrappers keep the figure alive); Spine
      `set_position`/`set_bounds`/`set_color`/`set_linewidth`/
      `set_linestyle`/`set_dashes`; `ax.relim`/`autoscale_view`/
      `autoscale`/`set_autoscalex_on`; `vp.xkcd()` context manager +
      `ax.set_sketch_params`; PathCollection artist methods.
- [x] **`vp.ticker` + axis tick introspection** — full mpl `ticker`
      submodule (`Locator`/`Formatter` hierarchies incl. `AutoLocator`,
      `AutoMinorLocator`, `IndexLocator`, `SymmetricalLogLocator`,
      `LogitLocator`, `NullLocator`, `OldAutoLocator`, `Base`,
      `TickHelper`; `StrMethodFormatter`, `LogFormatterExponent`,
      `LogFormatterSciNotation`, `LogitFormatter`, `OldScalarFormatter`,
      `NullFormatter`); mpl-exact locator semantics (FixedLocator
      returns out-of-range values, MultipleLocator 10% range expansion,
      EngFormatter `%g` mantissa); `Axis` proxies expose
      `get_major_locator`/`get_major_formatter`/`get_majorticklocs`/
      `get_minorticklocs`/`get_ticklabels`/`get_scale`/`get_label`/
      `get_view_interval`, `Axes.get_xticks`/`get_yticks`/
      `get_xticklabels`/`get_xaxis`/`get_yaxis`, `set_tick_params`,
      `ticklabel_format`, `minorticks_on/off`. C++: `Scale.hpp`
      locator/formatter classes + `TickLayout` mpl `get_tick_space`
      (nbins = clip(axis_pt/label_pt, min_n_ticks-1, 9)).
- [x] **Axes introspection + artist/container APIs** — `ax.dataLim`/
      `viewLim`/`bbox` (`Bbox` class with `p0/p1/x0/x1/y0/y1/width/
      height/bounds/extents`, `transformed`, iteration, `__array__`),
      `tightbbox`, `get_window_extent`, `get_position`/`set_position`
      (Bbox or [l,b,w,h], `which`), `get_subplotspec`/`set_subplotspec`,
      `get_aspect`/`set_aspect` (+`box_aspect`, `anchor`, `adjustable`,
      mpl `applyAspect` incl. 'datalim' mode), `margins`/`set_xmargin`/
      `set_ymargin`, `ignore_existing_data_limits`, eager `dataLim`
      (artist adds merge limits, mpl `update_datalim`), `relim`,
      `autoscale`/`autoscale_view` preserve inverted limits;
      artist state `get/set_visible`/`set_zorder`/`get_label`/
      `set_figure`/`is_transform_set`/`can_pan`/`can_zoom`/`hitlist`;
      `add_artist`/`add_line`/`add_collection`/`add_table`/`add_image`
      re-attach handles (`plots_` is `shared_ptr`), `containers`/
      `tables` lists populated by `bar`/`barh`/`table`; `start_pan`/
      `drag_pan`/`end_pan` + `get_navigate`/`get_navigate_mode`;
      `sharex`/`sharey`/`sharez` kwargs on `add_subplot`/`subplots`.
- [x] **Figure object APIs + transforms** — `fig.transFigure`/
      `dpi_scale_trans` (real `Transform` objects), `fig.text`/`figtext`
      (figure-level text storage rendered by both backends),
      `get_axes`/`get_ax`/`clf`/`clear`/`gca`/`sca`/`delaxes`,
      `subplotpars` (`SubplotParams` handle: left/right/bottom/top/
      wspace/hspace + `update`), `fig.subplots(nrows, ncols, ...)`,
      `subfigures`/`add_subfigure` (non-owning `PyFigure` views;
      `fig.axes` includes subfigure axes like mpl), `get_layout_engine`/
      `set_layout_engine`/`get_constrained_layout_pads`,
      `fig.set_size_inches`/`get_size_inches`/`get_dpi`/`set_dpi`/
      `set_facecolor`/`get_facecolor`/`set_frameon`/`get_frameon`,
      `figimage` returning an artist handle, `sci`/`gci` mappable
      tracking, `align_labels`; module-level `gca`/`gcf`/`sca`/`draw`/
      `close(fig|'all')`/`findobj`.
- [x] **`vp.image` module + image kwargs** — `imread`/`imsave`
      (PNG decode via libpng simplified API, WebP via libwebp,
      float32 0-1 RGBA output like mpl), `figimage`, `thumbnail`;
      `imshow` gains `extent`/`resample` kwargs and accepts
      (H,W,3|4) RGB/RGBA arrays (RGBA texture path in
      `HeatmapRenderer` + shader branch; `get_array`/`set_array`/
      `get_size` RGBA-aware); mpl-verbatim default extent
      `(-0.5, w-0.5, h-0.5, -0.5)` with `origin='upper'` preserving
      the inverted y-axis through autoscale (`get_extent` returns
      mpl order `(l, r, b, t)`).
- [x] **`vp.font_manager` + `FontProperties`** — `FontProperties`
      (family/style/variant/weight/stretch/size/file + mpl aliases
      `fontfamily`/`fontstyle`/…, fontconfig pattern parsing and
      `get_fontconfig_pattern`, `copy`/`__eq__`/`__hash__`/`__repr__`,
      positional family arg), `FontEntry`, `FontManager`
      (`ttflist`/`afmlist`, `addfont`, `findfont`,
      `findSystemFonts`, `defaultFont`, `score_*`), global
      `fontManager`, FreeType name-table metadata for system-font
      scanning (`volcano_text::fontMetadata`); font kwargs
      (`fontfamily`/`fontstyle`/`fontweight`/`fontsize`/`fontproperties`
      incl. fontconfig strings + `fontdict`) on text/annotate/title/
      labels/suptitle/fig.text/legend/tick_params; mpl `Text` artist
      surface on `PyText`/`PyAnnotation`/`PyTitle`/`PyTickLabel`
      (set/get fontfamily/style/weight/size/variant/stretch/
      fontproperties/name, usetex/wrap/rotation_mode/linespacing/
      multialignment/backgroundcolor/parse_math/antialiased);
      `annotate` returns an `Annotation` handle; `ax.title`,
      `get_suptitle`/`get_supxlabel`/`get_supylabel` return Text-like
      handles. Renderer resolves DejaVu faces by family/style/weight
      (`TextRenderer::faceFor`); **font sizes are mpl points** — all
      raster+vector text/tick/spine/pad measurements scale by
      `figDpi/72` at draw time (figure DPI resolved from the owning
      figure, not the axes style).
- [x] **Figure registry + pyplot state** — `figure(num=)` reuses
      numbered/labeled figures like mpl's pyplot; `fignum_exists`,
      `get_fignums` (sorted), `get_figlabels`, `close(num|fig|'all')`,
      `gcf`/`gca`/`sca`, `axes()`/`delaxes` + mpl subplot reuse
      (`add_subplot(111)` returns the existing axes), `cla`/`clear()`
      full reset, `ion`/`ioff`/`isinteractive`, `connect`/`disconnect`
      event stubs, `fig.number`/`label`; `atexit` cleanup of
      Python-object globals (registry, callbacks, rc shadow) so
      interpreter shutdown doesn't touch finalized objects.
- [x] **Complete pyplot module surface** — ~60 module-level forwards
      to `gca()`: bar/barh/hist/imshow/contour/contourf/clabel/clim/
      margins/locator_params/fill/fill_between/fill_betweenx/errorbar/
      semilogx/semilogy/loglog/step/stairs/stem/pie/stackplot/
      annotate/arrow/axline/autoscale/boxplot/bxp/hexbin/hist2d/
      matshow/pcolor/pcolormesh/quiver/quiverkey/spy/eventplot/
      violinplot/ecdf/psd/csd/cohere/specgram/magnitude_spectrum/
      phase_spectrum/angle_spectrum/xcorr/acorr/plot_date/bar_label/
      triplot/tripcolor/tricontour/tricontourf/broken_barh/polar/
      subplot2grid/subplots_adjust/grid/axis/minorticks_on-off/…
- [x] **Generic Artist API sweep** — `defArtistAPI<T>` installs the
      shared mpl `Artist` surface on every handle class (Figure, Axes,
      Line2D, PathCollection, AxesImage, Annotation, Text, Title,
      Legend, Colorbar, ContourSet, containers, ticks, spines, …):
      gid/url/picker/snap/sketch/path_effects/rasterized/animated/
      alpha/visible/zorder/mouseover/in_layout properties, figure/axes
      wiring, transforms, clip APIs, get_window_extent/get_tightbbox/
      get_datalim, stale/callback machinery (add_callback/remove_callback/
      pchanged/stale_callback), contains/pick, findobj, unit
      conversion, sticky_edges, draw, remove, `set`/`update`/
      `update_from`/`properties`/`set_props`. Properties installed via
      `PyProperty_Type` + `PyInstanceMethod_New` (this pybind has no
      `py::property` helper); native methods always win (`hasattr`
      check). mpl zorder defaults: lines 2, collections/patches 1,
      images 0.
- [x] **`vp.gridspec` submodule** — constructible `GridSpec` (nrows,
      ncols, left/right/bottom/top/wspace/hspace/width_ratios/
      height_ratios), `gs[i,j]` + row/col slicing → `SubplotSpec`,
      `subgridspec`/`GridSpecFromSubplotSpec` with parent keepalive
      chains, `fig.add_subplot`/`fig.add_gridspec`/module `subplot`/
      `axes` accepting `SubplotSpec`; nested grids fill their parent
      cell (verified subpixel-equal to mpl). `Figure::adoptGrid`
      retains foreign grids whose SubplotSpecs hold raw pointers.
- [x] **ScalarMappable + collection/image depth + return-handle
      parity** — `defMappableAPI` (changed/colorbar/colorizer/norm/
      cmap/array forwarding) and `defCollectionAPI` (offsets, sizes,
      facecolors/facecolor/fc, edgecolors/edgecolor/ec with 'face'
      resolution, linewidths/lw, linestyles/ls, paths, offset
      transforms, hatch/cap/join/antialiased aliases) on
      PathCollection/Collection/AxesImage/ContourSet; AxesImage
      extent/origin/interpolation/write_png. **All Axes plotting
      methods return mpl-shaped values**: hist→(n, bins, BarContainer),
      hist2d→(h, xedges, yedges, image), psd/csd/cohere→(P, freqs),
      specgram→(spec, freqs, t, im), spectra→(spec, freqs, line),
      xcorr/acorr→(lags, c, line, b), errorbar/stem/fill_between/
      fill_betweenx/pcolormesh/pcolor/hexbin/spy/matshow/ecdf/barbs/
      streamplot/tripcolor/tricontour*/axhline/axvline/axhspan/axvspan/
      hlines/vlines/axline→artist handles, step/stairs/plot_date→[h],
      eventplot/bar_label→list, pie→(wedges, texts), triplot→(line,
      markers), boxplot/bxp/violinplot→component dicts, clabel→[Text].
      Public `ensureComputed()` on Psd/Csd/Cohere/Specgram/XCorr/
      Hist/Hist2D/Spectrum plots makes derived arrays available before
      the first render. `XCorrPlot` rewritten to mpl semantics
      (np.correlate 'full', normed = L2-norm division); bindings raise
      mpl's `maxlags must be … < Nx` ValueError. Scatter edge colors
      (`edgecolors=`/`set_edgecolor`) now stroke marker outlines on
      raster+vector paths; `set_sizes`/`get_sizes`/`set_linewidths`
      convert mpl pt²/pt ↔ native px. `HistConfig.color` default now
      opaque C0 like mpl.
- [x] **`vp.tri` submodule** — `Triangulation` (x, y, triangles, mask;
      `get_masked_triangles`, `set_mask`, `calculate_plane_coefficients`),
      `TrapezoidMapTriFinder`, `LinearTriInterpolator` (triangle +
      gradient evaluation), `UniformTriRefiner` (4-way midpoint split
      honoring masks, `refine_field` interpolating vertex values),
      `TriAnalyzer` (`scale_factors`, `get_flat_tri_mask`,
      `circle_ratios`); all four `tri*` plotting methods accept a
      `Triangulation` object first arg plus `triangles=` kwarg; mpl
      per-vertex-vs-per-face precedence on ambiguous `tripcolor` C.
- [x] **Named containers/collections** — `ErrorbarContainer`
      (`.lines` = (line, caplines tuple, barlinecols tuple),
      `.has_xerr`/`has_yerr`), `StemContainer` (`.markerline`/
      `.stemlines`/`.baseline`), `EventCollection` (per-row handle
      with positions/orientation/linelengths/linewidths/colors),
      `QuadMesh` (pcolormesh/pcolor return, `get_array` preserving
      2D shape), `Container` base API (`remove`, `get_children`,
      `get_label`, `__iter__`/`__len__`/`__getitem__` unpacking);
      `vp.container` submodule + `vp.collections` aliases; channel-
      tagged artist handles route style calls to the right config
      field; `pcolormesh` gains mpl's `[X, Y,] C` signature;
      mpl `errorbar.capsize` rcParam default (0 → no caps).
- [x] **`vp.transforms` depth** — `ScaledTranslation` corrected to
      mpl's pure-translation semantics, `AffineDeltaTransform`,
      `BboxTransform`/`BboxTransformTo`/`BboxTransformFrom`,
      `TransformedBbox`/`TransformedPath`
      (`get_transformed_path_and_affine`/`get_fully_transformed_path`/
      `get_transformed_points_and_affine`), `composite_transform_factory`
      (`CompositeAffine2D`), `nonsingular`, `offset_copy(trans, fig,
      x, y, units)` mpl signature (inches default); `Transform`
      gains `input_dims`/`output_dims`/`has_inverse`/`get_affine`/
      `transform_path`/`transform_bbox`/`frozen`/`depth`; `Affine2D`
      gains `from_values`/`set_matrix`/`clear`/`to_values`; `Path`
      re-exported on `vp.transforms`.
- [x] **`vp.dates` depth** — `RRuleLocator` (dateutil `rrule`/
      `rrulewrapper` driven, `tick_values` accepting datetimes or
      day numbers), `drange`, `ConciseDateConverter` + `DateConverter`
      axisInfo, weekday constants (`MO`…`SU`); `AutoDateLocator`
      rewritten to mpl's exact algorithm (interval_multiples `by*`-
      set mode for hourly/minutely/secondly, `dtstart = vmin −
      relativedelta` anchoring, mpl `maxticks`, YearLocator
      `base.ge(vmax.year+1)` overshoot, `[vmin,vmax]` fallback when
      a rule produces nothing); all date locators gain mpl `by*`/
      `tz` kwargs; `tickValuesD` double-precision virtual (float day
      numbers can't hold sub-day precision); mpl `date2num`
      arithmetic (integer epoch-seconds + fractional term);
      `ConciseDateFormatter` rewritten to mpl's `format_ticks`
      (level detection, zero_formats, offset suppression).
- [x] **`vp.offsetbox` submodule** — `AnchoredText`, `AnchoredSizeBar`
      (mpl_toolkits compat), `AnchoredOffsetbox`, `TextArea`,
      `HPacker`/`VPacker`/`PackerBase` (mpl child/align/pad/sep
      signatures); `ax.add_artist` accepts the offsetbox handles;
      `detached` flags on native `AnchoredText`/`SizeBar` for
      `remove()`; mpl anchored-frame defaults (opaque white face,
      opaque black edge) stroked in raster + vector renderers.
      **Point-unit sweep**: `pt2px` helper converts mpl pt kwargs
      (linewidth/markersize/elinewidth/capsize/stem sizes,
      axh*/axv* lw, artist `set_linewidth`/`set_markersize`,
      collection `set_linewidths`) to native px at figure DPI;
      `errorbar` gains `fmt` (default `''` = line+bars, no markers)
      and `ecolor=None` → line-color fallback; errorbar caps render
      as `_`/`|` marker geometry (px-exact, raster + vector) instead
      of data-space segments.
- [x] **`vp.scale` submodule** — `ScaleBase`/`LinearScale`/`LogScale`/
      `SymmetricalLogScale`/`AsinhScale`/`LogitScale`/`FuncScale`/
      `FuncScaleLog` classes (class-level `name` attrs), transform
      classes + factories, `scale_factory`/`register_scale`/
      `get_scale_names`; `set_xscale`/`set_yscale`/`Axes.set(xscale=)`
      accept names **and** scale objects; log `base=`/`nonpositive=`
      kwargs (native `AxisScale::log(base, mask)`, base-aware
      `scaleTicks` + `LogLocator`/`LogFormatter` plumbing — `2^k`
      labels render correctly); symlog transform rewritten to mpl's
      formula (`linscale_adj = linscale/(1 − 1/base)`, `param3` base
      through the GLSL `vec4` scale params); `SymmetricalLogLocator`
      rewritten to mpl's a/b/c-section algorithm (decades + lone 0,
      stride from numticks=15, `subs` support); symlog axes default
      to `LogFormatterSciNotation`-style `±base^k` labels including
      negative decades (`-10^{0}`).
- [x] **`vp.units` + `vp.category` submodules** — `units.registry`
      dict, `ConversionInterface`/`AxisInfo`/`DecimalConverter`,
      `StrCategoryConverter`/`StrCategoryLocator`/
      `StrCategoryFormatter`, `UnitData`; `Axis.units`/`set_units`/
      `convert_units`/`update_units`; categorical string data routed
      through `toFloats` (`xCategoryIndex`/`setXCategories` installs
      `FixedLocator`+`FixedFormatter` like mpl's
      `StrCategoryConverter.axisinfo`). Static py-object registries
      are deliberately leaked to avoid interpreter-shutdown decref
      crashes.
- [x] **`vp.animation` submodule** — `Animation`/`TimedAnimation`/
      `FuncAnimation`/`ArtistAnimation` (mpl ctor kwargs, frame
      iteration via `__iter__`/`new_frame_seq`, `_draw_next_frame`
      artist visibility/`animated` sync back to native flags),
      `MovieWriter`/`AbstractMovieWriter`/`FileMovieWriter`/
      `PillowWriter`/`FFMpegWriter`/`AVConvWriter`/`HTMLWriter` +
      `writers` registry (`is_available`, `list`, `get`, `register`,
      `reset_available_writers`), `anim.save(writer=…, fps=…)` with
      mpl writer-selection + Pillow fallback; reuses the native
      `plot::Animation`/`MovieWriter` GIF/APNG/HTML pipeline.
- [x] **`vp.sankey` submodule** — `Sankey` with mpl ctor kwargs
      (`ax`, `scale`, `unit`, `format`, `gap`, `radius`, `shoulder`,
      `offset`, `head_angle`, `margin`, `tolerance`), `UP`/`DOWN`/
      `LEFT`/`RIGHT` constants, `add` (flows/labels/orientations/
      patchlabel kwargs), `finish()` returning mpl-shaped diagram
      dicts (`patch`, `flows`, `angles` (None for sub-tolerance
      flows), `tips`, `text`, `texts`, ribbon handles) backed by
      native `Sankey::Diagram` (deferred text-index resolution for
      pointer stability).
- [x] **`vp.colorbar` submodule** — `Colorbar`/`ColorbarBase`
      classes, `make_axes`/`make_axes_gridspec`/`colorbar_factory`,
      callable-module shim (`vp.colorbar(m)` → `gcf().colorbar(m)`);
      `Figure.colorbar` gains mpl kwargs (`location`, `orientation`,
      `fraction`, `pad`, `shrink`, `aspect`, `label`, `extend`,
      `extendfrac`, `extendrect`, `spacing`, `drawedges`, `ticks`,
      `format`, `ticklocation`, `alpha`, `cmap`, `norm`, `cax`,
      `use_gridspec`); `Colorbar` methods `set_label`/`set_ticks`/
      `set_ticklabels`/`get_ticks`/`minorticks_on`/`minorticks_off`/
      `set_alpha`/`update_normal`/`update_ticks`/`add_lines`/
      `remove` + locator/formatter/`long_axis`/`solids`/`lines`/
      `patch`/`outline`/`divider` properties; `cax` support via
      `ColorbarStyle::caxMode` (strip fills the axes rect, no
      chrome); raster + vector renderers honor `format`/`alpha`/
      `extendfrac`/`extendrect`/minor ticks.
- [x] **`vp.projections` package** — `ProjectionRegistry`,
      `register_projection`, `get_projection_class`/`get_projection_names`,
      `projection_registry`; submodules `polar` + `geo` (mpl layout)
      with mpl class names (`PolarAxes`, `GeoAxes`/`AitoffAxes`/
      `HammerAxes`/`LambertAxes`/`MollweideAxes`, `ThetaFormatter`,
      `ThetaLocator`, `RadialLocator`, `ThetaAxis`/`RadialAxis`/
      `ThetaTick`/`RadialTick`, `PolarTransform`/`PolarAffine`/
      `InvertedPolarTransform`, geo `Spine`, …);
      `projection=` accepts names **and** projection classes at
      `add_subplot`/`add_axes`/`subplots`/`plt.subplot`/`plt.axes`/
      `add_subplot_mosaic`-adjacent sites, returning subclass-typed
      axes (`projection='polar'` → `PolarAxes` with `set_thetagrids`/
      `set_thetamin/max`/`set_theta_offset/direction`/`set_rgrids`/
      `set_rlabel_position`/`set_rticks`/`set_rmin/max`/`set_rorigin`);
      geo axes get `set_longitude_grid`/`set_latitude_grid`/
      `set_longitude_grid_ends`; `Locators`/`Formatters` gained mpl
      `TickHelper` surface (`set_axis`/`axis`/`set_view_interval`/
      `set_data_interval`/`set_bounds`/`tick_values`/`__call__`/
      `raise_if_exceeds`). Native `Renderer::drawGeoGrid` draws the
      elliptical geo frame, graticules, equator lon labels and
      left-limb lat labels with mpl's `°` formatting; polar renderer
      honors theta offset/direction and radial label angle.
- [x] **`vp.legend` + `vp.legend_handler` submodules** — `Legend`
      gains mpl surface: `get_lines`/`get_patches`/`get_texts`/
      `legend_handles`/`legendHandles`/`get_legend_handlers_labels`/
      `get_frame` (frame proxy with face/edge color, alpha, lw),
      `get_window_extent`, `set_bbox_to_anchor`, `get_bbox_to_anchor`,
      `set_loc`/`get_loc`, `loc`, `set_draggable`/`get_draggable`,
      `get_children`, `update_from_first_child`,
      `get/set/update_default_handler_map` classmethods,
      `get_legend_handler_map`/`get_legend_handler`/
      `get_legend_handler_map` instance access; `handler_map=` kwarg
      in `legend()`/`fig.legend()` resolves `PyLine2D`/`PyPatch`/
      collection/container keys; `vp.legend_handler` provides
      `HandlerBase`/`HandlerNpoints`/`HandlerNpointsYoffsets`/
      `HandlerLine2D`/`HandlerStepPatch`/`HandlerPatch`/
      `HandlerRegularPolyCollection`/`HandlerCircleCollection`/
      `HandlerPathCollection`/`HandlerLineCollection`/`HandlerTuple`/
      `HandlerPolyCollection`/`HandlerStem`/`HandlerErrorbar`/
      `HandlerLine2DCompound`/`HandlerErrorbar` with mpl ctor
      signatures (`xpad`/`ypad`/`numpoints`/`marker_pad`/`update_func`/
      `ndivide`/`pad`/`autoscale`) — callable-module shim preserves
      the old `vp.legend(...)` pyplot forward.
- [x] **`vp.spines` submodule** — `Spines` `MutableMapping` bound to
      `ax.spines` (`__getitem__`/`__setitem__`/`__delitem__`/`__iter__`/
      `__len__`, tuple keys → `SpinesProxy`, slices `['top':'right']`,
      `from_dict`/`get_bounds`, custom side names via `__setitem__`);
      `Spine` gains mpl ctor signature `(axes, spine_type, path)`, 
      `set_position`/`get_position` accepting `('data',v)`/`('axes',v)`/
      `'zero'`/`'center'`/floats, `set_bounds`/`get_bounds`,
      `set_patch_arc`/`set_patch_line`/`set_patch_circle`/`get_patch_transform`,
      `set_path`/`get_path`, `get_spine_transform`, `linear_width`,
      `get_window_extent`, `get_extents`, `set_figure`/`get_figure`,
      `get_children`, `side` alias + `spine_type`; native
      `Axes::SpineSpec` stores arc specs (`theta1`/`theta2`) and the
      spine renderer strokes arc spines (`set_patch_arc` → curved
      spine); non-standard sides degrade gracefully.
- [x] **`vp.mlab` submodule** — `detrend`/`detrend_mean`/
      `detrend_linear`/`detrend_none`, `apply_window`, `stride_windows`,
      `window_none`/`window_hanning`, `psd`/`csd`/`cohere`/
      `magnitude_spectrum`/`angle_spectrum`/`phase_spectrum`/
      `complex_spectrum`/`specgram` (mpl `_spectral_helper` semantics:
      same_freqs validation, single-sided FFT scaling ×2 (DC/Nyquist
      exempt), pad_to zero-padding via Bluestein FFT for arbitrary n,
      detrend callables + sequence windows + NFFT > signal tiling —
      verified numerically against `mpl.mlab`), `prctile`,
      `bivariate_normal`, `GaussianKDE` (scipy-compatible
      scott/silverman bw + callable bw — verified against
      `scipy.stats.gaussian_kde`), `PCA`, `rk4`, `griddata`
      (linear + nearest over `vp.tri` interpolators).
- [x] **`vp.markers` submodule** — `MarkerStyle` with mpl ctor
      `(marker, fillstyle, transform)`; all string markers
      `.` `,` `o` `v` `^` `<` `>` `1`–`4` `8` `s` `p` `*` `h` `H`
      `+` `x` `D` `d` `|` `_` `P` `X` (`,` = `MarkerStyle::Pixel=41`
      with GPU SDF support), `None`/`'none'`/`''`/`' '` aliases,
      integer codes 0–13, tuple specs `(numsides, symstyle[, angle])`
      (symstyle 0/1/2 only like mpl), custom `Path` markers;
      `get_marker`/`set_marker`/`_set_marker`, `get_fillstyle`/
      `set_fillstyle` (full/left/right/top/bottom/none),
      `get_path`/`get_alt_path`, `get_transform`/`get_alt_transform`,
      `get_user_transform`/`set_user_transform`, `is_filled`,
      `get_snap_threshold`/`set_snap_threshold`, `get_capstyle`/
      `set_capstyle`, `get_joinstyle`/`set_joinstyle`, `transformed`,
      `rotated`, `scaled`, `filled`, `copy`. Geometry verified
      **exactly** against mpl: path vertices/codes for all 26
      markers (mpl's cubic-Bézier unit circle with `MAGIC`/`SQH`/
      `M45` constants, CLOSEPOLY repeating the first vertex),
      transform/path scale separation (`scale(0.5)` lives in the
      transform), and 75 half-fill combos (alt paths rotated
      0/90/180/270° for right/top/left/bottom).
- [x] **`vp.pyplot` submodule** — canonical `import volcanoplot.pyplot
      as plt`; mirrors the full top-level command surface (290 names),
      `get_plot_commands` introspection, `switch_backend`, `ion`/`ioff`
      state, `figure()`/`gcf()`/`gca()`/`savefig()`/`show()`/all
      plotting commands.
- [x] **`vp.patches` depth + style classes** — `BoxStyle`/
      `ArrowStyle`/`ConnectionStyle` style objects with mpl factory
      ctors (`BoxStyle("round", pad=0.3)`, `ArrowStyle("-|>")`,
      `ConnectionStyle("arc3", rad=..)`) accepted wherever style
      strings are (text `bbox=`, `FancyBboxPatch`, `FancyArrowPatch`);
      nested style aliases (`BoxStyle.Round`, `ArrowStyle.Fancy`, ...)
      under private pybind names to avoid patch-ctor collisions.
      `FancyArrowPatch` kwargs (`arrowstyle`/`connectionstyle`/
      `shrinkA`/`shrinkB`/`path`), `Shadow` offsets, `Annulus`,
      `RegularPolygon`, `StepPatch`, `ConnectionPatch`,
      `Patch.set_patch_transform`-style transform composition.
- [x] **`vp.hatch` module + `hatch=` kwargs** — `vp.hatch` submodule
      with mpl pattern classes (`HorizontalHatch`, `VerticalHatch`,
      `NorthEastHatch`, `SouthEastHatch`, `Circles`, `SmallCircles`,
      `LargeCircles`, `SmallFilledCircles`, `Stars`) and
      `hatch.get_path(pattern, density)`; `hatch=` on `bar`/`barh`/
      `hist`/`fill`/`fill_between`/`pie` via clipped `PolyCollection`
      overlays and `hatches=` on `contourf` natively (shared anchor
      sweep across fragments). mpl hatch color rules honored:
      edgecolor → hatch color, 'face' → fill color (`fill_between`),
      'none' → `hatch.color` (black) — bar/hist/fill/pie get black
      hatches by default; hatches draw even on transparent fills;
      explicit collection colors are no longer overwritten by the
      prop cycler (`Collection::applyCycleProps` fills only empty
      color lists); fresh `PolyCollection`s passed to
      `ax.add_collection` are adopted into axes ownership.
- [x] **`vp.path` module** — `vp.path` submodule + full `Path` API:
      code constants/mappings (`code_type`, `NUM_VERTICES_FOR_CODE`),
      `unit_circle`/`unit_circle_righthalf` (mpl's 26-vertex CURVE4
      circle), `unit_rectangle`, `unit_regular_polygon`,
      `unit_regular_star` (inner=0.5), `unit_regular_asterisk`,
      `arc`/`wedge`/`ellipse` (Masionobe control splines, mpl
      width/height convention), `hatch`, `make_compound_path`,
      `interpolated`/`transformed`, `get_extents`/`contains_point`/
      `contains_points`/`intersects_path`, `to_polygons`,
      `cleaned`/`clip_to_bbox`, `__eq__`/`__repr__`/`__len__`/
      `__deepcopy__`; `transforms.Path` re-export. Vertex/codes
      verified exactly against mpl for all factories.
- [x] **`vp.layout_engine` module** — `LayoutEngine`,
      `TightLayoutEngine`, `ConstrainedLayoutEngine`,
      `PlaceHolderLayoutEngine`; `figure(layout=)` accepts
      `"tight"`/`"constrained"`/`"compressed"`/`"none"` or engine
      objects; `set_layout_engine`/`get_layout_engine`,
      `set_tight_layout`/`set_constrained_layout` (mpl bool/dict
      semantics), `tight_layout(pad, h_pad, w_pad, rect)`,
      `get_constrained_layout_pads`, `do_constrained_layout`,
      `get_tight_layout_figure`, `get_subplotspec_list`,
      `set_axes_locator`. Native `Figure` state (`tightRect`,
      `tightPadScale`, `constrainedWSpace`/`HSpace`, `compress`)
      drives `computeTightMargins`: decoration-aware margins use the
      real title/suptitle font sizes, `rect` bounds, and mpl-style
      inter-axes spacing (row gap for titles/xlabels, column gap for
      ylabels).
