include(FetchContent)

FetchContent_Declare(
  doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
  URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
)
FetchContent_MakeAvailable(doctest)

# test_bezier_intersect/test_profile exercise the CGAL-based baking engine
# (profile.cc, bezier-intersect.cc); the rest only need the lightweight
# layout library, so they build regardless of POMELOLIB_BUILD_BAKING.
add_executable(pomelolib_unit_tests
  unit/test_utils.cc
  unit/test_material.cc
  unit/test_svg_path_flatten.cc
)
target_link_libraries(pomelolib_unit_tests PRIVATE layout doctest::doctest_with_main)

if(POMELOLIB_BUILD_BAKING)
  target_sources(pomelolib_unit_tests PRIVATE
    unit/test_bezier_intersect.cc
    unit/test_profile.cc
  )
  target_link_libraries(pomelolib_unit_tests PRIVATE engine)
endif()

add_test(NAME unit_tests COMMAND pomelolib_unit_tests)

# End to end smoke tests driving the real CLI tools, matching how a
# consumer (e.g. the 3dtext-creator web backend) actually calls them. All
# of these need pomelo-build-3d-font to bake a test font first, so they
# only exist when the baking engine is being built.
if(POMELOLIB_BUILD_BAKING)
  set(POMELOLIB_TEST_FONT ${CMAKE_CURRENT_SOURCE_DIR}/data/DejaVuSans-Bold.ttf)

  add_test(NAME cli_build_3d_font
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli/run_build_3d_font.sh
            $<TARGET_FILE:pomelo-build-3d-font>
            ${POMELOLIB_TEST_FONT}
            ${CMAKE_CURRENT_BINARY_DIR}/cli_build_3d_font.glb
  )

  add_test(NAME cli_layout_3d_text
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli/run_layout_3d_text.sh
            $<TARGET_FILE:pomelo-build-3d-font>
            $<TARGET_FILE:pomelo-layout-3d-text>
            ${POMELOLIB_TEST_FONT}
            ${CMAKE_CURRENT_BINARY_DIR}/cli_layout_3d_text_font.glb
            ${CMAKE_CURRENT_BINARY_DIR}/cli_layout_3d_text_out.glb
  )

  add_test(NAME cli_layout_server
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli/run_layout_server.sh
            $<TARGET_FILE:pomelo-build-3d-font>
            $<TARGET_FILE:pomelo-layout-server>
            ${POMELOLIB_TEST_FONT}
            ${CMAKE_CURRENT_BINARY_DIR}/cli_layout_server_font.glb
  )

  add_test(NAME cli_pomelo_cli
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/cli/run_pomelo_cli.sh
            $<TARGET_FILE:pomelo-cli>
            ${CMAKE_CURRENT_BINARY_DIR}/cli_pomelo_cli_out.stl
  )
endif()
