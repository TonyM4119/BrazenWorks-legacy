#pragma once

#include "Kernel.h"
#include "Viewport.h"
#include "model_core/src/ffi.rs.h"  // Rust timeline API (generated)

#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <vector>

// Create Sketch: the three origin planes, drawn translucent; click one.
class PlanePicker : public ViewportTool {
 public:
  PlanePicker(Viewport* view, double halfSize, std::function<void(model::OriginPlaneKind)> picked);
  ~PlanePicker() override;

  bool mousePress(QMouseEvent* e) override;
  bool mouseMove(QMouseEvent* e) override;
  bool mouseRelease(QMouseEvent* e) override;

 private:
  int planeAt(const QPointF& pos) const;  // 0 XY, 1 XZ, 2 YZ, -1 none
  void draw();

  Viewport* view_;
  double half_;
  std::function<void(model::OriginPlaneKind)> picked_;
  int hover_ = -1;
};

// Extrude: closed profiles of the sketches, click to pick (smallest one under
// the cursor). Profiles come from one sketch at a time.
class ProfilePicker : public ViewportTool {
 public:
  using Changed = std::function<void(std::uint64_t sketch, const QStringList& keys)>;
  ProfilePicker(Viewport* view, model::Timeline& timeline, const std::vector<std::uint64_t>& sketches,
                Changed changed);
  ~ProfilePicker() override;

  bool mousePress(QMouseEvent* e) override;
  bool mouseMove(QMouseEvent* e) override;
  bool mouseRelease(QMouseEvent* e) override;

  std::size_t profileCount() const { return profiles_.size(); }
  // Picks the only profile when there is exactly one.
  void selectIfSingle();
  std::uint64_t sketch() const { return sketch_; }
  const QStringList& keys() const { return keys_; }

 private:
  struct Profile {
    std::uint64_t sketch;
    QString key;
    double area;
    geom::Plane plane;
    std::vector<std::vector<geom::Point2>> loops;
    TopoDS_Shape face;
  };
  int profileAt(const QPointF& pos) const;
  void draw();
  void notify();

  Viewport* view_;
  std::vector<Profile> profiles_;
  std::vector<std::pair<geom::Plane, TopoDS_Shape>> curves_;  // sketch geometry per sketch
  Changed changed_;
  std::uint64_t sketch_ = 0;
  QStringList keys_;
  int hover_ = -1;
};

// Sketch plane (from the bridge) as a kernel plane.
geom::Plane toGeomPlane(const model::PlaneInfo& p);
// All curves of a sketch (non-construction), for display.
TopoDS_Shape sketchCurves(model::Timeline& t, std::uint64_t sketch, const geom::Plane& plane);
// The profiles of a sketch as polygon loops (first = outer).
std::vector<std::vector<geom::Point2>> profileLoops(const model::ProfileInfo& p);
