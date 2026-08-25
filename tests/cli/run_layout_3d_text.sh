#!/usr/bin/env bash
# Bakes a font3d, then lays a string out with it and checks the result.
set -euo pipefail

build_bin="$1"
layout_bin="$2"
font="$3"
font3d="$4"
out="$5"

"$build_bin" "$font" --chars "AB" -o "$font3d"
"$layout_bin" --font3d "$font3d" --font "$font" --text "AB" -o "$out"
test -s "$out"
