# Third-party dependencies for the pomelo engine.
#
# fmt/glm/spdlog/nlohmann_json are fetched at configure time and pinned to
# the same versions the sister GUI project (pomelo) vendors as meson
# subprojects. nanosvg and tinygltf are header-only forks with no usable
# CMake target of their own, so we fetch their sources and wrap them in
# INTERFACE libraries by hand.

include(FetchContent)

# Some vendored versions (nlohmann_json 3.9.1, glm 1.0.1) ship a
# cmake_minimum_required() older than modern CMake still accepts.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# fmt is declared with OVERRIDE_FIND_PACKAGE so that spdlog's own
# find_package(fmt) (triggered below by SPDLOG_FMT_EXTERNAL) resolves to
# this same fetched copy instead of searching the system.
FetchContent_Declare(
  fmt
  URL https://github.com/fmtlib/fmt/archive/11.2.0.tar.gz
  URL_HASH SHA256=bc23066d87ab3168f27cef3e97d545fa63314f5c79df5ea444d41d56f962c6af
  OVERRIDE_FIND_PACKAGE
)
find_package(fmt REQUIRED)

# v1.13.0 (the version pomelo vendors as a meson subproject) only builds
# against fmt 11 with a wrapdb-only compatibility patch we don't have
# here; v1.15.3 supports fmt 11 natively.
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
  URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
)

FetchContent_Declare(
  glm
  URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
  URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c
)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.9.1.tar.gz
  URL_HASH SHA256=4cf0df69731494668bdd6460ed8cb269b68de9c19ad8c27abc24cd72605b2d5b
)

FetchContent_MakeAvailable(spdlog glm nlohmann_json)

# nanosvg: single header, no build system of its own.
FetchContent_Declare(
  nanosvg
  GIT_REPOSITORY https://github.com/dov/nanosvg.git
  GIT_TAG master
)
FetchContent_MakeAvailable(nanosvg)
add_library(nanosvg INTERFACE)
target_include_directories(nanosvg INTERFACE ${nanosvg_SOURCE_DIR}/src)

# tinygltf: header-only fork. Its own CMakeLists.txt only adds an unwanted
# loader_example executable by default; disable that and use it purely for
# its include directory.
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/dov/tinygltf
  GIT_TAG meson
)
FetchContent_MakeAvailable(tinygltf)
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${tinygltf_SOURCE_DIR})

# System libraries, discovered via pkg-config.
#
# harfbuzz/fribidi/glib are all the lightweight layout engine needs to shape
# and bidi-order text; glibmm/pangomm/cairo and CGAL/GMP/MPFR are only
# needed by the CGAL-based 3D baking engine (extrusion, offsetting, mesh
# construction), so they're gated behind POMELOLIB_BUILD_BAKING to let
# consumers that only need text layout skip that dependency stack.
find_package(PkgConfig REQUIRED)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(FRIBIDI REQUIRED IMPORTED_TARGET fribidi)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

if(POMELOLIB_BUILD_BAKING)
  pkg_check_modules(GLIBMM REQUIRED IMPORTED_TARGET glibmm-2.4)
  pkg_check_modules(PANGOMM REQUIRED IMPORTED_TARGET pangomm-1.4)
  pkg_check_modules(PANGOFT2 REQUIRED IMPORTED_TARGET pangoft2)
  pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
  pkg_check_modules(CAIROFT REQUIRED IMPORTED_TARGET cairo-ft)

  find_package(Freetype REQUIRED)
  find_package(CGAL REQUIRED)
  find_package(Threads REQUIRED)

  find_library(GMP_LIBRARY gmp REQUIRED)
  find_library(MPFR_LIBRARY mpfr REQUIRED)
endif()
