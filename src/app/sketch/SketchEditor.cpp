#include "SketchEditor.h"

#include "Kernel.h"
#include "Theme.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QLineF>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace {

using K = model::ConstraintKind;
using EK = model::SketchEntityKind;

constexpr double kPi = 3.14159265358979323846;
constexpr double kSnapPx = 9.0;     // pick radius
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
    default:
      break;
  }
  return s;
}

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
    default: return QObject::tr("Constraint");
  }
}

}  // namespace

SketchEditor::SketchEditor(Viewport* view, Host host, std::uint64_t sketch, QObject* parent)
    : QObject(parent), view_(view), host_(std::move(host)), sketch_(sketch) {
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
  const double tol = kSnapPx * unitsPerPixel();

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
      default: break;
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
  Hit h = hitTest(pos);
  if (h.kind == Hit::Curve) {
    const auto* e = entity(h.id);
    if (e->kind == EK::Line) {
      double t;
      segmentDistance(h.u, h.v, e->x0, e->y0, e->x1, e->y1, &t);
      h.u = e->x0 + t * (e->x1 - e->x0);
      h.v = e->y0 + t * (e->y1 - e->y0);
    } else {
      const double a = std::atan2(h.v - e->y0, h.u - e->x0);
      h.u = e->x0 + e->radius * std::cos(a);
      h.v = e->y0 + e->radius * std::sin(a);
    }
  }
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
  if (h.kind == Hit::Curve && h.id) addConstraint(K::PointOnCurve, p, h.id, 0, 0, /*quiet=*/true);
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

void SketchEditor::setMode(Mode mode) {
  closeValueEditor();
  resetDraft();
  mode_ = mode;
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
  clicks_.clear();
  picks_.clear();
  chainStart_ = 0;
}

void SketchEditor::hintForMode() {
  switch (mode_) {
    case Mode::Select: emit message(tr("Select geometry. Drag points to move them. Double-click a dimension to edit it.")); break;
    case Mode::Line: emit message(tr("Line: click points. Double-click, right-click or Esc to stop.")); break;
    case Mode::Rectangle: emit message(tr("Rectangle: click two opposite corners.")); break;
    case Mode::Circle: emit message(tr("Circle: click the centre, then the radius.")); break;
    case Mode::Arc: emit message(tr("3-point arc: click the start, the end, then a point on the arc.")); break;
    case Mode::Point: emit message(tr("Point: click to place points.")); break;
    case Mode::Dimension: emit message(tr("Dimension: pick a line, circle or arc, or two points/lines. Type a value, Enter.")); break;
    case Mode::Constrain: emit message(tr("%1: pick the geometry.").arg(kindName(constraintKind_))); break;
  }
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
    if (!clicks_.empty() || !picks_.empty()) {
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

  const Hit h = (mode_ == Mode::Select || mode_ == Mode::Dimension) ? hitTest(cursor_, true, true)
                : mode_ == Mode::Constrain                          ? hitTest(cursor_)
                                                                    : snap(cursor_);
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
  if (mode_ == Mode::Line) {
    resetDraft();
    drawPreview();
    return true;
  }
  const Hit h = hitTest(e->position(), true, true);
  if (h.kind == Hit::Label && (mode_ == Mode::Select || mode_ == Mode::Dimension)) {
    for (const auto& c : constraints_) {
      if (c.id != h.id || !c.is_dimension) continue;
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
  switch (e->key()) {
    case Qt::Key_Escape:
      if (!clicks_.empty() || !picks_.empty()) {
        resetDraft();
        drawPreview();
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
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (mode_ == Mode::Dimension && picks_.size() == 1 && picks_[0].kind == Hit::Curve) {
        clickDimension(Hit{}, hasCursor_ ? cursor_ : QPointF(view_->width() / 2.0, view_->height() / 2.0));
      }
      return true;
    default: return false;
  }
}

void SketchEditor::viewChanged() { redrawTimer_.start(); }

// ---- Drawing tools ----------------------------------------------------------------

void SketchEditor::clickDraw(const Hit& hIn) {
  Hit h = hIn;
  const double tol = kSnapPx * unitsPerPixel();

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
      if (h.kind != Hit::Point && h.kind != Hit::Origin) {
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
        if (cross > 0) model::sketch_add_arc(tl(), sketch_, centre, pe, ps);
        else model::sketch_add_arc(tl(), sketch_, centre, ps, pe);
        return true;
      });
      clicks_.clear();
      drawPreview();
      return;
    }
    default: return;
  }
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
      PendingDimension d;
      d.existing = c.id;
      d.kind = c.kind;
      d.angle = c.kind == K::Angle;
      openValueEditor(d, pos, c.value);
      return;
    }
    return;
  }
  const auto* e = entity(h.id);
  if (picks_.empty() && h.kind == Hit::Curve && e && (e->kind == EK::Circle || e->kind == EK::Arc)) {
    PendingDimension d;
    d.kind = e->kind == EK::Circle ? K::Diameter : K::Radius;
    d.a = h;
    openValueEditor(d, pos, measure(d.kind, h, {}));
    return;
  }
  if (h.kind == Hit::None) {
    // Click in space: a lone line gets its length.
    if (picks_.size() == 1 && picks_[0].kind == Hit::Curve) {
      PendingDimension d;
      d.kind = K::Length;
      d.a = picks_[0];
      openValueEditor(d, pos, measure(K::Length, d.a, {}));
    }
    picks_.clear();
    drawPreview();
    return;
  }
  if (h.kind == Hit::Curve && e && e->kind != EK::Line) {
    emit message(tr("Circles and arcs are dimensioned on their own: click one first."));
    return;
  }
  picks_.push_back(h);
  if (picks_.size() < 2) {
    drawPreview();
    if (h.kind == Hit::Curve) emit message(tr("Click empty space for the line's length, or another line/point."));
    else emit message(tr("Pick a second point or a line."));
    return;
  }
  Hit a = picks_[0], b = picks_[1];
  picks_.clear();
  auto pointish = [](const Hit& x) { return x.kind == Hit::Point || x.kind == Hit::Origin; };
  PendingDimension d;
  if (pointish(a) && pointish(b)) {
    d.kind = K::Distance;
  } else if (pointish(a) != pointish(b)) {
    if (!pointish(a)) std::swap(a, b);
    d.kind = K::PointLineDistance;
  } else {
    d.kind = K::Angle;
    d.angle = true;
  }
  d.a = a, d.b = b;
  openValueEditor(d, pos, measure(d.kind, a, b));
  drawPreview();
}

void SketchEditor::openValueEditor(const PendingDimension& d, const QPointF& pos, double currentValue) {
  closeValueEditor();
  pending_ = d;
  auto* edit = new QLineEdit(view_);
  edit->setObjectName("SketchValueEdit");
  if (d.angle) {
    edit->setText(QString::number(currentValue * 180.0 / kPi, 'g', 6) + QStringLiteral("°"));
  } else {
    edit->setText(qs(model::length_format(currentValue, unit())));
  }
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
  emit message(d.angle ? tr("Type an angle in degrees, Enter to apply, Esc to cancel.")
                       : tr("Type a length (e.g. 2 1/4, 1'6\", 30mm), Enter to apply, Esc to cancel."));

  // Esc inside the field.
  struct EscFilter : QObject {
    SketchEditor* owner;
    explicit EscFilter(SketchEditor* o, QObject* parent) : QObject(parent), owner(o) {}
    bool eventFilter(QObject*, QEvent* ev) override {
      if (ev->type() != QEvent::KeyPress) return false;
      const int key = static_cast<QKeyEvent*>(ev)->key();
      if (key == Qt::Key_Escape) {
        QTimer::singleShot(0, owner, [o = owner] { o->closeValueEditor(); });
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
  if (view_) view_->setFocus();
}

void SketchEditor::commitValue() {
  if (!valueEdit_) return;
  const QString text = valueEdit_->text().trimmed();
  double value = 0;
  if (pending_.angle) {
    QString t = text;
    t.remove(QStringLiteral("°")).remove(QStringLiteral("deg"));
    bool ok = false;
    const double deg = t.trimmed().toDouble(&ok);
    if (!ok || deg <= 0 || deg >= 180) {
      emit message(tr("Enter an angle between 0° and 180°."));
      return;
    }
    value = deg * kPi / 180.0;
  } else {
    const model::LengthParse p = model::length_parse(text.toStdString(), unit());
    if (!p.ok) {
      emit message(qs(p.error));
      return;
    }
    if (p.mm <= 0) {
      emit message(tr("A dimension must be larger than zero."));
      return;
    }
    value = p.mm;
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
    return addConstraint(d.kind, a, b, 0, value);
  });
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
    default: picks_.clear(); break;
  }
  drawPreview();
}

// ---- Display ----------------------------------------------------------------------

QString SketchEditor::dimensionText(const model::SketchConstraintInfo& c) {
  if (c.kind == K::Angle) return QString::number(c.value * 180.0 / kPi, 'g', 4) + QStringLiteral("°");
  const QString len = qs(model::length_format(c.value, unit()));
  if (c.kind == K::Diameter) return QStringLiteral("Ø") + len;
  if (c.kind == K::Radius) return QStringLiteral("R") + len;
  return len;
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
  for (const auto& p : profiles_) {
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
  points(pFree, pal.highlight, 2.2);
  points(pFixed, pal.text, 2.2);
  points(pSelected, pal.accent, 3.0);
  // The sketch origin.
  points({{0.0, 0.0}}, pal.accent, 1.6);

  // Dimensions and constraint glyphs.
  labels_.clear();
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
  for (const auto& c : constraints_) {
    const bool sel = selectedCon_.contains(c.id);
    if (c.is_dimension) {
      const QColor color = sel ? pal.accent : c.conflicting ? pal.danger : pal.text;
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
    if (g.isEmpty()) continue;
    const QColor color = sel ? pal.accent : c.conflicting ? pal.danger : pal.text;
    std::vector<std::uint64_t> on;
    if (c.kind == K::Symmetric) on = {c.c};
    else if (c.kind == K::Midpoint) on = {c.a};
    else if (c.kind == K::HorizontalPoints || c.kind == K::VerticalPoints) on = {c.a};
    else on = {c.a, c.b};
    for (std::uint64_t id : on) {
      if (!id) continue;
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
  const bool showHover = !dragging_ && mode_ != Mode::Line && mode_ != Mode::Rectangle && mode_ != Mode::Circle &&
                         mode_ != Mode::Arc && mode_ != Mode::Point;
  auto highlight = [&](const Hit& h, const QColor& color) {
    if (h.kind == Hit::Point || h.kind == Hit::Origin) {
      o.points.push_back({geom::makeVertices(gp, {{h.u, h.v}}), color, 3.2});
    } else if (h.kind == Hit::Curve) {
      if (const auto* e = entity(h.id)) o.curves.push_back({geom::makeCurves(gp, {segOf(*e)}), color, 3.5, false});
    }
  };
  if (showHover) highlight(hover_, pal.accent.lighter(115));
  for (const Hit& p : picks_) highlight(p, pal.accent);
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

  const bool drawing = mode_ == Mode::Line || mode_ == Mode::Rectangle || mode_ == Mode::Circle ||
                       mode_ == Mode::Arc || mode_ == Mode::Point;
  if (drawing) {
    // Snap marker.
    if (h.kind == Hit::Point || h.kind == Hit::Origin) {
      o.points.push_back({geom::makeVertices(gp, {{cu, cv}}), pal.accent, 3.6});
    } else if (h.kind == Hit::Curve) {
      o.points.push_back({geom::makeVertices(gp, {{cu, cv}}), pal.accent, 2.4});
    }
  }
  const auto fmt = [this](double mm) { return qs(model::length_format(mm, unit())); };

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
        if (h.kind != Hit::Point && h.kind != Hit::Origin) {
          const double ang = std::abs(std::atan2(cv - a.v, cu - a.u)) * 180.0 / kPi;
          if (std::min(ang, 180.0 - ang) < kInferDeg) cv = a.v, infer = "H";
          else if (std::abs(ang - 90.0) < kInferDeg) cu = a.u, infer = "V";
        }
        line(a.u, a.v, cu, cv);
        const double len = dist(a.u, a.v, cu, cv);
        QString text = fmt(len);
        if (!infer.isEmpty()) text += "  " + infer;
        o.labels.push_back({world((a.u + cu) / 2, (a.v + cv) / 2 + 16 * upp), text, pal.accent, 14.0, pal.panel});
        break;
      }
      case Mode::Rectangle:
        line(a.u, a.v, cu, a.v), line(cu, a.v, cu, cv), line(cu, cv, a.u, cv), line(a.u, cv, a.u, a.v);
        o.labels.push_back({world((a.u + cu) / 2, std::min(a.v, cv) - 14 * upp), fmt(std::abs(cu - a.u)),
                            pal.accent, 14.0, pal.panel});
        o.labels.push_back({world(std::max(a.u, cu) + 24 * upp, (a.v + cv) / 2), fmt(std::abs(cv - a.v)),
                            pal.accent, 14.0, pal.panel});
        break;
      case Mode::Circle: {
        const double r = dist(a.u, a.v, cu, cv);
        if (r > 1e-9) {
          geom::Seg2 s;
          s.kind = geom::Seg2::Kind::Circle;
          s.cx = a.u, s.cy = a.v, s.radius = r;
          segs.push_back(s);
          line(a.u, a.v, cu, cv);
          o.labels.push_back({world((a.u + cu) / 2, (a.v + cv) / 2 + 14 * upp), QStringLiteral("Ø") + fmt(2 * r),
                              pal.accent, 14.0, pal.panel});
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
      default: break;
    }
    if (!segs.empty()) o.curves.push_back({geom::makeCurves(gp, segs), pal.highlight, 1.8, false});
  }
  view_->setOverlay("sketch-preview", o);
}
