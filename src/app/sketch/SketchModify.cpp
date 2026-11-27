// The SketchEditor's modify tools: trim, extend, break, fillet, chamfer,
// offset, mirror, move/copy, rotate, scale, patterns, project and Auto
// Constrain. The geometry work happens in the model (sketch/ops.rs); this
// file is the picking and the prompts.

#include "SketchEditor.h"

#include "Settings.h"
#include "Icons.h"
#include "Theme.h"
#include "widgets/CommandDialog.h"
#include "widgets/LengthField.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>

#include <QCursor>
#include <QDoubleSpinBox>
#include <QMenu>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

using K = model::ConstraintKind;
using EK = model::SketchEntityKind;

constexpr double kPi = 3.14159265358979323846;
double pickPx() { return prefs::get(prefs::kPickRadius).toDouble(); }

double dist(double x0, double y0, double x1, double y1) { return std::hypot(x1 - x0, y1 - y0); }

double segDist(double px, double py, double x0, double y0, double x1, double y1) {
  const double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
  const double t = len2 > 0 ? std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.0, 1.0) : 0;
  return dist(px, py, x0 + t * dx, y0 + t * dy);
}

QString qs(const rust::String& s) { return QString::fromUtf8(s.data(), int(s.size())); }

rust::Vec<std::uint64_t> toVec(const std::vector<std::uint64_t>& ids) {
  rust::Vec<std::uint64_t> v;
  for (std::uint64_t id : ids) v.push_back(id);
  return v;
}

// End points of a curve (none for closed curves).
std::vector<std::uint64_t> endsOf(const model::SketchEntityInfo& e) {
  switch (e.kind) {
    case EK::Line: return {e.p0, e.p1};
    case EK::Arc: return {e.p1, e.p2};
    case EK::EllipticalArc: return {e.p2, e.p3};
    case EK::Spline: return e.closed ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{e.p0, e.p1};
    default: return {};
  }
}

// Distance from (u, v) to a curve.
double curveDistance(const model::SketchEntityInfo& e, double u, double v) {
  switch (e.kind) {
    case EK::Line: return segDist(u, v, e.x0, e.y0, e.x1, e.y1);
    case EK::Circle:
    case EK::Arc: return std::abs(dist(u, v, e.x0, e.y0) - e.radius);
    default: {
      double best = 1e300;
      for (std::size_t i = 0; i + 3 < e.samples.size(); i += 2)
        best = std::min(best, segDist(u, v, e.samples[i], e.samples[i + 1], e.samples[i + 2], e.samples[i + 3]));
      return best;
    }
  }
}

// The same curve moved by (dx, dy), for previews.
geom::Seg2 shifted(geom::Seg2 s, double dx, double dy) {
  s.x0 += dx, s.y0 += dy, s.x1 += dx, s.y1 += dy, s.cx += dx, s.cy += dy;
  for (std::size_t i = 0; i + 1 < s.poles.size(); i += 2) s.poles[i] += dx, s.poles[i + 1] += dy;
  return s;
}

// The same curve mirrored in the line through (ax, ay) along unit (ux, uy).
bool mirrored(geom::Seg2& s, double ax, double ay, double ux, double uy) {
  auto ref = [&](double& x, double& y) {
    const double px = x - ax, py = y - ay, d = px * ux + py * uy;
    x = ax + 2 * d * ux - px, y = ay + 2 * d * uy - py;
  };
  auto refVec = [&](double& x, double& y) {
    const double d = x * ux + y * uy;
    const double nx = 2 * d * ux - x, ny = 2 * d * uy - y;
    x = nx, y = ny;
  };
  using SK = geom::Seg2::Kind;
  switch (s.kind) {
    case SK::Line: ref(s.x0, s.y0), ref(s.x1, s.y1); return true;
    case SK::Circle: ref(s.cx, s.cy); return true;
    case SK::Arc:
      ref(s.cx, s.cy), ref(s.x0, s.y0), ref(s.x1, s.y1);
      std::swap(s.x0, s.x1), std::swap(s.y0, s.y1);  // a mirror turns the direction round
      return true;
    case SK::Ellipse: ref(s.cx, s.cy), refVec(s.mx, s.my); return true;
    case SK::Spline:
      for (std::size_t i = 0; i + 1 < s.poles.size(); i += 2) ref(s.poles[i], s.poles[i + 1]);
      return true;
    default: return false;
  }
}

}  // namespace

// ---- Helpers ------------------------------------------------------------------

// Curves joined end to end with `curve` (shared points or coincident ones).
std::vector<std::uint64_t> SketchEditor::chainOf(std::uint64_t curve) const {
  const auto* start = entity(curve);
  if (!start) return {};
  // Points joined by coincident constraints count as one.
  std::map<std::uint64_t, std::uint64_t> parent;
  auto find = [&](std::uint64_t p) {
    while (parent.count(p) && parent[p] != p) p = parent[p];
    return p;
  };
  for (const auto& c : constraints_)
    if (c.kind == K::Coincident) parent[find(c.a)] = find(c.b);

  std::vector<std::uint64_t> chain = {curve};
  std::set<std::uint64_t> seen = {curve};
  for (std::size_t i = 0; i < chain.size(); ++i) {
    const auto* e = entity(chain[i]);
    if (!e) continue;
    std::set<std::uint64_t> ends;
    for (std::uint64_t p : endsOf(*e)) ends.insert(find(p));
    for (const auto& other : entities_) {
      if (other.kind == EK::Point || seen.count(other.id) || other.construction != start->construction) continue;
      for (std::uint64_t p : endsOf(other)) {
        if (!ends.count(find(p))) continue;
        chain.push_back(other.id);
        seen.insert(other.id);
        break;
      }
    }
  }
  return chain;
}

std::vector<std::uint64_t> SketchEditor::cornerLines(std::uint64_t point) const {
  std::vector<std::uint64_t> lines;
  for (const auto& e : entities_)
    if (e.kind == EK::Line && (e.p0 == point || e.p1 == point)) lines.push_back(e.id);
  return lines;
}

// ---- Picking ------------------------------------------------------------------

void SketchEditor::clickModify(const Hit& h, const QPointF& pos, Qt::KeyboardModifiers mods) {
  // First step of the curve tools: pick (or un-pick) curves.
  if (picksCurves(mode_) && opStage_ == 0) {
    if (h.kind != Hit::Curve) return;
    if (mode_ == Mode::Offset) {
      opCurves_ = chainOf(h.id);
      opStage_ = 1;
      emit message(tr("Offset: %n curve(s). Click the side to offset towards.", nullptr, int(opCurves_.size())));
      drawHover();
      return;
    }
    const auto it = std::find(opCurves_.begin(), opCurves_.end(), h.id);
    if (it != opCurves_.end()) opCurves_.erase(it);
    else opCurves_.push_back(h.id);
    emit message(tr("%n curve(s) picked. Enter to go on, Esc to start over.", nullptr, int(opCurves_.size())));
    drawHover();
    return;
  }

  const bool copy = mods & Qt::ControlModifier;
  switch (mode_) {
    case Mode::Trim:
    case Mode::Extend:
    case Mode::Break: {
      if (h.kind != Hit::Curve) return;
      const Mode m = mode_;
      edit([&] {
        if (m == Mode::Trim) model::sketch_trim(tl(), sketch_, h.id, h.u, h.v);
        else if (m == Mode::Extend) model::sketch_extend(tl(), sketch_, h.id, h.u, h.v);
        else model::sketch_break(tl(), sketch_, h.id, h.u, h.v);
        return true;
      });
      hover_ = Hit{};
      drawHover();
      return;
    }
    case Mode::Fillet:
    case Mode::Chamfer: {
      if (h.kind != Hit::Point) {
        emit message(tr("Click the corner point where two lines meet."));
        return;
      }
      const std::vector<std::uint64_t> lines = cornerLines(h.id);
      if (lines.size() != 2) {
        emit message(tr("Pick a corner where exactly two lines meet."));
        return;
      }
      double shortest = 1e300;
      for (std::uint64_t l : lines)
        if (const auto* e = entity(l)) shortest = std::min(shortest, dist(e->x0, e->y0, e->x1, e->y1));
      const bool fillet = mode_ == Mode::Fillet;
      const std::uint64_t corner = h.id;
      picks_ = {h};
      drawHover();
      askValue(fillet ? tr("Fillet radius, Enter to apply.") : tr("Chamfer distance, Enter to apply."),
               ValueKind::Length, shortest / 5, pos, [this, corner, fillet](double v) {
                 if (fillet) model::sketch_fillet(tl(), sketch_, corner, v);
                 else model::sketch_chamfer(tl(), sketch_, corner, v);
                 return true;
               });
      return;
    }
    case Mode::Offset: {
      double nearest = 1e300;
      for (std::uint64_t id : opCurves_)
        if (const auto* e = entity(id)) nearest = std::min(nearest, curveDistance(*e, h.u, h.v));
      const std::vector<std::uint64_t> curves = opCurves_;
      const double sx = h.u, sy = h.v;
      askValue(tr("Offset distance, Enter to apply."), ValueKind::Length, nearest > 0 && nearest < 1e299 ? nearest : 5.0,
               pos, [this, curves, sx, sy](double d) {
                 model::sketch_offset(tl(), sketch_, toVec(curves), d, sx, sy);
                 return true;
               });
      return;
    }
    case Mode::Mirror: {
      const auto* e = entity(h.id);
      if (h.kind != Hit::Curve || !e || e->kind != EK::Line) {
        emit message(tr("The mirror line must be a line."));
        return;
      }
      std::vector<std::uint64_t> curves = opCurves_;
      curves.erase(std::remove(curves.begin(), curves.end(), h.id), curves.end());
      const std::uint64_t group = editMirror_;
      editMirror_ = 0;
      edit([&] {
        if (group) {
          model::CopyGroupInfo g = model::sketch_group_of(tl(), sketch_, curves.front());
          g.id = group;
          g.kind = 2;
          g.axis = h.id;
          model::sketch_edit_group(tl(), sketch_, g);
        } else {
          model::sketch_mirror(tl(), sketch_, toVec(curves), h.id);
        }
        return true;
      });
      resetDraft();
      hintForMode();
      return;
    }
    case Mode::Move: {
      if (opStage_ == 1) {
        opAnchor_ = h;
        opStage_ = 2;
        emit message(tr("Click where the base point goes (hold Ctrl to copy)."));
        return;
      }
      const std::vector<std::uint64_t> curves = opCurves_;
      const double dx = h.u - opAnchor_.u, dy = h.v - opAnchor_.v;
      edit([&] {
        model::sketch_transform(tl(), sketch_, toVec(curves), 0, dx, dy, 0, copy);
        return true;
      });
      resetDraft();
      hintForMode();
      return;
    }
    case Mode::Rotate:
    case Mode::Scale: {
      const std::vector<std::uint64_t> curves = opCurves_;
      const double cx = h.u, cy = h.v;
      const bool rotate = mode_ == Mode::Rotate;
      opAnchor_ = h;
      opStage_ = 2;
      askValue(rotate ? tr("Angle in degrees (negative turns clockwise), Enter to apply.")
                      : tr("Scale factor (2 = twice the size), Enter to apply."),
               rotate ? ValueKind::Angle : ValueKind::Number, rotate ? kPi / 2 : 2.0, pos,
               [this, curves, cx, cy, rotate, copy](double v) {
                 model::sketch_transform(tl(), sketch_, toVec(curves), rotate ? 1 : 2, cx, cy, v, copy);
                 return true;
               });
      return;
    }
    case Mode::PatternCirc:
      opAnchor_ = h;
      opStage_ = 2;
      openPatternDialog(true);
      return;
    case Mode::Project: {
      const int edge = edgeAt(pos);
      if (edge < 0) {
        emit message(tr("Click an edge of the body."));
        return;
      }
      projectEdge(edge);
      return;
    }
    default: return;
  }
}

void SketchEditor::finishPicking() {
  if (opCurves_.empty()) {
    opStage_ = 0;
    emit message(tr("Pick the curves first (or select them, then choose the tool)."));
    return;
  }
  opStage_ = 1;
  switch (mode_) {
    case Mode::Offset: emit message(tr("Click the side to offset towards.")); break;
    case Mode::Mirror: emit message(tr("Click the mirror line.")); break;
    case Mode::Move: emit message(tr("Click a base point.")); break;
    case Mode::Rotate: emit message(tr("Click the centre of rotation (hold Ctrl to copy).")); break;
    case Mode::Scale: emit message(tr("Click the point to scale about.")); break;
    case Mode::PatternRect: openPatternDialog(false); break;
    case Mode::PatternCirc: emit message(tr("Click the centre of the pattern.")); break;
    default: break;
  }
  drawHover();
}

void SketchEditor::previewModify(Overlay& o, double cu, double cv) {
  const theme::Palette& pal = theme::current();
  const geom::Plane gp = [&] {
    geom::Plane g;
    for (int i = 0; i < 3; ++i) {
      g.origin[i] = plane_.origin[i], g.xAxis[i] = plane_.x_axis[i], g.yAxis[i] = plane_.y_axis[i];
      g.normal[i] = plane_.normal[i];
    }
    return g;
  }();
  const bool anchor = (mode_ == Mode::Move || mode_ == Mode::Rotate || mode_ == Mode::Scale ||
                       mode_ == Mode::PatternCirc) && opStage_ >= 1;
  if (anchor) o.points.push_back({geom::makeVertices(gp, {{cu, cv}}), pal.accent, 3.0});

  auto segOfId = [this](std::uint64_t id, geom::Seg2& out) {
    const auto* e = entity(id);
    if (!e) return false;
    // segOf lives with the main editor; rebuild what previews need.
    geom::Seg2 s;
    switch (e->kind) {
      case EK::Line: s.x0 = e->x0, s.y0 = e->y0, s.x1 = e->x1, s.y1 = e->y1; break;
      case EK::Circle: s.kind = geom::Seg2::Kind::Circle, s.cx = e->x0, s.cy = e->y0, s.radius = e->radius; break;
      case EK::Arc:
        s.kind = geom::Seg2::Kind::Arc, s.cx = e->x0, s.cy = e->y0, s.radius = e->radius;
        s.x0 = e->x1, s.y0 = e->y1, s.x1 = e->x2, s.y1 = e->y2, s.ccw = true;
        break;
      case EK::Ellipse:
        s.kind = geom::Seg2::Kind::Ellipse, s.cx = e->x0, s.cy = e->y0, s.mx = e->x1 - e->x0, s.my = e->y1 - e->y0;
        s.minor = e->minor;
        break;
      case EK::Spline:
        s.kind = geom::Seg2::Kind::Spline, s.degree = int(e->degree);
        s.poles.assign(e->poles.begin(), e->poles.end());
        s.knots.assign(e->knots.begin(), e->knots.end());
        s.weights.assign(e->weights.begin(), e->weights.end());
        break;
      default: return false;  // elliptical arcs: no preview
    }
    out = s;
    return true;
  };

  std::vector<geom::Seg2> ghost;
  if (mode_ == Mode::Move && opStage_ == 2) {
    for (std::uint64_t id : opCurves_)
      if (geom::Seg2 s; segOfId(id, s)) ghost.push_back(shifted(s, cu - opAnchor_.u, cv - opAnchor_.v));
  } else if (mode_ == Mode::Mirror && opStage_ == 1 && hover_.kind == Hit::Curve) {
    if (const auto* axis = entity(hover_.id); axis && axis->kind == EK::Line) {
      const double len = std::max(dist(axis->x0, axis->y0, axis->x1, axis->y1), 1e-12);
      const double ux = (axis->x1 - axis->x0) / len, uy = (axis->y1 - axis->y0) / len;
      for (std::uint64_t id : opCurves_) {
        geom::Seg2 s;
        if (id != axis->id && segOfId(id, s) && mirrored(s, axis->x0, axis->y0, ux, uy)) ghost.push_back(s);
      }
    }
  }
  if (!ghost.empty()) o.curves.push_back({geom::makeCurves(gp, ghost), pal.highlight, 1.6, true});
}

// ---- Patterns -------------------------------------------------------------------

void SketchEditor::openPatternDialog(bool circular, std::uint64_t editGroup) {
  model::CopyGroupInfo group{};
  if (editGroup) {
    // Editing: the dialog starts from the pattern's values.
    for (const auto& e : entities_) {
      const model::CopyGroupInfo g = model::sketch_group_of(tl(), sketch_, e.id);
      if (g.id == editGroup) {
        group = g;
        break;
      }
    }
    if (!group.id) return;
    opCurves_.assign(group.sources.begin(), group.sources.end());
    circular = group.kind == 1;
  }
  const std::vector<std::uint64_t> curves = opCurves_;
  // Default spacing: a little more than the picked curves are wide.
  double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
  for (std::uint64_t id : curves) {
    const auto* e = entity(id);
    if (!e) continue;
    auto grow = [&](double x, double y) { x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y); };
    if (e->kind == EK::Circle || e->kind == EK::Arc) {
      grow(e->x0 - e->radius, e->y0 - e->radius), grow(e->x0 + e->radius, e->y0 + e->radius);
    } else if (!e->samples.empty()) {
      for (std::size_t i = 0; i + 1 < e->samples.size(); i += 2) grow(e->samples[i], e->samples[i + 1]);
    } else {
      grow(e->x0, e->y0), grow(e->x1, e->y1);
    }
  }
  const double w = x1 > x0 ? x1 - x0 : 10.0, hgt = y1 > y0 ? y1 - y0 : 10.0;

  const QString title = circular ? tr("Circular Pattern") : tr("Rectangular Pattern");
  auto* d = new CommandDialog(editGroup ? tr("Edit %1").arg(title) : title, view_);
  d->addInfo(tr("Objects"), tr("%n curve(s)", nullptr, int(curves.size())));
  if (circular) {
    QDoubleSpinBox* count = d->addNumber(tr("Quantity"), editGroup ? double(group.n1) : 6.0, QString(), 2, 360, 0);
    QDoubleSpinBox* angle =
        d->addNumber(tr("Angle"), editGroup ? group.c * 180.0 / kPi : 360.0, QStringLiteral("°"), 1, 360, 1);
    const double cx = editGroup ? group.a : opAnchor_.u, cy = editGroup ? group.b : opAnchor_.v;
    connect(d, &CommandDialog::accepted, this, [=, this] {
      edit([&] {
        if (editGroup) {
          model::CopyGroupInfo g = group;
          g.n1 = std::size_t(count->value());
          g.c = angle->value() * kPi / 180.0;
          model::sketch_edit_group(tl(), sketch_, g);
        } else {
          model::sketch_pattern_circ(tl(), sketch_, toVec(curves), cx, cy, std::size_t(count->value()),
                                     angle->value() * kPi / 180.0);
        }
        return true;
      });
      resetDraft();
      hover_ = Hit{};
      drawPreview();
      hintForMode();
      emit d->rejected();  // closes it
    });
  } else {
    QDoubleSpinBox* cols = d->addNumber(tr("Columns"), editGroup ? double(group.n1) : 3.0, QString(), 1, 200, 0);
    QDoubleSpinBox* rows = d->addNumber(tr("Rows"), editGroup ? double(group.n2) : 1.0, QString(), 1, 200, 0);
    LengthField* sx = d->addLength(tr("Column spacing"), editGroup ? group.c : w * 1.25, unit(), 1e-6);
    LengthField* sy = d->addLength(tr("Row spacing"), editGroup ? group.d : hgt * 1.25, unit(), 1e-6);
    QDoubleSpinBox* dir = d->addNumber(tr("Direction"), editGroup ? std::atan2(group.b, group.a) * 180.0 / kPi : 0.0,
                                       QStringLiteral("°"), -360, 360, 1);
    connect(d, &CommandDialog::accepted, this, [=, this] {
      const double a = dir->value() * kPi / 180.0;
      edit([&] {
        if (editGroup) {
          model::CopyGroupInfo g = group;
          g.a = std::cos(a), g.b = std::sin(a);
          g.n1 = std::size_t(cols->value()), g.n2 = std::size_t(rows->value());
          g.c = sx->valueMm(), g.d = sy->valueMm();
          model::sketch_edit_group(tl(), sketch_, g);
        } else {
          model::sketch_pattern_rect(tl(), sketch_, toVec(curves), std::cos(a), std::sin(a),
                                     std::size_t(cols->value()), std::size_t(rows->value()), sx->valueMm(),
                                     sy->valueMm());
        }
        return true;
      });
      resetDraft();
      hover_ = Hit{};
      drawPreview();
      hintForMode();
      emit d->rejected();
    });
  }
  connect(d, &CommandDialog::rejected, this, [this] {
    if (picksCurves(mode_) && opStage_ > 0) resetDraft();
    drawHover();
  });
  emit commandDialog(d);
  emit message(tr("Set the pattern, then OK."));
}

// ---- Project ----------------------------------------------------------------------

int SketchEditor::edgeAt(const QPointF& pos) const {
  int best = -1;
  double bestPx = pickPx();
  for (int i = 0; i < int(bodyEdges_.size()); ++i) {
    BRepAdaptor_Curve c(TopoDS::Edge(bodyEdges_[i]));
    const double t0 = c.FirstParameter(), t1 = c.LastParameter();
    QPointF prev = view_->project(c.Value(t0));
    for (int k = 1; k <= 24; ++k) {
      const QPointF p = view_->project(c.Value(t0 + (t1 - t0) * k / 24.0));
      const double d = segDist(pos.x(), pos.y(), prev.x(), prev.y(), p.x(), p.y());
      if (d < bestPx) bestPx = d, best = i;
      prev = p;
    }
  }
  return best;
}

void SketchEditor::projectEdge(int index) {
  const TopoDS_Edge edge = TopoDS::Edge(bodyEdges_[index]);
  BRepAdaptor_Curve c(edge);
  const gp_Pnt o(plane_.origin[0], plane_.origin[1], plane_.origin[2]);
  const gp_Vec xa(plane_.x_axis[0], plane_.x_axis[1], plane_.x_axis[2]);
  const gp_Vec ya(plane_.y_axis[0], plane_.y_axis[1], plane_.y_axis[2]);
  const gp_Vec na(plane_.normal[0], plane_.normal[1], plane_.normal[2]);
  auto uv = [&](const gp_Pnt& p, double& u, double& v) {
    const gp_Vec r(o, p);
    u = r.Dot(xa), v = r.Dot(ya);
  };
  const double t0 = c.FirstParameter(), t1 = c.LastParameter();
  double su, sv, eu, ev, mu, mv;
  uv(c.Value(t0), su, sv);
  uv(c.Value(t1), eu, ev);
  uv(c.Value((t0 + t1) / 2), mu, mv);
  const double tiny = 1e-6;

  edit([&] {
    auto fixPoint = [&](std::uint64_t p) { addConstraint(K::Fix, p, 0, 0, 0, true); };
    auto point = [&](double u, double v) { return model::sketch_add_point(tl(), sketch_, u, v); };
    if (c.GetType() == GeomAbs_Line) {
      if (dist(su, sv, eu, ev) < tiny) {
        fixPoint(point(su, sv));  // seen end-on: a point
        return true;
      }
      const std::uint64_t a = point(su, sv), b = point(eu, ev);
      model::sketch_add_line(tl(), sketch_, a, b);
      fixPoint(a), fixPoint(b);
      return true;
    }
    if (c.GetType() == GeomAbs_Circle && std::abs(gp_Vec(c.Circle().Axis().Direction()).Dot(na)) > 1 - 1e-9) {
      double cu, cv;
      uv(c.Circle().Location(), cu, cv);
      const double r = c.Circle().Radius();
      const std::uint64_t centre = point(cu, cv);
      fixPoint(centre);
      if (BRep_Tool::IsClosed(edge) || dist(su, sv, eu, ev) < tiny) {
        const std::uint64_t circle = model::sketch_add_circle(tl(), sketch_, centre, r);
        addConstraint(K::Diameter, circle, 0, 0, 2 * r, true);
        return true;
      }
      // Arcs run counter-clockwise in the sketch: start where that passes the middle.
      auto ang = [&](double u, double v) { return std::atan2(v - cv, u - cu); };
      auto norm = [](double a) { return a - 2 * kPi * std::floor(a / (2 * kPi)); };
      const bool ccw = norm(ang(mu, mv) - ang(su, sv)) < norm(ang(eu, ev) - ang(su, sv));
      const std::uint64_t a = point(su, sv), b = point(eu, ev);
      model::sketch_add_arc(tl(), sketch_, centre, ccw ? a : b, ccw ? b : a);
      fixPoint(a), fixPoint(b);
      return true;
    }
    // Anything else: a spline through points along it, held in place.
    rust::Vec<std::uint64_t> pts;
    const bool closed = BRep_Tool::IsClosed(edge);
    const int n = 9;
    for (int k = 0; k < (closed ? n - 1 : n); ++k) {
      double u, v;
      uv(c.Value(t0 + (t1 - t0) * k / (n - 1)), u, v);
      const std::uint64_t p = point(u, v);
      fixPoint(p);
      pts.push_back(p);
    }
    model::sketch_add_spline(tl(), sketch_, std::move(pts), closed);
    return true;
  });
  emit message(tr("Projected. The copy is fixed; it won't follow later changes to the body yet."));
}

// ---- Auto Constrain --------------------------------------------------------------

void SketchEditor::autoConstrain() {
  std::size_t added = 0;
  edit([&] {
    added = model::sketch_auto_constrain(tl(), sketch_);
    return added > 0;
  });
  emit message(added ? tr("Auto Constrain added %n constraint(s).", nullptr, int(added))
                     : tr("Auto Constrain found nothing to add."));
}

// ---- Curve menu, clipboard ---------------------------------------------------------

void SketchEditor::showCurveMenu(std::uint64_t curve, const QPointF& pos) {
  if (!selected_.contains(curve)) {
    selected_ = {curve};
    selectedCon_.clear();
    redraw();
  }
  model::CopyGroupInfo group{};
  try {
    group = model::sketch_group_of(tl(), sketch_, curve);
  } catch (const std::exception&) {
  }
  QMenu menu(view_);
  QAction* editGroup = nullptr;
  QAction* removeCopies = nullptr;
  QAction* unlink = nullptr;
  if (group.id) {
    const QString what = group.kind == 0 ? tr("Rectangular Pattern")
                         : group.kind == 1 ? tr("Circular Pattern")
                                           : tr("Mirror");
    editGroup = menu.addAction(icons::get(group.kind == 0 ? "patternr" : group.kind == 1 ? "patternc" : "mirror", 16),
                               tr("Edit %1…").arg(what));
    removeCopies = menu.addAction(tr("Remove %1 Copies").arg(what));
    unlink = menu.addAction(tr("Keep Copies, Forget %1").arg(what));
    menu.addSeparator();
  }
  QAction* copy = menu.addAction(tr("Copy\tCtrl+C"));
  QAction* cut = menu.addAction(tr("Cut\tCtrl+X"));
  QAction* pasteAct = menu.addAction(tr("Paste\tCtrl+V"));
  pasteAct->setEnabled(!model::sketch_clipboard_centre().empty());
  menu.addSeparator();
  QAction* construction = menu.addAction(icons::get("construction", 16), tr("Normal / Construction\tX"));
  QAction* remove = menu.addAction(icons::get("close", 16), tr("Delete\tDel"));
  QAction* chosen = menu.exec(view_->mapToGlobal(pos.toPoint()));
  if (!chosen) return;
  if (chosen == editGroup) {
    selected_.clear();  // else the tool would take the selection as a new pick
    if (group.kind == 2) {
      setMode(Mode::Mirror);
      opCurves_.assign(group.sources.begin(), group.sources.end());
      opStage_ = 1;
      editMirror_ = group.id;
      drawHover();
      emit message(tr("Click the new mirror line."));
    } else {
      setMode(group.kind == 1 ? Mode::PatternCirc : Mode::PatternRect);
      openPatternDialog(group.kind == 1, group.id);
    }
  } else if (chosen == removeCopies || chosen == unlink) {
    const bool del = chosen == removeCopies;
    edit([&] {
      model::sketch_delete_group(tl(), sketch_, group.id, del);
      return true;
    });
  } else if (chosen == copy) {
    copySelection();
  } else if (chosen == cut) {
    cutSelection();
  } else if (chosen == pasteAct) {
    paste();
  } else if (chosen == construction) {
    toggleConstruction();
  } else if (chosen == remove) {
    deleteSelection();
  }
}

void SketchEditor::copySelection() {
  std::vector<std::uint64_t> curves;
  for (std::uint64_t id : selected_)
    if (isCurve(id)) curves.push_back(id);
  if (curves.empty()) {
    emit message(tr("Select curves to copy first."));
    return;
  }
  try {
    const std::size_t n = model::sketch_copy(tl(), sketch_, toVec(curves));
    emit message(tr("Copied %n curve(s). Ctrl+V pastes them at the cursor, in this or another sketch.", nullptr,
                    int(n)));
  } catch (const std::exception& ex) {
    emit message(QString::fromUtf8(ex.what()));
  }
}

void SketchEditor::cutSelection() {
  copySelection();
  if (!model::sketch_clipboard_centre().empty()) deleteSelection();
}

void SketchEditor::paste() {
  const rust::Vec<double> centre = model::sketch_clipboard_centre();
  if (centre.size() < 2) {
    emit message(tr("Nothing has been copied yet."));
    return;
  }
  // Centred on the cursor when it is over the view; else a little offset.
  double u = centre[0] + 5, v = centre[1] - 5;
  const QPointF at = view_->mapFromGlobal(QCursor::pos());
  if (view_->rect().contains(at.toPoint())) toPlane(at, u, v);
  std::vector<std::uint64_t> made;
  setMode(Mode::Select);
  edit([&] {
    for (std::uint64_t id : model::sketch_paste(tl(), sketch_, u - centre[0], v - centre[1])) made.push_back(id);
    return !made.empty();
  });
  selected_.clear();
  for (std::uint64_t id : made) selected_.insert(id);
  redraw();
  emit message(tr("Pasted %n curve(s). Drag them, or use Move to place them exactly.", nullptr, int(made.size())));
}
