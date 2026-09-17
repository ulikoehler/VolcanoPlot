#!/usr/bin/env python3
"""Generate side-by-side matplotlib vs VolcanoPlot gallery.

Builds VolcanoPlot (if needed), runs the C++ gallery example to produce
VolcanoPlot PNGs, runs the matplotlib gallery script to produce matplotlib
PNGs, and creates side-by-side comparison images.

All subprocess output is streamed live so progress is always visible.

Usage:
    ./scripts/generate_gallery.py [output_dir] [--filter NAME]
    ./scripts/generate_gallery.py gallery --filter fill
    ./scripts/generate_gallery.py gallery --filter fill,line

Output structure:
    gallery/
      volcano/       - PNGs rendered by VolcanoPlot
      matplotlib/    - PNGs rendered by matplotlib
      comparison/    - side-by-side comparison PNGs
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from collections import deque

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
GALLERY_BIN = os.path.join(BUILD_DIR, "examples", "example_gallery")
MPL_SCRIPT = os.path.join(ROOT_DIR, "scripts", "matplotlib_gallery.py")


def fmt_elapsed(seconds):
    return f"{seconds:.1f}s" if seconds < 60 else f"{int(seconds // 60)}m{seconds % 60:.0f}s"


def stream(cmd, prefix="  ", progress_re=None):
    """Run cmd, streaming output live. Returns (returncode, elapsed).

    If progress_re is given, only matching lines are printed live; on
    failure the last 40 lines of output are dumped for context.
    """
    t0 = time.monotonic()
    proc = subprocess.Popen(
        cmd, cwd=ROOT_DIR,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    tail = deque(maxlen=40)
    for line in proc.stdout:
        tail.append(line)
        if progress_re is None or progress_re.search(line):
            print(prefix + line, end="", flush=True)
    rc = proc.wait()
    if rc != 0 and progress_re is not None:
        print("\n--- last build output ---", flush=True)
        for line in tail:
            print(line, end="")
        print("-------------------------", flush=True)
    return rc, time.monotonic() - t0


def step(num, total, title):
    print(f"\n[{num}/{total}] {title}...", flush=True)


def fail(msg, rc=1):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(rc)


def build_volcano():
    step(1, 4, "Building VolcanoPlot")
    if not os.path.isfile(GALLERY_BIN):
        rc, el = stream([
            "cmake", "-B", "build",
            "-DCMAKE_BUILD_TYPE=Release", "-DVOLCANO_BUILD_EXAMPLES=ON",
        ])
        if rc != 0:
            fail("cmake configure failed")
        print(f"  configured in {fmt_elapsed(el)}", flush=True)

    # make/ninja print "[NN%] Building ..." progress lines — show only those
    # plus diagnostics, so the build doesn't look frozen.
    progress_re = re.compile(r"\[\s*\d+%\]|error|warning|Linking|Built target", re.IGNORECASE)
    rc, el = stream(
        ["cmake", "--build", "build", "--target", "example_gallery", "-j4"],
        progress_re=progress_re,
    )
    if rc != 0:
        fail("build failed")
    print(f"  Build complete in {fmt_elapsed(el)}.", flush=True)


def gen_volcano(volcano_dir):
    step(2, 4, "Generating VolcanoPlot PNGs")
    os.makedirs(volcano_dir, exist_ok=True)
    # Force line-buffered stdio so "wrote ..." lines appear as they happen.
    cmd = [GALLERY_BIN, volcano_dir]
    if shutil.which("stdbuf"):
        cmd = ["stdbuf", "-oL", "-eL"] + cmd
    rc, el = stream(cmd)
    if rc != 0:
        fail("example_gallery failed")
    print(f"  VolcanoPlot PNGs done in {fmt_elapsed(el)}.", flush=True)


def gen_matplotlib(mpl_dir, name_filter):
    step(3, 4, "Generating matplotlib PNGs")
    os.makedirs(mpl_dir, exist_ok=True)
    cmd = [sys.executable, "-u", MPL_SCRIPT, mpl_dir]
    if name_filter:
        cmd += ["--filter", name_filter]
    rc, el = stream(cmd)
    if rc != 0:
        fail("matplotlib_gallery.py failed (is matplotlib installed?)")
    print(f"  matplotlib PNGs done in {fmt_elapsed(el)}.", flush=True)


def _passes_filter(name, filter_names):
    return filter_names is None or name in filter_names


def compare_with_pil(volcano_dir, mpl_dir, compare_dir, filter_names):
    from PIL import Image

    wrote = 0
    for f in sorted(os.listdir(volcano_dir)):
        if not f.endswith(".png"):
            continue
        name = f[:-4]
        if not _passes_filter(name, filter_names):
            continue
        mpl_path = os.path.join(mpl_dir, f)
        if not os.path.exists(mpl_path):
            continue
        img_mpl = Image.open(mpl_path)
        img_vol = Image.open(os.path.join(volcano_dir, f))
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
        print(f"  wrote {out_path}", flush=True)
        wrote += 1
    return wrote


def compare_with_imagemagick(volcano_dir, mpl_dir, compare_dir, filter_names):
    wrote = 0
    for f in sorted(os.listdir(volcano_dir)):
        if not f.endswith(".png"):
            continue
        name = f[:-4]
        if not _passes_filter(name, filter_names):
            continue
        mpl_path = os.path.join(mpl_dir, f)
        if not os.path.exists(mpl_path):
            continue
        out_path = os.path.join(compare_dir, f"{name}_compare.png")
        rc = subprocess.call(
            ["convert", mpl_path, os.path.join(volcano_dir, f), "+append", out_path],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        if rc == 0:
            print(f"  wrote {out_path}", flush=True)
            wrote += 1
    return wrote


def gen_comparisons(volcano_dir, mpl_dir, compare_dir, name_filter):
    step(4, 4, "Creating comparison images")
    os.makedirs(compare_dir, exist_ok=True)
    filter_names = (
        {n.strip() for n in name_filter.split(",") if n.strip()}
        if name_filter else None
    )

    try:
        import PIL  # noqa: F401
        wrote = compare_with_pil(volcano_dir, mpl_dir, compare_dir, filter_names)
    except ImportError:
        if shutil.which("convert"):
            wrote = compare_with_imagemagick(
                volcano_dir, mpl_dir, compare_dir, filter_names)
        else:
            print("  (Neither Pillow nor ImageMagick available — skipping)")
            print("  Install one of: pip3 install Pillow | imagemagick")
            return
    if wrote == 0:
        print("  (no matching plot pairs found)")


def count_pngs(d):
    if not os.path.isdir(d):
        return 0
    return sum(1 for f in os.listdir(d) if f.endswith(".png"))


def main():
    parser = argparse.ArgumentParser(
        description="Generate side-by-side matplotlib vs VolcanoPlot gallery")
    parser.add_argument("out_dir", nargs="?", default="gallery",
                        help="Output directory (default: gallery)")
    parser.add_argument("--filter", default=None,
                        help="Comma-separated plot names to generate (e.g. 'fill,line')")
    args = parser.parse_args()

    volcano_dir = os.path.join(args.out_dir, "volcano")
    mpl_dir = os.path.join(args.out_dir, "matplotlib")
    compare_dir = os.path.join(args.out_dir, "comparison")

    print("=== VolcanoPlot Gallery Generator ===")
    print(f"Output directory: {args.out_dir}")
    if args.filter:
        print(f"Filter: {args.filter}")

    t0 = time.monotonic()
    build_volcano()
    gen_volcano(volcano_dir)
    gen_matplotlib(mpl_dir, args.filter)
    gen_comparisons(volcano_dir, mpl_dir, compare_dir, args.filter)

    print("\n=== Summary ===")
    print(f"  VolcanoPlot PNGs:   {count_pngs(volcano_dir)}  ({volcano_dir}/)")
    print(f"  Matplotlib PNGs:    {count_pngs(mpl_dir)}  ({mpl_dir}/)")
    print(f"  Comparison PNGs:    {count_pngs(compare_dir)}  ({compare_dir}/)")
    print(f"\nDone in {fmt_elapsed(time.monotonic() - t0)}. "
          f"View the gallery at: {args.out_dir}/")


if __name__ == "__main__":
    main()
