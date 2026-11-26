#include "Pickers.h"

#include "Theme.h"

#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {

bool inPolygon(const std::vector<geom::Point2>& poly, double x, double y) {
  bool inside = false;
  for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const auto& a = poly[i];
    const auto& b = poly[j];
    if ((a[1] > y) != (b[1] > y) && x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]) inside = !inside;
  }
  return inside;
}

// Ray → (u, v) on a plane; false if parallel. `t` = distance along the ray.
bool rayPlane(const Viewport* view, const QPointF& pos, const geom::Plane& pl, double& u, double& v, double& t) {
  const auto [p, d] = view->rayAt(pos);
  const gp_Vec n(pl.normal[0], pl.normal[1], pl.normal[2]);
  const gp_Pnt o(pl.origin[0], pl.origin[1], pl.origin[2]);
  const double denom = gp_Vec(d).Dot(n);
  if (std::abs(denom) < 1e-9) return false;
  t = gp_Vec(p, o).Dot(n) / denom;
  const gp_Vec rel(o, p.Translated(gp_Vec(d) * t));
  u = rel.Dot(gp_Vec(pl.xAxis[0], pl.xAxis[1], pl.xAxis[2]));
  v = rel.Dot(gp_Vec(pl.yAxis[0], pl.yAxis[1], pl.yAxis[2]));
  return true;
}

geom::Plane originPlane(int which) {
  geom::Plane p;
  switch (which) {
    case 0: p.xAxis = {1, 0, 0}, p.yAxis = {0, 1, 0}, p.normal = {0, 0, 1}; break;
    case 1: p.xAxis = {1, 0, 0}, p.yAxis = {0, 0, 1}, p.normal = {0, -1, 0}; break;
    default: p.xAxis = {0, 1, 0}, p.yAxis = {0, 0, 1}, p.normal = {1, 0, 0}; break;
  }
  return p;
}

}  // namespace

geom::Plane toGeomPlane(const model::PlaneInfo& p) {
  geom::Plane g;
  for (int i = 0; i < 3; ++i) {
    g.origin[i] = p.origin[i];
    g.xAxis[i] = p.x_axis[i];
    g.yAxis[i] = p.y_axis[i];
    g.normal[i] = p.normal[i];
  }
  return g;
}

TopoDS_Shape sketchCurves(model::Timeline& t, std::uint64_t sketch, const geom::Plane& plane) {
  std::vector<geom::Seg2> segs;
  for (const auto& e : model::sketch_entities(t, sketch)) {
    if (e.construction) continue;
    geom::Seg2 s;
    if (e.kind == model::SketchEntityKind::Line) {
      s.x0 = e.x0, s.y0 = e.y0, s.x1 = e.x1, s.y1 = e.y1;
    } else if (e.kind == model::SketchEntityKind::Circle) {
      s.kind = geom::Seg2::Kind::Circle;
      s.cx = e.x0, s.cy = e.y0, s.radius = e.radius;
    } else if (e.kind == model::SketchEntityKind::Arc) {
      s.kind = geom::Seg2::Kind::Arc;
      s.cx = e.x0, s.cy = e.y0, s.x0 = e.x1, s.y0 = e.y1, s.x1 = e.x2, s.y1 = e.y2, s.radius = e.radius;
    } else {
      continue;
    }
    segs.push_back(s);
  }
  return geom::makeCurves(plane, segs);
}

std::vector<std::vector<geom::Point2>> profileLoops(const model::ProfileInfo& p) {
  std::vector<std::vector<geom::Point2>> loops;
  std::size_t k = 0;
  for (std::size_t n : p.loop_sizes) {
    std::vector<geom::Point2> loop;
    for (std::size_t i = 0; i < n && 2 * k + 1 < p.outlines.size(); ++i, ++k)
      loop.push_back({p.outlines[2 * k], p.outlines[2 * k + 1]});
    loops.push_back(std::move(loop));
  }
  return loops;
}

// ---- PlanePicker ----------------------------------------------------------------

PlanePicker::PlanePicker(Viewport* view, double halfSize, std::function<void(model::OriginPlaneKind)> picked)
    : view_(view), half_(halfSize), picked_(std::move(picked)) {
  draw();
}

PlanePicker::~PlanePicker() { view_->clearOverlay("planes"); }

int PlanePicker::planeAt(const QPointF& pos) const {
  int best = -1;
  double bestT = 1e300;
  for (int i = 0; i < 3; ++i) {
    double u, v, t;
    if (!rayPlane(view_, pos, originPlane(i), u, v, t)) continue;
    if (std::abs(u) <= half_ && std::abs(v) <= half_ && t < bestT) bestT = t, best = i;
  }
  return best;
}

void PlanePicker::draw() {
  const theme::Palette& pal = theme::current();
  Overlay o;
  for (int i = 0; i < 3; ++i) {
    const geom::Plane p = originPlane(i);
    const double h = half_;
    std::vector<std::vector<geom::Point2>> loop{{{-h, -h}, {h, -h}, {h, h}, {-h, h}}};
    const bool hot = i == hover_;
    o.faces.push_back({geom::makePolygonFace(p, loop), hot ? pal.accent : pal.highlight, hot ? 0.45 : 0.78});
    std::vector<geom::Seg2> edges;
    for (int k = 0; k < 4; ++k) {
      geom::Seg2 s;
      s.x0 = loop[0][k][0], s.y0 = loop[0][k][1];
      s.x1 = loop[0][(k + 1) % 4][0], s.y1 = loop[0][(k + 1) % 4][1];
      edges.push_back(s);
    }
    o.curves.push_back({geom::makeCurves(p, edges), hot ? pal.accent : pal.highlight, hot ? 2.0 : 1.2, false});
  }
  view_->setOverlay("planes", o);
}

bool PlanePicker::mousePress(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return false;
  const int i = planeAt(e->position());
  if (i < 0) return true;
  const model::OriginPlaneKind kinds[] = {model::OriginPlaneKind::XY, model::OriginPlaneKind::XZ,
                                          model::OriginPlaneKind::YZ};
  auto cb = picked_;  // the callback may delete this picker
  cb(kinds[i]);
  return true;
}

bool PlanePicker::mouseMove(QMouseEvent* e) {
  if (e->buttons() & Qt::MiddleButton) return false;
  const int i = planeAt(e->position());
  if (i != hover_) {
    hover_ = i;
    draw();
  }
  return true;
}

bool PlanePicker::mouseRelease(QMouseEvent* e) { return e->button() == Qt::LeftButton; }

// ---- ProfilePicker --------------------------------------------------------------

ProfilePicker::ProfilePicker(Viewport* view, model::Timeline& timeline, const std::vector<std::uint64_t>& sketches,
                             Changed changed)
    : view_(view), changed_(std::move(changed)) {
  for (std::uint64_t s : sketches) {
    try {
      const geom::Plane plane = toGeomPlane(model::sketch_plane(timeline, s));
      curves_.push_back({plane, sketchCurves(timeline, s, plane)});
      for (const auto& p : model::sketch_profiles(timeline, s)) {
        Profile pr{s, QString::fromUtf8(p.key.data(), int(p.key.size())), p.area, plane, profileLoops(p), {}};
        pr.face = geom::makePolygonFace(plane, pr.loops);
        profiles_.push_back(std::move(pr));
      }
    } catch (const std::exception&) {
    }
  }
  draw();
}

ProfilePicker::~ProfilePicker() { view_->clearOverlay("profiles"); }

int ProfilePicker::profileAt(const QPointF& pos) const {
  int best = -1;
  double bestArea = 1e300;
  for (std::size_t i = 0; i < profiles_.size(); ++i) {
    const Profile& p = profiles_[i];
    double u, v, t;
    if (p.loops.empty() || !rayPlane(view_, pos, p.plane, u, v, t)) continue;
    if (!inPolygon(p.loops[0], u, v)) continue;
    bool inHole = false;
    for (std::size_t k = 1; k < p.loops.size(); ++k) inHole |= inPolygon(p.loops[k], u, v);
    if (inHole) continue;
    if (p.area < bestArea) bestArea = p.area, best = int(i);
  }
  return best;
}

void ProfilePicker::draw() {
  const theme::Palette& pal = theme::current();
  Overlay o;
  for (const auto& [plane, shape] : curves_) o.curves.push_back({shape, pal.highlight, 1.6, false});
  for (std::size_t i = 0; i < profiles_.size(); ++i) {
    const Profile& p = profiles_[i];
    const bool sel = p.sketch == sketch_ && keys_.contains(p.key);
    const bool hot = int(i) == hover_;
    const QColor c = sel ? pal.accent : pal.highlight;
    const double transparency = sel ? (hot ? 0.3 : 0.4) : (hot ? 0.55 : 0.82);
    o.faces.push_back({p.face, c, transparency});
  }
  view_->setOverlay("profiles", o);
}

void ProfilePicker::notify() {
  draw();
  if (changed_) changed_(sketch_, keys_);
}

void ProfilePicker::selectIfSingle() {
  if (profiles_.size() != 1) return;
  sketch_ = profiles_[0].sketch;
  keys_ = {profiles_[0].key};
  notify();
}

bool ProfilePicker::mousePress(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return false;
  const int i = profileAt(e->position());
  if (i < 0) return true;
  const Profile& p = profiles_[std::size_t(i)];
  if (p.sketch != sketch_) {
    sketch_ = p.sketch;
    keys_.clear();
  }
  if (keys_.contains(p.key)) keys_.removeAll(p.key);
  else keys_.push_back(p.key);
  if (keys_.isEmpty()) sketch_ = 0;
  notify();
  return true;
}

bool ProfilePicker::mouseMove(QMouseEvent* e) {
  if (e->buttons() & Qt::MiddleButton) return false;
  const int i = profileAt(e->position());
  if (i != hover_) {
    hover_ = i;
    draw();
  }
  return true;
}

bool ProfilePicker::mouseRelease(QMouseEvent* e) { return e->button() == Qt::LeftButton; }
