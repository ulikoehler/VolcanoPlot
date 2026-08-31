#!/usr/bin/env python3
# scripts/llm_compare_plot.py — Ask an LLM to spot issues in the right plot
# of a side-by-side comparison image, using the left plot as the reference.
#
# The comparison images produced by scripts/generate_gallery.sh place the
# matplotlib reference on the LEFT and the VolcanoPlot rendering on the RIGHT.
# This script sends the image to a vision-capable LLM and prints a detailed
# description of any issues found in the right plot.
#
# Usage:
#   ./scripts/llm_compare_plot.py PATH/TO/comparison.png
#   ./scripts/llm_compare_plot.py PATH/TO/comparison.png --json
#   ./scripts/llm_compare_plot.py gallery/comparison/line_compare.png
#   ./scripts/llm_compare_plot.py gallery/comparison/*.png   # batch mode
#   ./scripts/llm_compare_plot.py PATH/TO/comparison.png --prompt "Focus on tick label alignment"
#
# Credentials are read from ~/.volcanoplot.llm.yaml:
#   llm:
#     api_key: "..."
#     base_url: "https://inference.hetzner.com/api/v1"
#     model: "Qwen3.8-27B"
#
# Exit codes:
#   0  — LLM responded (issues may or may not have been found)
#   2  — config/usage error
#   3  — LLM/API error
from __future__ import annotations

import argparse
import base64
import json
import mimetypes
import os
import sys
from pathlib import Path

import yaml

try:
    from openai import OpenAI
except ImportError:
    print("ERROR: openai package not installed. Run: pip3 install openai", file=sys.stderr)
    sys.exit(2)


CONFIG_PATH = Path.home() / ".volcanoplot.llm.yaml"

# The prompt asks the model to act as a careful reviewer comparing two plots.
SYSTEM_PROMPT = (
    "You are an expert data-visualization reviewer. You are shown a single "
    "image that contains TWO plots placed side by side:\n"
    "  - LEFT  plot: the reference rendering (matplotlib). Treat it as ground truth.\n"
    "  - RIGHT plot: a re-rendering of the same data by a different plotter "
    "(VolcanoPlot) that is being checked for correctness.\n"
    "Your job is to find every way in which the RIGHT plot differs from the "
    "LEFT plot or is otherwise wrong/buggy. Be precise and concrete."
)

USER_PROMPT = """Look at this comparison image. The LEFT plot is the reference (matplotlib). The RIGHT plot is the rendering under review (VolcanoPlot).

Inspect the RIGHT plot carefully and compare it to the LEFT plot. Identify ALL issues, bugs, and discrepancies in the RIGHT plot, such as (but not limited to):

- Missing or wrong plot elements (lines, markers, fills, bars, error bars, legends, colorbars, axis spines, tick marks, tick labels, axis labels, titles, grid lines).
- Incorrect geometry: wrong positions, sizes, shapes, orientations, or counts of graphical elements.
- Incorrect colors, colormaps, opacity, or alpha blending (e.g. transparent pixels, wrong fill colors).
- Incorrect axis ranges, scaling, tick placement, or label formatting.
- Text rendering problems: missing glyphs, garbled text, wrong font, misaligned labels, overlapping text.
- Anti-aliasing or edge artifacts (jagged lines, missing MSAA, hard edges where the reference is smooth).
- Clipping, overflow, or elements drawn outside the axes area.
- Aspect ratio or layout differences vs. the reference.
- Anything else that makes the RIGHT plot deviate from the LEFT plot or look wrong.

For EACH issue, report:
1. A short title.
2. Where it is located in the RIGHT plot (region / element).
3. A detailed description of what is wrong vs. the LEFT plot.
4. Severity: critical | major | minor | cosmetic.

If the RIGHT plot matches the LEFT plot with no issues, say so explicitly.

End your answer with one of these verdict lines on its own line:
VERDICT: ISSUES FOUND     (if any issue was reported)
VERDICT: NO ISSUES        (if the right plot matches the reference)
"""


def load_config(path: Path) -> dict:
    if not path.exists():
        print(f"ERROR: config file not found: {path}", file=sys.stderr)
        print("Create it with:", file=sys.stderr)
        print("  llm:", file=sys.stderr)
        print('    api_key: "..."', file=sys.stderr)
        print('    base_url: "https://inference.hetzner.com/api/v1"', file=sys.stderr)
        print('    model: "Qwen3.8-27B"', file=sys.stderr)
        sys.exit(2)
    with path.open("r") as f:
        cfg = yaml.safe_load(f)
    if not isinstance(cfg, dict) or "llm" not in cfg:
        print(f"ERROR: config file {path} is missing a top-level 'llm:' mapping", file=sys.stderr)
        sys.exit(2)
    llm = cfg["llm"]
    for key in ("api_key", "base_url", "model"):
        if not llm.get(key):
            print(f"ERROR: config file {path} is missing llm.{key}", file=sys.stderr)
            sys.exit(2)
    return llm


def encode_image(path: Path) -> tuple[str, str]:
    """Return (data_url, mime_type) for the image, downscaled if very large."""
    mime, _ = mimetypes.guess_type(str(path))
    if mime is None or not mime.startswith("image/"):
        mime = "image/png"
    data = path.read_bytes()
    # Some vision endpoints reject very large images. Downscale if width > 1600.
    try:
        from PIL import Image
        import io
        with Image.open(path) as img:
            if img.width > 1600:
                scale = 1600 / img.width
                new_size = (1600, max(1, int(img.height * scale)))
                img = img.convert("RGB").resize(new_size, Image.LANCZOS)
                buf = io.BytesIO()
                img.save(buf, format="PNG")
                data = buf.getvalue()
                mime = "image/png"
    except Exception as e:
        # Non-fatal: send the original bytes.
        print(f"WARNING: could not resize image ({e}); sending original", file=sys.stderr)
    b64 = base64.b64encode(data).decode("ascii")
    return f"data:{mime};base64,{b64}", mime


def query_llm(client: OpenAI, model: str, image_data_url: str, image_name: str,
              custom_prompt: str | None = None) -> str:
    # Qwen3 models emit a long internal "thinking" trace that counts toward
    # max_tokens and can leave content empty (finish_reason=length). Disable
    # thinking via the chat template kwarg exposed by the Hetzner endpoint.
    user_text = (
        f"Image under review: {image_name}\n\n"
        + (custom_prompt if custom_prompt else USER_PROMPT)
    )
    completion = client.chat.completions.create(
        model=model,
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT},
            {
                "role": "user",
                "content": [
                    {
                        "type": "text",
                        "text": user_text,
                    },
                    {
                        "type": "image_url",
                        "image_url": {"url": image_data_url},
                    },
                ],
            },
        ],
        temperature=0.2,
        max_tokens=4096,
        extra_body={"chat_template_kwargs": {"enable_thinking": False}},
    )
    msg = completion.choices[0].message
    # Some OpenAI-compatible servers expose the thinking trace separately as
    # "reasoning_content"; we only want the final answer.
    return getattr(msg, "content", None) or ""


def process_one(path: Path, client: OpenAI, model: str, as_json: bool,
                custom_prompt: str | None = None) -> dict:
    if not path.exists() or not path.is_file():
        raise FileNotFoundError(f"image not found: {path}")
    image_data_url, mime = encode_image(path)
    if as_json:
        # Stream nothing; just collect.
        pass
    response = query_llm(client, model, image_data_url, path.name, custom_prompt)
    verdict = "UNKNOWN"
    for line in response.splitlines():
        s = line.strip()
        if s.upper().startswith("VERDICT:"):
            verdict = s[len("VERDICT:"):].strip().upper()
            break
    return {
        "image": str(path),
        "verdict": verdict,
        "response": response,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Ask an LLM to spot issues in the right plot of a side-by-side "
                    "comparison image (left = reference).",
    )
    parser.add_argument(
        "images",
        nargs="+",
        help="Path(s) to comparison PNG image(s). Multiple paths run in batch.",
    )
    parser.add_argument(
        "--config",
        default=str(CONFIG_PATH),
        help=f"Path to LLM credentials YAML (default: {CONFIG_PATH})",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON instead of plain text.",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress per-image progress lines on stderr.",
    )
    parser.add_argument(
        "--prompt",
        default=None,
        help="Custom prompt to send to the LLM instead of the default review prompt. "
             "Use this to focus on specific aspects (e.g. tick label alignment).",
    )
    args = parser.parse_args(argv)

    cfg = load_config(Path(args.config))
    client = OpenAI(api_key=cfg["api_key"], base_url=cfg["base_url"])
    model = cfg["model"]

    # Expand any globs the shell didn't expand (defensive).
    paths: list[Path] = []
    for p in args.images:
        pp = Path(p)
        if pp.exists():
            paths.append(pp)
        else:
            print(f"ERROR: no such file: {p}", file=sys.stderr)
            return 2

    results = []
    failures = []
    for i, p in enumerate(paths, 1):
        if not args.quiet:
            print(f"[{i}/{len(paths)}] {p} ...", file=sys.stderr, flush=True)
        try:
            res = process_one(p, client, model, args.json, args.prompt)
        except Exception as e:
            failures.append({"image": str(p), "error": str(e)})
            if not args.quiet:
                print(f"  ERROR: {e}", file=sys.stderr)
            continue
        results.append(res)
        if not args.json:
            print(f"=== {p} ===")
            print(res["response"])
            print(f"[verdict] {res['verdict']}")
            print()

    if args.json:
        print(json.dumps(
            {"results": results, "failures": failures, "model": model},
            indent=2,
        ))

    if not args.quiet and failures:
        print(f"\n{len(failures)} image(s) failed.", file=sys.stderr)
    return 3 if failures and not results else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
