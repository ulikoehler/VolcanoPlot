# FEATURES-TODO — remaining work

This file tracks VolcanoPlot's progress toward matplotlib feature parity.
Status legend: `[ ]` not started · `[-]` in progress · `[x]` done · `[~]` won't fix
(completed items have been pruned; git history has the full record).

---

## Remaining parity gaps

- [~] `text.usetex` full LaTeX rendering (requires external TeX — not
      planned; MathText subset covers the common cases)
- [~] `handler_map` / legend handlers — `IPlot::legendMarker()` provides
      per-plot handle shapes; no arbitrary handler factory
- [~] `pgf` (LaTeX/PGF backend — needs a true vector command stream;
      deferred)
- [~] **Remaining parity deltas** — verified: colorbar `fraction`/`pad`/
      `aspect`/`shrink`/`extend` semantics (axes shrink by fraction+pad;
      strip width = min(region, height/aspect) like mpl set_box_aspect),
      minor tick-label suppression (LogFormatterSciNotation
      minor_thresholds on crowded axes), `labelPad`/`tick pad` all in
      place. Legend box sizing/spacing in dense cases remains.
- [~] **3D polish** — `plot_surface` light-source shading
      (`SurfacePlot::shade`, mpl LightSource azdeg=315/altdeg=45
      lambert via screen-space normals), scatter `depthshade`,
      `Camera3D::viewInit(elev, azim, roll)`, and interactive
      mouse-drag rotation are done; residual polish remains.
- [~] **Documentation site** — mkdocs site (`mkdocs.yml`,
      `docs/index.md`) with architecture/microfeatures pages and an
      auto-generated gallery browser (`scripts/build_docs.py`
      regenerates `docs/gallery.md` + assets from `gallery*/comparison`).
      Doxygen API reference and mpl migration guide still open.
- [~] **Accessibility** — `FigureStyle::colorblindSafe()` swaps the
      prop_cycle to Okabe-Ito (named styles seaborn-v0_8-colorblind /
      tableau-colorblind10 already existed); SVG savefig metadata
      "Title"/"Description" keys emit <title>/<desc> elements for
      screen readers.

---

## Performance pipeline — remaining

Context: the §19 sweep is implemented and benchmarked (results in
`gallery/benchmark/`). Two ideas are intentionally left open:

- [ ] **Striped parallel deflate** — split PNG rows into stripes, deflate
      in worker threads, emit as separate IDAT chunks (still spec-valid
      PNG). Note: savefig already defaults to zlib level 3 and
      `Renderer::savefigAsync` overlaps encode with the next render —
      remaining upside is single-figure latency only.
- [ ] **GPU contour tessellation** — compute-shader marching-squares band
      extraction + triangle emit (mirror `PcolormeshPlot::gpuTessellate`).
      `contourf_400` is the one remaining native loss vs Agg (~0.75×);
      row-parallel CPU tessellation already exists.
