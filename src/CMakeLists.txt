# The lightweight text-layout library: shapes and bidi-orders text with
# harfbuzz/fribidi and lays it out against an already-baked 3D font. No
# CGAL/cairo/pango dependency, so it builds on boxes that don't have the
# baking engine's toolchain available.
set(LAYOUT_SOURCES
  svg-path-flatten.cc
  utils.cc
  material.cc
  mesh.cc
  font3d.cc
  font3d-layout.cc
)

add_library(layout STATIC ${LAYOUT_SOURCES})
add_library(PomeloLib::layout ALIAS layout)

target_include_directories(layout PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include/pomelolib>
)

target_compile_definitions(layout PUBLIC
  SPDLOG_FMT_EXTERNAL
)

target_link_libraries(layout PUBLIC
  PkgConfig::HARFBUZZ
  PkgConfig::FRIBIDI
  PkgConfig::GLIB
  fmt::fmt
  glm::glm
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  nanosvg
  tinygltf
)

# Lay a string out with a baked 3d font.
add_executable(pomelo-layout-3d-text pomelo-layout-3d-text.cc)
target_link_libraries(pomelo-layout-3d-text PRIVATE layout)

# Answer layout requests on stdin, keeping the 3d font loaded between
# them. This is what the 3dtext-creator web backend talks to.
add_executable(pomelo-layout-server pomelo-layout-server.cc)
target_link_libraries(pomelo-layout-server PRIVATE layout)

set(POMELOLIB_EXECUTABLES
  pomelo-layout-3d-text
  pomelo-layout-server
)

set(POMELOLIB_PUBLIC_HEADERS
  svg-path-flatten.h
  utils.h
  material.h
  mesh.h
  font3d.h
  font3d-layout.h
)

if(POMELOLIB_BUILD_BAKING)
  # The CGAL-based baking engine: font extrusion/beveling and the general
  # svg/text -> mesh pipeline. Everything here ultimately needs CGAL
  # (offsetting, straight skeletons) plus cairo/pango for rasterizing and
  # shaping the glyph outlines that get extruded.
  set(ENGINE_SOURCES
    textrusion.cc
    smooth-sharp-angles.cc
    svgpath-to-cairo.cc
    pango-to-cairo.cc
    profile.cc
    bezier-intersect.cc
    cairo-flatten-by-bitmap.cc
    image-tracer-bw.cc
    giv-debug-utils.cc
    mesh-utils.cc
    pomelo-engine.cc
    font3d-bake.cc
  )

  add_library(engine STATIC ${ENGINE_SOURCES})
  add_library(PomeloLib::engine ALIAS engine)

  target_compile_definitions(engine PUBLIC
    COMMIT_ID="${POMELOLIB_COMMIT_ID}"
    COMMIT_TIME="${POMELOLIB_COMMIT_TIME}"
    VERSION="${PROJECT_VERSION}"
  )

  target_link_libraries(engine PUBLIC
    layout
    PkgConfig::GLIBMM
    PkgConfig::PANGOMM
    PkgConfig::PANGOFT2
    PkgConfig::CAIRO
    PkgConfig::CAIROFT
    Freetype::Freetype
    CGAL::CGAL
    Threads::Threads
    ${GMP_LIBRARY}
    ${MPFR_LIBRARY}
  )

  # Bake every glyph of a ttf/otf font into a 3d mesh and pack the result
  # into a single glb "3d font".
  add_executable(pomelo-build-3d-font pomelo-build-3d-font.cc)
  target_link_libraries(pomelo-build-3d-font PRIVATE engine)

  # General purpose engine CLI (svg/text -> beveled mesh).
  add_executable(pomelo-cli pomelo-cli.cc)
  target_link_libraries(pomelo-cli PRIVATE engine)

  add_executable(test-svgpath-to-cairo test-svgpath-to-cairo.cc)
  target_link_libraries(test-svgpath-to-cairo PRIVATE engine)

  add_executable(test-svg-to-skeleton test-svg-to-skeleton.cc)
  target_link_libraries(test-svg-to-skeleton PRIVATE engine)

  add_executable(test-pango-to-cairo test-pango-to-cairo.cc)
  target_link_libraries(test-pango-to-cairo PRIVATE engine)

  list(APPEND POMELOLIB_EXECUTABLES
    pomelo-build-3d-font
    pomelo-cli
    test-svgpath-to-cairo
    test-svg-to-skeleton
    test-pango-to-cairo
  )

  list(APPEND POMELOLIB_PUBLIC_HEADERS
    textrusion.h
    smooth-sharp-angles.h
    svgpath-to-cairo.h
    pango-to-cairo.h
    profile.h
    bezier-intersect.h
    cairo-flatten-by-bitmap.h
    image-tracer-bw.h
    giv-debug-utils.h
    mesh-utils.h
    pomelo-engine.h
    font3d-bake.h
  )
endif()

install(TARGETS layout ${POMELOLIB_EXECUTABLES}
  EXPORT PomeloLibTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)
if(POMELOLIB_BUILD_BAKING)
  install(TARGETS engine EXPORT PomeloLibTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
endif()
install(FILES ${POMELOLIB_PUBLIC_HEADERS} DESTINATION include/pomelolib)
