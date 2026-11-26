#pragma once

// C++ half of the CXX bridge declared in crates/model_core/src/ffi.rs.
// The Rust model calls these to do geometry on Open CASCADE.

#include <TopoDS_Shape.hxx>

#include <memory>

namespace model {

// Shared structs, defined by the generated bridge header.
struct Profile;
struct PlaneInfo;
struct RegionData;

struct Shape {
  TopoDS_Shape value;
};

bool shape_is_null(const Shape& shape);
std::unique_ptr<Shape> geom_extrude(const Profile& profile, double distance);
std::unique_ptr<Shape> geom_fuse(const Shape& a, const Shape& b);
std::unique_ptr<Shape> geom_cut(const Shape& a, const Shape& b);
std::unique_ptr<Shape> geom_fillet_all_edges(const Shape& shape, double radius);
std::unique_ptr<Shape> geom_extrude_region(const PlaneInfo& plane, const RegionData& region, double distance);

}  // namespace model
