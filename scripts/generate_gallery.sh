#!/usr/bin/env bash
# Thin wrapper for generate_gallery.py — kept so existing docs/CI keep working.
# All real work (with live progress output) happens in the Python script.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$SCRIPT_DIR/generate_gallery.py" "$@"
