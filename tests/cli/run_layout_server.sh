#!/usr/bin/env bash
# Bakes a font3d, then checks the layout server answers a request with
# "ok":true on both the font-info line and the layout response line.
set -euo pipefail

build_bin="$1"
server_bin="$2"
font="$3"
font3d="$4"

"$build_bin" "$font" --chars "HI" -o "$font3d"

response=$(printf '{"text":"HI"}\n' | "$server_bin" --font3d "$font3d" --font "$font")

lines=$(echo "$response" | wc -l)
if [ "$lines" -lt 2 ]; then
  echo "expected a font-info line and a layout response line, got:" >&2
  echo "$response" >&2
  exit 1
fi

if ! echo "$response" | grep -q '"ok":true'; then
  echo "expected \"ok\":true somewhere in the response, got:" >&2
  echo "$response" >&2
  exit 1
fi
