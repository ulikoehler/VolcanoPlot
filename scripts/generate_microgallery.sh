#!/usr/bin/env bash
# scripts/generate_microgallery.sh — microfeature-by-microfeature
# matplotlib vs VolcanoPlot comparison gallery.
#
# Each microfeature renders ONE small thing (a grid line, a tick mark, a
# marker shape, ...). Numbers follow the dependency-ordered checklist in
# docs/MICROFEATURES.md — the lowest failing number is the one to fix.
#
# Usage:
#   ./scripts/generate_microgallery.sh [output_dir] [--jobs N] [--filter PAT]
#
# Generation is parallelized:
#   - VolcanoPlot: N worker processes each render a shard of features
#     (--shard=K/N), each with its own Vulkan instance.
#   - matplotlib:  ProcessPoolExecutor over the feature registry.
#   - comparisons: xargs -P.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

OUT_DIR="gallery_micro"
FILTER=""
JOBS="$(nproc 2>/dev/null || echo 4)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter)   FILTER="$2"; shift 2 ;;
        --filter=*) FILTER="${1#--filter=}"; shift ;;
        --jobs|-j)  JOBS="$2"; shift 2 ;;
        --jobs=*|-j=*) JOBS="${1#*=}"; shift ;;
        *)          OUT_DIR="$1"; shift ;;
    esac
done
# Cap shard count at 8 — each shard spins up a full Vulkan instance.
[[ "$JOBS" -gt 8 ]] && JOBS=8

VOLCANO_DIR="$OUT_DIR/volcano"
MPL_DIR="$OUT_DIR/matplotlib"
COMPARE_DIR="$OUT_DIR/comparison"
mkdir -p "$VOLCANO_DIR" "$MPL_DIR" "$COMPARE_DIR"

cd "$ROOT_DIR"

echo "=== VolcanoPlot Microfeature Gallery ==="
echo "Output: $OUT_DIR   workers: $JOBS"
echo ""

# ── 1. Build ─────────────────────────────────────────────────────────────
echo "[1/4] Building..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVOLCANO_BUILD_EXAMPLES=ON \
    > /dev/null 2>&1 || true
cmake --build build --target example_microgallery -j4 2>&1 | tail -2

# ── 2. VolcanoPlot PNGs (parallel shards) ────────────────────────────────
echo ""
echo "[2/4] VolcanoPlot ($JOBS parallel shards)..."
pids=()
for ((k = 0; k < JOBS; k++)); do
    if [[ -n "$FILTER" ]]; then
        ./build/examples/example_microgallery "$VOLCANO_DIR" \
            --only="$FILTER" &
    else
        ./build/examples/example_microgallery "$VOLCANO_DIR" \
            --shard="$k/$JOBS" &
    fi
    pids+=($!)
done
rc=0
for p in "${pids[@]}"; do wait "$p" || rc=1; done
[[ "$rc" -eq 0 ]] || { echo "  some shards failed"; exit 1; }

# ── 3. matplotlib PNGs (parallel workers) ────────────────────────────────
echo ""
echo "[3/4] matplotlib ($JOBS parallel workers)..."
MPL_ARGS=("$MPL_DIR" --jobs "$JOBS")
[[ -n "$FILTER" ]] && MPL_ARGS+=(--filter "$FILTER")
python3 scripts/matplotlib_microgallery.py "${MPL_ARGS[@]}"

# ── 4. Side-by-side comparisons (parallel) ───────────────────────────────
echo ""
echo "[4/4] Comparisons..."
if command -v convert &>/dev/null; then
    ls "$VOLCANO_DIR"/*.png 2>/dev/null | \
    xargs -P "$JOBS" -I{} bash -c '
        name=$(basename "{}" .png)
        mpl="'"$MPL_DIR"'/${name}.png"
        [[ -f "$mpl" ]] && \
        convert "$mpl" "{}" +append \
            "'"$COMPARE_DIR"'/${name}_compare.png"
    '
elif python3 -c "import PIL" 2>/dev/null; then
    python3 - "$VOLCANO_DIR" "$MPL_DIR" "$COMPARE_DIR" "$JOBS" <<'PYEOF'
import sys, os
from concurrent.futures import ProcessPoolExecutor
from PIL import Image
vol, mpl, out, jobs = sys.argv[1:5]
jobs = int(jobs)
def combine(name):
    a, b = os.path.join(mpl, name), os.path.join(vol, name)
    if not os.path.exists(a) or not os.path.exists(b):
        return None
    ia, ib = Image.open(a), Image.open(b)
    h = max(ia.height, ib.height)
    ia = ia.resize((int(ia.width * h / ia.height), h))
    ib = ib.resize((int(ib.width * h / ib.height), h))
    c = Image.new("RGB", (ia.width + ib.width, h), "white")
    c.paste(ia, (0, 0)); c.paste(ib, (ia.width, 0))
    p = os.path.join(out, name[:-4] + "_compare.png")
    c.save(p)
    return p
names = [f for f in sorted(os.listdir(vol)) if f.endswith(".png")]
with ProcessPoolExecutor(max_workers=jobs) as pool:
    for r in pool.map(combine, names):
        if r: print(f"  wrote {r}")
PYEOF
else
    echo "  (no ImageMagick/Pillow — skipping comparisons)"
fi

echo ""
VOL_COUNT=$(ls "$VOLCANO_DIR"/*.png 2>/dev/null | wc -l)
MPL_COUNT=$(ls "$MPL_DIR"/*.png 2>/dev/null | wc -l)
CMP_COUNT=$(ls "$COMPARE_DIR"/*.png 2>/dev/null | wc -l)
echo "=== Summary ==="
echo "  VolcanoPlot:  $VOL_COUNT  ($VOLCANO_DIR/)"
echo "  matplotlib:   $MPL_COUNT  ($MPL_DIR/)"
echo "  comparisons:  $CMP_COUNT  ($COMPARE_DIR/)"
echo ""
echo "Check order: docs/MICROFEATURES.md (lowest failing number first)"
