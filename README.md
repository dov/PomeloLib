# PomeloLib

The font-to-3D-glb baking engine and text layout engine behind
[Pomelo](https://github.com/dov/pomelo), split out into a standalone,
GUI-free, CMake-built library and set of command line tools so it can be
consumed as a dependency by Pomelo and other projects.

## Contents

- `engine` — static library: SVG/font tracing, bevel/extrusion meshing,
  glTF baking (`font3d*`), and text layout (`font3d-layout`).
- `pomelo-build-3d-font` — bakes every glyph of a ttf/otf font into a 3d
  mesh and packs the result into a single self-describing glb "3d font".
- `pomelo-layout-3d-text` — lays a string out with a baked 3d font.
- `pomelo-layout-server` — answers layout requests on stdin/stdout,
  keeping a baked font loaded between requests.
- `pomelo-cli` — general purpose engine CLI (svg/text -> beveled mesh).

None of this depends on gtkmm/goocanvas (GUI) or any web components; those
remain in the [pomelo](https://github.com/dov/pomelo) repository.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

Requires a C++20 compiler and, via pkg-config/find_package: glibmm-2.4,
pangomm-1.4, pangoft2, cairo, cairo-ft, harfbuzz, fribidi, glib-2.0,
freetype2, CGAL (with GMP/MPFR). fmt, spdlog, glm, nlohmann_json, nanosvg
and tinygltf are fetched automatically at configure time via
`FetchContent`.
