# TODO.md — Next Major Features (priority order)

The matplotlib microfeature-parity sweep is complete (see
`FEATURES-TODO.md` and `docs/MICROFEATURES.md`). This file lists the next
major areas of work, sorted by priority. Items are roughly ordered by
user-visible value vs. implementation cost.

## P0 — Correctness & robustness

- [ ] **Remaining parity deltas** — residual microfeature diffs: legend box
      sizing/spacing vs mpl in dense cases, colorbar `fraction`/`aspect`
      semantics (mpl `aspect=20` drives strip width; `fraction` drives axes
      shrink — our `ColorbarStyle` only has pixel `width`), minor tick-label
      suppression on crowded axes, `labelpad`/`tick pad` fine tuning.
- [ ] **Vector/raster parity pass** — sweep the vector (SVG/PDF) path for
      the fixes that landed raster-only this round: secondary axis labels,
      table placement outside axes, legend shadow, faux-bold titles,
      colorbar extend triangles, tick-label rotation anchoring.
- [ ] **`draggable` legends/annotations** — flag exists; needs the §11
      event system wired to pick+drag on screen backend.
- [ ] **Deterministic vector regression tests** — pixel tests cover the
      raster path; add golden-geometry tests for `emitVector` output.

## P1 — High-value features

- [ ] **`matplotlib.path.Path` infrastructure** — the big unlock: bezier
      curve flattening (`Path.curve3/curve4`), compound paths with
      fill rules, then rebuild on top of it:
  - [ ] custom `Path` markers (`marker=Path`)
  - [ ] `FancyArrowPatch` styles (`-|>`, `<->`, `-[`, `]-[`, `fancy`,
        `simple`, `wedge`) with `connectionstyle` arcs
  - [ ] `FancyBboxPatch` (`boxstyle=round,roundtooth,sawtooth`)
  - [ ] clip paths for artists (`set_clip_path`)
- [ ] **Filled contours** — `contourf` levels/banding with colormap +
      `contour`/`contourf` colorbar integration and `clabel` inline
      label gaps (labels exist; line gaps don't).
- [ ] **`matplotlib.widgets`-style interactivity on screen backend** —
      rubber-band zoom rectangle, pan cursor, toolbar (home/back/zoom),
      `SpanSelector`/`LassoSelector` equivalents.
- [ ] **MathText coverage** — expand the mathtext subset: fractions
      (`\frac`), `\sqrt`, `\sum`/`∫` large operators, `\left(\right`
      delimiters, `\vec`/accents, nested scripts.
- [ ] **`pcolormesh` shading="gouraud"/"flat" edge modes** and `imshow`
      `interpolation=` variants (`"nearest"` done; `"bilinear"`,
      `"bicubic"`, `"antialiased"` sampling in the heatmap shader).
- [ ] **Log-scale data clipping** — non-positive points in log mode
      should be dropped at data-conversion time (currently clamps).

## P2 — Performance & GPU leverage

- [ ] **GPU-side marker/path tessellation** — marker quads currently use
      SDF; large `PathCollection`s should triangulate on GPU or use
      instanced glyph atlases.
- [ ] **MSDF text atlas** — replace the grayscale bitmap atlas with
      multi-channel SDF for crisp text at any DPI (glyb has an MSDF
      renderer stub already).
- [ ] **Multi-frame animation GPU encode** — APNG/GIF encoders are CPU;
      wire the compute-shader encoder into the animation path.
- [ ] **Partial redraw / damage tracking** — screen backend currently
      re-renders everything; cache the framebuffer and blit overlays.
- [ ] **GPU `pcolormesh`/`contour`** — move CPU tessellation to compute
      for million-cell meshes.

## P3 — Broader matplotlib surface

- [ ] **`gridspec` nested/variable ratios** — `GridSpecFromSubplotSpec`,
      per-row/col `height_ratios`/`width_ratios` (partially present?),
      `subfigures`.
- [ ] **`AxesImage` transforms** — `imshow` `extent` + `transform=`
      non-affine support (polar projection of images).
- [ ] **Polar axes completion** — polar grid labels placement, `thetagrids`
      /`rgrids` formatters, `set_theta_zero_location`/`direction`,
      sector (`thetalim`) wedges.
- [ ] **`mpl_toolkits.axes_grid1`** — `inset_axes`, `AnchoredSizeBar`,
      `anchored_artists` (legend exists; sizebar/zoom-effect doesn't).
- [ ] **3D polish** — `plot_surface` colormap shading (light source),
      `view_init` interactive rotation on screen backend, 3D tick
      labels on pane edges, `scatter` size/depth cueing.
- [ ] **`quiver`/`streamplot` density + arrow styles** — quiver `scale`,
      `width`, `headwidth` params; streamplot `arrowsize`/`broken_streamlines`.
- [ ] **`tri_*` functions** — `triplot`, `tricontour`, `tripcolor` exist?
      audit coverage; add `TriRefiner`/`UniformTriRefiner` smoothing.

## P4 — Ecosystem & ergonomics

- [ ] **pybind11 bindings (`volcanoplot` Python module)** — the single
      biggest adoption driver; keep the API mirroring `matplotlib.pyplot`.
- [ ] **`xarray`/`pandas` plotting hooks** — `__array__` + `plot` dispatch,
      index/date handling via existing unit converters.
- [ ] **Nix/Homebrew packaging + CI matrix** — prebuilt binaries, Vulkan
      ICD (lavapipe) in CI for headless tests.
- [ ] **Documentation site** — API reference (Doxygen/mkdocs), gallery
      browser from `gallery_micro`, migration guide from matplotlib.
- [ ] **Serialization round-trip** — save/load a `Figure` to JSON for
      reproducible test fixtures and headless replays.
- [ ] **Accessibility** — colorblind-safe default cycle option, alt-text
      metadata in vector output.
