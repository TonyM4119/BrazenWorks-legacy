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
