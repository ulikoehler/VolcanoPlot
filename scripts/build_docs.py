#!/usr/bin/env python3
"""Build the VolcanoPlot documentation site.

Generates docs/gallery.md from the rendered gallery comparisons (if present),
copies the comparison images into docs/assets/, then runs `mkdocs build`.

Usage:
    ./scripts/build_docs.py            # build site into ./site
    ./scripts/build_docs.py --serve    # mkdocs serve for local preview
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
ASSETS = DOCS / "assets"


def generate_gallery_md() -> str:
    lines = [
        "# Gallery",
        "",
        "Side-by-side matplotlib (left) vs VolcanoPlot (right) comparisons.",
        "Regenerate with `./scripts/generate_gallery.py gallery` and",
        "`./scripts/generate_microgallery.py gallery_micro --jobs 8`.",
        "",
    ]
    macro_dir = ROOT / "gallery" / "comparison"
    micro_dir = ROOT / "gallery_micro" / "comparison"

    def section(title: str, src: Path, prefix: str) -> None:
        if not src.is_dir():
            return
        pngs = sorted(src.glob("*.png"))
        if not pngs:
            return
        dst_dir = ASSETS / prefix
        dst_dir.mkdir(parents=True, exist_ok=True)
        lines.append(f"## {title}")
        lines.append("")
        for p in pngs:
            dst = dst_dir / p.name
            if not dst.exists() or dst.stat().st_mtime < p.stat().st_mtime:
                shutil.copy2(p, dst)
            name = p.stem.removesuffix("_compare").replace("_", " ")
            lines.append(f"### {name}")
            lines.append(f"![{name}](assets/{prefix}/{p.name})")
            lines.append("")

    section("Plot styles", macro_dir, "gallery")
    section("Microfeature comparisons", micro_dir, "micro")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--serve", action="store_true")
    args = ap.parse_args()

    (DOCS / "gallery.md").write_text(generate_gallery_md())

    cmd = ["mkdocs", "serve" if args.serve else "build"]
    try:
        return subprocess.call(cmd, cwd=ROOT)
    except FileNotFoundError:
        print("mkdocs not installed: pip install mkdocs", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
