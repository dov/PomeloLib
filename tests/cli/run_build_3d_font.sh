#!/usr/bin/env bash
# Bakes a couple of glyphs into a glb and checks a non-empty file came out.
set -euo pipefail

bin="$1"
font="$2"
out="$3"

"$bin" "$font" --chars "AB" -o "$out"
test -s "$out"
