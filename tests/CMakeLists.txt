# Minimal dependency-free C++ tests; a non-zero exit code (from CHECK) fails.
# Pure-Rust model tests are registered in src/bridge/CMakeLists.txt.

add_executable(test_branding test_branding.cpp)
target_link_libraries(test_branding PRIVATE branding)
target_compile_definitions(test_branding PRIVATE
  EXPECTED_DISPLAY_NAME="${BRAND_DISPLAY_NAME}"
  EXPECTED_SLUG="${BRAND_SLUG}")
add_test(NAME test_branding COMMAND test_branding)

add_executable(test_geometry test_geometry.cpp)
target_link_libraries(test_geometry PRIVATE geometry)
add_test(NAME test_geometry COMMAND test_geometry)

# Rust timeline + C++ geometry across the CXX bridge.
add_executable(test_timeline test_timeline.cpp)
target_link_libraries(test_timeline PRIVATE model)
add_test(NAME test_timeline COMMAND test_timeline)

# Sketch API through the bridge, as the UI uses it.
add_executable(test_sketch test_sketch.cpp)
target_link_libraries(test_sketch PRIVATE model)
add_test(NAME test_sketch COMMAND test_sketch)
