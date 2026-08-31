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

path_out="${out%.glb}_path.glb"
"$layout_bin" --font3d "$font3d" --font "$font" --text "AB" \
              --path "M0,0 Q100,150 200,0" -o "$path_out"
test -s "$path_out"
