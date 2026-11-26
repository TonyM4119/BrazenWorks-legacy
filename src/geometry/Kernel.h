#pragma once

#include <TopoDS_Shape.hxx>

#include <string>

// Thin facade over Open CASCADE. Keep OCCT algorithm headers out of the rest
// of the codebase: add a function here instead of calling BRep*API directly.
namespace geom {

// A closed rectangular profile on the XY plane; (x, y) is its lower-left corner.
struct RectProfile {
  double width = 10.0;
  double height = 10.0;
  double x = 0.0;
  double y = 0.0;
};

// A closed circular profile on the XY plane, centred on (cx, cy).
struct CircleProfile {
  double radius = 5.0;
  double cx = 0.0;
  double cy = 0.0;
};

TopoDS_Shape makeBox(double dx, double dy, double dz);

// Extrude a profile along +Z by `distance` (negative extrudes along -Z).
TopoDS_Shape extrude(const RectProfile& profile, double distance);
TopoDS_Shape extrude(const CircleProfile& profile, double distance);

// Round every edge of `shape`. Returns a null shape if the fillet fails.
TopoDS_Shape filletAllEdges(const TopoDS_Shape& shape, double radius);

// Boolean operations. Return a null shape on failure.
TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b);
TopoDS_Shape cut(const TopoDS_Shape& a, const TopoDS_Shape& b);

double volume(const TopoDS_Shape& shape);
bool isValid(const TopoDS_Shape& shape);

bool exportStep(const TopoDS_Shape& shape, const std::string& path);

}  // namespace geom
