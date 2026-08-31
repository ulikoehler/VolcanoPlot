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
- [x] `imshow` (HeatmapPlot — stub)
- [x] `matshow` (MatshowPlot — matrix display with row-0-at-top convention,
      colormap coloring, NaN cell skipping, explicit value range, nearest-
      neighbor display via FillRenderer triangle tessellation)
- [x] `pcolor`, `pcolormesh` (PcolormeshPlot — rectangular cells with per-cell
      colormap colors via FillRenderer, non-uniform cell edges, NaN skipping,
      explicit value range support)
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
- [x] GPU-side function evaluation (FunctionPlot — CPU fallback, GPU compute TODO)
- [x] GPU-side KDE (KDEPlot — CPU fallback, GPU compute TODO)
- [x] GPU autoscale (parallel min/max reduce)
- [x] Infinite zoom (FunctionPlot::reevaluate — framework in place)
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
- [ ] `petroff10`
- [ ] Composable style lists
- [ ] XKCD sketch style (`plt.xkcd()` context manager)

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
- [ ] `berlin`, `managua`, `vanimo`

### 3.4 Cyclic
- [x] `twilight`, `twilight_shifted`, `hsv`

### 3.5 Qualitative
- [x] `Pastel1`, `Pastel2`, `Paired`, `Accent`
- [ ] `okabe_ito` (added in matplotlib 3.11)
- [x] `Dark2`, `Set1`, `Set2`, `Set3`
- [x] `tab10`, `tab20`, `tab20b`, `tab20c`

### 3.6 Miscellaneous
- [x] `turbo`, `jet`
- [x] `flag`, `prism`, `ocean`, `gist_earth`, `terrain`, `gist_stern`
- [x] `gnuplot`, `gnuplot2`, `CMRmap`, `cubehelix`, `brg`
- [x] `gist_rainbow`, `rainbow`, `nipy_spectral`, `gist_ncar`

### 3.7 Colormap API
- [ ] `LinearSegmentedColormap`
- [ ] `ListedColormap`
- [x] Reversed colormaps (`name + '_r'`)
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
- [x] `colorbar` (vertical color strip + tick labels, right of axes)
- [ ] Projections: `rectilinear`, `polar`, `aitoff`, `hammer`, `lambert`, `mollweide`, `3d`
- [ ] Scales: `linear`, `log`, `symlog`, `logit`, `asinh`, `function`, `functionlog`, `mercator`
- [ ] Polar: `set_rgrids`, `set_thetagrids`, `set_theta_offset`, `set_theta_direction`
- [ ] Aspect ratio, equal axis, invert axis, set limits, autoscale (autoscale: [x])

---

## 5. Text and Annotations
- [x] `title`, `suptitle`, `figtext`, `xlabel`, `ylabel` (data model: [x], rendering: [x] — glyb bitmap atlas)
- [ ] `text`, `figtext`, `annotate`
- [x] `tick_params`, `set_xticklabels`, `set_yticklabels` (auto tick labels: [x])
- [ ] `Annotation` with `arrowprops` / `FancyArrowPatch`
- [ ] Coordinate systems: `data`, `axes`, `figure`, `display`, `offset points`
- [ ] MathText (TeX-like subset): sub/sup, fractions, radicals, Greek, accents, calligraphic, etc.
- [ ] MathText fontsets: `dejavusans` (default), `dejavuserif`, `cm`, `stix`, `stixsans`
- [ ] `text.usetex` full LaTeX rendering (requires external TeX)
- [x] Font properties: family, weight, style, size, color (FontProperties struct)
- [ ] Font properties: rotation, alignment

### 5.1 Text Rendering (glyb bitmap atlas)
- [x] Hole bridging for glyphs with holes (O, A, B, etc.) — resolved by
      switching from vectorized triangulation to glyb's FreeType span
      rasterizer, which correctly handles the even-odd fill rule.
- [x] HarfBuzz text shaping (kerning, ligatures, RTL/CJK ready)
- [x] Y-axis label rotation — 90° rotation via per-glyph vertex transform
      in TextRenderer::draw() (rotation parameter in radians).
- [ ] Font rotation and alignment properties (FontProperties::rotation,
      horizontal/vertical alignment)
- [ ] Text clipping to axes rect for `text()` / `annotate()` (data-space text)
- [ ] Multi-line text (newline support)
- [ ] Text layout engine (word wrap, justified text for legends/annotations)
- [ ] Subpixel positioning (currently snapped to integer pixel coords)
- [ ] Font subsetting for large character sets (currently loads all ASCII)
- [ ] CJK / RTL text shaping (HarfBuzz integrated, needs testing with CJK fonts)

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
- [x] Single-letter shorthands: `bgrcmykw`
- [x] Named colors (X11/CSS4, `tab:...`, `C0`–`C9` cycle)
- [x] Hex (`#RGB`, `#RRGGBB`, `#RGBA`, `#RRGGBBAA`)
- [x] RGB/RGBA tuples (0–1 float) (Color struct)
- [x] Grayscale string (`'0.5'`)
- [x] `CN` index colors (`C0`–`C9`)
- [ ] `xkcd:` color names
- [x] Color cycles / `axes.prop_cycle` (ColorCycle with tab10 palette)
- [ ] `cycler` library integration (color + linestyle + marker + linewidth cycling)
- [ ] Normalization:
  - [x] `Normalize`, `NoNorm`
  - [x] `LogNorm`, `PowerNorm`, `SymLogNorm`, `AsinhNorm`
  - [x] `BoundaryNorm`, `CenteredNorm`, `TwoSlopeNorm`, `FuncNorm`, `MultiNorm`
  - [x] Norm integration in colormapped plots (pcolormesh, matshow, hexbin,
        hist2d, tripcolor, pcolorfast, trisurf, specgram)
  - [x] Norm autoscale from data (vmin/vmax auto-computed if unset)
  - [x] Factory functions (`norms::linear`, `norms::log`, `norms::power`, etc.)
  - [ ] `Colormap` `bad`, `under`, `over` colors

---

## 8. Legend Features
- [x] `legend` with auto or explicit `handles`/`labels` (data model: LegendStyle struct)
- [x] Legend rendering (colored markers + text labels, semi-transparent background + border)
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
- [x] `ax.grid` (major/minor, x/y/both, color, linestyle, linewidth) (AxisStyle fields + GridRenderer)
- [x] `tick_params` (auto tick label rendering: [x])
- [x] Auto tick computation (nice-number locator, tick label formatting)

### 9.2 Locators
- [x] `AutoLocator`, `MaxNLocator` (nice-number algorithm, TickConfig::nbins)
- [ ] `LinearLocator`, `MultipleLocator`, `FixedLocator`, `IndexLocator`
- [ ] `LogLocator`, `LogitLocator`, `AutoMinorLocator`
- [ ] `NullLocator`, `SymmetricalLogLocator`

### 9.3 Formatters
- [x] `ScalarFormatter` (basic: auto-format with %.0f, %.1f, %.2f, %.1e)
- [ ] `NullFormatter`, `FixedFormatter`
- [ ] `FuncFormatter`, `StrMethodFormatter`, `FormatStrFormatter`
- [ ] `LogFormatter`, `LogFormatterExponent`, `LogFormatterMathtext`, `LogFormatterSciNotation`
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
- [x] Colorbar rendering (vertical color strip with viridis colormap + tick labels)
- [ ] Spines / axis styling (hide individual spines, `spines.set_visible`)
- [x] Spines / axis border rendering (rectangle border + tick marks around axes rect)
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
