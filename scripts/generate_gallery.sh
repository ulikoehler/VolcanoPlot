#!/usr/bin/env bash
# scripts/generate_gallery.sh — generate side-by-side matplotlib vs VolcanoPlot gallery
#
# Builds VolcanoPlot (if needed), runs the C++ gallery example to produce
# VolcanoPlot PNGs, runs the Python matplotlib gallery script to produce
# matplotlib PNGs, and optionally creates side-by-side comparison images.
#
# Usage:
#   ./scripts/generate_gallery.sh [output_dir]
#
# Defaults:
#   output_dir = gallery/
#
# Output structure:
#   gallery/
#     volcano/       — PNGs rendered by VolcanoPlot
#     matplotlib/    — PNGs rendered by matplotlib
#     comparison/    — side-by-side comparison PNGs (if ImageMagick available)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${1:-gallery}"
VOLCANO_DIR="$OUT_DIR/volcano"
MPL_DIR="$OUT_DIR/matplotlib"
COMPARE_DIR="$OUT_DIR/comparison"

cd "$ROOT_DIR"

echo "=== VolcanoPlot Gallery Generator ==="
echo "Output directory: $OUT_DIR"
echo ""

# ── Step 1: Build VolcanoPlot (if needed) ──────────────────────────────────
echo "[1/4] Building VolcanoPlot..."
if [ ! -f "build/examples/example_gallery" ]; then
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DVOLCANO_BUILD_EXAMPLES=ON 2>&1 | tail -3
fi
cmake --build build --target example_gallery -j4 2>&1 | tail -3
echo "  Build complete."
echo ""

# ── Step 2: Generate VolcanoPlot PNGs ──────────────────────────────────────
echo "[2/4] Generating VolcanoPlot PNGs..."
mkdir -p "$VOLCANO_DIR"
./build/examples/example_gallery "$VOLCANO_DIR"
echo ""

# ── Step 3: Generate matplotlib PNGs ───────────────────────────────────────
echo "[3/4] Generating matplotlib PNGs..."
mkdir -p "$MPL_DIR"
python3 scripts/matplotlib_gallery.py "$MPL_DIR"
echo ""

# ── Step 4: Create side-by-side comparisons (optional) ─────────────────────
echo "[4/4] Creating comparison images..."
mkdir -p "$COMPARE_DIR"

if command -v convert &>/dev/null; then
    # ImageMagick available — create side-by-side composites.
    for volcano_png in "$VOLCANO_DIR"/*.png; do
        name=$(basename "$volcano_png" .png)
        mpl_png="$MPL_DIR/$name.png"
        if [ -f "$mpl_png" ]; then
            compare_png="$COMPARE_DIR/${name}_compare.png"
            convert "$mpl_png" "$volcano_png" +append "$compare_png"
            echo "  wrote $compare_png"
        fi
    done
elif command -v python3 &>/dev/null; then
    # Fall back to PIL if available.
    python3 - "$VOLCANO_DIR" "$MPL_DIR" "$COMPARE_DIR" << 'PYEOF'
import sys, os
from PIL import Image

volcano_dir, mpl_dir, compare_dir = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(compare_dir, exist_ok=True)

for f in sorted(os.listdir(volcano_dir)):
    if not f.endswith(".png"):
        continue
    name = f[:-4]
    mpl_path = os.path.join(mpl_dir, f)
    vol_path = os.path.join(volcano_dir, f)
    if not os.path.exists(mpl_path):
        continue
    img_mpl = Image.open(mpl_path)
    img_vol = Image.open(vol_path)
    # Resize to same height for side-by-side.
    h = max(img_mpl.height, img_vol.height)
    if img_mpl.height != h:
        img_mpl = img_mpl.resize((int(img_mpl.width * h / img_mpl.height), h))
    if img_vol.height != h:
        img_vol = img_vol.resize((int(img_vol.width * h / img_vol.height), h))
    combined = Image.new("RGB", (img_mpl.width + img_vol.width, h), "white")
    combined.paste(img_mpl, (0, 0))
    combined.paste(img_vol, (img_mpl.width, 0))
    out_path = os.path.join(compare_dir, f"{name}_compare.png")
    combined.save(out_path)
    print(f"  wrote {out_path}")
PYEOF
    if [ $? -ne 0 ]; then
        echo "  (PIL not available — skipping comparison images)"
        echo "  Install Pillow: pip3 install Pillow"
    fi
else
    echo "  (Neither ImageMagick nor Pillow available — skipping comparison images)"
    echo "  Install one of: imagemagick | Pillow (pip3 install Pillow)"
fi
echo ""

# ── Summary ────────────────────────────────────────────────────────────────
VOL_COUNT=$(ls "$VOLCANO_DIR"/*.png 2>/dev/null | wc -l)
MPL_COUNT=$(ls "$MPL_DIR"/*.png 2>/dev/null | wc -l)
CMP_COUNT=$(ls "$COMPARE_DIR"/*.png 2>/dev/null | wc -l)

echo "=== Summary ==="
echo "  VolcanoPlot PNGs:   $VOL_COUNT  ($VOLCANO_DIR/)"
echo "  Matplotlib PNGs:    $MPL_COUNT  ($MPL_DIR/)"
echo "  Comparison PNGs:    $CMP_COUNT  ($COMPARE_DIR/)"
echo ""
echo "Done. View the gallery at: $OUT_DIR/"
