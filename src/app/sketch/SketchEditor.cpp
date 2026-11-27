#include "SketchEditor.h"

#include "Kernel.h"
#include "Settings.h"
#include "SketchTools.h"
#include "Theme.h"

#include "Icons.h"

#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QLineF>
#include <QTimer>

#include <TopExp_Explorer.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace {

using K = model::ConstraintKind;
using EK = model::SketchEntityKind;

constexpr double kPi = 3.14159265358979323846;
double pickPx() { return prefs::get(prefs::kPickRadius).toDouble(); }  // Preferences → Sketch
constexpr double kInferDeg = 4.0;   // horizontal/vertical inference while drawing

QString qs(const rust::String& s) { return QString::fromUtf8(s.data(), int(s.size())); }

double dist(double x0, double y0, double x1, double y1) { return std::hypot(x1 - x0, y1 - y0); }

double norm2pi(double a) {
  while (a < 0) a += 2 * kPi;
  while (a >= 2 * kPi) a -= 2 * kPi;
  return a;
}

// Distance from (px,py) to the segment (x0,y0)-(x1,y1); `t` = parameter of the foot.
double segmentDistance(double px, double py, double x0, double y0, double x1, double y1, double* t = nullptr) {
  const double dx = x1 - x0, dy = y1 - y0;
  const double len2 = dx * dx + dy * dy;
  double s = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0;
  s = std::clamp(s, 0.0, 1.0);
  if (t) *t = s;
  return dist(px, py, x0 + s * dx, y0 + s * dy);
}

bool onArc(const model::SketchEntityInfo& e, double u, double v) {
  const double a0 = std::atan2(e.y1 - e.y0, e.x1 - e.x0);
  const double a1 = std::atan2(e.y2 - e.y0, e.x2 - e.x0);
  const double a = std::atan2(v - e.y0, u - e.x0);
  return norm2pi(a - a0) <= norm2pi(a1 - a0) + 1e-9;
}

// Eccentric-angle parameter of (x, y) on an ellipse entity.
double ellipseParam(const model::SketchEntityInfo& e, double x, double y) {
  const double mx = e.x1 - e.x0, my = e.y1 - e.y0, a = std::max(std::hypot(mx, my), 1e-12);
  const double ux = mx / a, uy = my / a, dx = x - e.x0, dy = y - e.y0;
  return std::atan2((-dx * uy + dy * ux) / std::max(e.minor, 1e-12), (dx * ux + dy * uy) / a);
}

geom::Seg2 segOf(const model::SketchEntityInfo& e) {
  geom::Seg2 s;
  switch (e.kind) {
    case EK::Line:
      s.kind = geom::Seg2::Kind::Line;
      s.x0 = e.x0, s.y0 = e.y0, s.x1 = e.x1, s.y1 = e.y1;
      break;
    case EK::Circle:
      s.kind = geom::Seg2::Kind::Circle;
      s.cx = e.x0, s.cy = e.y0, s.radius = e.radius;
      break;
    case EK::Arc:
      s.kind = geom::Seg2::Kind::Arc;
      s.cx = e.x0, s.cy = e.y0, s.x0 = e.x1, s.y0 = e.y1, s.x1 = e.x2, s.y1 = e.y2, s.radius = e.radius;
      s.ccw = true;
      break;
    case EK::Ellipse:
    case EK::EllipticalArc: {
      s.kind = e.kind == EK::Ellipse ? geom::Seg2::Kind::Ellipse : geom::Seg2::Kind::EllipseArc;
      s.cx = e.x0, s.cy = e.y0, s.mx = e.x1 - e.x0, s.my = e.y1 - e.y0, s.minor = e.minor;
      if (e.kind == EK::EllipticalArc) {
        s.t0 = ellipseParam(e, e.x2, e.y2);
        s.t1 = ellipseParam(e, e.x3, e.y3);
        s.x0 = e.x2, s.y0 = e.y2, s.x1 = e.x3, s.y1 = e.y3;
        s.ccw = true;
      }
      break;
    }
    case EK::Spline:
      s.kind = geom::Seg2::Kind::Spline;
      s.degree = int(e.degree);
      s.poles.assign(e.poles.begin(), e.poles.end());
      s.knots.assign(e.knots.begin(), e.knots.end());
      s.weights.assign(e.weights.begin(), e.weights.end());
      break;
    default:
      break;
  }
  return s;
}

// Distance from (u,v) to a polyline of x,y pairs; `fx, fy` = the nearest spot.
double polylineDistance(const rust::Vec<double>& xy, double u, double v, double* fx = nullptr, double* fy = nullptr) {
  double best = 1e300;
  for (std::size_t i = 0; i + 3 < xy.size(); i += 2) {
    double t;
    const double d = segmentDistance(u, v, xy[i], xy[i + 1], xy[i + 2], xy[i + 3], &t);
    if (d < best) {
      best = d;
      if (fx) *fx = xy[i] + t * (xy[i + 2] - xy[i]);
      if (fy) *fy = xy[i + 1] + t * (xy[i + 3] - xy[i + 1]);
    }
  }
  return best;
}

bool usesSamples(EK k) { return k == EK::Ellipse || k == EK::EllipticalArc || k == EK::Spline; }

geom::Plane geomPlane(const model::PlaneInfo& p) {
  geom::Plane g;
  for (int i = 0; i < 3; ++i) {
    g.origin[i] = p.origin[i];
    g.xAxis[i] = p.x_axis[i];
    g.yAxis[i] = p.y_axis[i];
    g.normal[i] = p.normal[i];
  }
  return g;
}

QString glyph(K k) {
  switch (k) {
    case K::Horizontal: case K::HorizontalPoints: return "H";
    case K::Vertical: case K::VerticalPoints: return "V";
    case K::Parallel: return "//";
    case K::Perpendicular: return "Perp";
    case K::Tangent: return "T";
    case K::Equal: return "=";
    case K::Fix: return "Fix";
    case K::Midpoint: return "Mid";
    case K::Concentric: return "Con";
    case K::Collinear: return "Col";
    case K::Symmetric: return "Sym";
    case K::Smooth: return "G2";
    default: return {};
  }
}

QString kindName(K k) {
  switch (k) {
    case K::Coincident: return QObject::tr("Coincident");
    case K::Horizontal: return QObject::tr("Horizontal");
    case K::Vertical: return QObject::tr("Vertical");
    case K::Parallel: return QObject::tr("Parallel");
    case K::Perpendicular: return QObject::tr("Perpendicular");
    case K::Tangent: return QObject::tr("Tangent");
    case K::Equal: return QObject::tr("Equal");
    case K::Fix: return QObject::tr("Fix");
    case K::Midpoint: return QObject::tr("Midpoint");
    case K::Concentric: return QObject::tr("Concentric");
    case K::Collinear: return QObject::tr("Collinear");
    case K::Symmetric: return QObject::tr("Symmetric");
    case K::Smooth: return QObject::tr("Smooth");
    default: return QObject::tr("Constraint");
  }
}

}  // namespace

SketchEditor::SketchEditor(Viewport* view, Host host, std::uint64_t sketch, QObject* parent)
    : QObject(parent), view_(view), host_(std::move(host)), sketch_(sketch) {
  showTags_ = prefs::flag(prefs::kConstraintTags);
  snap_ = prefs::flag(prefs::kSnap);
  redrawTimer_.setSingleShot(true);
  redrawTimer_.setInterval(60);
  connect(&redrawTimer_, &QTimer::timeout, this, [this] {
    redraw();
    drawHover();
    drawPreview();
  });
  reload();
}

SketchEditor::~SketchEditor() {
  closeValueEditor();
  clearDisplay();
}

model::LengthUnitKind SketchEditor::unit() { return model::timeline_units(tl()); }

// ---- Geometry -------------------------------------------------------------------

gp_Pnt SketchEditor::world(double u, double v) const {
  return gp_Pnt(plane_.origin[0] + u * plane_.x_axis[0] + v * plane_.y_axis[0],
                plane_.origin[1] + u * plane_.x_axis[1] + v * plane_.y_axis[1],
                plane_.origin[2] + u * plane_.x_axis[2] + v * plane_.y_axis[2]);
}

bool SketchEditor::toPlane(const QPointF& pos, double& u, double& v) const {
  const auto [p, d] = view_->rayAt(pos);
  const gp_Vec n(plane_.normal[0], plane_.normal[1], plane_.normal[2]);
  const gp_Pnt o(plane_.origin[0], plane_.origin[1], plane_.origin[2]);
  const double denom = gp_Vec(d).Dot(n);
  if (std::abs(denom) < 1e-9) return false;
  const double t = gp_Vec(p, o).Dot(n) / denom;
  const gp_Pnt hit = p.Translated(gp_Vec(d) * t);
  const gp_Vec rel(o, hit);
  u = rel.Dot(gp_Vec(plane_.x_axis[0], plane_.x_axis[1], plane_.x_axis[2]));
  v = rel.Dot(gp_Vec(plane_.y_axis[0], plane_.y_axis[1], plane_.y_axis[2]));
  return true;
}

double SketchEditor::unitsPerPixel() const {
  const QPointF c(view_->width() / 2.0, view_->height() / 2.0);
  double u0, v0, u1, v1;
  if (!toPlane(c, u0, v0) || !toPlane(c + QPointF(50, 0), u1, v1)) return 0.05;
  return std::max(dist(u0, v0, u1, v1) / 50.0, 1e-6);
}

const model::SketchEntityInfo* SketchEditor::entity(std::uint64_t id) const {
  for (const auto& e : entities_)
    if (e.id == id) return &e;
  return nullptr;
}

bool SketchEditor::isCurve(std::uint64_t id) const {
  const auto* e = entity(id);
  return e && e->kind != EK::Point;
}

bool SketchEditor::isPoint(std::uint64_t id) const {
  const auto* e = entity(id);
  return e && e->kind == EK::Point;
}

std::uint64_t SketchEditor::originPointId() const {
  for (const auto& c : constraints_) {
    if (c.kind != K::Fix) continue;
    const auto* e = entity(c.a);
    if (e && e->kind == EK::Point && std::abs(e->x0) < 1e-9 && std::abs(e->y0) < 1e-9) return e->id;
  }
  return 0;
}

SketchEditor::Hit SketchEditor::hitTest(const QPointF& pos, bool pointsFirst, bool labels) const {
  Hit h;
  if (labels) {
    double best = 12.0;
    for (const LabelSpot& l : labels_) {
      const double d = std::hypot(l.screen.x() - pos.x(), l.screen.y() - pos.y());
      if (d < best) {
        best = d;
        h.kind = Hit::Label;
        h.id = l.constraint;
      }
    }
    if (h.kind == Hit::Label) return h;
  }
  double u, v;
  if (!toPlane(pos, u, v)) return h;
  h.u = u, h.v = v;
  const double tol = pickPx() * unitsPerPixel();

  if (pointsFirst) {
    double best = tol;
    for (const auto& e : entities_) {
      if (e.kind != EK::Point) continue;
      const double d = dist(u, v, e.x0, e.y0);
      if (d < best) {
        best = d;
        h.kind = Hit::Point;
        h.id = e.id;
        h.u = e.x0, h.v = e.y0;
      }
    }
    if (h.kind == Hit::Point) return h;
    if (dist(u, v, 0, 0) < tol) {
      h.kind = Hit::Origin;
      h.id = originPointId();
      h.u = 0, h.v = 0;
      if (h.id) h.kind = Hit::Point;
      return h;
    }
  }
  double best = tol;
  for (const auto& e : entities_) {
    double d = 1e300;
    switch (e.kind) {
      case EK::Line: d = segmentDistance(u, v, e.x0, e.y0, e.x1, e.y1); break;
      case EK::Circle: d = std::abs(dist(u, v, e.x0, e.y0) - e.radius); break;
      case EK::Arc:
        if (onArc(e, u, v)) d = std::abs(dist(u, v, e.x0, e.y0) - e.radius);
        break;
      default:
        if (usesSamples(e.kind)) d = polylineDistance(e.samples, u, v);
        break;
    }
    if (d < best) {
      best = d;
      h.kind = Hit::Curve;
      h.id = e.id;
    }
  }
  return h;
}

// Where a click lands while drawing: an existing point, the origin, a spot on a
// curve (projected onto it), or free space.
SketchEditor::Hit SketchEditor::snap(const QPointF& pos) const {
  guides_.clear();
  if (!snap_) {
    Hit free;
    toPlane(pos, free.u, free.v);
    return free;
  }
  Hit h = hitTest(pos);
  const double tol = pickPx() * unitsPerPixel();
  if (h.kind == Hit::Curve) {
    const auto* e = entity(h.id);
    if (e->kind == EK::Line) {
      double t;
      segmentDistance(h.u, h.v, e->x0, e->y0, e->x1, e->y1, &t);
      h.u = e->x0 + t * (e->x1 - e->x0);
      h.v = e->y0 + t * (e->y1 - e->y0);
      // Midpoint snap: a little stickier than the pick radius.
      const double mx = (e->x0 + e->x1) / 2, my = (e->y0 + e->y1) / 2;
      if (dist(h.u, h.v, mx, my) < 1.5 * tol) h.u = mx, h.v = my, h.mid = true;
    } else if (usesSamples(e->kind)) {
      polylineDistance(e->samples, h.u, h.v, &h.u, &h.v);
    } else {
      const double a = std::atan2(h.v - e->y0, h.u - e->x0);
      h.u = e->x0 + e->radius * std::cos(a);
      h.v = e->y0 + e->radius * std::sin(a);
    }
    return h;
  }
  if (h.kind != Hit::None) return h;

  // Free space: line extensions first, then alignment with existing points.
  for (const auto& e : entities_) {
    if (e.kind != EK::Line) continue;
    const double dx = e.x1 - e.x0, dy = e.y1 - e.y0, len2 = dx * dx + dy * dy;
    if (len2 < 1e-18) continue;
    const double t = ((h.u - e.x0) * dx + (h.v - e.y0) * dy) / len2;
    if (t > 0 && t < 1) continue;  // over the line itself: hitTest would have found it
    const double fx = e.x0 + t * dx, fy = e.y0 + t * dy;
    if (dist(h.u, h.v, fx, fy) < 0.8 * tol) {
      const double ex = t < 0 ? e.x0 : e.x1, ey = t < 0 ? e.y0 : e.y1;
      h.u = fx, h.v = fy;
      guides_.push_back({ex, ey, fx, fy});
      return h;
    }
  }
  double bestU = tol, bestV = tol;
  double alignU = 0, alignV = 0, fromU[2] = {0, 0}, fromV[2] = {0, 0};
  bool hasU = false, hasV = false;
  auto consider = [&](double x, double y) {
    if (std::abs(h.u - x) < bestU) bestU = std::abs(h.u - x), alignU = x, fromU[0] = x, fromU[1] = y, hasU = true;
    if (std::abs(h.v - y) < bestV) bestV = std::abs(h.v - y), alignV = y, fromV[0] = x, fromV[1] = y, hasV = true;
  };
  for (const auto& e : entities_)
    if (e.kind == EK::Point) consider(e.x0, e.y0);
  consider(0, 0);
  for (const Hit& c : clicks_) consider(c.u, c.v);
  if (hasU) h.u = alignU;
  if (hasV) h.v = alignV;
  if (hasU) guides_.push_back({fromU[0], fromU[1], h.u, h.v});
  if (hasV) guides_.push_back({fromV[0], fromV[1], h.u, h.v});
  return h;
}

// ---- Transactions --------------------------------------------------------------

bool SketchEditor::edit(const std::function<bool()>& change) {
  rust::Box<model::Timeline> before = model::timeline_clone(tl());
  bool ok = false;
  try {
    ok = change();
  } catch (const std::exception& ex) {
    emit message(tr("Could not do that: %1").arg(QString::fromUtf8(ex.what())));
    ok = false;
  }
  if (ok) {
    host_.pushUndo(std::move(before));
  } else {
    host_.restore(std::move(before));
  }
  reload();
  if (ok) emit edited();
  return ok;
}

std::uint64_t SketchEditor::resolve(const Hit& h) {
  if (h.kind == Hit::Point && h.id) return h.id;
  if (h.kind == Hit::Origin || (h.kind == Hit::Point && !h.id)) {
    if (const std::uint64_t o = originPointId()) return o;
    const std::uint64_t p = model::sketch_add_point(tl(), sketch_, 0.0, 0.0);
    model::sketch_add_constraint(tl(), sketch_, K::Fix, p, 0, 0, 0);
    reload();
    return p;
  }
  const std::uint64_t p = model::sketch_add_point(tl(), sketch_, h.u, h.v);
  if (h.kind == Hit::Curve && h.id) addConstraint(h.mid ? K::Midpoint : K::PointOnCurve, p, h.id, 0, 0, /*quiet=*/true);
  reload();
  return p;
}

bool SketchEditor::addConstraint(K kind, std::uint64_t a, std::uint64_t b, std::uint64_t c, double value,
                                 bool quiet) {
  const model::ConstraintResult r = model::sketch_add_constraint(tl(), sketch_, kind, a, b, c, value);
  if (!r.ok && !quiet) emit message(qs(r.message));
  return r.ok;
}

// ---- Modes ----------------------------------------------------------------------

bool SketchEditor::isDrawing(Mode m) {
  switch (m) {
    case Mode::Line: case Mode::Rectangle: case Mode::Circle: case Mode::Arc: case Mode::Point:
    case Mode::Rectangle3: case Mode::RectangleCentre: case Mode::Circle2: case Mode::Circle3:
    case Mode::CentreArc: case Mode::TangentArc: case Mode::Polygon: case Mode::Ellipse: case Mode::Slot:
    case Mode::Spline:
      return true;
    default: return false;
  }
}

bool SketchEditor::picksCurves(Mode m) {
  switch (m) {
    case Mode::Offset: case Mode::Mirror: case Mode::Move: case Mode::Rotate: case Mode::Scale:
    case Mode::PatternRect: case Mode::PatternCirc:
      return true;
    default: return false;
  }
}

void SketchEditor::setMode(Mode mode) {
  closeValueEditor();
  // Selected curves carry into the tools that work on curves.
  std::vector<std::uint64_t> chosen;
  if (picksCurves(mode))
    for (std::uint64_t id : selected_)
      if (isCurve(id)) chosen.push_back(id);
  resetDraft();
  mode_ = mode;
  opCurves_ = chosen;
  opStage_ = chosen.empty() ? 0 : 1;
  bodyEdges_.clear();
  hoverEdge_ = -1;
  if (mode == Mode::Project && host_.body) {
    for (TopExp_Explorer ex(host_.body(), TopAbs_EDGE); ex.More(); ex.Next()) bodyEdges_.push_back(ex.Current());
    if (bodyEdges_.empty()) emit message(tr("There is no body to project yet."));
  }
  if (mode != Mode::Select) {
    selected_.clear();
    selectedCon_.clear();
  }
  redraw();
  drawPreview();
  hintForMode();
  emit modeChanged(mode);
}

void SketchEditor::setConstraint(K kind) {
  constraintKind_ = kind;
  // Selected geometry first, then the tool: apply right away (Fusion style).
  if (mode_ == Mode::Select && !selected_.isEmpty()) {
    std::vector<Hit> hits;
    for (std::uint64_t id : selected_) {
      Hit h;
      h.kind = isPoint(id) ? Hit::Point : Hit::Curve;
      h.id = id;
      hits.push_back(h);
    }
    setMode(Mode::Constrain);
    for (const Hit& h : hits) clickConstrain(h);
    return;
  }
  setMode(Mode::Constrain);
}

void SketchEditor::resetDraft() {
  sizeTyped_[0] = sizeTyped_[1] = false;
  hideSizeBoxes();
  clicks_.clear();
  picks_.clear();
  chainStart_ = 0;
  opCurves_.clear();
  opStage_ = 0;
  guides_.clear();
}

void SketchEditor::hintForMode() {
  if (mode_ == Mode::Constrain) {
    emit message(tr("%1: pick the geometry.").arg(kindName(constraintKind_)));
    return;
  }
  if (picksCurves(mode_) && opStage_ == 1 && mode_ != Mode::Offset) {
    finishPicking();  // curves came selected: go straight to the next step
    return;
  }
  if (const SketchToolDef* t = sketchToolFor(mode_)) emit message(tr(t->title) + QStringLiteral(": ") + tr(t->hint));
}

void SketchEditor::toggleConstruction() {
  std::vector<std::uint64_t> curves;
  for (std::uint64_t id : selected_)
    if (isCurve(id)) curves.push_back(id);
  if (curves.empty()) {
    emit message(tr("Select lines, circles or arcs first, then press X."));
    return;
  }
  edit([&] {
    for (std::uint64_t id : curves) model::sketch_set_construction(tl(), sketch_, id, !entity(id)->construction);
    return true;
  });
}

void SketchEditor::deleteSelection() {
  if (selected_.isEmpty() && selectedCon_.isEmpty()) return;
  const QSet<std::uint64_t> cons = selectedCon_;
  QSet<std::uint64_t> ents = selected_;
  selected_.clear();
  selectedCon_.clear();
  int skipped = 0;
  edit([&] {
    for (std::uint64_t c : cons) model::sketch_remove_constraint(tl(), sketch_, c);
    // Curves first: points they use are removed with them when unused.
    std::vector<std::uint64_t> order(ents.begin(), ents.end());
    std::stable_partition(order.begin(), order.end(), [this](std::uint64_t id) { return isCurve(id); });
    for (std::uint64_t id : order) {
      try {
        model::sketch_remove_entity(tl(), sketch_, id);
      } catch (const std::exception&) {
        ++skipped;  // already gone with its curve, or still used by a curve
      }
    }
    return true;
  });
  if (skipped) emit message(tr("Points that still belong to a curve were kept."));
}

void SketchEditor::reload() {
  try {
    plane_ = model::sketch_plane(tl(), sketch_);
    entities_.clear();
    for (const auto& e : model::sketch_entities(tl(), sketch_)) entities_.push_back(e);
    constraints_.clear();
    for (const auto& c : model::sketch_constraints(tl(), sketch_)) constraints_.push_back(c);
    profiles_.clear();
    for (const auto& p : model::sketch_profiles(tl(), sketch_)) profiles_.push_back(p);
    const model::SketchSolveInfo info = model::sketch_solve(tl(), sketch_);
    looseEnds_.clear();
    for (std::uint64_t id : model::sketch_loose_ends(tl(), sketch_)) looseEnds_.push_back(id);
    const bool newlyDegenerate = !info.degenerate.empty() && degenerate_.empty();
    degenerate_.assign(info.degenerate.begin(), info.degenerate.end());
    if (newlyDegenerate) emit message(tr("Some curves have shrunk to nothing; check the dimensions."));
    dof_ = int(info.dof);
    conflicting_ = info.status == model::SketchSolveStatus::Conflicting || !info.conflicting.empty();
  } catch (const std::exception&) {
    entities_.clear();
    constraints_.clear();
    profiles_.clear();
  }
  // Drop selection of things that no longer exist.
  for (auto it = selected_.begin(); it != selected_.end();) it = entity(*it) ? std::next(it) : selected_.erase(it);
  for (auto it = selectedCon_.begin(); it != selectedCon_.end();) {
    const bool exists = std::any_of(constraints_.begin(), constraints_.end(),
                                    [id = *it](const auto& c) { return c.id == id; });
    it = exists ? std::next(it) : selectedCon_.erase(it);
  }
  redraw();
  drawHover();
  drawPreview();
  emit solveStateChanged(dof_, conflicting_);
}

void SketchEditor::clearDisplay() {
  for (const char* layer : {"sketch", "sketch-hover", "sketch-preview"}) view_->clearOverlay(layer);
}

// ---- Mouse ----------------------------------------------------------------------

bool SketchEditor::mousePress(QMouseEvent* e) {
  const QPointF pos = e->position();
  if (valueEdit_) closeValueEditor();

  if (e->button() == Qt::RightButton) {
    if (mode_ == Mode::Select) {
      const Hit h = hitTest(pos, true, true);
      if (h.kind == Hit::Label) {
        showDimensionMenu(h.id, pos);
        return true;
      }
    }
    if (mode_ == Mode::Spline && clicks_.size() >= 2) {
      finishSpline();
      return true;
    }
    if (!clicks_.empty() || !picks_.empty() || !opCurves_.empty()) {
      resetDraft();
      drawPreview();
      return true;
    }
    if (mode_ != Mode::Select) {
      setMode(Mode::Select);
      return true;
    }
    return false;
  }

  pressPos_ = pos;
  switch (mode_) {
    case Mode::Select: {
      const Hit h = hitTest(pos, true, true);
      const bool add = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
      if (h.kind == Hit::Label) {
        if (!add) selected_.clear(), selectedCon_.clear();
        if (selectedCon_.contains(h.id) && add) selectedCon_.remove(h.id);
        else selectedCon_.insert(h.id);
      } else if (h.kind == Hit::Point || h.kind == Hit::Curve) {
        if (!add && !selected_.contains(h.id)) selected_.clear(), selectedCon_.clear();
        if (add && selected_.contains(h.id)) selected_.remove(h.id);
        else selected_.insert(h.id);
        pressed_ = true;
        dragHit_ = h;
        toPlane(pos, dragU_, dragV_);
      } else {
        if (!add) selected_.clear(), selectedCon_.clear();
      }
      redraw();
      return true;
    }
    case Mode::Line:
    case Mode::Rectangle:
    case Mode::Circle:
    case Mode::Arc:
    case Mode::Point:
      clickDraw(snap(pos));
      return true;
    case Mode::Rectangle3: case Mode::RectangleCentre: case Mode::Circle2: case Mode::Circle3:
    case Mode::CentreArc: case Mode::TangentArc: case Mode::Polygon: case Mode::Ellipse: case Mode::Slot:
    case Mode::Spline:
      clickShape(snap(pos), Typed());
      return true;
    case Mode::Trim: case Mode::Extend: case Mode::Break: case Mode::Fillet: case Mode::Chamfer:
    case Mode::Offset: case Mode::Mirror: case Mode::Move: case Mode::Rotate: case Mode::Scale:
    case Mode::PatternRect: case Mode::PatternCirc: case Mode::Project: {
      // Anchors (base point, centre) snap; curve picks don't.
      const bool anchor = (mode_ == Mode::Move || mode_ == Mode::Rotate || mode_ == Mode::Scale ||
                           mode_ == Mode::PatternCirc) && opStage_ >= 1;
      const bool side = mode_ == Mode::Offset && opStage_ == 1;
      Hit h = anchor ? snap(pos) : hitTest(pos, mode_ == Mode::Fillet || mode_ == Mode::Chamfer);
      if (side) toPlane(pos, h.u, h.v);
      clickModify(h, pos, e->modifiers());
      return true;
    }
    case Mode::Dimension:
      clickDimension(hitTest(pos, true, true), pos);
      return true;
    case Mode::Constrain: {
      Hit h = hitTest(pos);
      if (h.kind == Hit::Origin) h.kind = Hit::Point;  // resolved when applied
      clickConstrain(h);
      return true;
    }
  }
  return true;
}

bool SketchEditor::mouseMove(QMouseEvent* e) {
  cursor_ = e->position();
  hasCursor_ = true;
  if (e->buttons() & Qt::MiddleButton) {
    redrawTimer_.start();
    return false;
  }

  if (pressed_ && mode_ == Mode::Select && (e->buttons() & Qt::LeftButton)) {
    if (!dragging_ && QLineF(pressPos_, cursor_).length() < 4) return true;
    double u, v;
    if (!toPlane(cursor_, u, v)) return true;
    if (!dragging_) {
      dragging_ = true;
      dragBefore_ = std::make_unique<rust::Box<model::Timeline>>(model::timeline_clone(tl()));
    }
    const double du = u - dragU_, dv = v - dragV_;
    dragU_ = u, dragV_ = v;
    try {
      const auto* ent = entity(dragHit_.id);
      if (!ent) return true;
      if (ent->kind == EK::Point) {
        model::sketch_drag_point(tl(), sketch_, ent->id, u, v);
      } else if (ent->kind == EK::Line) {
        const std::uint64_t p0 = ent->p0, p1 = ent->p1;
        const double x0 = ent->x0 + du, y0 = ent->y0 + dv, x1 = ent->x1 + du, y1 = ent->y1 + dv;
        model::sketch_drag_point(tl(), sketch_, p0, x0, y0);
        model::sketch_drag_point(tl(), sketch_, p1, x1, y1);
      } else {
        model::sketch_drag_point(tl(), sketch_, ent->p0, ent->x0 + du, ent->y0 + dv);
      }
    } catch (const std::exception&) {
    }
    reload();
    return true;
  }

  if (mode_ == Mode::Project) {
    const int edge = edgeAt(cursor_);
    if (edge != hoverEdge_) {
      hoverEdge_ = edge;
      drawHover();
    }
    return true;
  }
  const bool anchor = (mode_ == Mode::Move || mode_ == Mode::Rotate || mode_ == Mode::Scale ||
                       mode_ == Mode::PatternCirc) && opStage_ >= 1;
  const Hit h = (mode_ == Mode::Select || mode_ == Mode::Dimension) ? hitTest(cursor_, true, true)
                : mode_ == Mode::Constrain                          ? hitTest(cursor_)
                : (isDrawing(mode_) || anchor)                      ? snap(cursor_)
                                                                    : hitTest(cursor_, mode_ == Mode::Fillet ||
                                                                                           mode_ == Mode::Chamfer);
  if (h.kind != hover_.kind || h.id != hover_.id) {
    hover_ = h;
    drawHover();
  } else {
    hover_ = h;
  }
  if (!clicks_.empty() || mode_ != Mode::Select) drawPreview();
  return true;
}

bool SketchEditor::mouseRelease(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return e->button() == Qt::RightButton;
  if (dragging_ && dragBefore_) {
    host_.pushUndo(std::move(*dragBefore_));
    dragBefore_.reset();
    emit edited();
  }
  pressed_ = false;
  dragging_ = false;
  return true;
}

bool SketchEditor::mouseDoubleClick(QMouseEvent* e) {
  if (mode_ == Mode::Spline) {
    // The press before the double-click already added this spot.
    finishSpline();
    return true;
  }
  if (mode_ == Mode::Line) {
    resetDraft();
    drawPreview();
    return true;
  }
  const Hit h = hitTest(e->position(), true, true);
  if (h.kind == Hit::Label && (mode_ == Mode::Select || mode_ == Mode::Dimension)) {
    for (const auto& c : constraints_) {
      if (c.id != h.id || !c.is_dimension || c.driven) continue;
      PendingDimension d;
      d.existing = c.id;
      d.kind = c.kind;
      d.angle = c.kind == K::Angle;
      openValueEditor(d, e->position(), c.value);
      return true;
    }
  }
  return true;
}

bool SketchEditor::keyPress(QKeyEvent* e) {
  if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
  // While drawing, digits or Tab go to the size box (like Fusion and MitCAD).
  if (sizeBoxCount() > 0) {
    const QString t = e->text();
    if (e->key() == Qt::Key_Tab) {
      focusSizeBox(0);
      return true;
    }
    if (!t.isEmpty() && (t[0].isDigit() || t[0] == '.') && sizeEdit_[0]) {
      focusSizeBox(0);
      sizeEdit_[0]->setText(t);
      sizeTyped_[0] = true;
      drawPreview();
      return true;
    }
  }
  switch (e->key()) {
    case Qt::Key_Escape:
      if (!clicks_.empty() || !picks_.empty() || !opCurves_.empty()) {
        resetDraft();
        drawPreview();
        hintForMode();
      } else if (mode_ != Mode::Select) {
        setMode(Mode::Select);
      } else if (!selected_.isEmpty() || !selectedCon_.isEmpty()) {
        selected_.clear();
        selectedCon_.clear();
        redraw();
      }
      return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteSelection(); return true;
    case Qt::Key_L: setMode(Mode::Line); return true;
    case Qt::Key_R: setMode(Mode::Rectangle); return true;
    case Qt::Key_C: setMode(Mode::Circle); return true;
    case Qt::Key_A: setMode(Mode::Arc); return true;
    case Qt::Key_P: setMode(Mode::Point); return true;
    case Qt::Key_D: setMode(Mode::Dimension); return true;
    case Qt::Key_S: setMode(Mode::Select); return true;
    case Qt::Key_X: toggleConstruction(); return true;
    case Qt::Key_T: setMode(Mode::Trim); return true;
    case Qt::Key_O: setMode(Mode::Offset); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (mode_ == Mode::Spline && clicks_.size() >= 2) {
        finishSpline();
        return true;
      }
      if (picksCurves(mode_) && opStage_ == 0) {
        finishPicking();
        return true;
      }
      if (mode_ == Mode::Dimension && picks_.size() == 1 && picks_[0].kind == Hit::Curve) {
        clickDimension(Hit{}, hasCursor_ ? cursor_ : QPointF(view_->width() / 2.0, view_->height() / 2.0));
      }
      return true;
    default: return false;
  }
}

void SketchEditor::viewChanged() { redrawTimer_.start(); }

// ---- Drawing tools ----------------------------------------------------------------

void SketchEditor::clickDraw(const Hit& hIn, const Typed& typed) {
  Hit h = hIn;
  const double tol = pickPx() * unitsPerPixel();

  if (mode_ == Mode::Point) {
    if (h.kind == Hit::Point) return;
    edit([&] {
      resolve(h);
      return true;
    });
    return;
  }

  if (clicks_.empty()) {
    clicks_.push_back(h);
    drawPreview();
    return;
  }

  const Hit first = clicks_.front();
  switch (mode_) {
    case Mode::Line: {
      if (dist(first.u, first.v, h.u, h.v) < tol) return;
      // Horizontal / vertical inference when not landing on a point.
      bool horiz = false, vert = false;
      if (h.kind != Hit::Point && h.kind != Hit::Origin && prefs::flag(prefs::kInference)) {
        const double ang = std::abs(std::atan2(h.v - first.v, h.u - first.u)) * 180.0 / kPi;
        if (std::min(ang, 180.0 - ang) < kInferDeg) horiz = true, h.v = first.v, h.kind = Hit::None;
        else if (std::abs(ang - 90.0) < kInferDeg) vert = true, h.u = first.u, h.kind = Hit::None;
      }
      std::uint64_t endId = 0;
      bool closed = false;
      const bool ok = edit([&] {
        const std::uint64_t a = resolve(first);
        const std::uint64_t b = resolve(h);
        if (a == b) return false;
        const std::uint64_t line = model::sketch_add_line(tl(), sketch_, a, b);
        if (horiz) addConstraint(K::Horizontal, line, 0, 0, 0, true);
        if (vert) addConstraint(K::Vertical, line, 0, 0, 0, true);
        if (typed.set[0]) addConstraint(K::Length, line, 0, 0, typed.value[0], true);
        markNew({line});
        if (!chainStart_) chainStart_ = a;
        closed = b == chainStart_;
        endId = b;
        return true;
      });
      clicks_.clear();
      if (ok && !closed) {
        Hit next;
        next.kind = Hit::Point;
        next.id = endId;
        if (const auto* e = entity(endId)) next.u = e->x0, next.v = e->y0;
        clicks_.push_back(next);
      } else {
        chainStart_ = 0;
      }
      drawPreview();
      return;
    }
    case Mode::Rectangle: {
      if (std::abs(first.u - h.u) < tol || std::abs(first.v - h.v) < tol) return;
      edit([&] {
        const auto lines = model::sketch_add_rectangle(tl(), sketch_, std::min(first.u, h.u), std::min(first.v, h.v),
                                                       std::max(first.u, h.u), std::max(first.v, h.v));
        reload();
        auto cornerAt = [&](double u, double v) -> std::uint64_t {
          std::uint64_t best = 0;
          double bd = 1e300;
          for (std::uint64_t l : lines) {
            const auto* e = entity(l);
            if (!e) continue;
            for (auto [id, x, y] : {std::tuple{e->p0, e->x0, e->y0}, std::tuple{e->p1, e->x1, e->y1}}) {
              const double d = dist(u, v, x, y);
              if (d < bd) bd = d, best = id;
            }
          }
          return best;
        };
        for (const Hit& c : {first, h}) {
          const std::uint64_t corner = cornerAt(c.u, c.v);
          if (!corner) continue;
          if (c.kind == Hit::Point || c.kind == Hit::Origin) {
            const std::uint64_t p = resolve(c);
            addConstraint(K::Coincident, p, corner, 0, 0, true);
          } else if (c.kind == Hit::Curve) {
            addConstraint(K::PointOnCurve, corner, c.id, 0, 0, true);
          }
        }
        // lines: bottom (width), right (height), top, left.
        if (typed.set[0] && lines.size() > 0) addConstraint(K::Length, lines[0], 0, 0, typed.value[0], true);
        if (typed.set[1] && lines.size() > 1) addConstraint(K::Length, lines[1], 0, 0, typed.value[1], true);
        markNew(std::vector<std::uint64_t>(lines.begin(), lines.end()));
        return true;
      });
      clicks_.clear();
      drawPreview();
      return;
    }
    case Mode::Circle: {
      const double r = dist(first.u, first.v, h.u, h.v);
      if (r < tol) return;
      edit([&] {
        std::uint64_t circle;
        if (first.kind == Hit::Point || first.kind == Hit::Origin) {
          circle = model::sketch_add_circle(tl(), sketch_, resolve(first), r);
        } else {
          circle = model::sketch_add_circle_xy(tl(), sketch_, first.u, first.v, r);
          reload();
          if (first.kind == Hit::Curve)
            if (const auto* e = entity(circle)) addConstraint(K::PointOnCurve, e->p0, first.id, 0, 0, true);
        }
        if (h.kind == Hit::Point && h.id) addConstraint(K::PointOnCurve, h.id, circle, 0, 0, true);
        if (typed.set[0]) addConstraint(K::Diameter, circle, 0, 0, typed.value[0], true);
        markNew({circle});
        return true;
      });
      clicks_.clear();
      drawPreview();
      return;
    }
    case Mode::Arc: {
      if (clicks_.size() == 1) {
        if (dist(first.u, first.v, h.u, h.v) < tol) return;
        clicks_.push_back(h);
        drawPreview();
        return;
      }
      const Hit s = clicks_[0], en = clicks_[1];
      const double ax = s.u, ay = s.v, bx = en.u, by = en.v, cx = h.u, cy = h.v;
      const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
      if (std::abs(d) < 1e-12) {
        emit message(tr("Those three points are in a line; pick a point off the line."));
        return;
      }
      const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
      const double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
      const double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
      const double cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
      edit([&] {
        const std::uint64_t centre = model::sketch_add_point(tl(), sketch_, ux, uy);
        const std::uint64_t ps = resolve(s);
        const std::uint64_t pe = resolve(en);
        const std::uint64_t arc = cross > 0 ? model::sketch_add_arc(tl(), sketch_, centre, pe, ps)
                                            : model::sketch_add_arc(tl(), sketch_, centre, ps, pe);
        markNew({arc});
        return true;
      });
      clicks_.clear();
      drawPreview();
      return;
    }
    default: return;
  }
}

void SketchEditor::markNew(const std::vector<std::uint64_t>& curves) {
  if (!constructionMode_) return;
  for (std::uint64_t id : curves) model::sketch_set_construction(tl(), sketch_, id, true);
}

// ---- Palette settings ------------------------------------------------------------

void SketchEditor::setDisplay(Display what, bool on) {
  switch (what) {
    case Display::Dimensions: showDims_ = on; break;
    case Display::Constraints: showTags_ = on; break;
    case Display::Points: showPoints_ = on; break;
    case Display::Profiles: showProfiles_ = on; break;
    case Display::Snap: snap_ = on; break;
  }
  redraw();
  drawPreview();
}

bool SketchEditor::display(Display what) const {
  switch (what) {
    case Display::Dimensions: return showDims_;
    case Display::Constraints: return showTags_;
    case Display::Points: return showPoints_;
    case Display::Profiles: return showProfiles_;
    case Display::Snap: return snap_;
  }
  return false;
}

void SketchEditor::setConstructionMode(bool on) {
  constructionMode_ = on;
  emit message(on ? tr("New lines, circles and arcs are construction geometry.")
                  : tr("New geometry is normal (not construction)."));
}

void SketchEditor::selectUnconstrained() {
  setMode(Mode::Select);
  selected_.clear();
  for (const auto& e : entities_)
    if (!e.fully_constrained && e.kind != EK::Point) selected_.insert(e.id);
  redraw();
  emit message(selected_.isEmpty() ? tr("Everything is fully constrained.")
                                   : tr("%n item(s) can still move.", nullptr, int(selected_.size())));
}

// ---- Typed sizes while drawing ---------------------------------------------------

int SketchEditor::sizeBoxCount() const {
  if (clicks_.empty()) return 0;
  switch (mode_) {
    case Mode::Line: case Mode::Circle: return 1;
    case Mode::Rectangle: case Mode::RectangleCentre: case Mode::Polygon: return 2;
    case Mode::Slot: return clicks_.size() == 2 ? 1 : 0;
    default: return 0;
  }
}

bool SketchEditor::typedValue(int i, double& mm) {
  if (i >= 2 || !sizeEdit_[i] || !sizeTyped_[i]) return false;
  if (mode_ == Mode::Polygon && i == 1) {  // number of sides
    bool ok = false;
    const int n = sizeEdit_[i]->text().trimmed().toInt(&ok);
    if (!ok || n < 3 || n > 64) return false;
    mm = n;
    return true;
  }
  const model::LengthParse p = model::length_parse(sizeEdit_[i]->text().trimmed().toStdString(), unit());
  if (!p.ok || p.mm <= 0) return false;
  mm = p.mm;
  return true;
}

void SketchEditor::showSizeBoxes(const double live[2], const QPointF& anchor) {
  const int n = sizeBoxCount();
  for (int i = 0; i < 2; ++i) {
    if (i >= n) {
      if (sizeEdit_[i]) sizeEdit_[i]->hide();
      continue;
    }
    if (!sizeEdit_[i]) {
      auto* e = new QLineEdit(view_);
      e->setObjectName("SketchValueEdit");
      e->setFixedWidth(104);
      connect(e, &QLineEdit::textEdited, this, [this, i] {
        sizeTyped_[i] = true;
        drawPreview();
      });
      struct Keys : QObject {
        SketchEditor* ed;
        int index;
        Keys(SketchEditor* o, int i, QObject* parent) : QObject(parent), ed(o), index(i) {}
        bool eventFilter(QObject*, QEvent* ev) override {
          if (ev->type() != QEvent::KeyPress) return false;
          const int key = static_cast<QKeyEvent*>(ev)->key();
          if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            QTimer::singleShot(0, ed, [e = ed] { e->commitSizes(); });
            return true;
          }
          if (key == Qt::Key_Escape) {
            ed->resetSizeBoxes();
            ed->view_->setFocus();
            return true;
          }
          if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            ed->focusSizeBox((index + 1) % std::max(ed->sizeBoxCount(), 1));
            return true;
          }
          return false;
        }
      };
      e->installEventFilter(new Keys(this, i, e));
      sizeEdit_[i] = e;
    }
    QLineEdit* e = sizeEdit_[i];
    if (!sizeTyped_[i] && !e->hasFocus()) {
      if (mode_ == Mode::Polygon && i == 1) e->setText(tr("%1 sides").arg(int(live[i])));
      else e->setText(qs(model::length_format(live[i], unit())));
    }
    QPoint at = anchor.toPoint() + QPoint(18, 14 + i * 30);
    at.setX(std::clamp(at.x(), 4, view_->width() - e->width() - 4));
    at.setY(std::clamp(at.y(), 4, view_->height() - e->height() - 4));
    e->move(at);
    e->show();
    e->raise();
  }
}

void SketchEditor::hideSizeBoxes() {
  for (auto& e : sizeEdit_)
    if (e) e->hide();
}

void SketchEditor::resetSizeBoxes() {
  sizeTyped_[0] = sizeTyped_[1] = false;
  for (auto& e : sizeEdit_)
    if (e) e->clearFocus();
  drawPreview();
}

void SketchEditor::focusSizeBox(int i) {
  if (i < 0 || i >= sizeBoxCount() || !sizeEdit_[i]) return;
  sizeEdit_[i]->setFocus();
  sizeEdit_[i]->selectAll();
}

// Where the shape ends if the typed sizes win over the cursor.
SketchEditor::Hit SketchEditor::typedTarget(const Hit& first, Hit cursor, Typed& typed) {
  for (int i = 0; i < 2; ++i) typed.set[i] = typedValue(i, typed.value[i]);
  Hit h = cursor;
  switch (mode_) {
    case Mode::Line:
      if (typed.set[0]) {
        double du = h.u - first.u, dv = h.v - first.v;
        double len = std::hypot(du, dv);
        if (len < 1e-9) du = 1, dv = 0, len = 1;  // no direction yet: draw along +X
        h.u = first.u + du / len * typed.value[0];
        h.v = first.v + dv / len * typed.value[0];
        h.kind = Hit::None;
      }
      break;
    case Mode::Circle:
      if (typed.set[0]) {
        h.u = first.u + typed.value[0] / 2;
        h.v = first.v;
        h.kind = Hit::None;
      }
      break;
    case Mode::Rectangle:
    case Mode::RectangleCentre: {
      const double k = mode_ == Mode::RectangleCentre ? 0.5 : 1.0;
      for (int i = 0; i < 2; ++i) {
        if (!typed.set[i]) continue;
        double& c = i == 0 ? h.u : h.v;
        const double f = i == 0 ? first.u : first.v;
        c = f + (c >= f ? 1 : -1) * typed.value[i] * k;
        h.kind = Hit::None;
      }
      break;
    }
    case Mode::Polygon:
      if (typed.set[0]) {
        double du = h.u - first.u, dv = h.v - first.v;
        double len = std::hypot(du, dv);
        if (len < 1e-9) du = 0, dv = 1, len = 1;
        h.u = first.u + du / len * typed.value[0];
        h.v = first.v + dv / len * typed.value[0];
        h.kind = Hit::None;
      }
      break;
    default: break;
  }
  return h;
}

void SketchEditor::commitSizes() {
  if (clicks_.empty()) return;
  Typed typed;
  Hit cursor = snap(cursor_);
  if (mode_ == Mode::Line && prefs::flag(prefs::kInference) && cursor.kind != Hit::Point) {
    const Hit& a = clicks_.front();
    const double ang = std::abs(std::atan2(cursor.v - a.v, cursor.u - a.u)) * 180.0 / kPi;
    if (std::min(ang, 180.0 - ang) < kInferDeg) cursor.v = a.v;
    else if (std::abs(ang - 90.0) < kInferDeg) cursor.u = a.u;
  }
  const Hit target = typedTarget(clicks_.front(), cursor, typed);
  sizeTyped_[0] = sizeTyped_[1] = false;
  view_->setFocus();
  if (mode_ == Mode::Line || mode_ == Mode::Rectangle || mode_ == Mode::Circle) clickDraw(target, typed);
  else clickShape(target, typed);
}

// ---- Dimensions ----------------------------------------------------------------

double SketchEditor::measure(K kind, const Hit& a, const Hit& b) const {
  auto pt = [this](const Hit& h, double& x, double& y) {
    if (const auto* e = entity(h.id); e && e->kind == EK::Point) x = e->x0, y = e->y0;
    else x = h.u, y = h.v;
  };
  const auto* ea = entity(a.id);
  const auto* eb = entity(b.id);
  switch (kind) {
    case K::Length: return ea ? dist(ea->x0, ea->y0, ea->x1, ea->y1) : 0;
    case K::Diameter: return ea ? 2 * ea->radius : 0;
    case K::Radius: return ea ? ea->radius : 0;
    case K::Distance: {
      double x0, y0, x1, y1;
      pt(a, x0, y0);
      pt(b, x1, y1);
      return dist(x0, y0, x1, y1);
    }
    case K::PointLineDistance: {
      double x, y;
      pt(a, x, y);
      if (!eb) return 0;
      const double dx = eb->x1 - eb->x0, dy = eb->y1 - eb->y0, len = std::hypot(dx, dy);
      return len > 0 ? std::abs((x - eb->x0) * dy - (y - eb->y0) * dx) / len : 0;
    }
    case K::MajorRadius: return ea ? ea->radius : 0;
    case K::MinorRadius: return ea ? ea->minor : 0;
    case K::PointCircleDistance: {
      double x, y;
      pt(a, x, y);
      return eb ? std::abs(dist(x, y, eb->x0, eb->y0) - eb->radius) : 0;
    }
    case K::LineDistance:
    case K::LineCircleDistance: {
      if (!ea || !eb) return 0;
      const double px = eb->x0, py = eb->y0;  // b's start, or its centre
      const double dx = ea->x1 - ea->x0, dy = ea->y1 - ea->y0, len = std::hypot(dx, dy);
      const double d = len > 0 ? std::abs((px - ea->x0) * dy - (py - ea->y0) * dx) / len : 0;
      return kind == K::LineDistance ? d : std::abs(d - eb->radius);
    }
    case K::CircleDistance: {
      if (!ea || !eb) return 0;
      const double d = dist(ea->x0, ea->y0, eb->x0, eb->y0);
      const double out = d - ea->radius - eb->radius, in = std::abs(ea->radius - eb->radius) - d;
      return out > 0 ? out : in > 0 ? in : std::abs(out);
    }
    case K::Angle: {
      if (!ea || !eb) return 0;
      const double a1 = std::atan2(ea->y1 - ea->y0, ea->x1 - ea->x0);
      const double a2 = std::atan2(eb->y1 - eb->y0, eb->x1 - eb->x0);
      double deg = std::abs(a2 - a1) * 180.0 / kPi;
      if (deg > 180) deg = 360 - deg;
      return deg * kPi / 180.0;
    }
    default: return 0;
  }
}

void SketchEditor::clickDimension(const Hit& h, const QPointF& pos) {
  if (h.kind == Hit::Label) {
    for (const auto& c : constraints_) {
      if (c.id != h.id || !c.is_dimension) continue;
      if (c.driven) {
        emit message(tr("That is a driven dimension: it follows the geometry. Right-click it to make it driving."));
        return;
      }
      PendingDimension d;
      d.existing = c.id;
      d.kind = c.kind;
      d.angle = c.kind == K::Angle;
      openValueEditor(d, pos, c.value);
      return;
    }
    return;
  }
  auto kindOf = [this](const Hit& x) -> EK {
    if (x.kind == Hit::Point || x.kind == Hit::Origin) return EK::Point;
    const auto* e = entity(x.id);
    return e ? e->kind : EK::Point;
  };
  auto pointish = [&](const Hit& x) { return kindOf(x) == EK::Point; };
  auto isLine = [&](const Hit& x) { return kindOf(x) == EK::Line; };
  auto isRound = [&](const Hit& x) { return kindOf(x) == EK::Circle || kindOf(x) == EK::Arc; };

  if (h.kind == Hit::None) {
    // Click in space: a lone curve gets its own size.
    if (picks_.size() == 1 && picks_[0].kind == Hit::Curve) {
      const Hit c = picks_[0];
      const auto* e = entity(c.id);
      PendingDimension d;
      d.a = c;
      bool ok = true;
      switch (e ? e->kind : EK::Point) {
        case EK::Line: d.kind = K::Length; break;
        case EK::Circle: d.kind = K::Diameter; break;
        case EK::Arc: d.kind = K::Radius; break;
        case EK::Ellipse:
        case EK::EllipticalArc: {
          // Near the ends of the major axis: the major radius; else the minor.
          const double t = ellipseParam(*e, c.u, c.v);
          d.kind = std::abs(std::cos(t)) >= std::abs(std::sin(t)) ? K::MajorRadius : K::MinorRadius;
          break;
        }
        default:
          ok = false;
          emit message(tr("Splines are shaped by their points: dimension those instead."));
          break;
      }
      if (ok) openValueEditor(d, pos, measure(d.kind, d.a, {}));
    }
    picks_.clear();
    drawPreview();
    return;
  }
  picks_.push_back(h);
  if (picks_.size() < 2) {
    drawPreview();
    if (h.kind == Hit::Curve) emit message(tr("Click empty space to place its size, or pick another point or curve."));
    else emit message(tr("Pick a second point or a curve."));
    return;
  }
  Hit a = picks_[0], b = picks_[1];
  picks_.clear();
  PendingDimension d;
  if (pointish(b) && !pointish(a)) std::swap(a, b);  // points first
  if (isRound(a) && isLine(b)) std::swap(a, b);     // lines before circles
  if (pointish(a) && pointish(b)) {
    d.kind = K::Distance;
  } else if (pointish(a) && isLine(b)) {
    d.kind = K::PointLineDistance;
  } else if (pointish(a) && isRound(b)) {
    d.kind = K::PointCircleDistance;
  } else if (isLine(a) && isLine(b)) {
    const auto* ea = entity(a.id);
    const auto* eb = entity(b.id);
    const double cross = (ea->x1 - ea->x0) * (eb->y1 - eb->y0) - (ea->y1 - ea->y0) * (eb->x1 - eb->x0);
    const double la = dist(ea->x0, ea->y0, ea->x1, ea->y1), lb = dist(eb->x0, eb->y0, eb->x1, eb->y1);
    if (std::abs(cross) <= 1e-3 * la * lb) {
      d.kind = K::LineDistance;  // parallel lines: the gap between them
    } else {
      d.kind = K::Angle;
      d.angle = true;
    }
  } else if (isLine(a) && isRound(b)) {
    d.kind = K::LineCircleDistance;
  } else if (isRound(a) && isRound(b)) {
    d.kind = K::CircleDistance;
  } else {
    emit message(tr("Those two can't be dimensioned to each other."));
    drawPreview();
    return;
  }
  d.a = a, d.b = b;
  openValueEditor(d, pos, measure(d.kind, a, b));
  drawPreview();
}

void SketchEditor::openValueEditor(const PendingDimension& d, const QPointF& pos, double currentValue) {
  closeValueEditor();
  pending_ = d;
  valueOp_ = nullptr;
  valueKind_ = d.angle ? ValueKind::Angle : ValueKind::Length;
  auto* edit = new QLineEdit(view_);
  edit->setObjectName("SketchValueEdit");
  if (d.angle) {
    edit->setText(QString::number(currentValue * 180.0 / kPi, 'g', 6) + QStringLiteral("°"));
  } else {
    edit->setText(qs(model::length_format(currentValue, unit())));
  }
  placeValueBox(edit, pos);
  emit message(d.angle ? tr("Type an angle in degrees, Enter to apply, Esc to cancel.")
                       : tr("Type a length (e.g. 2 1/4, 1'6\", 30mm), Enter to apply, Esc to cancel."));
}

void SketchEditor::askValue(const QString& prompt, ValueKind kind, double initial, const QPointF& pos,
                            std::function<bool(double)> op) {
  closeValueEditor();
  valueKind_ = kind;
  auto* edit = new QLineEdit(view_);
  edit->setObjectName("SketchValueEdit");
  switch (kind) {
    case ValueKind::Length: edit->setText(qs(model::length_format(initial, unit()))); break;
    case ValueKind::Angle: edit->setText(QString::number(initial * 180.0 / kPi, 'g', 6) + QStringLiteral("°")); break;
    case ValueKind::Number: edit->setText(QString::number(initial, 'g', 6)); break;
  }
  placeValueBox(edit, pos);
  valueOp_ = std::move(op);  // after placeValueBox: closing an older box clears it
  emit message(prompt);
}

void SketchEditor::placeValueBox(QLineEdit* edit, const QPointF& pos) {
  edit->setFixedWidth(120);
  edit->adjustSize();
  QPoint at = pos.toPoint() + QPoint(12, -edit->height() - 6);
  at.setX(std::clamp(at.x(), 4, view_->width() - edit->width() - 4));
  at.setY(std::clamp(at.y(), 4, view_->height() - edit->height() - 4));
  edit->move(at);
  edit->show();
  edit->setFocus();
  edit->selectAll();
  valueEdit_ = edit;

  // Esc and Enter inside the field.
  struct EscFilter : QObject {
    SketchEditor* owner;
    explicit EscFilter(SketchEditor* o, QObject* parent) : QObject(parent), owner(o) {}
    bool eventFilter(QObject*, QEvent* ev) override {
      if (ev->type() != QEvent::KeyPress) return false;
      const int key = static_cast<QKeyEvent*>(ev)->key();
      if (key == Qt::Key_Escape) {
        QTimer::singleShot(0, owner, [o = owner] {
          o->closeValueEditor();
          o->resetDraft();
          o->drawPreview();
        });
        return true;
      }
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Swallow it: unhandled, Enter would travel on to the view.
        QTimer::singleShot(0, owner, [o = owner] { o->commitValue(); });
        return true;
      }
      return false;
    }
  };
  edit->installEventFilter(new EscFilter(this, edit));
}

void SketchEditor::closeValueEditor() {
  if (valueEdit_) {
    valueEdit_->hide();
    valueEdit_->deleteLater();
  }
  valueEdit_ = nullptr;
  valueOp_ = nullptr;
  if (view_) view_->setFocus();
}

void SketchEditor::commitValue() {
  if (!valueEdit_) return;
  const QString text = valueEdit_->text().trimmed();
  double value = 0;
  const bool forTool = bool(valueOp_);
  switch (valueKind_) {
    case ValueKind::Angle: {
      QString t = text;
      t.remove(QStringLiteral("°")).remove(QStringLiteral("deg"));
      bool ok = false;
      const double deg = t.trimmed().toDouble(&ok);
      // Dimensions sit between 0° and 180°; a rotation may be any turn.
      if (!ok || (!forTool && (deg <= 0 || deg >= 180)) || (forTool && (deg == 0 || std::abs(deg) > 360))) {
        emit message(forTool ? tr("Enter an angle in degrees (negative turns clockwise).")
                             : tr("Enter an angle between 0° and 180°."));
        return;
      }
      value = deg * kPi / 180.0;
      break;
    }
    case ValueKind::Number: {
      bool ok = false;
      value = text.toDouble(&ok);
      if (!ok || value <= 0) {
        emit message(tr("Enter a number larger than zero."));
        return;
      }
      break;
    }
    case ValueKind::Length: {
      const model::LengthParse p = model::length_parse(text.toStdString(), unit());
      if (!p.ok) {
        emit message(qs(p.error));
        return;
      }
      if (p.mm <= 0) {
        emit message(tr("A value must be larger than zero."));
        return;
      }
      value = p.mm;
      break;
    }
  }
  if (forTool) {
    const std::function<bool(double)> op = valueOp_;
    closeValueEditor();
    edit([&] { return op(value); });
    resetDraft();
    hintForMode();
    drawPreview();
    return;
  }
  const PendingDimension d = pending_;
  closeValueEditor();
  edit([&] {
    if (d.existing) {
      const model::SketchSolveInfo info = model::sketch_set_dimension(tl(), sketch_, d.existing, value);
      if (info.status != model::SketchSolveStatus::Solved) {
        emit message(tr("The sketch can't take that value."));
        return false;
      }
      return true;
    }
    std::uint64_t a = d.a.id, b = d.b.id;
    if (d.a.kind == Hit::Origin || (d.a.kind == Hit::Point && !a)) a = resolve(d.a);
    if (d.b.kind == Hit::Origin || (d.b.kind == Hit::Point && !b)) b = resolve(d.b);
    return applyResult(model::sketch_add_constraint(tl(), sketch_, d.kind, a, b, 0, value), d.kind, a, b, 0);
  });
}

// A dimension the other constraints already imply can go in as a driven
// (reference) one instead, as Fusion offers.
bool SketchEditor::applyResult(const model::ConstraintResult& r, K kind, std::uint64_t a, std::uint64_t b,
                               std::uint64_t c) {
  if (r.ok) return true;
  if (!r.redundant) {
    emit message(qs(r.message));
    return false;
  }
  const auto answer = QMessageBox::question(
      view_, tr("Over-constrained"),
      tr("Adding this dimension would over-constrain the sketch.\n\nAdd it as a driven dimension? It will "
         "show the size without controlling it."),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
  if (answer != QMessageBox::Yes) return false;
  const model::ConstraintResult d = model::sketch_add_driven(tl(), sketch_, kind, a, b, c);
  if (!d.ok) emit message(qs(d.message));
  return d.ok;
}

void SketchEditor::showDimensionMenu(std::uint64_t constraint, const QPointF& pos) {
  const model::SketchConstraintInfo* info = nullptr;
  for (const auto& c : constraints_)
    if (c.id == constraint) info = &c;
  if (!info) return;
  selected_.clear();
  selectedCon_ = {constraint};
  redraw();
  QMenu menu(view_);
  QAction* edit = nullptr;
  QAction* driven = nullptr;
  if (info->is_dimension) {
    edit = menu.addAction(tr("Edit Dimension"));
    edit->setEnabled(!info->driven);
    driven = menu.addAction(tr("Driven Dimension"));
    driven->setCheckable(true);
    driven->setChecked(info->driven);
    driven->setToolTip(tr("A driven dimension shows the size; it does not control it."));
    menu.addSeparator();
  }
  QAction* remove = menu.addAction(icons::get("close", 16), tr("Delete"));
  const bool isDriven = info->driven;
  const K kind = info->kind;
  const double value = info->value;
  QAction* chosen = menu.exec(view_->mapToGlobal(pos.toPoint()));
  if (!chosen) return;
  if (chosen == remove) {
    deleteSelection();
  } else if (chosen == edit) {
    PendingDimension d;
    d.existing = constraint;
    d.kind = kind;
    d.angle = kind == K::Angle;
    openValueEditor(d, pos, value);
  } else if (chosen == driven) {
    this->edit([&] {
      const model::ConstraintResult r = model::sketch_set_driven(tl(), sketch_, constraint, !isDriven);
      if (!r.ok) emit message(r.redundant ? tr("The other constraints already set that size, so it stays driven.")
                                          : qs(r.message));
      return bool(r.ok);
    });
  }
}

// ---- Constraints ------------------------------------------------------------------

void SketchEditor::clickConstrain(const Hit& h) {
  if (h.kind == Hit::None || h.kind == Hit::Label) {
    picks_.clear();
    drawPreview();
    return;
  }
  picks_.push_back(h);
  auto kindOf = [this](const Hit& x) -> EK {
    if (x.kind == Hit::Point || x.kind == Hit::Origin) return EK::Point;
    const auto* e = entity(x.id);
    return e ? e->kind : EK::Point;
  };
  auto isRound = [&](const Hit& x) { return kindOf(x) == EK::Circle || kindOf(x) == EK::Arc; };
  auto isLine = [&](const Hit& x) { return kindOf(x) == EK::Line; };
  auto isPt = [&](const Hit& x) { return kindOf(x) == EK::Point; };

  auto apply = [&](K kind, Hit a, Hit b = {}, Hit c = {}) {
    picks_.clear();
    edit([&] {
      auto id = [&](const Hit& x) -> std::uint64_t {
        if (x.kind == Hit::None) return 0;
        return isPt(x) ? resolve(x) : x.id;
      };
      return addConstraint(kind, id(a), id(b), id(c));
    });
    drawPreview();
  };
  auto wrong = [&](const QString& what) {
    picks_.clear();
    emit message(what);
    drawPreview();
  };

  const Hit& a = picks_[0];
  switch (constraintKind_) {
    case K::Horizontal:
    case K::Vertical: {
      const bool horiz = constraintKind_ == K::Horizontal;
      if (picks_.size() == 1) {
        if (isLine(a)) return apply(horiz ? K::Horizontal : K::Vertical, a);
        if (!isPt(a)) return wrong(tr("Pick a line, or two points."));
        emit message(tr("Pick the second point."));
        break;
      }
      if (!isPt(picks_[1])) return wrong(tr("Pick a line, or two points."));
      return apply(horiz ? K::HorizontalPoints : K::VerticalPoints, a, picks_[1]);
    }
    case K::Fix: {
      picks_.clear();
      if (isPt(a)) return apply(K::Fix, a);
      const auto* e = entity(a.id);
      if (usesSamples(e->kind)) return apply(K::Fix, a);  // the model fixes the whole curve
      edit([&] {
        if (e->kind == EK::Line) {
          addConstraint(K::Fix, e->p0, 0, 0, 0, true);
          addConstraint(K::Fix, e->p1, 0, 0, 0, true);
        } else {
          addConstraint(K::Fix, e->p0, 0, 0, 0, true);
          if (e->kind == EK::Arc) {
            addConstraint(K::Fix, e->p1, 0, 0, 0, true);
            addConstraint(K::Fix, e->p2, 0, 0, 0, true);
          } else {
            addConstraint(K::Diameter, e->id, 0, 0, 2 * e->radius, true);
          }
        }
        return true;
      });
      break;
    }
    case K::Coincident: {
      if (picks_.size() < 2) break;
      Hit p = a, q = picks_[1];
      if (isPt(p) && isPt(q)) return apply(K::Coincident, p, q);
      if (!isPt(p)) std::swap(p, q);
      if (!isPt(p)) return wrong(tr("Coincident needs a point and a point or curve."));
      return apply(K::PointOnCurve, p, q);
    }
    case K::Midpoint: {
      if (picks_.size() < 2) break;
      Hit p = a, q = picks_[1];
      if (!isPt(p)) std::swap(p, q);
      if (!isPt(p) || !isLine(q)) return wrong(tr("Midpoint needs a point and a line."));
      return apply(K::Midpoint, p, q);
    }
    case K::Parallel:
    case K::Perpendicular:
    case K::Collinear: {
      if (!isLine(picks_.back())) return wrong(tr("%1 needs two lines.").arg(kindName(constraintKind_)));
      if (picks_.size() < 2) break;
      return apply(constraintKind_, a, picks_[1]);
    }
    case K::Equal: {
      if (isPt(picks_.back())) return wrong(tr("Equal needs two lines, or two circles/arcs."));
      if (picks_.size() < 2) break;
      if (isLine(a) != isLine(picks_[1])) return wrong(tr("Equal needs two lines, or two circles/arcs."));
      return apply(K::Equal, a, picks_[1]);
    }
    case K::Concentric: {
      if (!isRound(picks_.back())) return wrong(tr("Concentric needs two circles or arcs."));
      if (picks_.size() < 2) break;
      return apply(K::Concentric, a, picks_[1]);
    }
    case K::Tangent: {
      if (isPt(picks_.back())) return wrong(tr("Tangent needs two curves (one round)."));
      if (picks_.size() < 2) break;
      if (isLine(a) && isLine(picks_[1])) return wrong(tr("Tangent needs a circle or arc."));
      return apply(K::Tangent, a, picks_[1]);
    }
    case K::Symmetric: {
      const size_t n = picks_.size();
      if (n <= 2 && !isPt(picks_.back())) return wrong(tr("Symmetric: pick two points, then the line."));
      if (n == 3 && !isLine(picks_.back())) return wrong(tr("Symmetric: the third pick is the line."));
      if (n < 3) {
        emit message(n == 1 ? tr("Pick the second point.") : tr("Pick the line of symmetry."));
        break;
      }
      return apply(K::Symmetric, a, picks_[1], picks_[2]);
    }
    case K::Smooth: {
      if (isPt(picks_.back())) return wrong(tr("Smooth needs a spline and a curve that meets it."));
      if (picks_.size() < 2) break;
      if (kindOf(a) != EK::Spline && kindOf(picks_[1]) != EK::Spline)
        return wrong(tr("Smooth needs at least one spline."));
      return apply(K::Smooth, a, picks_[1]);
    }
    default: picks_.clear(); break;
  }
  drawPreview();
}

// ---- Display ----------------------------------------------------------------------

QString SketchEditor::dimensionText(const model::SketchConstraintInfo& c) {
  QString text;
  if (c.kind == K::Angle) {
    text = QString::number(c.value * 180.0 / kPi, 'g', 4) + QStringLiteral("°");
  } else {
    const QString len = qs(model::length_format(c.value, unit()));
    if (c.kind == K::Diameter) text = QStringLiteral("Ø") + len;
    else if (c.kind == K::Radius || c.kind == K::MajorRadius || c.kind == K::MinorRadius) text = QStringLiteral("R") + len;
    else text = len;
  }
  // Driven (reference) dimensions in brackets, as drawings show them.
  return c.driven ? QStringLiteral("(%1)").arg(text) : text;
}

void SketchEditor::redraw() {
  const theme::Palette& pal = theme::current();
  const geom::Plane gp = geomPlane(plane_);
  const double upp = unitsPerPixel();

  // Entities touched by a conflicting constraint draw red.
  QSet<std::uint64_t> conflicted;
  for (const auto& c : constraints_)
    if (c.conflicting)
      for (std::uint64_t id : {c.a, c.b, c.c})
        if (id) conflicted.insert(id);

  std::vector<geom::Seg2> free, fixed, construction, conflict, selected;
  std::vector<geom::Point2> pFree, pFixed, pSelected;
  for (const auto& e : entities_) {
    if (e.kind == EK::Point) {
      auto& bucket = selected_.contains(e.id) ? pSelected : e.fully_constrained ? pFixed : pFree;
      bucket.push_back({e.x0, e.y0});
      continue;
    }
    const geom::Seg2 s = segOf(e);
    if (selected_.contains(e.id)) selected.push_back(s);
    else if (conflicted.contains(e.id)) conflict.push_back(s);
    else if (e.construction) construction.push_back(s);
    else if (e.fully_constrained) fixed.push_back(s);
    else free.push_back(s);
  }

  Overlay o;
  // Closed regions, faintly filled so you can see what will extrude.
  for (const auto& p : showProfiles_ ? profiles_ : std::vector<model::ProfileInfo>{}) {
    std::vector<std::vector<geom::Point2>> loops;
    std::size_t k = 0;
    for (std::size_t n : p.loop_sizes) {
      std::vector<geom::Point2> loop;
      for (std::size_t i = 0; i < n; ++i, ++k) loop.push_back({p.outlines[2 * k], p.outlines[2 * k + 1]});
      loops.push_back(std::move(loop));
    }
    o.faces.push_back({geom::makePolygonFace(gp, loops), pal.highlight, 0.85});
  }
  auto curves = [&](const std::vector<geom::Seg2>& segs, const QColor& c, double w, bool dashed = false) {
    if (!segs.empty()) o.curves.push_back({geom::makeCurves(gp, segs), c, w, dashed});
  };
  curves(construction, pal.textMuted, 1.5, true);
  curves(free, pal.highlight, 2.0);
  curves(fixed, pal.text, 2.0);
  curves(conflict, pal.danger, 2.5);
  curves(selected, pal.accent, 2.5);
  auto points = [&](const std::vector<geom::Point2>& pts, const QColor& c, double size) {
    if (!pts.empty()) o.points.push_back({geom::makeVertices(gp, pts), c, size});
  };
  if (!showPoints_) pFree.clear(), pFixed.clear();
  points(pFree, pal.highlight, 2.2);
  points(pFixed, pal.text, 2.2);
  points(pSelected, pal.accent, 3.0);
  // The sketch origin.
  points({{0.0, 0.0}}, pal.accent, 1.6);
  // Open ends that keep a profile from closing, and collapsed curves.
  std::vector<geom::Point2> loose;
  for (std::uint64_t id : looseEnds_)
    if (const auto* e = entity(id)) loose.push_back({e->x0, e->y0});
  if (showProfiles_) points(loose, pal.danger, 3.0);

  // Dimensions and constraint glyphs.
  labels_.clear();
  const bool showTags = showTags_;
  std::map<std::uint64_t, double> glyphOffset;  // px used so far along each entity
  auto addLabel = [&](double u, double v, const QString& text, const QColor& color, double height,
                      std::uint64_t con, bool dim) {
    const gp_Pnt w = world(u, v);
    o.labels.push_back({w, text, color, height, pal.panel});
    labels_.push_back({view_->project(w), con, dim});
  };
  auto glyphSpot = [&](std::uint64_t id, const QString& text, double& u, double& v) -> bool {
    const auto* e = entity(id);
    if (!e) return false;
    // Tags sit side by side: advance by this tag's width.
    const double width = 8.0 * text.size() + 10.0;
    const double used = glyphOffset[id];
    glyphOffset[id] = used + width;
    const double n = 1.0;  // step multiplier (step carries the px offset)
    const double off = 14 * upp, step = (used + width / 2) * upp;
    switch (e->kind) {
      case EK::Point: u = e->x0 + off + n * step, v = e->y0 + off; return true;
      case EK::Line: {
        const double dx = e->x1 - e->x0, dy = e->y1 - e->y0, len = std::max(std::hypot(dx, dy), 1e-12);
        const double nx = -dy / len, ny = dx / len, tx = dx / len, ty = dy / len;
        const double centre = n * step;  // tags run along the line from its middle
        // Screen-aligned tags: clear the line by half the tag's width when it runs vertically.
        const double clear = off + std::abs(nx) * (width / 2 - 6) * upp;
        u = (e->x0 + e->x1) / 2 + nx * clear + tx * centre;
        v = (e->y0 + e->y1) / 2 + ny * clear + ty * centre;
        return true;
      }
      default: {
        const double a = kPi / 4 + used * upp / std::max(e->radius + off, 1e-9);
        u = e->x0 + (e->radius + off) * std::cos(a), v = e->y0 + (e->radius + off) * std::sin(a);
        return true;
      }
    }
  };
  std::set<std::pair<std::uint64_t, int>> tagged;
  for (const auto& c : constraints_) {
    const bool sel = selectedCon_.contains(c.id);
    if (c.is_dimension) {
      if (!showDims_) continue;
      const QColor color = sel ? pal.accent : c.conflicting ? pal.danger : c.driven ? pal.textMuted : pal.text;
      const auto* ea = entity(c.a);
      const auto* eb = entity(c.b);
      double u = 0, v = 0;
      auto pointPos = [](const model::SketchEntityInfo* e, double& x, double& y) {
        if (e) x = e->x0, y = e->y0;
      };
      switch (c.kind) {
        case K::Length:
          if (!ea) continue;
          {
            const double dx = ea->x1 - ea->x0, dy = ea->y1 - ea->y0, len = std::max(std::hypot(dx, dy), 1e-12);
            // Opposite side from the constraint glyphs.
            u = (ea->x0 + ea->x1) / 2 + dy / len * 20 * upp;
            v = (ea->y0 + ea->y1) / 2 - dx / len * 20 * upp;
          }
          break;
        case K::Diameter:
        case K::Radius:
          if (!ea) continue;
          if (ea->kind == EK::Arc) {
            const double a0 = std::atan2(ea->y1 - ea->y0, ea->x1 - ea->x0);
            const double a1 = std::atan2(ea->y2 - ea->y0, ea->x2 - ea->x0);
            const double mid = a0 + norm2pi(a1 - a0) / 2;
            u = ea->x0 + (ea->radius + 22 * upp) * std::cos(mid);
            v = ea->y0 + (ea->radius + 22 * upp) * std::sin(mid);
          } else {
            u = ea->x0 + (ea->radius + 26 * upp) * std::cos(-kPi / 4);
            v = ea->y0 + (ea->radius + 26 * upp) * std::sin(-kPi / 4);
          }
          break;
        case K::MajorRadius:
        case K::MinorRadius: {
          if (!ea) continue;
          // Along the axis the radius measures, just outside the curve.
          const double mx = ea->x1 - ea->x0, my = ea->y1 - ea->y0, a = std::max(std::hypot(mx, my), 1e-12);
          const double ux = mx / a, uy = my / a;
          if (c.kind == K::MajorRadius) {
            u = ea->x0 + ux * (a / 2) - uy * 12 * upp, v = ea->y0 + uy * (a / 2) + ux * 12 * upp;
          } else {
            u = ea->x0 - uy * (ea->minor / 2) + ux * 12 * upp, v = ea->y0 + ux * (ea->minor / 2) + uy * 12 * upp;
          }
          break;
        }
        case K::PointCircleDistance:
        case K::CircleDistance:
        case K::LineCircleDistance:
        case K::LineDistance: {
          if (!ea || !eb) continue;
          // Halfway between the two, a little off the joining line.
          double x0 = ea->x0, y0 = ea->y0;
          if (c.kind == K::LineCircleDistance || c.kind == K::LineDistance) {
            double t;
            segmentDistance(eb->x0, eb->y0, ea->x0, ea->y0, ea->x1, ea->y1, &t);
            x0 = ea->x0 + t * (ea->x1 - ea->x0), y0 = ea->y0 + t * (ea->y1 - ea->y0);
          }
          u = (x0 + eb->x0) / 2 + 10 * upp, v = (y0 + eb->y0) / 2 + 10 * upp;
          break;
        }
        case K::PointLineDistance: {
          if (!ea || !eb) continue;
          double fx, fy, t;
          segmentDistance(ea->x0, ea->y0, eb->x0, eb->y0, eb->x1, eb->y1, &t);
          // Foot on the infinite line.
          const double dx = eb->x1 - eb->x0, dy = eb->y1 - eb->y0, len2 = std::max(dx * dx + dy * dy, 1e-12);
          t = ((ea->x0 - eb->x0) * dx + (ea->y0 - eb->y0) * dy) / len2;
          fx = eb->x0 + t * dx, fy = eb->y0 + t * dy;
          u = (ea->x0 + fx) / 2, v = (ea->y0 + fy) / 2;
          break;
        }
        case K::Angle: {
          if (!ea || !eb) continue;
          const double x1 = ea->x0, y1 = ea->y0, dx1 = ea->x1 - ea->x0, dy1 = ea->y1 - ea->y0;
          const double x2 = eb->x0, y2 = eb->y0, dx2 = eb->x1 - eb->x0, dy2 = eb->y1 - eb->y0;
          const double den = dx1 * dy2 - dy1 * dx2;
          if (std::abs(den) > 1e-12) {
            const double s = ((x2 - x1) * dy2 - (y2 - y1) * dx2) / den;
            const double ix = x1 + s * dx1, iy = y1 + s * dy1;
            const double mx = ((ea->x0 + ea->x1) / 2 + (eb->x0 + eb->x1) / 2) / 2;
            const double my = ((ea->y0 + ea->y1) / 2 + (eb->y0 + eb->y1) / 2) / 2;
            const double l = std::max(dist(ix, iy, mx, my), 1e-12);
            u = ix + (mx - ix) / l * 40 * upp, v = iy + (my - iy) / l * 40 * upp;
          } else {
            u = (ea->x0 + eb->x0) / 2, v = (ea->y0 + eb->y0) / 2;
          }
          break;
        }
        default: {
          double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
          pointPos(ea, x0, y0);
          pointPos(eb, x1, y1);
          const double dx = x1 - x0, dy = y1 - y0, len = std::max(std::hypot(dx, dy), 1e-12);
          u = (x0 + x1) / 2 - dy / len * 14 * upp, v = (y0 + y1) / 2 + dx / len * 14 * upp;
          break;
        }
      }
      addLabel(u, v, dimensionText(c), color, 15.0, c.id, true);
      continue;
    }
    const QString g = glyph(c.kind);
    if (g.isEmpty() || !showTags) continue;
    const QColor color = sel ? pal.accent : c.conflicting ? pal.danger : pal.text;
    std::vector<std::uint64_t> on;
    if (c.kind == K::Symmetric) on = {c.c};
    else if (c.kind == K::Midpoint) on = {c.a};
    else if (c.kind == K::HorizontalPoints || c.kind == K::VerticalPoints) on = {c.a};
    else on = {c.a, c.b};
    for (std::uint64_t id : on) {
      if (!id) continue;
      // One tag of a kind per curve: a hexagon's five Equals show once on each side.
      if (!tagged.insert({id, int(c.kind)}).second) continue;
      double u, v;
      if (glyphSpot(id, g, u, v)) addLabel(u, v, g, color, 12.0, c.id, false);
    }
  }

  view_->setOverlay("sketch", o);
}

void SketchEditor::drawHover() {
  const theme::Palette& pal = theme::current();
  Overlay o;
  const geom::Plane gp = geomPlane(plane_);
  const bool showHover = !dragging_ && !isDrawing(mode_) && mode_ != Mode::Project;
  auto highlight = [&](const Hit& h, const QColor& color) {
    if (h.kind == Hit::Point || h.kind == Hit::Origin) {
      o.points.push_back({geom::makeVertices(gp, {{h.u, h.v}}), color, 3.2});
    } else if (h.kind == Hit::Curve) {
      if (const auto* e = entity(h.id)) o.curves.push_back({geom::makeCurves(gp, {segOf(*e)}), color, 3.5, false});
    }
  };
  const bool trimming = mode_ == Mode::Trim;
  if (showHover) highlight(hover_, trimming ? pal.danger : pal.accent.lighter(115));
  for (const Hit& p : picks_) highlight(p, pal.accent);
  for (std::uint64_t id : opCurves_) {
    Hit h;
    h.kind = Hit::Curve;
    h.id = id;
    highlight(h, pal.accent);
  }
  if (mode_ == Mode::Project && hoverEdge_ >= 0 && hoverEdge_ < int(bodyEdges_.size()))
    o.curves.push_back({bodyEdges_[hoverEdge_], pal.accent, 4.0, false});
  view_->setOverlay("sketch-hover", o);
}

void SketchEditor::drawPreview() {
  const theme::Palette& pal = theme::current();
  const geom::Plane gp = geomPlane(plane_);
  Overlay o;
  drawHover();
  if (!hasCursor_) {
    view_->setOverlay("sketch-preview", o);
    return;
  }
  const double upp = unitsPerPixel();
  const Hit h = snap(cursor_);
  double cu = h.u, cv = h.v;
  if (h.kind == Hit::None && !toPlane(cursor_, cu, cv)) return;

  const bool drawing = isDrawing(mode_);
  // Alignment and extension guides from the snap.
  if (drawing || (picksCurves(mode_) && opStage_ >= 1)) {
    std::vector<geom::Seg2> g;
    for (const auto& q : guides_) {
      geom::Seg2 s;
      s.x0 = q[0], s.y0 = q[1], s.x1 = q[2], s.y1 = q[3];
      if (dist(s.x0, s.y0, s.x1, s.y1) > 1e-9) g.push_back(s);
    }
    if (!g.empty()) o.curves.push_back({geom::makeCurves(gp, g), pal.textMuted, 1.0, true});
    if (h.mid) o.labels.push_back({world(cu, cv + 14 * upp), QStringLiteral("Mid"), pal.accent, 12.0, pal.panel});
  }
  if (drawing) {
    // Snap marker.
    if (h.kind == Hit::Point || h.kind == Hit::Origin) {
      o.points.push_back({geom::makeVertices(gp, {{cu, cv}}), pal.accent, 3.6});
    } else if (h.kind == Hit::Curve) {
      o.points.push_back({geom::makeVertices(gp, {{cu, cv}}), pal.accent, 2.4});
    }
  }
  const auto fmt = [this](double mm) { return qs(model::length_format(mm, unit())); };

  double live[2] = {0, 0};  // sizes for the typed-value boxes
  if (sizeBoxCount() == 0) hideSizeBoxes();
  if (!clicks_.empty() && drawing) {
    const Hit& a = clicks_[0];
    std::vector<geom::Seg2> segs;
    auto line = [&](double x0, double y0, double x1, double y1) {
      geom::Seg2 s;
      s.x0 = x0, s.y0 = y0, s.x1 = x1, s.y1 = y1;
      segs.push_back(s);
    };
    switch (mode_) {
      case Mode::Line: {
        QString infer;
        if (h.kind != Hit::Point && h.kind != Hit::Origin && prefs::flag(prefs::kInference)) {
          const double ang = std::abs(std::atan2(cv - a.v, cu - a.u)) * 180.0 / kPi;
          if (std::min(ang, 180.0 - ang) < kInferDeg) cv = a.v, infer = "H";
          else if (std::abs(ang - 90.0) < kInferDeg) cu = a.u, infer = "V";
        }
        {
          Hit c = h;
          c.u = cu, c.v = cv;
          Typed t;
          c = typedTarget(a, c, t);
          cu = c.u, cv = c.v;
        }
        line(a.u, a.v, cu, cv);
        live[0] = dist(a.u, a.v, cu, cv);
        if (!infer.isEmpty())
          o.labels.push_back({world((a.u + cu) / 2, (a.v + cv) / 2 + 16 * upp), infer, pal.accent, 14.0, pal.panel});
        break;
      }
      case Mode::Rectangle: {
        Hit c = h;
        c.u = cu, c.v = cv;
        Typed t;
        c = typedTarget(a, c, t);
        cu = c.u, cv = c.v;
        line(a.u, a.v, cu, a.v), line(cu, a.v, cu, cv), line(cu, cv, a.u, cv), line(a.u, cv, a.u, a.v);
        live[0] = std::abs(cu - a.u);
        live[1] = std::abs(cv - a.v);
        break;
      }
      case Mode::Circle: {
        double r = dist(a.u, a.v, cu, cv);
        if (double typedD; typedValue(0, typedD)) {
          const double k = r > 1e-9 ? typedD / 2 / r : 0;
          if (r <= 1e-9) cu = a.u + typedD / 2, cv = a.v;
          else cu = a.u + (cu - a.u) * k, cv = a.v + (cv - a.v) * k;
          r = typedD / 2;
        }
        live[0] = 2 * r;
        if (r > 1e-9) {
          geom::Seg2 s;
          s.kind = geom::Seg2::Kind::Circle;
          s.cx = a.u, s.cy = a.v, s.radius = r;
          segs.push_back(s);
          line(a.u, a.v, cu, cv);
        }
        break;
      }
      case Mode::Arc: {
        if (clicks_.size() == 1) {
          line(a.u, a.v, cu, cv);
          break;
        }
        const Hit& b = clicks_[1];
        const double ax = a.u, ay = a.v, bx = b.u, by = b.v, cx = cu, cy = cv;
        const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (std::abs(d) < 1e-12) {
          line(ax, ay, bx, by);
          break;
        }
        const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        geom::Seg2 s;
        s.kind = geom::Seg2::Kind::Arc;
        s.cx = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
        s.cy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
        s.radius = dist(s.cx, s.cy, ax, ay);
        const double cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (cross > 0) s.x0 = bx, s.y0 = by, s.x1 = ax, s.y1 = ay;
        else s.x0 = ax, s.y0 = ay, s.x1 = bx, s.y1 = by;
        s.ccw = true;
        segs.push_back(s);
        o.labels.push_back({world(cu, cv + 16 * upp), QStringLiteral("R") + fmt(s.radius), pal.accent, 14.0, pal.panel});
        break;
      }
      default:
        previewShape(a, cu, cv, segs, live);
        break;
    }
    if (!segs.empty()) o.curves.push_back({geom::makeCurves(gp, segs), pal.highlight, 1.8, false});
    if (sizeBoxCount() > 0) showSizeBoxes(live, cursor_);
  }
  if (!drawing) previewModify(o, cu, cv);
  view_->setOverlay("sketch-preview", o);
}
