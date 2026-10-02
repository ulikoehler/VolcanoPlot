#!/usr/bin/env python3
"""Generate a side-by-side matplotlib vs VolcanoPlot-web gallery.

Renders every demo kind through the WebGPU WASM engine (headless Chrome +
SwiftShader, via web/scripts/capture_gallery.mjs) and through matplotlib
(scripts/matplotlib_webgallery.py — identical data), then composes
side-by-side PNGs and an index.html.

Usage:
    ./scripts/generate_webgallery.py [output_dir] [--filter name1,name2]

Output structure (default web/gallery/):
    web/gallery/web/          - PNGs rendered by VolcanoPlot in the browser
    web/gallery/matplotlib/   - PNGs rendered by matplotlib
    web/gallery/comparison/   - labeled side-by-side PNGs
    web/gallery/index.html    - browsable gallery
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
CAPTURE = os.path.join(WEB, "scripts", "capture_gallery.mjs")
MPL = os.path.join(ROOT, "scripts", "matplotlib_webgallery.py")

W, H = 640, 480
LABEL_H = 24


def run(cmd, what):
    print(f"[{what}] {' '.join(cmd)}", flush=True)
    rc = subprocess.run(cmd, cwd=ROOT).returncode
    if rc != 0:
        print(f"ERROR: {what} failed (rc={rc})", file=sys.stderr)
        sys.exit(rc)


def compare(web_dir, mpl_dir, out_dir, filt):
    from PIL import Image, ImageDraw
    wrote = []
    for f in sorted(os.listdir(web_dir)):
        if not f.endswith(".png"):
            continue
        name = f[:-4]
        if filt and name not in filt:
            continue
        mp = os.path.join(mpl_dir, f)
        if not os.path.exists(mp):
            print(f"  no mpl reference for {name}, skipping")
            continue
        a = Image.open(mp).convert("RGB").resize((W, H))
        b = Image.open(os.path.join(web_dir, f)).convert("RGB").resize((W, H))
        canvas = Image.new("RGB", (W * 2 + 20, H + LABEL_H), "white")
        canvas.paste(a, (0, LABEL_H))
        canvas.paste(b, (W + 20, LABEL_H))
        d = ImageDraw.Draw(canvas)
        d.text((4, 6), f"{name} — matplotlib", fill="black")
        d.text((W + 24, 6), f"{name} — VolcanoPlot (WebGPU)", fill="black")
        canvas.save(os.path.join(out_dir, f))
        wrote.append(name)
    return wrote


def write_index(gal_dir, names):
    rows = "".join(
        f'<h2>{n}</h2><img src="comparison/{n}.png" loading="lazy">\n'
        for n in names)
    html = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>VolcanoPlot-web vs matplotlib</title>
<style>body{{font-family:sans-serif;max-width:1400px;margin:auto}}
img{{max-width:100%;border:1px solid #ccc}}</style></head>
<body><h1>VolcanoPlot (WebGPU/WASM) vs matplotlib</h1>
<p>Left: matplotlib reference. Right: VolcanoPlot rendered in headless
Chrome via WebGPU+WASM (SwiftShader). Identical input data.</p>
{rows}</body></html>
"""
    with open(os.path.join(gal_dir, "index.html"), "w") as f:
        f.write(html)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", nargs="?", default=os.path.join(WEB, "gallery"))
    ap.add_argument("--filter", default=None)
    a = ap.parse_args()
    gal = a.out_dir
    filt = set(a.filter.split(",")) if a.filter else None
    web_dir, mpl_dir = (os.path.join(gal, "web"),
                        os.path.join(gal, "matplotlib"))
    cmp_dir = os.path.join(gal, "comparison")
    for d in (web_dir, mpl_dir, cmp_dir):
        os.makedirs(d, exist_ok=True)

    mpl_cmd = [sys.executable, MPL, mpl_dir]
    cap_cmd = ["node", CAPTURE, web_dir]
    if filt:
        f = ",".join(sorted(filt))
        mpl_cmd += ["--filter", f]
        cap_cmd.append(f)

    run(mpl_cmd, "matplotlib")
    run(cap_cmd, "webgpu capture")
    names = compare(web_dir, mpl_dir, cmp_dir, filt)
    write_index(gal, names)
    print(f"\n{len(names)} comparisons → {cmp_dir}")
    print(f"open {os.path.join(gal, 'index.html')}")


if __name__ == "__main__":
    main()
