#include "Kernel.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeArcOfEllipse.hxx>
#include <Geom_BSplineCurve.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <gp_Elips.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <ShapeFix_Face.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace geom {

namespace {

TopoDS_Shape prism(const TopoDS_Face& face, double distance) {
  if (face.IsNull() || distance == 0.0) return {};
  BRepPrimAPI_MakePrism maker(face, gp_Vec(0.0, 0.0, distance));
  return maker.IsDone() ? maker.Shape() : TopoDS_Shape();
}

}  // namespace

namespace {

gp_Pnt worldPoint(const Plane& p, double u, double v) {
  return gp_Pnt(p.origin[0] + u * p.xAxis[0] + v * p.yAxis[0], p.origin[1] + u * p.xAxis[1] + v * p.yAxis[1],
                p.origin[2] + u * p.xAxis[2] + v * p.yAxis[2]);
}

gp_Dir dir(const std::array<double, 3>& d) { return gp_Dir(d[0], d[1], d[2]); }

TopoDS_Edge makeEdge(const Plane& plane, const Seg2& s) {
  switch (s.kind) {
    case Seg2::Kind::Line:
      return BRepBuilderAPI_MakeEdge(worldPoint(plane, s.x0, s.y0), worldPoint(plane, s.x1, s.y1)).Edge();
    case Seg2::Kind::Circle: {
      gp_Circ circle(gp_Ax2(worldPoint(plane, s.cx, s.cy), dir(plane.normal), dir(plane.xAxis)), s.radius);
      return BRepBuilderAPI_MakeEdge(circle).Edge();
    }
    case Seg2::Kind::Arc: {
      // Through start, the middle of the sweep and end: no ambiguity about direction.
      constexpr double kTau = 6.283185307179586;
      const double a0 = std::atan2(s.y0 - s.cy, s.x0 - s.cx);
      const double a1 = std::atan2(s.y1 - s.cy, s.x1 - s.cx);
      double sweep = std::fmod(a1 - a0 + 2 * kTau, kTau);
      if (sweep < 1e-12) sweep = kTau;
      if (!s.ccw) sweep -= kTau;
      const double am = a0 + sweep / 2;
      GC_MakeArcOfCircle arc(worldPoint(plane, s.x0, s.y0),
                             worldPoint(plane, s.cx + s.radius * std::cos(am), s.cy + s.radius * std::sin(am)),
                             worldPoint(plane, s.x1, s.y1));
      if (!arc.IsDone()) return {};
      return BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
    }
    case Seg2::Kind::Ellipse:
    case Seg2::Kind::EllipseArc: {
      constexpr double kHalfPi = 1.5707963267948966;
      const double a = std::hypot(s.mx, s.my);
      if (a <= 0 || s.minor <= 0) return {};
      // OCCT wants the major radius ≥ the minor: swap the axes if needed,
      // which shifts the parameter by a quarter turn.
      std::array<double, 2> u{s.mx / a, s.my / a};
      double major = a, minor = s.minor, shift = 0;
      if (minor > major) {
        u = {-u[1], u[0]};
        std::swap(major, minor);
        shift = -kHalfPi;
      }
      const std::array<double, 3> xDir{u[0] * plane.xAxis[0] + u[1] * plane.yAxis[0],
                                       u[0] * plane.xAxis[1] + u[1] * plane.yAxis[1],
                                       u[0] * plane.xAxis[2] + u[1] * plane.yAxis[2]};
      const gp_Elips elips(gp_Ax2(worldPoint(plane, s.cx, s.cy), dir(plane.normal), dir(xDir)), major, minor);
      if (s.kind == Seg2::Kind::Ellipse) return BRepBuilderAPI_MakeEdge(elips).Edge();
      const double t0 = s.t0 + shift, t1 = s.t1 + shift;
      GC_MakeArcOfEllipse arc(elips, s.ccw ? t0 : t1, s.ccw ? t1 : t0, Standard_True);
      if (!arc.IsDone()) return {};
      TopoDS_Edge e = BRepBuilderAPI_MakeEdge(arc.Value()).Edge();
      return s.ccw ? e : TopoDS::Edge(e.Reversed());
    }
    case Seg2::Kind::Spline: {
      const int n = int(s.poles.size() / 2);
      if (n < 2 || s.degree < 1 || int(s.knots.size()) != n + s.degree + 1) return {};
      TColgp_Array1OfPnt poles(1, n);
      TColStd_Array1OfReal weights(1, n);
      for (int i = 0; i < n; ++i) {
        poles.SetValue(i + 1, worldPoint(plane, s.poles[2 * i], s.poles[2 * i + 1]));
        weights.SetValue(i + 1, i < int(s.weights.size()) ? s.weights[i] : 1.0);
      }
      // Full knot vector → distinct knots with multiplicities.
      std::vector<double> distinct;
      std::vector<int> mults;
      for (double k : s.knots) {
        if (!distinct.empty() && std::abs(k - distinct.back()) < 1e-12) {
          ++mults.back();
        } else {
          distinct.push_back(k);
          mults.push_back(1);
        }
      }
      TColStd_Array1OfReal knots(1, int(distinct.size()));
      TColStd_Array1OfInteger multiplicities(1, int(distinct.size()));
      for (int i = 0; i < int(distinct.size()); ++i) {
        knots.SetValue(i + 1, distinct[i]);
        multiplicities.SetValue(i + 1, mults[i]);
      }
      Handle(Geom_BSplineCurve) curve = new Geom_BSplineCurve(poles, weights, knots, multiplicities, s.degree);
      TopoDS_Edge e = BRepBuilderAPI_MakeEdge(curve).Edge();
      return s.reversed ? TopoDS::Edge(e.Reversed()) : e;
    }
  }
  return {};
}

TopoDS_Wire makeWire(const Plane& plane, const Loop2& loop) {
  BRepBuilderAPI_MakeWire wire;
  for (const Seg2& s : loop) {
    const TopoDS_Edge edge = makeEdge(plane, s);
    if (edge.IsNull()) return {};
    wire.Add(edge);
    if (!wire.IsDone()) return {};
  }
  return wire.IsDone() ? wire.Wire() : TopoDS_Wire();
}

}  // namespace

TopoDS_Shape makeRegionFace(const Plane& plane, const Loop2& outer, const std::vector<Loop2>& holes) {
  if (outer.empty()) return {};
  try {
    const TopoDS_Wire outerWire = makeWire(plane, outer);
    if (outerWire.IsNull()) return {};
    const gp_Pln pln(gp_Ax3(worldPoint(plane, 0, 0), dir(plane.normal), dir(plane.xAxis)));
    BRepBuilderAPI_MakeFace face(pln, outerWire, Standard_True);
    // Loops arrive counter-clockwise (profile detection guarantees it); a hole
    // must run the other way round to cut the face, so it is reversed.
    for (const Loop2& hole : holes) {
      const TopoDS_Wire w = makeWire(plane, hole);
      if (w.IsNull()) return {};
      face.Add(TopoDS::Wire(w.Reversed()));
    }
    if (!face.IsDone()) return {};
    ShapeFix_Face fix(face.Face());  // tolerances, small gaps
    fix.Perform();
    return fix.Face();
  } catch (const Standard_Failure&) {
    return {};
  }
}

TopoDS_Shape extrudeRegion(const Plane& plane, const Loop2& outer, const std::vector<Loop2>& holes,
                           double distance) {
  if (distance == 0.0) return {};
  const TopoDS_Shape face = makeRegionFace(plane, outer, holes);
  if (face.IsNull()) return {};
  const gp_Vec v(plane.normal[0] * distance, plane.normal[1] * distance, plane.normal[2] * distance);
  BRepPrimAPI_MakePrism prism(face, v);
  return prism.IsDone() ? prism.Shape() : TopoDS_Shape();
}

TopoDS_Shape makeCurves(const Plane& plane, const std::vector<Seg2>& segments) {
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  for (const Seg2& s : segments) {
    try {
      const TopoDS_Edge e = makeEdge(plane, s);
      if (!e.IsNull()) builder.Add(compound, e);
    } catch (const Standard_Failure&) {
      // Skip degenerate preview geometry (zero-length line, collinear arc points).
    }
  }
  return compound;
}

TopoDS_Shape makeVertices(const Plane& plane, const std::vector<Point2>& points) {
  BRep_Builder builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  for (const Point2& p : points) builder.Add(compound, BRepBuilderAPI_MakeVertex(worldPoint(plane, p[0], p[1])).Vertex());
  return compound;
}

TopoDS_Shape makePolygonFace(const Plane& plane, const std::vector<std::vector<Point2>>& loops) {
  if (loops.empty() || loops[0].size() < 3) return {};
  try {
    auto wire = [&plane](const std::vector<Point2>& pts) {
      BRepBuilderAPI_MakePolygon poly;
      for (const Point2& p : pts) poly.Add(worldPoint(plane, p[0], p[1]));
      poly.Close();
      return poly.IsDone() ? poly.Wire() : TopoDS_Wire();
    };
    const TopoDS_Wire outer = wire(loops[0]);
    if (outer.IsNull()) return {};
    const gp_Pln pln(gp_Ax3(worldPoint(plane, 0, 0), dir(plane.normal), dir(plane.xAxis)));
    BRepBuilderAPI_MakeFace face(pln, outer, Standard_True);
    for (std::size_t i = 1; i < loops.size(); ++i) {
      if (loops[i].size() < 3) continue;
      const TopoDS_Wire w = wire(loops[i]);
      if (!w.IsNull()) face.Add(TopoDS::Wire(w.Reversed()));
    }
    if (!face.IsDone()) return {};
    return face.Face();
  } catch (const Standard_Failure&) {
    return {};
  }
}

double area(const TopoDS_Shape& shape) {
  if (shape.IsNull()) return 0.0;
  GProp_GProps props;
  BRepGProp::SurfaceProperties(shape, props);
  return props.Mass();
}

TopoDS_Shape makeBox(double dx, double dy, double dz) {
  return BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
}

TopoDS_Shape extrude(const RectProfile& p, double distance) {
  if (p.width <= 0.0 || p.height <= 0.0) return {};
  const double x0 = p.x, y0 = p.y, x1 = p.x + p.width, y1 = p.y + p.height;
  BRepBuilderAPI_MakePolygon poly(gp_Pnt(x0, y0, 0), gp_Pnt(x1, y0, 0), gp_Pnt(x1, y1, 0),
                                  gp_Pnt(x0, y1, 0), Standard_True /*close*/);
  if (!poly.IsDone()) return {};
  BRepBuilderAPI_MakeFace face(poly.Wire(), Standard_True /*planar*/);
  return face.IsDone() ? prism(face.Face(), distance) : TopoDS_Shape();
}

TopoDS_Shape extrude(const CircleProfile& p, double distance) {
  if (p.radius <= 0.0) return {};
  gp_Circ circle(gp_Ax2(gp_Pnt(p.cx, p.cy, 0), gp::DZ()), p.radius);
  BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(circle).Edge());
  BRepBuilderAPI_MakeFace face(wire.Wire(), Standard_True);
  return face.IsDone() ? prism(face.Face(), distance) : TopoDS_Shape();
}

TopoDS_Shape filletAllEdges(const TopoDS_Shape& shape, double radius) {
  if (shape.IsNull() || radius <= 0.0) return {};
  try {
    BRepFilletAPI_MakeFillet fillet(shape);
    for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
      fillet.Add(radius, TopoDS::Edge(ex.Current()));
    }
    fillet.Build();
    return fillet.IsDone() ? fillet.Shape() : TopoDS_Shape();
  } catch (const Standard_Failure&) {
    return {};
  }
}

TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  BRepAlgoAPI_Fuse op(a, b);
  return op.IsDone() ? op.Shape() : TopoDS_Shape();
}

TopoDS_Shape cut(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  BRepAlgoAPI_Cut op(a, b);
  return op.IsDone() ? op.Shape() : TopoDS_Shape();
}

double volume(const TopoDS_Shape& shape) {
  if (shape.IsNull()) return 0.0;
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  return props.Mass();
}

bool isValid(const TopoDS_Shape& shape) {
  return !shape.IsNull() && BRepCheck_Analyzer(shape).IsValid();
}

bool exportStep(const TopoDS_Shape& shape, const std::string& path) {
  if (shape.IsNull()) return false;
  STEPControl_Writer writer;
  if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) return false;
  return writer.Write(path.c_str()) == IFSelect_RetDone;
}

}  // namespace geom
