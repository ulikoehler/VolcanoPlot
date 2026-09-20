# Microfeature Comparison Gallery — Ordered Verification Checklist

The microfeature gallery renders **one tiny feature per image** on both
matplotlib and VolcanoPlot, so parity can be checked feature-by-feature
instead of by eyeballing complex plots.

Generate with:

```bash
./scripts/generate_microgallery.py gallery_micro          # all features
./scripts/generate_microgallery.py gallery_micro --jobs 8 # parallel width
./scripts/generate_microgallery.py gallery_micro --filter grid,line
```

Output: `gallery_micro/{matplotlib,volcano,comparison}/NNN_name.png`.

## Why order matters

Features are numbered `NNN` in **dependency order**. A high-numbered image
can only be wrong in ways its dependencies aren't — e.g. if `026_grid_major`
fails, every later feature that incidentally shows a grid is suspect, and a
"broken dashed line" inside a busy plot is meaningless until `046_line_dashed`
has been verified in isolation.

**Rule of thumb: when a complex plot looks wrong, find the lowest-numbered
failing microfeature and fix that first.** Never debug tier ≥ 4 features
while any tier 0–3 feature fails — the canvas/ticks/grid/label substrate
affects almost every pixel comparison downstream.

## Tier 0 — Canvas & axes chrome (001–008)

The substrate. If any of these fail, *nothing else is trustworthy* —
background, axes box, and titles appear in every later image.

| # | Name | Checks | Failure signature |
|---|------|--------|-------------------|
| 001 | `blank_canvas` | framebuffer clears to figure facecolor | non-white/non-uniform canvas |
| 002 | `fig_facecolor` | figure patch color fills margins | margins stay white |
| 003 | `axes_facecolor` | axes patch fills plot area | axes area wrong color |
| 004 | `spines_box` | all 4 spines drawn | missing/offset border |
| 005 | `spines_left_bottom` | per-side spine visibility | top/right still drawn |
| 006 | `spines_none` | all spines hideable | ghost border remains |
| 007 | `axes_inset_rect` | `add_axes([l,b,w,h])` fractional rect | axes fills whole figure |
| 008 | `suptitle` | figure-level title above axes | title missing/misplaced |

## Tier 1 — Ticks & tick labels (009–025)

Grid lines are drawn *at tick positions* — verify ticks **before** grid.
If tick positions are wrong, grid (tier 2) will look wrong too.

| # | Name | Checks |
|---|------|--------|
| 009 | `ticks_major_x` | major tick marks on bottom spine |
| 010 | `ticks_major_y` | major ticks on left spine |
| 011 | `ticks_none` | `set_ticks([])` removes marks |
| 012 | `ticks_minor` | minor ticks between majors (`minorticks_on`) |
| 013 | `ticks_dir_in` | `direction='in'` — ticks point inward |
| 014 | `ticks_dir_inout` | `direction='inout'` straddle the spine |
| 015 | `ticks_long` | `majorSize` lengthens tick marks |
| 016 | `ticks_wide` | `majorWidth` thickens tick marks |
| 017 | `ticks_top_right` | ticks on top/right spines |
| 018 | `ticklabels_default` | numeric labels appear beside ticks |
| 019 | `ticklabels_fixed` | custom positions + text labels |
| 020 | `ticklabels_format` | `%.2f` style formatting |
| 021 | `ticklabels_sci` | scientific notation / offset text |
| 022 | `ticklabels_rotation` | rotated x labels |
| 023 | `ticklabels_hidden` | ticks drawn, no labels (NullFormatter) |
| 024 | `minor_ticklabels` | minor labels via minor formatter |
| 025 | `tick_nbins` | locator honors `nbins` hint |

## Tier 2 — Grid (026–034)

Depends on tier 1. A "wrong grid" is usually a wrong tick locator —
check 009–025 first.

| # | Name | Checks |
|---|------|--------|
| 026 | `grid_major` | **the basic grid** — lines at major ticks |
| 027 | `grid_x_only` | `axis='x'` — vertical lines only |
| 028 | `grid_y_only` | `axis='y'` — horizontal lines only |
| 029 | `grid_minor` | `which='minor'` |
| 030 | `grid_both` | `which='both'` — two line densities |
| 031 | `grid_dashed` | grid linestyle |
| 032 | `grid_color` | grid color |
| 033 | `grid_linewidth` | grid width |
| 034 | `grid_axisbelow` | grid drawn under a thick data line |

## Tier 3 — Labels & titles (035–042)

| # | Name | Checks |
|---|------|--------|
| 035 | `xlabel` | axis label below x axis |
| 036 | `ylabel` | rotated label left of y axis |
| 037 | `label_fontsize` | large label font |
| 038 | `title` | axes title centered above |
| 039 | `title_color` | colored title |
| 040 | `title_bold` | bold weight |
| 041 | `title_pad` | title offset from axes |
| 042 | `label_color` | colored axis labels |

## Tier 4 — Lines (043–052)

| # | Name | Checks |
|---|------|--------|
| 043 | `line_solid` | **the basic line** — one solid polyline |
| 044 | `line_color` | line color |
| 045 | `line_width` | thick line |
| 046 | `line_dashed` | `--` |
| 047 | `line_dotted` | `:` |
| 048 | `line_dashdot` | `-.` |
| 049 | `line_dashes_custom` | explicit on/off tuple |
| 050 | `line_alpha` | translucent line |
| 051 | `line_zorder` | z-order decides overlap |
| 052 | `line_prop_cycle` | two unstyled lines get C0/C1 |

## Tier 5 — Markers (053–064)

| # | Name | Checks |
|---|------|--------|
| 053 | `marker_circle` | `o` |
| 054 | `marker_square` | `s` |
| 055 | `marker_diamond` | `D` |
| 056 | `marker_triangle` | `^` |
| 057 | `marker_star` | `*` |
| 058 | `marker_plus` | `+` stroked |
| 059 | `marker_size` | large markers |
| 060 | `marker_hollow` | `fillstyle='none'` — edge only |
| 061 | `marker_edge_color` | hollow marker edge color |
| 062 | `marker_with_line` | line + markers combined |
| 063 | `marker_path` | custom `Path` marker |
| 064 | `marker_tex` | `$…$` math-glyph marker |

## Tier 6 — Fills & reference regions (065–074)

| # | Name | Checks |
|---|------|--------|
| 065 | `fill_polygon` | filled polygon |
| 066 | `fill_between` | band between two curves |
| 067 | `fill_alpha` | translucent fill |
| 068 | `axhline` | full-width horizontal line |
| 069 | `axvline` | full-height vertical line |
| 070 | `hlines` | bounded horizontal segments |
| 071 | `vlines` | bounded vertical segments |
| 072 | `axhspan` | shaded horizontal band |
| 073 | `axvspan` | shaded vertical band |
| 074 | `hatch_rect` | hatched rectangle patch |

## Tier 7 — Scales & limits (075–085)

A scale bug repositions *every* data element — check these before
interpreting any data-carrying tier.

| # | Name | Checks |
|---|------|--------|
| 075 | `xlim_ylim` | manual limits clip the view |
| 076 | `invert_x` | reversed x axis |
| 077 | `invert_y` | reversed y axis |
| 078 | `log_y` | `yscale('log')` |
| 079 | `log_x` | `xscale('log')` |
| 080 | `loglog` | log both + log minor ticks |
| 081 | `symlog` | `xscale('symlog')` |
| 082 | `aspect_equal` | equal aspect — square stays square |
| 083 | `categorical_x` | string categories on x |
| 084 | `date_x` | date tick labels |
| 085 | `secondary_y` | secondary axis function mapping |

## Tier 8 — Legend (086–092)

| # | Name | Checks |
|---|------|--------|
| 086 | `legend_basic` | one labeled series |
| 087 | `legend_loc` | `loc='lower left'` placement |
| 088 | `legend_ncols` | 2-column legend |
| 089 | `legend_noframe` | `frameon=False` |
| 090 | `legend_title` | legend title row |
| 091 | `legend_outside` | `bbox_to_anchor` outside axes |
| 092 | `legend_shadow` | fancybox + shadow |

## Tier 9 — Colorbar (093–096)

| # | Name | Checks |
|---|------|--------|
| 093 | `colorbar_basic` | vertical strip beside axes |
| 094 | `colorbar_extend` | `extend='both'` triangle caps |
| 095 | `colorbar_colormap` | non-default cmap |
| 096 | `colorbar_width` | wide strip |

## Tier 10 — Text & annotations (097–105)

| # | Name | Checks |
|---|------|--------|
| 097 | `text_data` | text at data coords |
| 098 | `text_axes` | text at axes-fraction coords |
| 099 | `text_rotation` | rotated text |
| 100 | `text_halign` | left/center/right alignment |
| 101 | `text_bbox` | background box behind text |
| 102 | `mathtext` | `$x^2$` math rendering |
| 103 | `annotate_arrow` | arrow annotation |
| 104 | `annotate_offset` | offset-points text |
| 105 | `unicode_text` | non-ASCII/CJK text |

## Tier 11 — Multi-axes layout (106–110)

| # | Name | Checks |
|---|------|--------|
| 106 | `subplots_2x2` | 2×2 grid of axes |
| 107 | `subplots_sharex` | shared x axis |
| 108 | `twinx` | twin y axis on right |
| 109 | `inset_axes` | small axes inside parent |
| 110 | `mosaic` | `subplot_mosaic` uneven layout |

## Tier 12 — Misc composite (111–115)

| # | Name | Checks |
|---|------|--------|
| 111 | `errorbar_caps` | caps at error bar ends |
| 112 | `bar_edges` | bar edge color/width |
| 113 | `step_post` | `where='post'` staircase |
| 114 | `eventplot_rows` | multi-row event markers |
| 115 | `table_bottom` | cell table below axes |

## Tier 13 — Arrow styles (116–118)

| # | Name | Checks |
|---|------|--------|
| 116 | `arrowstyle_filled` | `-|>` filled head, `-[`/`|-|` bracket ends |
| 117 | `arrowstyle_double` | `<|-|>` filled heads both ends, `<->` open |
| 118 | `arrowstyle_fancy` | `fancy`/`wedge` filled body arrows |

## Tier 14 — Paths (119–121)

| # | Name | Checks |
|---|------|--------|
| 119 | `clip_path` | line + patch `clipPath` to a circle |
| 120 | `boxstyle` | `sawtooth`/`roundtooth`/`round` FancyBboxPatch |
| 121 | `arrow_bezier` | `simple`/`fancy`/`wedge` on arc3 connections |

## Tier 15 — Extended coverage (122–145)

| # | Name | Checks |
|---|------|--------|
| 122 | `colorbar_horizontal` | `orientation='horizontal'` strip below axes |
| 123 | `colorbar_shrink` | `shrink=0.5` strip length |
| 124 | `marker_tex_beta` | `marker='$\\beta$'` MathText glyph marker |
| 125 | `imshow_extent` | `extent=` maps data coords |
| 126 | `imshow_aspect_auto` | `aspect='auto'` fills axes |
| 127 | `inset_indicator` | `indicate_inset_zoom` rect + connectors |
| 128 | `sizebar` | `AnchoredSizeBar` lower right |
| 129 | `anchored_text` | `AnchoredText` box |
| 130 | `scatter3d_depthshade` | mpl `depthshade` alpha cue |
| 131 | `scatter3d_view_init` | `view_init(elev=0, azim=-90)` XZ view |
| 132 | `surface_shade` | `plot_surface` `shade=True` |
| 133 | `surface_noshade` | `shade=False` |
| 134 | `quiver_pivot_mid` | `pivot='mid'` anchoring |
| 135 | `quiver_headwidth` | `width`/`headwidth`/`headlength`/`headaxislength` |
| 136 | `streamplot_arrowsize` | `arrowsize=2` |
| 137 | `streamplot_nan_hole` | NaN hole breaks streamlines |
| 138 | `clabel_gap` | inline label gaps in contour lines |
| 139 | `log_clip` | log y-scale drops non-positive points |
| 140 | `colorblind_cycle` | Okabe-Ito / tableau-colorblind10 cycle |
| 141 | `legend_handlelength` | `handlelength=4` |
| 142 | `legend_labelcolor` | `labelcolor='red'` |
| 143 | `pie_explode` | exploded wedges |
| 144 | `secondary_x` | `secondary_xaxis('top')` |
| 145 | `mathtext_frac_sum` | `\\frac` + `\\sum` limits |

## Debugging workflow

1. Run the gallery: `./scripts/generate_microgallery.py`
2. Scan `comparison/` top-down (filename order = check order).
3. At the **first** mismatch: fix that feature, regenerate with
   `--filter <name>`, then rescan — higher-tier failures often evaporate.
4. Only compare complex gallery plots once all 121 pass inspection.
