// The SketchEditor's newer create tools: 3-point and centre rectangles,
// 2- and 3-point circles, centre and tangent arcs, polygons, ellipses, slots
// and fit-point splines.

#include "SketchEditor.h"

#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

using K = model::ConstraintKind;
using EK = model::SketchEntityKind;
using M = SketchEditor::Mode;

constexpr double kPi = 3.14159265358979323846;
double pickPx() { return prefs::get(prefs::kPickRadius).toDouble(); }

double dist(double x0, double y0, double x1, double y1) { return std::hypot(x1 - x0, y1 - y0); }

struct P2 {
  double x = 0, y = 0;
};

// Centre of the circle through three points; nullopt when they are in a line.
std::optional<P2> circumcentre(P2 a, P2 b, P2 c) {
  const double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
  if (std::abs(d) < 1e-12) return std::nullopt;
  const double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
  return P2{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
            (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
}

geom::Seg2 lineSeg(double x0, double y0, double x1, double y1) {
  geom::Seg2 s;
  s.x0 = x0, s.y0 = y0, s.x1 = x1, s.y1 = y1;
  return s;
}

geom::Seg2 circleSeg(double cx, double cy, double r) {
  geom::Seg2 s;
  s.kind = geom::Seg2::Kind::Circle;
  s.cx = cx, s.cy = cy, s.radius = r;
  return s;
}

// Counter-clockwise arc about (cx, cy) from (x0, y0) to (x1, y1).
geom::Seg2 arcSeg(double cx, double cy, double x0, double y0, double x1, double y1) {
  geom::Seg2 s;
  s.kind = geom::Seg2::Kind::Arc;
  s.cx = cx, s.cy = cy, s.x0 = x0, s.y0 = y0, s.x1 = x1, s.y1 = y1;
  s.radius = dist(cx, cy, x0, y0);
  s.ccw = true;
  return s;
}

// Number of clicks each tool takes.
int clicksFor(M m) {
  switch (m) {
    case M::RectangleCentre: case M::Circle2: case M::TangentArc: case M::Polygon: case M::PolygonCirc:
    case M::MidLine:
      return 2;
    case M::Rectangle3: case M::Circle3: case M::CentreArc: case M::Ellipse: case M::Slot: case M::SlotOverall:
    case M::SlotCentre: case M::PolygonEdge:
      return 3;
    default: return 0;
  }
}

// The four corners of a 3-point rectangle: one side a→b, height towards c.
void rect3(P2 a, P2 b, P2 c, P2 out[4]) {
  const double dx = b.x - a.x, dy = b.y - a.y, len = std::max(std::hypot(dx, dy), 1e-12);
  const double nx = -dy / len, ny = dx / len;
  const double h = (c.x - a.x) * nx + (c.y - a.y) * ny;
  out[0] = a, out[1] = b, out[2] = {b.x + nx * h, b.y + ny * h}, out[3] = {a.x + nx * h, a.y + ny * h};
}

// The arc of a tangent arc: from p leaving along unit t, ending at q.
// Returns false when q lies straight ahead (no arc).
bool tangentArc(P2 p, P2 t, P2 q, P2& centre, bool& ccw) {
  const double nx = -t.y, ny = t.x, wx = q.x - p.x, wy = q.y - p.y;
  const double denom = 2 * (nx * wx + ny * wy);
  if (std::abs(denom) < 1e-9 * std::max(1.0, wx * wx + wy * wy)) return false;
  const double r = (wx * wx + wy * wy) / denom;  // signed: + = centre on the left
  centre = {p.x + nx * r, p.y + ny * r};
  ccw = r > 0;
  return true;
}

// The outline of a slot with arc centres a, b and radius r.
void slotOutline(P2 a, P2 b, double r, std::vector<geom::Seg2>& segs) {
  const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
  if (len < 1e-9 || r < 1e-9) return;
  const double nx = -dy / len, ny = dx / len;
  segs.push_back(lineSeg(a.x + nx * r, a.y + ny * r, b.x + nx * r, b.y + ny * r));
  segs.push_back(lineSeg(b.x - nx * r, b.y - ny * r, a.x - nx * r, a.y - ny * r));
  segs.push_back(arcSeg(b.x, b.y, b.x - nx * r, b.y - ny * r, b.x + nx * r, b.y + ny * r));
  segs.push_back(arcSeg(a.x, a.y, a.x + nx * r, a.y + ny * r, a.x - nx * r, a.y - ny * r));
}

// A slot's arc centres and radius from the clicks of each slot tool:
// centre to centre (c0, c1 = centres), overall (c0, c1 = ends), centre
// point (c0 = middle, c1 = one centre); w = the width click.
bool slotGeometry(M mode, P2 c0, P2 c1, P2 w, const double* typedWidth, P2& a, P2& b, double& r) {
  P2 x0 = c0, x1 = c1;
  if (mode == M::SlotCentre) x0 = {2 * c0.x - c1.x, 2 * c0.y - c1.y};
  const double dx = x1.x - x0.x, dy = x1.y - x0.y, len = std::hypot(dx, dy);
  if (len < 1e-9) return false;
  const double ux = dx / len, uy = dy / len;
  r = typedWidth ? *typedWidth / 2 : std::abs((w.x - x0.x) * -uy + (w.y - x0.y) * ux);
  if (r < 1e-9) return false;
  a = x0, b = x1;
  if (mode == M::SlotOverall) {
    if (2 * r >= len) return false;  // wider than it is long
    a = {x0.x + ux * r, x0.y + uy * r};
    b = {x1.x - ux * r, x1.y - uy * r};
  }
  return true;
}

// A regular polygon's centre, corner radius and first-corner angle from the
// clicks of each polygon tool. Edge polygons: c0, c1 = a side, w = the side
// it goes on (ccw = the side runs counter-clockwise around the centre).
bool polygonGeometry(M mode, P2 c0, P2 c1, P2 w, int n, const double* typed, P2& centre, double& R, double& a0,
                     bool& ccw) {
  ccw = true;
  if (mode == M::PolygonEdge) {
    const double dx = c1.x - c0.x, dy = c1.y - c0.y, side = std::hypot(dx, dy);
    if (side < 1e-9) return false;
    const double nx = -dy / side, ny = dx / side;
    ccw = (w.x - c0.x) * nx + (w.y - c0.y) * ny >= 0;
    const double k = (ccw ? 1 : -1) * (side / 2) / std::tan(kPi / n);
    centre = {(c0.x + c1.x) / 2 + nx * k, (c0.y + c1.y) / 2 + ny * k};
    R = side / (2 * std::sin(kPi / n));
    a0 = std::atan2(c0.y - centre.y, c0.x - centre.x);
    return true;
  }
  centre = c0;
  double d = typed ? *typed : dist(c0.x, c0.y, c1.x, c1.y);
  if (d < 1e-9) return false;
  a0 = std::atan2(c1.y - c0.y, c1.x - c0.x);
  if (mode == M::PolygonCirc) {  // the click is the middle of a side
    R = d / std::cos(kPi / n);
    a0 += kPi / n;
  } else {
    R = d;
  }
  return true;
}

// A smooth curve through points for the spline preview (Catmull-Rom).
std::vector<geom::Seg2> catmullRom(const std::vector<P2>& pts, bool closed) {
  std::vector<geom::Seg2> out;
  const int n = int(pts.size());
  if (n < 2) return out;
  const int spans = closed ? n : n - 1;
  auto at = [&](int i) {
    if (closed) return pts[((i % n) + n) % n];
    return pts[std::clamp(i, 0, n - 1)];
  };
  for (int i = 0; i < spans; ++i) {
    const P2 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    P2 prev = p1;
    for (int k = 1; k <= 12; ++k) {
      const double t = k / 12.0, t2 = t * t, t3 = t2 * t;
      const P2 q{0.5 * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
                        (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
                 0.5 * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
                        (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)};
      out.push_back(lineSeg(prev.x, prev.y, q.x, q.y));
      prev = q;
    }
  }
  return out;
}

}  // namespace

// The corner of `lines` nearest (u, v).
std::uint64_t SketchEditor::cornerAt(const std::vector<std::uint64_t>& lines, double u, double v) const {
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
}

void SketchEditor::attach(const Hit& h, std::uint64_t point) {
  if (!point) return;
  if (h.kind == Hit::Point || h.kind == Hit::Origin) {
    const std::uint64_t p = resolve(h);
    if (p != point) addConstraint(K::Coincident, p, point, 0, 0, true);
  } else if (h.kind == Hit::Curve && h.id) {
    addConstraint(h.mid ? K::Midpoint : K::PointOnCurve, point, h.id, 0, 0, true);
  }
}

// Where a tangent arc may start: the end of a line or arc. Gives the curve
// and the direction leaving it.
static bool leavingDirection(const std::vector<model::SketchEntityInfo>& entities, std::uint64_t point,
                             std::uint64_t& curve, double& tx, double& ty) {
  for (const auto& e : entities) {
    if (e.construction) continue;
    if (e.kind == EK::Line && (e.p0 == point || e.p1 == point)) {
      const bool atEnd = e.p1 == point;
      tx = atEnd ? e.x1 - e.x0 : e.x0 - e.x1;
      ty = atEnd ? e.y1 - e.y0 : e.y0 - e.y1;
    } else if (e.kind == EK::Arc && (e.p1 == point || e.p2 == point)) {
      // Arcs run counter-clockwise from p1 to p2.
      const bool atEnd = e.p2 == point;
      const double rx = (atEnd ? e.x2 : e.x1) - e.x0, ry = (atEnd ? e.y2 : e.y1) - e.y0;
      tx = atEnd ? -ry : ry;
      ty = atEnd ? rx : -rx;
    } else {
      continue;
    }
    const double len = std::hypot(tx, ty);
    if (len < 1e-12) continue;
    tx /= len, ty /= len;
    curve = e.id;
    return true;
  }
  return false;
}

void SketchEditor::clickShape(const Hit& hIn, const Typed& typed) {
  const Hit h = hIn;
  const double tol = pickPx() * unitsPerPixel();

  if (mode_ == Mode::Spline) {
    if (clicks_.size() >= 3 && dist(h.u, h.v, clicks_.front().u, clicks_.front().v) < tol) {
      // Back on the first point: a closed spline.
      std::vector<Hit> pts = clicks_;
      clicks_.clear();
      edit([&] {
        rust::Vec<std::uint64_t> ids;
        for (const Hit& c : pts) ids.push_back(resolve(c));
        const std::uint64_t s = model::sketch_add_spline(tl(), sketch_, std::move(ids), true);
        markNew({s});
        return true;
      });
      drawPreview();
      return;
    }
    if (!clicks_.empty() && dist(h.u, h.v, clicks_.back().u, clicks_.back().v) < tol) return;
    clicks_.push_back(h);
    drawPreview();
    return;
  }

  if (mode_ == Mode::TangentArc && clicks_.empty()) {
    std::uint64_t curve;
    double tx, ty;
    if (h.kind != Hit::Point || !leavingDirection(entities_, h.id, curve, tx, ty)) {
      emit message(tr("Tangent arc: start on the end of a line or arc."));
      return;
    }
  }

  const int need = clicksFor(mode_);
  if (int(clicks_.size()) + 1 < need) {
    if (!clicks_.empty() && dist(h.u, h.v, clicks_.back().u, clicks_.back().v) < tol) return;
    clicks_.push_back(h);
    drawPreview();
    return;
  }

  const std::vector<Hit> c = clicks_;
  const Hit first = c.front();
  bool done = false;
  switch (mode_) {
    case Mode::Rectangle3: {
      P2 k[4];
      rect3({c[0].u, c[0].v}, {c[1].u, c[1].v}, {h.u, h.v}, k);
      if (dist(k[1].x, k[1].y, k[2].x, k[2].y) < tol) return;
      done = edit([&] {
        const std::uint64_t p[4] = {resolve(c[0]), resolve(c[1]), model::sketch_add_point(tl(), sketch_, k[2].x, k[2].y),
                                    model::sketch_add_point(tl(), sketch_, k[3].x, k[3].y)};
        std::uint64_t l[4];
        for (int i = 0; i < 4; ++i) l[i] = model::sketch_add_line(tl(), sketch_, p[i], p[(i + 1) % 4]);
        addConstraint(K::Perpendicular, l[0], l[1], 0, 0, true);
        addConstraint(K::Parallel, l[0], l[2], 0, 0, true);
        addConstraint(K::Parallel, l[1], l[3], 0, 0, true);
        markNew({l[0], l[1], l[2], l[3]});
        return true;
      });
      break;
    }
    case Mode::RectangleCentre: {
      const double hx = std::abs(h.u - first.u), hy = std::abs(h.v - first.v);
      if (hx < tol || hy < tol) return;
      done = edit([&] {
        const auto lines = model::sketch_add_rectangle(tl(), sketch_, first.u - hx, first.v - hy, first.u + hx,
                                                       first.v + hy);
        reload();
        const std::vector<std::uint64_t> ls(lines.begin(), lines.end());
        const std::uint64_t a = cornerAt(ls, first.u - hx, first.v - hy);
        const std::uint64_t b = cornerAt(ls, first.u + hx, first.v + hy);
        // A construction diagonal with the centre at its middle, as Fusion draws it.
        const std::uint64_t diag = model::sketch_add_line(tl(), sketch_, a, b);
        model::sketch_set_construction(tl(), sketch_, diag, true);
        const std::uint64_t centre = resolve(first);
        addConstraint(K::Midpoint, centre, diag, 0, 0, true);
        attach(h, cornerAt(ls, h.u, h.v));
        if (typed.set[0] && ls.size() > 0) addConstraint(K::Length, ls[0], 0, 0, typed.value[0], true);
        if (typed.set[1] && ls.size() > 1) addConstraint(K::Length, ls[1], 0, 0, typed.value[1], true);
        markNew(ls);
        if (roundCorners_) roundRectangle(ls, 2 * hx, 2 * hy);
        return true;
      });
      break;
    }
    case Mode::Circle2: {
      const double r = dist(first.u, first.v, h.u, h.v) / 2;
      if (r < tol / 2) return;
      done = edit([&] {
        const std::uint64_t circle =
            model::sketch_add_circle_xy(tl(), sketch_, (first.u + h.u) / 2, (first.v + h.v) / 2, r);
        addConstraint(K::PointOnCurve, resolve(first), circle, 0, 0, true);
        addConstraint(K::PointOnCurve, resolve(h), circle, 0, 0, true);
        markNew({circle});
        return true;
      });
      break;
    }
    case Mode::Circle3: {
      const auto cc = circumcentre({c[0].u, c[0].v}, {c[1].u, c[1].v}, {h.u, h.v});
      if (!cc) {
        emit message(tr("Those three points are in a line; pick a point off the line."));
        return;
      }
      done = edit([&] {
        const std::uint64_t circle =
            model::sketch_add_circle_xy(tl(), sketch_, cc->x, cc->y, dist(cc->x, cc->y, h.u, h.v));
        for (const Hit& p : {c[0], c[1], h}) addConstraint(K::PointOnCurve, resolve(p), circle, 0, 0, true);
        markNew({circle});
        return true;
      });
      break;
    }
    case Mode::CentreArc: {
      const Hit s = c[1];
      const double r = dist(first.u, first.v, s.u, s.v);
      const double a = std::atan2(h.v - first.v, h.u - first.u);
      const double ex = first.u + r * std::cos(a), ey = first.v + r * std::sin(a);
      if (dist(ex, ey, s.u, s.v) < tol) return;
      done = edit([&] {
        const std::uint64_t centre = resolve(first);
        const std::uint64_t ps = resolve(s);
        std::uint64_t pe;
        if (h.kind == Hit::Point) {
          pe = resolve(h);
        } else {
          pe = model::sketch_add_point(tl(), sketch_, ex, ey);
          if (h.kind == Hit::Curve) addConstraint(K::PointOnCurve, pe, h.id, 0, 0, true);
        }
        markNew({model::sketch_add_arc(tl(), sketch_, centre, ps, pe)});
        return true;
      });
      break;
    }
    case Mode::TangentArc: {
      std::uint64_t curve = 0;
      double tx = 1, ty = 0;
      leavingDirection(entities_, first.id, curve, tx, ty);
      P2 centre;
      bool ccw;
      if (dist(first.u, first.v, h.u, h.v) < tol || !tangentArc({first.u, first.v}, {tx, ty}, {h.u, h.v}, centre, ccw)) {
        emit message(tr("Move off the line's direction to bend an arc."));
        return;
      }
      done = edit([&] {
        const std::uint64_t pc = model::sketch_add_point(tl(), sketch_, centre.x, centre.y);
        const std::uint64_t ps = resolve(first);
        const std::uint64_t pe = resolve(h);
        const std::uint64_t arc = ccw ? model::sketch_add_arc(tl(), sketch_, pc, ps, pe)
                                      : model::sketch_add_arc(tl(), sketch_, pc, pe, ps);
        addConstraint(K::Tangent, curve, arc, 0, 0, true);
        markNew({arc});
        return true;
      });
      // Keep going from the arc's end, like the line tool.
      if (done) {
        reload();
        Hit next;
        for (const auto& e : entities_)
          if (e.kind == EK::Point && dist(e.x0, e.y0, h.u, h.v) < 1e-6) next.kind = Hit::Point, next.id = e.id;
        next.u = h.u, next.v = h.v;
        clicks_.clear();
        if (next.id) clicks_.push_back(next);
        drawPreview();
        return;
      }
      break;
    }
    case Mode::Polygon:
    case Mode::PolygonCirc:
    case Mode::PolygonEdge: {
      const bool edge = mode_ == Mode::PolygonEdge;
      const int n = (!edge && typed.set[1]) ? int(typed.value[1]) : polygonSides_;
      if (n < 3) return;
      const P2 p1 = edge ? P2{c[1].u, c[1].v} : P2{h.u, h.v};
      P2 centre;
      double R, a0;
      bool ccw;
      if (!polygonGeometry(mode_, {first.u, first.v}, p1, {h.u, h.v}, n, (!edge && typed.set[0]) ? &typed.value[0] : nullptr,
                           centre, R, a0, ccw) ||
          R < tol)
        return;
      polygonSides_ = n;
      done = edit([&] {
        const std::uint64_t cp =
            edge ? model::sketch_add_point(tl(), sketch_, centre.x, centre.y) : resolve(first);
        const std::uint64_t ring = model::sketch_add_circle(tl(), sketch_, cp, R);
        model::sketch_set_construction(tl(), sketch_, ring, true);
        std::vector<std::uint64_t> pts, lines;
        for (int i = 0; i < n; ++i) {
          // Corners that were clicked keep their points.
          const Hit* clicked = nullptr;
          if (edge && i == 0) clicked = &c[0];
          if (edge && i == (ccw ? 1 : n - 1)) clicked = &c[1];
          if (mode_ == Mode::Polygon && i == 0 && h.kind == Hit::Point) clicked = &h;
          if (clicked) {
            pts.push_back(resolve(*clicked));
          } else {
            const double a = a0 + 2 * kPi * i / n * (ccw ? 1 : -1);
            pts.push_back(model::sketch_add_point(tl(), sketch_, centre.x + R * std::cos(a), centre.y + R * std::sin(a)));
          }
          addConstraint(K::PointOnCurve, pts.back(), ring, 0, 0, true);
        }
        for (int i = 0; i < n; ++i) lines.push_back(model::sketch_add_line(tl(), sketch_, pts[i], pts[(i + 1) % n]));
        for (int i = 1; i < n; ++i) addConstraint(K::Equal, lines[0], lines[i], 0, 0, true);
        if (mode_ == Mode::Polygon && typed.set[0]) addConstraint(K::Radius, ring, 0, 0, typed.value[0], true);
        markNew(lines);
        return true;
      });
      break;
    }
    case Mode::MidLine: {
      double ex = h.u, ey = h.v;
      if (typed.set[0]) {
        const double d = std::max(dist(first.u, first.v, h.u, h.v), 1e-12);
        ex = first.u + (h.u - first.u) / d * typed.value[0] / 2;
        ey = first.v + (h.v - first.v) / d * typed.value[0] / 2;
      }
      if (dist(first.u, first.v, ex, ey) < tol / 2) return;
      done = edit([&] {
        Hit end = h;
        if (typed.set[0]) end.kind = Hit::None, end.u = ex, end.v = ey;
        const std::uint64_t pe = resolve(end);
        const std::uint64_t po = model::sketch_add_point(tl(), sketch_, 2 * first.u - ex, 2 * first.v - ey);
        const std::uint64_t line = model::sketch_add_line(tl(), sketch_, po, pe);
        addConstraint(K::Midpoint, resolve(first), line, 0, 0, true);
        if (typed.set[0]) addConstraint(K::Length, line, 0, 0, typed.value[0], true);
        markNew({line});
        return true;
      });
      break;
    }
    case Mode::Ellipse: {
      const Hit m = c[1];
      const double mx = m.u - first.u, my = m.v - first.v, a = std::hypot(mx, my);
      if (a < tol) return;
      const double minor = std::abs((h.u - first.u) * (-my / a) + (h.v - first.v) * (mx / a));
      if (minor < tol) {
        emit message(tr("Move away from the major axis to give the ellipse some width."));
        return;
      }
      done = edit([&] {
        const std::uint64_t centre = resolve(first);
        const std::uint64_t major = resolve(m);
        markNew({model::sketch_add_ellipse(tl(), sketch_, centre, major, minor)});
        return true;
      });
      break;
    }
    case Mode::Slot:
    case Mode::SlotOverall:
    case Mode::SlotCentre: {
      P2 ca, cb;
      double r;
      if (!slotGeometry(mode_, {first.u, first.v}, {c[1].u, c[1].v}, {h.u, h.v}, typed.set[0] ? &typed.value[0] : nullptr,
                        ca, cb, r) ||
          r < tol / 2) {
        emit message(tr("Give the slot some width, less than its length."));
        return;
      }
      const double dx = cb.x - ca.x, dy = cb.y - ca.y, len = std::hypot(dx, dy);
      const double nx = -dy / len, ny = dx / len;
      const Mode m = mode_;
      done = edit([&] {
        auto pt = [&](double x, double y) { return model::sketch_add_point(tl(), sketch_, x, y); };
        // Arc centres: the clicked points where the clicks were the centres.
        const std::uint64_t p1 = m == Mode::Slot ? resolve(first) : pt(ca.x, ca.y);
        const std::uint64_t p2 = m == Mode::SlotOverall ? pt(cb.x, cb.y) : resolve(c[1]);
        const std::uint64_t a1 = pt(ca.x + nx * r, ca.y + ny * r), a2 = pt(cb.x + nx * r, cb.y + ny * r);
        const std::uint64_t b1 = pt(ca.x - nx * r, ca.y - ny * r), b2 = pt(cb.x - nx * r, cb.y - ny * r);
        const std::uint64_t top = model::sketch_add_line(tl(), sketch_, a1, a2);
        const std::uint64_t bottom = model::sketch_add_line(tl(), sketch_, b2, b1);
        const std::uint64_t end2 = model::sketch_add_arc(tl(), sketch_, p2, b2, a2);
        const std::uint64_t end1 = model::sketch_add_arc(tl(), sketch_, p1, a1, b1);
        for (std::uint64_t l : {top, bottom})
          for (std::uint64_t arc : {end1, end2}) addConstraint(K::Tangent, l, arc, 0, 0, true);
        addConstraint(K::Equal, end1, end2, 0, 0, true);
        if (m == Mode::SlotCentre) {
          // The centre click sits in the middle of a construction centre line.
          const std::uint64_t axis = model::sketch_add_line(tl(), sketch_, p1, p2);
          model::sketch_set_construction(tl(), sketch_, axis, true);
          addConstraint(K::Midpoint, resolve(first), axis, 0, 0, true);
        }
        if (typed.set[0]) addConstraint(K::Radius, end1, 0, 0, r, true);
        markNew({top, bottom, end1, end2});
        return true;
      });
      break;
    }
    default: return;
  }
  (void)done;
  clicks_.clear();
  drawPreview();
}

void SketchEditor::finishSpline() {
  if (mode_ != Mode::Spline) return;
  if (clicks_.size() < 2) {
    clicks_.clear();
    drawPreview();
    return;
  }
  const std::vector<Hit> pts = clicks_;
  clicks_.clear();
  edit([&] {
    rust::Vec<std::uint64_t> ids;
    for (const Hit& c : pts) ids.push_back(resolve(c));
    markNew({model::sketch_add_spline(tl(), sketch_, std::move(ids), false)});
    return true;
  });
  drawPreview();
}

void SketchEditor::previewShape(const Hit& a, double cu, double cv, std::vector<geom::Seg2>& segs, double live[2]) {
  const std::size_t n = clicks_.size();
  switch (mode_) {
    case Mode::Rectangle3: {
      if (n == 1) {
        segs.push_back(lineSeg(a.u, a.v, cu, cv));
        break;
      }
      P2 k[4];
      rect3({a.u, a.v}, {clicks_[1].u, clicks_[1].v}, {cu, cv}, k);
      for (int i = 0; i < 4; ++i) segs.push_back(lineSeg(k[i].x, k[i].y, k[(i + 1) % 4].x, k[(i + 1) % 4].y));
      break;
    }
    case Mode::RectangleCentre: {
      Hit c;
      c.u = cu, c.v = cv;
      Typed t;
      c = typedTarget(a, c, t);
      const double hx = std::abs(c.u - a.u), hy = std::abs(c.v - a.v);
      const double x0 = a.u - hx, y0 = a.v - hy, x1 = a.u + hx, y1 = a.v + hy;
      segs.push_back(lineSeg(x0, y0, x1, y0));
      segs.push_back(lineSeg(x1, y0, x1, y1));
      segs.push_back(lineSeg(x1, y1, x0, y1));
      segs.push_back(lineSeg(x0, y1, x0, y0));
      live[0] = 2 * hx, live[1] = 2 * hy;
      break;
    }
    case Mode::Circle2: {
      const double r = dist(a.u, a.v, cu, cv) / 2;
      if (r > 1e-9) segs.push_back(circleSeg((a.u + cu) / 2, (a.v + cv) / 2, r));
      break;
    }
    case Mode::Circle3: {
      if (n == 1) {
        segs.push_back(lineSeg(a.u, a.v, cu, cv));
        break;
      }
      if (const auto cc = circumcentre({a.u, a.v}, {clicks_[1].u, clicks_[1].v}, {cu, cv}))
        segs.push_back(circleSeg(cc->x, cc->y, dist(cc->x, cc->y, cu, cv)));
      break;
    }
    case Mode::CentreArc: {
      if (n == 1) {
        segs.push_back(lineSeg(a.u, a.v, cu, cv));
        break;
      }
      const Hit& s = clicks_[1];
      const double r = dist(a.u, a.v, s.u, s.v), ang = std::atan2(cv - a.v, cu - a.u);
      segs.push_back(arcSeg(a.u, a.v, s.u, s.v, a.u + r * std::cos(ang), a.v + r * std::sin(ang)));
      segs.push_back(lineSeg(a.u, a.v, s.u, s.v));
      break;
    }
    case Mode::TangentArc: {
      std::uint64_t curve;
      double tx, ty;
      if (!leavingDirection(entities_, a.id, curve, tx, ty)) break;
      P2 centre;
      bool ccw;
      if (!tangentArc({a.u, a.v}, {tx, ty}, {cu, cv}, centre, ccw)) {
        segs.push_back(lineSeg(a.u, a.v, cu, cv));
        break;
      }
      segs.push_back(ccw ? arcSeg(centre.x, centre.y, a.u, a.v, cu, cv) : arcSeg(centre.x, centre.y, cu, cv, a.u, a.v));
      break;
    }
    case Mode::Polygon:
    case Mode::PolygonCirc:
    case Mode::PolygonEdge: {
      const bool edge = mode_ == Mode::PolygonEdge;
      if (edge && n == 1) {
        segs.push_back(lineSeg(a.u, a.v, cu, cv));
        break;
      }
      Typed t;
      Hit c;
      c.u = cu, c.v = cv;
      if (!edge) c = typedTarget(a, c, t);
      const int sides = (!edge && t.set[1]) ? int(t.value[1]) : polygonSides_;
      const P2 p1 = edge ? P2{clicks_[1].u, clicks_[1].v} : P2{c.u, c.v};
      P2 centre;
      double R, a0;
      bool ccw;
      live[0] = dist(a.u, a.v, c.u, c.v), live[1] = sides;
      if (!polygonGeometry(mode_, {a.u, a.v}, p1, {cu, cv}, sides, nullptr, centre, R, a0, ccw)) break;
      for (int i = 0; i < sides; ++i) {
        const double t0 = a0 + 2 * kPi * i / sides, t1 = a0 + 2 * kPi * (i + 1) / sides;
        segs.push_back(lineSeg(centre.x + R * std::cos(t0), centre.y + R * std::sin(t0), centre.x + R * std::cos(t1),
                               centre.y + R * std::sin(t1)));
      }
      break;
    }
    case Mode::MidLine: {
      double ex = cu, ey = cv;
      if (double l; typedValue(0, l)) {
        const double d = std::max(dist(a.u, a.v, cu, cv), 1e-12);
        ex = a.u + (cu - a.u) / d * l / 2, ey = a.v + (cv - a.v) / d * l / 2;
      }
      segs.push_back(lineSeg(2 * a.u - ex, 2 * a.v - ey, ex, ey));
      live[0] = 2 * dist(a.u, a.v, ex, ey);
      break;
    }
    case Mode::Ellipse: {
      if (n == 1) {
        segs.push_back(lineSeg(2 * a.u - cu, 2 * a.v - cv, cu, cv));
        break;
      }
      const Hit& m = clicks_[1];
      const double mx = m.u - a.u, my = m.v - a.v, len = std::hypot(mx, my);
      if (len < 1e-9) break;
      const double minor = std::abs((cu - a.u) * (-my / len) + (cv - a.v) * (mx / len));
      if (minor < 1e-9) {
        segs.push_back(lineSeg(2 * a.u - m.u, 2 * a.v - m.v, m.u, m.v));
        break;
      }
      geom::Seg2 s;
      s.kind = geom::Seg2::Kind::Ellipse;
      s.cx = a.u, s.cy = a.v, s.mx = mx, s.my = my, s.minor = minor;
      segs.push_back(s);
      break;
    }
    case Mode::Slot:
    case Mode::SlotOverall:
    case Mode::SlotCentre: {
      if (n == 1) {
        if (mode_ == Mode::SlotCentre) segs.push_back(lineSeg(2 * a.u - cu, 2 * a.v - cv, cu, cv));
        else segs.push_back(lineSeg(a.u, a.v, cu, cv));
        break;
      }
      double w;
      const bool typedW = typedValue(0, w);
      P2 ca, cb;
      double r;
      if (!slotGeometry(mode_, {a.u, a.v}, {clicks_[1].u, clicks_[1].v}, {cu, cv}, typedW ? &w : nullptr, ca, cb, r))
        break;
      live[0] = 2 * r;
      slotOutline(ca, cb, r, segs);
      break;
    }
    case Mode::Spline: {
      std::vector<P2> pts;
      for (const Hit& c : clicks_) pts.push_back({c.u, c.v});
      const bool closing = n >= 3 && dist(cu, cv, a.u, a.v) < pickPx() * unitsPerPixel();
      if (!closing) pts.push_back({cu, cv});
      for (const geom::Seg2& s : catmullRom(pts, closing)) segs.push_back(s);
      break;
    }
    default: break;
  }
}
