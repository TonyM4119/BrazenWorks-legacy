#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <string>
#include <vector>

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

// A sketch plane in world coordinates: (u, v) maps to origin + u*xAxis + v*yAxis.
struct Plane {
  std::array<double, 3> origin{0, 0, 0};
  std::array<double, 3> xAxis{1, 0, 0};
  std::array<double, 3> yAxis{0, 1, 0};
  std::array<double, 3> normal{0, 0, 1};
};

// One oriented piece of a closed 2D loop, in sketch (u, v) coordinates.
struct Seg2 {
  enum class Kind { Line, Arc, Circle } kind = Kind::Line;
  double x0 = 0, y0 = 0;  // start (line, arc)
  double x1 = 0, y1 = 0;  // end (line, arc)
  double cx = 0, cy = 0;  // centre (arc, circle)
  double radius = 0;      // arc, circle
  bool ccw = true;        // arc direction
};
using Loop2 = std::vector<Seg2>;

TopoDS_Shape makeBox(double dx, double dy, double dz);

// A planar face from an outer loop and holes. Null on failure.
TopoDS_Shape makeRegionFace(const Plane& plane, const Loop2& outer, const std::vector<Loop2>& holes);

// Extrudes a region along the plane normal by `distance`. Null on failure.
TopoDS_Shape extrudeRegion(const Plane& plane, const Loop2& outer, const std::vector<Loop2>& holes, double distance);

double area(const TopoDS_Shape& shape);

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
