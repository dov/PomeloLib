cmake_minimum_required(VERSION 3.24)

project(PomeloLib
  VERSION 0.0.9
  DESCRIPTION "Font-to-3D-glb baking and text layout engine used by Pomelo"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-flarge-source-files)
endif()

# Put every executable (engine tools and, if enabled, the unit test
# binary) in one place, rather than scattered across their per-target
# subdirectories under the build tree. Guarded so that consuming
# PomeloLib via add_subdirectory()/FetchContent doesn't redirect the
# parent project's own executables into PomeloLib's build directory.
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

option(POMELOLIB_BUILD_BAKING
  "Build the CGAL-based 3D font baking engine and its CLI tools \
(pomelo-build-3d-font, pomelo-cli). Requires CGAL/GMP/MPFR/cairo/pango. \
When OFF, only the lightweight text-layout library and \
pomelo-layout-3d-text/pomelo-layout-server are built." ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(cmake/Version.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
  add_subdirectory(tests)
endif()
