# geometry — the ONLY layer that talks to Open CASCADE modeling APIs.
# Everything above it (model, app) goes through this facade, so the kernel
# can be upgraded or swapped without touching feature or UI code.
add_library(geometry
  Kernel.cpp
  Kernel.h)

target_include_directories(geometry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# SYSTEM: don't report warnings from inside OCCT's own headers.
target_include_directories(geometry SYSTEM PUBLIC "${OpenCASCADE_INCLUDE_DIR}")

target_link_libraries(geometry PUBLIC
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo
  TKTopAlgo TKPrim TKBO TKFillet TKShHealing TKMesh
  TKXSBase TKSTEP)
