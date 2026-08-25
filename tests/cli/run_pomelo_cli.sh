#!/usr/bin/env bash
# General purpose engine CLI: text markup traced and beveled into a mesh.
# Relies only on fontconfig's generic "Sans" alias, not a specific font,
# so it doesn't need the vendored test font.
set -euo pipefail

bin="$1"
out="$2"

"$bin" --markup "A" -o "$out"
test -s "$out"
