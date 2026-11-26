cmake_minimum_required(VERSION 3.21)

# Branding must load before project() so the project takes its name from branding.json.
include(cmake/Branding.cmake)

project(${BRAND_SLUG}
  VERSION 0.1.0
  DESCRIPTION "${BRAND_TAGLINE}"
  HOMEPAGE_URL "${BRAND_REPO_URL}"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  # Source files are UTF-8; keep <windows.h> (pulled in by OCCT) from
  # defining min/max macros that break std::min/std::max.
  add_compile_options(/utf-8 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
  # The Rust library is always built against the release C runtime.
  # Mixing it with a Debug (/MDd) C++ build fails at link time.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "On Windows use -DCMAKE_BUILD_TYPE=RelWithDebInfo (debuggable) "
                    "or Release; Debug mixes C runtimes with the Rust library.")
  endif()
endif()

# Put the app and tests directly in the build folder (build/dev/brazenworks),
# so on Windows the DLLs copied next to them are shared.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(APP_BUILD_TESTS "Build unit tests" ON)
option(APP_BUILD_GUI   "Build the desktop application (needs Qt 6)" ON)
if(APP_BUILD_TESTS)
  enable_testing()
endif()

# ---- Dependencies -----------------------------------------------------------
# OCCT's version file rejects "any newer minor", so check the minimum by hand.
find_package(OpenCASCADE REQUIRED)
if(OpenCASCADE_VERSION VERSION_LESS 7.6)
  message(FATAL_ERROR "Open CASCADE 7.6 or newer is required (found ${OpenCASCADE_VERSION})")
endif()
message(STATUS "Open CASCADE ${OpenCASCADE_VERSION}: ${OpenCASCADE_INCLUDE_DIR}")

if(APP_BUILD_GUI)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)
endif()

# ---- Generated branding header ---------------------------------------------
# Code includes <Branding.h>; it never hard-codes the product name.
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/Branding.h.in "${GENERATED_DIR}/Branding.h" @ONLY)
add_library(branding INTERFACE)
target_include_directories(branding INTERFACE "${GENERATED_DIR}")

# ---- Targets -----------------------------------------------------------------
add_subdirectory(src/geometry)   # C++: Open CASCADE facade
add_subdirectory(src/bridge)     # Rust model crate (crates/model_core) + CXX bridge
if(APP_BUILD_GUI)
  add_subdirectory(src/app)
endif()

if(APP_BUILD_TESTS)
  add_subdirectory(tests)
endif()
