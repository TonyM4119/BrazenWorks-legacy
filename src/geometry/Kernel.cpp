#include "Kernel.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
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

namespace geom {

namespace {

TopoDS_Shape prism(const TopoDS_Face& face, double distance) {
  if (face.IsNull() || distance == 0.0) return {};
  BRepPrimAPI_MakePrism maker(face, gp_Vec(0.0, 0.0, distance));
  return maker.IsDone() ? maker.Shape() : TopoDS_Shape();
}

}  // namespace

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
