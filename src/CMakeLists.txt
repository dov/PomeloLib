set(ENGINE_SOURCES
  textrusion.cc
  smooth-sharp-angles.cc
  svgpath-to-cairo.cc
  svg-path-flatten.cc
  pango-to-cairo.cc
  profile.cc
  material.cc
  bezier-intersect.cc
  cairo-flatten-by-bitmap.cc
  image-tracer-bw.cc
  utils.cc
  giv-debug-utils.cc
  mesh.cc
  mesh-utils.cc
  pomelo-engine.cc
  font3d.cc
  font3d-bake.cc
  font3d-layout.cc
)

add_library(engine STATIC ${ENGINE_SOURCES})
add_library(PomeloLib::engine ALIAS engine)

target_include_directories(engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include/pomelolib>
)

target_compile_definitions(engine PUBLIC
  COMMIT_ID="${POMELOLIB_COMMIT_ID}"
  COMMIT_TIME="${POMELOLIB_COMMIT_TIME}"
  VERSION="${PROJECT_VERSION}"
  SPDLOG_FMT_EXTERNAL
)

target_link_libraries(engine PUBLIC
  PkgConfig::GLIBMM
  PkgConfig::PANGOMM
  PkgConfig::PANGOFT2
  PkgConfig::CAIRO
  PkgConfig::CAIROFT
  PkgConfig::HARFBUZZ
  PkgConfig::FRIBIDI
  PkgConfig::GLIB
  Freetype::Freetype
  CGAL::CGAL
  Threads::Threads
  ${GMP_LIBRARY}
  ${MPFR_LIBRARY}
  fmt::fmt
  glm::glm
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  nanosvg
  tinygltf
)

# Bake every glyph of a ttf/otf font into a 3d mesh and pack the result
# into a single glb "3d font".
add_executable(pomelo-build-3d-font pomelo-build-3d-font.cc)
target_link_libraries(pomelo-build-3d-font PRIVATE engine)

# Lay a string out with a baked 3d font.
add_executable(pomelo-layout-3d-text pomelo-layout-3d-text.cc)
target_link_libraries(pomelo-layout-3d-text PRIVATE engine)

# Answer layout requests on stdin, keeping the 3d font loaded between
# them. This is what the 3dtext-creator web backend talks to.
add_executable(pomelo-layout-server pomelo-layout-server.cc)
target_link_libraries(pomelo-layout-server PRIVATE engine)

# General purpose engine CLI (svg/text -> beveled mesh).
add_executable(pomelo-cli pomelo-cli.cc)
target_link_libraries(pomelo-cli PRIVATE engine)

add_executable(test-svgpath-to-cairo test-svgpath-to-cairo.cc)
target_link_libraries(test-svgpath-to-cairo PRIVATE engine)

add_executable(test-svg-to-skeleton test-svg-to-skeleton.cc)
target_link_libraries(test-svg-to-skeleton PRIVATE engine)

add_executable(test-pango-to-cairo test-pango-to-cairo.cc)
target_link_libraries(test-pango-to-cairo PRIVATE engine)

set(POMELOLIB_EXECUTABLES
  pomelo-build-3d-font
  pomelo-layout-3d-text
  pomelo-layout-server
  pomelo-cli
  test-svgpath-to-cairo
  test-svg-to-skeleton
  test-pango-to-cairo
)

set(POMELOLIB_PUBLIC_HEADERS
  textrusion.h
  smooth-sharp-angles.h
  svgpath-to-cairo.h
  svg-path-flatten.h
  pango-to-cairo.h
  profile.h
  material.h
  bezier-intersect.h
  cairo-flatten-by-bitmap.h
  image-tracer-bw.h
  utils.h
  giv-debug-utils.h
  mesh.h
  mesh-utils.h
  pomelo-engine.h
  font3d.h
  font3d-bake.h
  font3d-layout.h
)

install(TARGETS engine ${POMELOLIB_EXECUTABLES}
  EXPORT PomeloLibTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)
install(FILES ${POMELOLIB_PUBLIC_HEADERS} DESTINATION include/pomelolib)
