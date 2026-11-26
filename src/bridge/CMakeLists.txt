# Builds the Rust model crate with Cargo and exposes it to C++ as the `model`
# target. Linking `model` gives you the timeline API (generated header
# "model_core/src/ffi.rs.h") plus the geometry it calls.

find_program(CARGO_EXECUTABLE cargo
  HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)
find_package(Threads REQUIRED)

set(CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
set(MODEL_CRATE_DIR "${CMAKE_SOURCE_DIR}/crates/model_core")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_cargo_profile_flag "")
  set(_cargo_profile_dir "debug")
else()
  set(_cargo_profile_flag "--release")
  set(_cargo_profile_dir "release")
endif()

if(MSVC)
  set(MODEL_RUST_LIB "${CARGO_TARGET_DIR}/${_cargo_profile_dir}/model_core.lib")
else()
  set(MODEL_RUST_LIB "${CARGO_TARGET_DIR}/${_cargo_profile_dir}/libmodel_core.a")
endif()
set(MODEL_BRIDGE_HEADER "${CARGO_TARGET_DIR}/cxxbridge/model_core/src/ffi.rs.h")

file(GLOB_RECURSE _crate_sources CONFIGURE_DEPENDS
  "${MODEL_CRATE_DIR}/src/*.rs")

add_custom_command(
  OUTPUT "${MODEL_RUST_LIB}" "${MODEL_BRIDGE_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E env
          "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}"
          "OCCT_INCLUDE_DIR=${OpenCASCADE_INCLUDE_DIR}"
          "BRIDGE_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          "${CARGO_EXECUTABLE}" build ${_cargo_profile_flag} --features ffi
          --manifest-path "${MODEL_CRATE_DIR}/Cargo.toml"
  DEPENDS ${_crate_sources}
          "${MODEL_CRATE_DIR}/Cargo.toml"
          "${MODEL_CRATE_DIR}/build.rs"
          "${CMAKE_CURRENT_SOURCE_DIR}/GeometryBridge.h"
  COMMENT "Building Rust crate model_core (cargo ${_cargo_profile_dir})"
  USES_TERMINAL
  VERBATIM)
add_custom_target(model_core_cargo DEPENDS "${MODEL_RUST_LIB}" "${MODEL_BRIDGE_HEADER}")

# C++ implementations of the geometry functions the Rust side calls.
add_library(geometry_bridge STATIC GeometryBridge.cpp GeometryBridge.h)
add_dependencies(geometry_bridge model_core_cargo)
target_include_directories(geometry_bridge PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CARGO_TARGET_DIR}/cxxbridge")
target_link_libraries(geometry_bridge PUBLIC geometry)

# What C++ code links against. Order matters for static linking:
# Rust lib -> bridge (C++ functions Rust calls) -> geometry -> OCCT.
add_library(model INTERFACE)
add_dependencies(model model_core_cargo)
target_link_libraries(model INTERFACE
  "${MODEL_RUST_LIB}"
  geometry_bridge
  Threads::Threads
  ${CMAKE_DL_LIBS})
if(WIN32)
  # System libraries the Rust standard library needs on Windows
  # (rustc --print native-static-libs). synchronization = WaitOnAddress.
  target_link_libraries(model INTERFACE
    ws2_32 userenv ntdll bcrypt synchronization advapi32 kernel32 dbghelp)
endif()

# Run the pure-Rust unit tests (mock kernel, no OCCT) as part of ctest.
if(APP_BUILD_TESTS)
  add_test(NAME rust_model_tests
    COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${CARGO_TARGET_DIR}/test"
            "${CARGO_EXECUTABLE}" test --manifest-path "${MODEL_CRATE_DIR}/Cargo.toml")
endif()
