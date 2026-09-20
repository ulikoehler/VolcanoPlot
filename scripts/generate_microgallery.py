#!/usr/bin/env python3
"""Microfeature-by-microfeature matplotlib vs VolcanoPlot comparison gallery.

Each microfeature renders ONE small thing (a grid line, a tick mark, a
marker shape, ...). Numbers follow the dependency-ordered checklist in
docs/MICROFEATURES.md — the lowest failing number is the one to fix.

Usage:
    ./scripts/generate_microgallery.py [output_dir] [--jobs N] [--filter PAT]

Generation is parallelized:
    - VolcanoPlot: N worker processes each render a shard of features
      (--shard=K/N), each with its own Vulkan instance.
    - matplotlib:  ProcessPoolExecutor inside matplotlib_microgallery.py.
    - comparisons: parallel workers (PIL or ImageMagick).

Output structure:
    <output_dir>/
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
MICRO_BIN = os.path.join(BUILD_DIR, "examples", "example_microgallery")
MPL_SCRIPT = os.path.join(ROOT_DIR, "scripts", "matplotlib_microgallery.py")


def fmt_elapsed(seconds):
    return f"{seconds:.1f}s" if seconds < 60 else f"{int(seconds // 60)}m{seconds % 60:.0f}s"


def stream(cmd, prefix="  ", progress_re=None):
    """Run cmd, streaming output live. Returns (returncode, elapsed)."""
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
    step(1, 4, "Building")
    if not os.path.isfile(MICRO_BIN):
        stream([
            "cmake", "-B", "build",
            "-DCMAKE_BUILD_TYPE=Release", "-DVOLCANO_BUILD_EXAMPLES=ON",
        ])
    progress_re = re.compile(
        r"\[\s*\d+%\]|error|warning|Linking|Built target", re.IGNORECASE)
    rc, el = stream(
        ["cmake", "--build", "build", "--target",
         "example_microgallery", "-j4"],
        progress_re=progress_re)
    if rc != 0:
        fail("build failed")
    print(f"  Build complete in {fmt_elapsed(el)}.", flush=True)


def gen_volcano(volcano_dir, jobs, name_filter):
    """Spawn `jobs` worker processes, each rendering a shard of features."""
    step(2, 4, f"VolcanoPlot ({jobs} parallel shards)")
    os.makedirs(volcano_dir, exist_ok=True)
    procs = []
    for k in range(jobs):
        if name_filter:
            cmd = [MICRO_BIN, volcano_dir, f"--only={name_filter}"]
        else:
            cmd = [MICRO_BIN, volcano_dir, f"--shard={k}/{jobs}"]
        if shutil.which("stdbuf"):
            cmd = ["stdbuf", "-oL", "-eL"] + cmd
        procs.append(subprocess.Popen(
            cmd, cwd=ROOT_DIR,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
    rc = 0
    for p in procs:
        out, _ = p.communicate()
        print(out, end="", flush=True)
        if p.returncode != 0:
            rc = 1
    if rc != 0:
        fail("some shards failed")


def gen_matplotlib(mpl_dir, jobs, name_filter):
    step(3, 4, f"matplotlib ({jobs} parallel workers)")
    os.makedirs(mpl_dir, exist_ok=True)
    cmd = [sys.executable, "-u", MPL_SCRIPT, mpl_dir, "--jobs", str(jobs)]
    if name_filter:
        cmd += ["--filter", name_filter]
    rc, _ = stream(cmd)
    if rc != 0:
        fail("matplotlib_microgallery.py failed (is matplotlib installed?)")


def _combine_png(args):
    vol_path, mpl_path, out_path = args
    from PIL import Image
    if not (os.path.exists(vol_path) and os.path.exists(mpl_path)):
        return None
    ia, ib = Image.open(mpl_path), Image.open(vol_path)
    h = max(ia.height, ib.height)
    ia = ia.resize((int(ia.width * h / ia.height), h))
    ib = ib.resize((int(ib.width * h / ib.height), h))
    c = Image.new("RGB", (ia.width + ib.width, h), "white")
    c.paste(ia, (0, 0))
    c.paste(ib, (ia.width, 0))
    c.save(out_path)
    return out_path


def gen_comparisons(volcano_dir, mpl_dir, compare_dir, jobs):
    step(4, 4, "Comparisons")
    os.makedirs(compare_dir, exist_ok=True)
    names = [f for f in sorted(os.listdir(volcano_dir))
             if f.endswith(".png")]

    if shutil.which("convert"):
        from concurrent.futures import ThreadPoolExecutor
        def conv(f):
            mpl_path = os.path.join(mpl_dir, f)
            if not os.path.exists(mpl_path):
                return None
            out_path = os.path.join(compare_dir, f[:-4] + "_compare.png")
            rc = subprocess.call(
                ["convert", mpl_path, os.path.join(volcano_dir, f),
                 "+append", out_path],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return out_path if rc == 0 else None
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for r in pool.map(conv, names):
                if r:
                    print(f"  wrote {r}", flush=True)
        return

    try:
        from concurrent.futures import ProcessPoolExecutor
        tasks = [
            (os.path.join(volcano_dir, f),
             os.path.join(mpl_dir, f),
             os.path.join(compare_dir, f[:-4] + "_compare.png"))
            for f in names
        ]
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            for r in pool.map(_combine_png, tasks):
                if r:
                    print(f"  wrote {r}", flush=True)
    except ImportError:
        print("  (no ImageMagick/Pillow — skipping comparisons)")


def count_pngs(d):
    if not os.path.isdir(d):
        return 0
    return sum(1 for f in os.listdir(d) if f.endswith(".png"))


def main():
    parser = argparse.ArgumentParser(
        description="Generate the microfeature comparison gallery")
    parser.add_argument("out_dir", nargs="?", default="gallery_micro",
                        help="Output directory (default: gallery_micro)")
    parser.add_argument("--jobs", "-j", type=int,
                        default=os.cpu_count() or 4,
                        help="Parallel workers (capped at 8 for Vulkan)")
    parser.add_argument("--filter", default=None,
                        help="Comma-separated feature names to generate")
    args = parser.parse_args()

    # Cap shard count at 8 — each shard spins up a full Vulkan instance.
    jobs = min(args.jobs, 8)

    volcano_dir = os.path.join(args.out_dir, "volcano")
    mpl_dir = os.path.join(args.out_dir, "matplotlib")
    compare_dir = os.path.join(args.out_dir, "comparison")

    print("=== VolcanoPlot Microfeature Gallery ===")
    print(f"Output: {args.out_dir}   workers: {jobs}\n")

    build_volcano()
    gen_volcano(volcano_dir, jobs, args.filter)
    gen_matplotlib(mpl_dir, jobs, args.filter)
    gen_comparisons(volcano_dir, mpl_dir, compare_dir, jobs)

    print("\n=== Summary ===")
    print(f"  VolcanoPlot:  {count_pngs(volcano_dir)}  ({volcano_dir}/)")
    print(f"  matplotlib:   {count_pngs(mpl_dir)}  ({mpl_dir}/)")
    print(f"  comparisons:  {count_pngs(compare_dir)}  ({compare_dir}/)")
    print("\nCheck order: docs/MICROFEATURES.md (lowest failing number first)")


if __name__ == "__main__":
    main()
