//! Sketch editing operations: trim, extend, break, fillet, chamfer, offset,
//! mirror, patterns, move/copy/rotate/scale, Auto Constrain and loose ends.
//!
//! These are BrazenWorks' own implementations of the standard sketch tools
//! (the same set MitCAD and SolidCraft offer). Lines, arcs and circles are
//! intersected exactly; ellipses and splines through a fine polyline.

use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::f64::consts::TAU;

use super::*;
use crate::sketch::profiles::{spline_domain, spline_point};

type P = [f64; 2];
const EPS: f64 = 1e-9;
const SAMPLES: usize = 256;

fn sub(a: P, b: P) -> P {
    [a[0] - b[0], a[1] - b[1]]
}
fn add(a: P, b: P) -> P {
    [a[0] + b[0], a[1] + b[1]]
}
fn mul(a: P, k: f64) -> P {
    [a[0] * k, a[1] * k]
}
fn dot(a: P, b: P) -> f64 {
    a[0] * b[0] + a[1] * b[1]
}
fn cross(a: P, b: P) -> f64 {
    a[0] * b[1] - a[1] * b[0]
}
fn len(a: P) -> f64 {
    a[0].hypot(a[1])
}
fn unit(a: P) -> P {
    let l = len(a).max(1e-300);
    [a[0] / l, a[1] / l]
}
fn left(a: P) -> P {
    [-a[1], a[0]]
}

/// A curve as plain geometry, parametrised on [0, 1] (closed curves wrap).
#[derive(Clone, Debug)]
enum Shape {
    Line {
        a: P,
        b: P,
    },
    /// `a0` start angle, `sweep` > 0 counter-clockwise (2π for a circle).
    Arc {
        c: P,
        r: f64,
        a0: f64,
        sweep: f64,
        closed: bool,
    },
    /// Ellipse (arc): `m` major semi-axis vector, eccentric angles.
    Ellipse {
        c: P,
        m: P,
        minor: f64,
        t0: f64,
        sweep: f64,
        closed: bool,
    },
    Poly {
        pts: Vec<P>,
        closed: bool,
    },
}

impl Shape {
    fn closed(&self) -> bool {
        match self {
            Shape::Line { .. } => false,
            Shape::Arc { closed, .. }
            | Shape::Ellipse { closed, .. }
            | Shape::Poly { closed, .. } => *closed,
        }
    }

    fn at(&self, u: f64) -> P {
        match self {
            Shape::Line { a, b } => add(*a, mul(sub(*b, *a), u)),
            Shape::Arc {
                c, r, a0, sweep, ..
            } => {
                let t = a0 + sweep * u;
                [c[0] + r * t.cos(), c[1] + r * t.sin()]
            }
            Shape::Ellipse {
                c,
                m,
                minor,
                t0,
                sweep,
                ..
            } => {
                let t = t0 + sweep * u;
                let n = mul(left(unit(*m)), *minor);
                add(*c, add(mul(*m, t.cos()), mul(n, t.sin())))
            }
            Shape::Poly { pts, closed } => {
                let segs = if *closed { pts.len() } else { pts.len() - 1 };
                let x = (u.clamp(0.0, 1.0) * segs as f64).min(segs as f64 - 1e-12);
                let i = x.floor() as usize;
                let f = x - i as f64;
                let (p, q) = (pts[i], pts[(i + 1) % pts.len()]);
                add(p, mul(sub(q, p), f))
            }
        }
    }

    /// Parameter of the curve point nearest `p` (for an arc: may fall
    /// outside [0, 1] on the missing part of its circle).
    fn param_of(&self, p: P) -> f64 {
        match self {
            Shape::Line { a, b } => {
                let d = sub(*b, *a);
                dot(sub(p, *a), d) / dot(d, d).max(1e-300)
            }
            Shape::Arc {
                c,
                a0,
                sweep,
                closed,
                ..
            } => {
                let t = (p[1] - c[1]).atan2(p[0] - c[0]);
                let rel = (t - a0).rem_euclid(TAU);
                if *closed {
                    rel / TAU
                } else if rel <= *sweep {
                    rel / sweep
                } else {
                    // On the gap: nearer end decides (negative = before start).
                    let to_end = rel - sweep;
                    let to_start = TAU - rel;
                    if to_end < to_start {
                        1.0 + to_end / sweep
                    } else {
                        -to_start / sweep
                    }
                }
            }
            Shape::Ellipse {
                c,
                m,
                minor,
                t0,
                sweep,
                closed,
            } => {
                let a = len(*m);
                let u = unit(*m);
                let d = sub(p, *c);
                let t = (dot(d, left(u)) / minor).atan2(dot(d, u) / a);
                let rel = (t - t0).rem_euclid(TAU);
                if *closed {
                    rel / TAU
                } else {
                    rel / sweep
                }
            }
            Shape::Poly { .. } => {
                let mut best = (f64::MAX, 0.0);
                for i in 0..=SAMPLES {
                    let u = i as f64 / SAMPLES as f64;
                    let d = len(sub(self.at(u), p));
                    if d < best.0 {
                        best = (d, u);
                    }
                }
                best.1
            }
        }
    }

    fn polyline(&self) -> Vec<P> {
        match self {
            Shape::Line { a, b } => vec![*a, *b],
            Shape::Poly { pts, closed } => {
                let mut v = pts.clone();
                if *closed {
                    v.push(pts[0]);
                }
                v
            }
            _ => (0..=SAMPLES)
                .map(|i| self.at(i as f64 / SAMPLES as f64))
                .collect(),
        }
    }

    /// The full circle / ellipse an arc lies on, or the infinite line.
    fn carrier(&self) -> Shape {
        match self {
            Shape::Arc { c, r, a0, .. } => Shape::Arc {
                c: *c,
                r: *r,
                a0: *a0,
                sweep: TAU,
                closed: true,
            },
            other => other.clone(),
        }
    }
}

/// Intersection points of two shapes (on both, within their extents unless
/// `extend_a`: then on `a`'s carrier — infinite line or whole circle).
fn intersections(a: &Shape, b: &Shape, extend_a: bool) -> Vec<P> {
    let raw = match raw_intersections(a, b, extend_a) {
        Ok(points) => points,
        Err(points) => return points, // polyline crossings are already within both
    };
    let within = |s: &Shape, p: P, ext: bool| -> bool {
        if ext {
            return true;
        }
        let u = s.param_of(p);
        s.closed() || (-1e-9..=1.0 + 1e-9).contains(&u)
    };
    dedupe(
        raw.into_iter()
            .filter(|&p| within(a, p, extend_a) && within(b, p, false))
            .collect(),
    )
}

/// Where the carriers (infinite lines, whole circles) of two shapes cross.
fn carrier_intersections(a: &Shape, b: &Shape) -> Vec<P> {
    match raw_intersections(&a.carrier(), &b.carrier(), true) {
        Ok(p) | Err(p) => dedupe(p),
    }
}

/// Ok: analytic crossings of the carriers (not yet limited to the shapes);
/// Err: polyline crossings (already limited).
fn raw_intersections(a: &Shape, b: &Shape, extend_a: bool) -> Result<Vec<P>, Vec<P>> {
    let raw: Vec<P> = match (a, b) {
        (Shape::Line { a: a0, b: a1 }, Shape::Line { a: b0, b: b1 }) => {
            let (r, s) = (sub(*a1, *a0), sub(*b1, *b0));
            let den = cross(r, s);
            if den.abs() < 1e-14 {
                vec![]
            } else {
                let t = cross(sub(*b0, *a0), s) / den;
                vec![add(*a0, mul(r, t))]
            }
        }
        (Shape::Line { a: p0, b: p1 }, Shape::Arc { c, r, .. })
        | (Shape::Arc { c, r, .. }, Shape::Line { a: p0, b: p1 }) => line_circle(*p0, *p1, *c, *r),
        (Shape::Arc { c: c1, r: r1, .. }, Shape::Arc { c: c2, r: r2, .. }) => {
            circle_circle(*c1, *r1, *c2, *r2)
        }
        _ => {
            let pa = if extend_a {
                a.carrier().polyline()
            } else {
                a.polyline()
            };
            let pb = b.polyline();
            let mut out = Vec::new();
            for i in 0..pa.len().saturating_sub(1) {
                for j in 0..pb.len().saturating_sub(1) {
                    if let Some(p) = seg_seg(pa[i], pa[i + 1], pb[j], pb[j + 1]) {
                        out.push(p);
                    }
                }
            }
            return Err(dedupe(out));
        }
    };
    Ok(raw)
}

fn dedupe(mut v: Vec<P>) -> Vec<P> {
    let mut out: Vec<P> = Vec::new();
    v.sort_by(|a, b| a[0].total_cmp(&b[0]));
    for p in v {
        if !out.iter().any(|q| len(sub(*q, p)) < 1e-7) {
            out.push(p);
        }
    }
    out
}

fn line_circle(p0: P, p1: P, c: P, r: f64) -> Vec<P> {
    let d = sub(p1, p0);
    let f = sub(p0, c);
    let (a, b, cc) = (dot(d, d), 2.0 * dot(f, d), dot(f, f) - r * r);
    let disc = b * b - 4.0 * a * cc;
    if a < 1e-300 || disc < -1e-12 {
        return vec![];
    }
    let s = disc.max(0.0).sqrt();
    let mut v = vec![add(p0, mul(d, (-b - s) / (2.0 * a)))];
    if s > 1e-12 {
        v.push(add(p0, mul(d, (-b + s) / (2.0 * a))));
    }
    v
}

fn circle_circle(c1: P, r1: f64, c2: P, r2: f64) -> Vec<P> {
    let d = len(sub(c2, c1));
    if d < 1e-12 || d > r1 + r2 + 1e-12 || d < (r1 - r2).abs() - 1e-12 {
        return vec![];
    }
    let a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    let h = (r1 * r1 - a * a).max(0.0).sqrt();
    let u = unit(sub(c2, c1));
    let m = add(c1, mul(u, a));
    if h < 1e-12 {
        vec![m]
    } else {
        vec![add(m, mul(left(u), h)), sub(m, mul(left(u), h))]
    }
}

fn seg_seg(a0: P, a1: P, b0: P, b1: P) -> Option<P> {
    let (r, s) = (sub(a1, a0), sub(b1, b0));
    let den = cross(r, s);
    if den.abs() < 1e-14 {
        return None;
    }
    let q = sub(b0, a0);
    let t = cross(q, s) / den;
    let u = cross(q, r) / den;
    ((0.0..=1.0).contains(&t) && (0.0..=1.0).contains(&u)).then(|| add(a0, mul(r, t)))
}

/// How to move geometry for [`Sketch::transform`].
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Transform {
    Move { dx: f64, dy: f64 },
    Rotate { centre: P, angle: f64 },
    Scale { centre: P, factor: f64 },
}

impl Transform {
    fn apply(&self, p: P) -> P {
        match *self {
            Transform::Move { dx, dy } => [p[0] + dx, p[1] + dy],
            Transform::Rotate { centre, angle } => {
                let (s, c) = angle.sin_cos();
                let d = sub(p, centre);
                [
                    centre[0] + c * d[0] - s * d[1],
                    centre[1] + s * d[0] + c * d[1],
                ]
            }
            Transform::Scale { centre, factor } => add(centre, mul(sub(p, centre), factor)),
        }
    }
    fn scale(&self) -> f64 {
        match *self {
            Transform::Scale { factor, .. } => factor.abs(),
            _ => 1.0,
        }
    }
}

impl Sketch {
    // ---- Shapes -----------------------------------------------------------------

    fn shape(&self, id: EntityId) -> Option<Shape> {
        let pt = |p: EntityId| self.point(p);
        Some(match self.entity(id)?.geometry.clone() {
            Geometry::Line { start, end } => Shape::Line {
                a: pt(start)?,
                b: pt(end)?,
            },
            Geometry::Circle { centre, radius } => Shape::Arc {
                c: pt(centre)?,
                r: radius,
                a0: 0.0,
                sweep: TAU,
                closed: true,
            },
            Geometry::Arc { centre, start, end } => {
                let (c, s, e) = (pt(centre)?, pt(start)?, pt(end)?);
                let a0 = (s[1] - c[1]).atan2(s[0] - c[0]);
                let a1 = (e[1] - c[1]).atan2(e[0] - c[0]);
                let mut sweep = (a1 - a0).rem_euclid(TAU);
                if sweep < 1e-12 {
                    sweep = TAU;
                }
                Shape::Arc {
                    c,
                    r: len(sub(s, c)),
                    a0,
                    sweep,
                    closed: false,
                }
            }
            Geometry::Ellipse {
                centre,
                major,
                minor_radius,
            } => {
                let c = pt(centre)?;
                Shape::Ellipse {
                    c,
                    m: sub(pt(major)?, c),
                    minor: minor_radius,
                    t0: 0.0,
                    sweep: TAU,
                    closed: true,
                }
            }
            Geometry::EllipticalArc {
                centre,
                major,
                minor_radius,
                start,
                end,
            } => {
                let c = pt(centre)?;
                let m = sub(pt(major)?, c);
                let full = Shape::Ellipse {
                    c,
                    m,
                    minor: minor_radius,
                    t0: 0.0,
                    sweep: TAU,
                    closed: true,
                };
                let t0 = full.param_of(pt(start)?) * TAU;
                let t1 = full.param_of(pt(end)?) * TAU;
                let mut sweep = (t1 - t0).rem_euclid(TAU);
                if sweep < 1e-12 {
                    sweep = TAU;
                }
                Shape::Ellipse {
                    c,
                    m,
                    minor: minor_radius,
                    t0,
                    sweep,
                    closed: false,
                }
            }
            Geometry::Spline { closed, .. } => {
                let data = self.spline(id)?;
                let (lo, hi) = spline_domain(data);
                let n = SAMPLES;
                let pts = (0..n + usize::from(!closed))
                    .map(|i| spline_point(data, lo + (hi - lo) * i as f64 / n as f64))
                    .collect();
                Shape::Poly { pts, closed }
            }
            Geometry::Point { .. } => return None,
        })
    }

    fn curves(&self) -> Vec<EntityId> {
        self.entities
            .iter()
            .filter(|e| !matches!(e.geometry, Geometry::Point { .. }))
            .map(|e| e.id)
            .collect()
    }

    /// Where other curves cross `id`: (parameter on `id`, the crossing curve).
    fn cuts(&self, id: EntityId) -> Vec<(f64, EntityId)> {
        let Some(shape) = self.shape(id) else {
            return vec![];
        };
        let mut cuts = Vec::new();
        for other in self.curves() {
            if other == id {
                continue;
            }
            let Some(os) = self.shape(other) else {
                continue;
            };
            for p in intersections(&shape, &os, false) {
                let u = shape.param_of(p);
                if shape.closed() {
                    cuts.push((u.rem_euclid(1.0), other));
                } else if u > 1e-7 && u < 1.0 - 1e-7 {
                    cuts.push((u, other));
                }
            }
        }
        cuts.sort_by(|a, b| a.0.total_cmp(&b.0));
        cuts.dedup_by(|a, b| (a.0 - b.0).abs() < 1e-9);
        cuts
    }

    fn push_constraint(&mut self, c: SketchConstraint) {
        let id = ConstraintId(self.take_id());
        self.constraints.push((id, c));
    }

    fn new_point_on(&mut self, shape: &Shape, u: f64, on: Option<EntityId>) -> EntityId {
        let p = shape.at(u);
        let id = self.add_point(p[0], p[1]);
        if let Some(curve) = on {
            self.push_constraint(SketchConstraint::PointOnCurve { point: id, curve });
        }
        id
    }

    /// Drops the dimensions a change of a curve's extent makes wrong.
    fn drop_extent_dimensions(&mut self, id: EntityId) {
        let driven = &mut self.driven;
        self.constraints.retain(|(cid, c)| {
            let stale = match *c {
                SketchConstraint::Length { line, .. } => line == id,
                SketchConstraint::ArcLength { arc, .. } => arc == id,
                SketchConstraint::Midpoint { line, .. } => line == id,
                _ => false,
            };
            if stale {
                driven.remove(cid);
            }
            !stale
        });
    }

    /// Removes points no curve uses (with their constraints).
    fn remove_orphan_points(&mut self, candidates: &[EntityId]) {
        for &p in candidates {
            let used = self
                .entities
                .iter()
                .any(|e| e.geometry.defining_points().contains(&p));
            if !used && self.entity(p).is_some() {
                let _ = self.remove_entity(p);
            }
        }
    }

    fn set_geometry(&mut self, id: EntityId, g: Geometry) {
        if let Some(e) = self.entities.iter_mut().find(|e| e.id == id) {
            e.geometry = g;
        }
    }

    fn copy_flags(&mut self, from: EntityId, to: EntityId) {
        let construction = self.entity(from).is_some_and(|e| e.construction);
        let _ = self.set_construction(to, construction);
    }

    // ---- Trim / break -------------------------------------------------------

    /// Trim: removes the piece of `curve` between the crossings around `pick`
    /// (the whole curve if nothing crosses it). Cut ends are kept on the
    /// curves that cut them.
    pub fn trim(&mut self, curve: EntityId, pick: P) -> Result<(), SketchError> {
        self.split(curve, pick, true)
    }

    /// Break: splits `curve` at the crossings around `pick`, keeping every piece.
    pub fn break_at(&mut self, curve: EntityId, pick: P) -> Result<(), SketchError> {
        self.split(curve, pick, false)
    }

    fn split(&mut self, curve: EntityId, pick: P, remove_piece: bool) -> Result<(), SketchError> {
        let shape = self.shape(curve).ok_or(SketchError::UnknownEntity(curve))?;
        let geometry = self
            .entity(curve)
            .ok_or(SketchError::UnknownEntity(curve))?
            .geometry
            .clone();
        let cuts = self.cuts(curve);
        let up = shape.param_of(pick);

        if matches!(geometry, Geometry::Spline { .. }) {
            if cuts.is_empty() && remove_piece {
                return self.remove_entity(curve);
            }
            return Err(SketchError::WrongKind(
                "splines can't be trimmed or broken yet (delete it, or move its points)".into(),
            ));
        }

        if shape.closed() {
            if cuts.len() < 2 {
                return if remove_piece {
                    self.remove_entity(curve)
                } else {
                    Err(SketchError::InvalidValue(
                        "a closed curve needs two crossings to break".into(),
                    ))
                };
            }
            // The interval (cuts[k], cuts[k+1]) holding the pick, cyclically.
            let u = up.rem_euclid(1.0);
            let k = cuts
                .iter()
                .rposition(|(c, _)| *c <= u)
                .unwrap_or(cuts.len() - 1);
            let (lo, lo_by) = cuts[k];
            let (hi, hi_by) = cuts[(k + 1) % cuts.len()];
            let p_lo = self.new_point_on(&shape, lo, Some(lo_by));
            let p_hi = self.new_point_on(&shape, hi, Some(hi_by));
            let open = |s: &Self, start: EntityId, end: EntityId| -> Geometry {
                match &geometry {
                    Geometry::Circle { centre, .. } => Geometry::Arc {
                        centre: *centre,
                        start,
                        end,
                    },
                    Geometry::Ellipse {
                        centre,
                        major,
                        minor_radius,
                    } => Geometry::EllipticalArc {
                        centre: *centre,
                        major: *major,
                        minor_radius: *minor_radius,
                        start,
                        end,
                    },
                    other => {
                        let _ = s;
                        other.clone()
                    }
                }
            };
            // Kept: from hi round to lo (counter-clockwise).
            let kept = open(self, p_hi, p_lo);
            self.set_geometry(curve, kept);
            if !remove_piece {
                let piece = open(self, p_lo, p_hi);
                let id = self.push(piece);
                self.copy_flags(curve, id);
            }
            self.solve();
            return Ok(());
        }

        // Open curves: the interval around the pick.
        let lo = cuts.iter().rev().find(|(c, _)| *c < up).copied();
        let hi = cuts.iter().find(|(c, _)| *c > up).copied();
        let (start, end) = geometry.ends().ok_or(SketchError::UnknownEntity(curve))?;
        if lo.is_none() && hi.is_none() {
            return if remove_piece {
                self.remove_entity(curve)
            } else {
                Err(SketchError::InvalidValue(
                    "nothing crosses that curve".into(),
                ))
            };
        }
        let rebuild = |g: &Geometry, a: EntityId, b: EntityId| -> Geometry {
            match g {
                Geometry::Line { .. } => Geometry::Line { start: a, end: b },
                Geometry::Arc { centre, .. } => Geometry::Arc {
                    centre: *centre,
                    start: a,
                    end: b,
                },
                Geometry::EllipticalArc {
                    centre,
                    major,
                    minor_radius,
                    ..
                } => Geometry::EllipticalArc {
                    centre: *centre,
                    major: *major,
                    minor_radius: *minor_radius,
                    start: a,
                    end: b,
                },
                other => other.clone(),
            }
        };
        let p_lo = lo.map(|(u, by)| self.new_point_on(&shape, u, Some(by)));
        let p_hi = hi.map(|(u, by)| self.new_point_on(&shape, u, Some(by)));
        self.drop_extent_dimensions(curve);

        // Pieces: [start, lo], [lo, hi] (the picked one), [hi, end].
        let mut pieces: Vec<(EntityId, EntityId, bool)> = Vec::new(); // (a, b, picked)
        if let Some(a) = p_lo {
            pieces.push((start, a, false));
        }
        pieces.push((p_lo.unwrap_or(start), p_hi.unwrap_or(end), true));
        if let Some(b) = p_hi {
            pieces.push((b, end, false));
        }
        let kept: Vec<(EntityId, EntityId)> = pieces
            .iter()
            .filter(|(_, _, picked)| !(remove_piece && *picked))
            .map(|&(a, b, _)| (a, b))
            .collect();
        let line = matches!(geometry, Geometry::Line { .. });
        // The original entity keeps the first piece (and its constraints).
        let (a0, b0) = kept[0];
        self.set_geometry(curve, rebuild(&geometry, a0, b0));
        for &(a, b) in &kept[1..] {
            let id = self.push(rebuild(&geometry, a, b));
            self.copy_flags(curve, id);
            if line {
                self.push_constraint(SketchConstraint::Collinear(curve, id));
            }
        }
        if remove_piece {
            self.remove_orphan_points(&[start, end]);
            // A cut point that ended up unused (trimmed both ways) goes too.
            let unused: Vec<EntityId> = [p_lo, p_hi].into_iter().flatten().collect();
            self.remove_orphan_points(&unused);
        }
        self.solve();
        Ok(())
    }

    // ---- Extend -----------------------------------------------------------------

    /// Extend: lengthens the end of a line or arc nearest `pick` to the first
    /// curve it meets.
    pub fn extend(&mut self, curve: EntityId, pick: P) -> Result<(), SketchError> {
        let shape = self.shape(curve).ok_or(SketchError::UnknownEntity(curve))?;
        let geometry = self
            .entity(curve)
            .ok_or(SketchError::UnknownEntity(curve))?
            .geometry
            .clone();
        if !matches!(geometry, Geometry::Line { .. } | Geometry::Arc { .. }) {
            return Err(SketchError::WrongKind(
                "only lines and arcs can be extended".into(),
            ));
        }
        let (start, end) = geometry.ends().expect("open curve");
        let at_end = shape.param_of(pick) > 0.5;
        let moving = if at_end { end } else { start };
        let shared = self
            .entities
            .iter()
            .any(|e| e.id != curve && e.geometry.defining_points().contains(&moving));
        if shared {
            return Err(SketchError::InvalidValue(
                "that end is joined to another curve".into(),
            ));
        }
        if self
            .constraints
            .iter()
            .any(|(_, c)| matches!(*c, SketchConstraint::Length { line, .. } if line == curve))
        {
            return Err(SketchError::InvalidValue(
                "remove the line's length dimension first".into(),
            ));
        }
        // Nearest crossing beyond the end, on the carrier.
        let mut best: Option<(f64, P, EntityId)> = None;
        for other in self.curves() {
            if other == curve {
                continue;
            }
            let Some(os) = self.shape(other) else {
                continue;
            };
            for p in intersections(&shape, &os, true) {
                let beyond = match &shape {
                    Shape::Line { .. } => {
                        let u = shape.param_of(p);
                        if at_end {
                            (u > 1.0 + 1e-9).then_some(u - 1.0)
                        } else {
                            (u < -1e-9).then_some(-u)
                        }
                    }
                    Shape::Arc { c, a0, sweep, .. } => {
                        let t = (p[1] - c[1]).atan2(p[0] - c[0]);
                        let rel = (t - a0).rem_euclid(TAU);
                        if rel <= sweep + 1e-9 {
                            None
                        } else if at_end {
                            Some(rel - sweep)
                        } else {
                            Some(TAU - rel)
                        }
                    }
                    _ => None,
                };
                if let Some(d) = beyond {
                    if best.is_none_or(|b| d < b.0) {
                        best = Some((d, p, other));
                    }
                }
            }
        }
        let (_, p, target) = best.ok_or_else(|| {
            SketchError::InvalidValue("nothing to extend to in that direction".into())
        })?;
        self.set_point(moving, p);
        self.push_constraint(SketchConstraint::PointOnCurve {
            point: moving,
            curve: target,
        });
        self.solve();
        Ok(())
    }

    // ---- Fillet / chamfer ------------------------------------------------------

    /// The two lines meeting at `corner` (a shared end point).
    fn corner_lines(&self, corner: EntityId) -> Result<[(EntityId, EntityId); 2], SketchError> {
        let lines: Vec<(EntityId, EntityId)> = self
            .entities
            .iter()
            .filter_map(|e| match e.geometry {
                Geometry::Line { start, end } if start == corner => Some((e.id, end)),
                Geometry::Line { start, end } if end == corner => Some((e.id, start)),
                _ => None,
            })
            .collect();
        if lines.len() != 2 {
            return Err(SketchError::WrongKind(
                "pick a corner where exactly two lines meet".into(),
            ));
        }
        Ok([lines[0], lines[1]])
    }

    fn replace_line_end(&mut self, line: EntityId, old: EntityId, new: EntityId) {
        if let Some(e) = self.entities.iter_mut().find(|e| e.id == line) {
            if let Geometry::Line { start, end } = &mut e.geometry {
                if *start == old {
                    *start = new;
                } else if *end == old {
                    *end = new;
                }
            }
        }
    }

    /// Fillet: rounds the corner where two lines meet with an arc of `radius`
    /// tangent to both (and a radius dimension).
    pub fn fillet(&mut self, corner: EntityId, radius: f64) -> Result<EntityId, SketchError> {
        if !positive(radius, 0.0) {
            return Err(SketchError::InvalidValue(
                "the fillet radius must be positive".into(),
            ));
        }
        let [(l1, f1), (l2, f2)] = self.corner_lines(corner)?;
        let c = self
            .point(corner)
            .ok_or(SketchError::UnknownEntity(corner))?;
        let (p1, p2) = (self.point(f1).unwrap_or(c), self.point(f2).unwrap_or(c));
        let (d1, d2) = (unit(sub(p1, c)), unit(sub(p2, c)));
        let theta = dot(d1, d2).clamp(-1.0, 1.0).acos();
        if !(1e-6..=std::f64::consts::PI - 1e-6).contains(&theta) {
            return Err(SketchError::InvalidValue(
                "those lines don't make a corner".into(),
            ));
        }
        let back = radius / (theta / 2.0).tan();
        if back >= len(sub(p1, c)) - EPS || back >= len(sub(p2, c)) - EPS {
            return Err(SketchError::InvalidValue(
                "that radius doesn't fit the corner".into(),
            ));
        }
        let t1 = add(c, mul(d1, back));
        let t2 = add(c, mul(d2, back));
        let centre = add(c, mul(unit(add(d1, d2)), radius / (theta / 2.0).sin()));
        let (pt1, pt2) = (self.add_point(t1[0], t1[1]), self.add_point(t2[0], t2[1]));
        let pc = self.add_point(centre[0], centre[1]);
        self.replace_line_end(l1, corner, pt1);
        self.replace_line_end(l2, corner, pt2);
        self.drop_extent_dimensions(l1);
        self.drop_extent_dimensions(l2);
        let ccw = cross(sub(t1, centre), sub(t2, centre)) > 0.0;
        let (s, e) = if ccw { (pt1, pt2) } else { (pt2, pt1) };
        let arc = self.push(Geometry::Arc {
            centre: pc,
            start: s,
            end: e,
        });
        self.remove_orphan_points(&[corner]);
        for c in [
            SketchConstraint::Tangent(arc, l1),
            SketchConstraint::Tangent(arc, l2),
        ] {
            self.push_constraint(c);
        }
        self.solve();
        let _ = self.add_constraint(SketchConstraint::Radius {
            curve: arc,
            value: radius,
        });
        Ok(arc)
    }

    /// Chamfer: cuts the corner where two lines meet with a line `distance`
    /// back along each.
    pub fn chamfer(&mut self, corner: EntityId, distance: f64) -> Result<EntityId, SketchError> {
        if !positive(distance, 0.0) {
            return Err(SketchError::InvalidValue(
                "the chamfer distance must be positive".into(),
            ));
        }
        let [(l1, f1), (l2, f2)] = self.corner_lines(corner)?;
        let c = self
            .point(corner)
            .ok_or(SketchError::UnknownEntity(corner))?;
        let (p1, p2) = (self.point(f1).unwrap_or(c), self.point(f2).unwrap_or(c));
        if distance >= len(sub(p1, c)) - EPS || distance >= len(sub(p2, c)) - EPS {
            return Err(SketchError::InvalidValue(
                "that distance doesn't fit the corner".into(),
            ));
        }
        let t1 = add(c, mul(unit(sub(p1, c)), distance));
        let t2 = add(c, mul(unit(sub(p2, c)), distance));
        let (pt1, pt2) = (self.add_point(t1[0], t1[1]), self.add_point(t2[0], t2[1]));
        self.replace_line_end(l1, corner, pt1);
        self.replace_line_end(l2, corner, pt2);
        self.drop_extent_dimensions(l1);
        self.drop_extent_dimensions(l2);
        let line = self.push(Geometry::Line {
            start: pt1,
            end: pt2,
        });
        self.remove_orphan_points(&[corner]);
        self.solve();
        Ok(line)
    }

    // ---- Offset ------------------------------------------------------------------

    /// Offset: copies of lines, arcs and circles `distance` away, on the side
    /// of `side`. Joined curves stay joined (corners meet). Each copy keeps
    /// its distance through a constraint (lines: parallel + distance; arcs and
    /// circles: concentric + gap).
    pub fn offset(
        &mut self,
        curves: &[EntityId],
        distance: f64,
        side: P,
    ) -> Result<Vec<EntityId>, SketchError> {
        if !positive(distance, 0.0) {
            return Err(SketchError::InvalidValue(
                "the offset distance must be positive".into(),
            ));
        }
        for &c in curves {
            match self.entity(c).map(|e| &e.geometry) {
                Some(Geometry::Line { .. } | Geometry::Arc { .. } | Geometry::Circle { .. }) => {}
                Some(_) => {
                    return Err(SketchError::WrongKind(
                        "offset works on lines, arcs and circles for now".into(),
                    ))
                }
                None => return Err(SketchError::UnknownEntity(c)),
            }
        }
        let mut made = Vec::new();
        // Circles on their own.
        for &c in curves {
            if let Geometry::Circle { centre, radius } = self.entity(c).expect("checked").geometry {
                let cp = self.point(centre).unwrap_or([0.0, 0.0]);
                let inside = len(sub(side, cp)) < radius;
                let r = if inside {
                    radius - distance
                } else {
                    radius + distance
                };
                if r <= EPS {
                    return Err(SketchError::InvalidValue(
                        "that offset is bigger than the circle".into(),
                    ));
                }
                let id = self.push(Geometry::Circle { centre, radius: r });
                self.copy_flags(c, id);
                self.push_constraint(SketchConstraint::CircleDistance {
                    a: c,
                    b: id,
                    value: distance,
                    inside: true,
                });
                made.push(id);
            }
        }
        // Chains of lines and arcs.
        let open: Vec<EntityId> = curves
            .iter()
            .copied()
            .filter(|&c| {
                !matches!(
                    self.entity(c).map(|e| &e.geometry),
                    Some(Geometry::Circle { .. })
                )
            })
            .collect();
        for chain in self.chains(&open) {
            made.extend(self.offset_chain(&chain, distance, side)?);
        }
        self.solve();
        Ok(made)
    }

    /// Orders curves into chains that share end points: (curve, forward).
    fn chains(&self, curves: &[EntityId]) -> Vec<(Vec<(EntityId, bool)>, bool)> {
        let ends: BTreeMap<EntityId, (EntityId, EntityId)> = curves
            .iter()
            .filter_map(|&c| Some((c, self.entity(c)?.geometry.ends()?)))
            .collect();
        let mut used = BTreeSet::new();
        let mut out = Vec::new();
        let degree = |p: EntityId| ends.values().filter(|(a, b)| *a == p || *b == p).count();
        let mut order: Vec<EntityId> = ends.keys().copied().collect();
        // Start chains at free ends first.
        order.sort_by_key(|c| {
            let (a, b) = ends[c];
            usize::from(degree(a) != 1 && degree(b) != 1)
        });
        for &first in &order {
            if used.contains(&first) {
                continue;
            }
            let (a, b) = ends[&first];
            let forward = degree(a) == 1 || degree(b) != 1;
            let mut chain = vec![(first, forward)];
            used.insert(first);
            let mut tip = if forward { b } else { a };
            let origin = if forward { a } else { b };
            loop {
                let next = ends
                    .iter()
                    .find(|(c, (s, e))| !used.contains(*c) && (*s == tip || *e == tip))
                    .map(|(c, (s, e))| (*c, *s, *e));
                let Some((c, s, e)) = next else { break };
                let fwd = s == tip;
                chain.push((c, fwd));
                used.insert(c);
                tip = if fwd { e } else { s };
            }
            out.push((chain, tip == origin));
        }
        out
    }

    fn offset_chain(
        &mut self,
        (chain, closed): &(Vec<(EntityId, bool)>, bool),
        distance: f64,
        side: P,
    ) -> Result<Vec<EntityId>, SketchError> {
        // Oriented pieces along the chain, and which side `side` is on.
        let shapes: Vec<Shape> = chain
            .iter()
            .map(|&(c, fwd)| {
                let s = self.shape(c).expect("curve");
                if fwd {
                    s
                } else {
                    reverse(&s)
                }
            })
            .collect();
        let off = if *closed {
            // Closed: outside or inside of the loop.
            let poly: Vec<P> = shapes
                .iter()
                .flat_map(|s| (0..16).map(move |i| s.at(i as f64 / 16.0)))
                .collect();
            let area: f64 = (0..poly.len())
                .map(|i| cross(poly[i], poly[(i + 1) % poly.len()]))
                .sum::<f64>()
                / 2.0;
            let inside = point_in_poly(side, &poly);
            // Counter-clockwise loops have their inside on the left.
            let to_left = (area > 0.0) == inside;
            if to_left {
                distance
            } else {
                -distance
            }
        } else {
            // Open: the side of the piece nearest the pick.
            let mut best = (f64::MAX, 0usize, 0.5);
            for (i, s) in shapes.iter().enumerate() {
                for k in 0..=32 {
                    let u = k as f64 / 32.0;
                    let d = len(sub(s.at(u), side));
                    if d < best.0 {
                        best = (d, i, u);
                    }
                }
            }
            let (_, i, u) = best;
            let t = tangent(&shapes[i], u);
            distance
                * if cross(t, sub(side, shapes[i].at(u))) > 0.0 {
                    1.0
                } else {
                    -1.0
                }
        };
        // Offset carriers (left of travel = +off).
        let moved: Vec<Shape> = shapes.iter().map(|s| offset_shape(s, off)).collect();
        if moved
            .iter()
            .any(|s| matches!(s, Shape::Arc { r, .. } if *r <= EPS))
        {
            return Err(SketchError::InvalidValue(
                "that offset is bigger than an arc's radius".into(),
            ));
        }
        // Join points between consecutive pieces.
        let n = moved.len();
        let mut joins: Vec<P> = Vec::with_capacity(n + 1);
        let join = |a: &Shape, b: &Shape| -> P {
            let guess = a.at(1.0);
            let cands = carrier_intersections(a, b);
            cands
                .into_iter()
                .min_by(|p, q| len(sub(*p, guess)).total_cmp(&len(sub(*q, guess))))
                .filter(|p| len(sub(*p, guess)) < 10.0 * distance.max(1.0))
                .unwrap_or(guess)
        };
        joins.push(if *closed {
            join(&moved[n - 1], &moved[0])
        } else {
            moved[0].at(0.0)
        });
        for i in 0..n - 1 {
            joins.push(join(&moved[i], &moved[i + 1]));
        }
        joins.push(if *closed {
            joins[0]
        } else {
            moved[n - 1].at(1.0)
        });
        let ids: Vec<EntityId> = (0..joins.len() - usize::from(*closed))
            .map(|i| self.add_point(joins[i][0], joins[i][1]))
            .collect();
        let point = |i: usize| ids[i % ids.len()];
        let mut made = Vec::new();
        for (i, (&(orig, _), s)) in chain.iter().zip(&shapes).enumerate() {
            let (a, b) = (point(i), point(i + 1));
            let id = match s {
                Shape::Line { .. } => {
                    let id = self.push(Geometry::Line { start: a, end: b });
                    for c in [
                        SketchConstraint::Parallel(orig, id),
                        SketchConstraint::LineDistance {
                            a: orig,
                            b: id,
                            value: distance,
                        },
                    ] {
                        self.push_constraint(c);
                    }
                    id
                }
                Shape::Arc { c, sweep, .. } => {
                    let pc = self.add_point(c[0], c[1]);
                    // Arcs stay counter-clockwise: a reversed piece swaps ends.
                    let ccw = *sweep > 0.0;
                    let (st, en) = if ccw { (a, b) } else { (b, a) };
                    let id = self.push(Geometry::Arc {
                        centre: pc,
                        start: st,
                        end: en,
                    });
                    self.push_constraint(SketchConstraint::Concentric(orig, id));
                    id
                }
                _ => unreachable_shape(),
            };
            self.copy_flags(orig, id);
            made.push(id);
        }
        Ok(made)
    }

    // ---- Copies: mirror, patterns, transform -------------------------------------

    /// Copies curves through a point map. Points shared between the curves stay
    /// shared in the copy. Returns the new curves and the point map.
    /// Gives copies made by `copy_curves` the constraints that hold the
    /// originals' shape (those among the copied curves and their points).
    /// `axes`: also horizontal/vertical; `sizes`: also dimensions.
    fn copy_constraints(
        &mut self,
        curves: &[EntityId],
        made: &[EntityId],
        points: &HashMap<EntityId, EntityId>,
        axes: bool,
        sizes: bool,
    ) {
        let originals: Vec<EntityId> = curves
            .iter()
            .copied()
            .filter(|&c| {
                self.entity(c)
                    .is_some_and(|e| !matches!(e.geometry, Geometry::Point { .. }))
            })
            .collect();
        if originals.len() != made.len() {
            return;
        }
        let mut map = points.clone();
        map.extend(originals.iter().copied().zip(made.iter().copied()));
        let f = |id: EntityId| map.get(&id).copied();
        let copies: Vec<SketchConstraint> = self
            .constraints
            .iter()
            .filter(|(id, _)| !self.driven.contains(id))
            .filter_map(|(_, c)| remap_constraint(c, &f, axes, sizes))
            .collect();
        for c in copies {
            self.push_constraint(c);
        }
    }

    /// Pattern and mirror copies of circles keep the original's size (their
    /// points don't hold it).
    fn link_sizes(&mut self, curves: &[EntityId], made: &[EntityId]) {
        let originals: Vec<EntityId> = curves
            .iter()
            .copied()
            .filter(|&c| {
                self.entity(c)
                    .is_some_and(|e| !matches!(e.geometry, Geometry::Point { .. }))
            })
            .collect();
        if originals.len() != made.len() {
            return;
        }
        for (&orig, &copy) in originals.iter().zip(made) {
            if matches!(
                self.entity(orig).map(|e| &e.geometry),
                Some(Geometry::Circle { .. })
            ) {
                self.push_constraint(SketchConstraint::Equal(orig, copy));
            }
        }
    }

    fn copy_curves(
        &mut self,
        curves: &[EntityId],
        map: &dyn Fn(P) -> P,
        mirrored: bool,
        scale: f64,
    ) -> (Vec<EntityId>, HashMap<EntityId, EntityId>) {
        let mut points: HashMap<EntityId, EntityId> = HashMap::new();
        let mut made = Vec::new();
        for &c in curves {
            let Some(e) = self.entity(c).cloned() else {
                continue;
            };
            for p in e.geometry.defining_points() {
                if let std::collections::hash_map::Entry::Vacant(v) = points.entry(p) {
                    let q = map(self.point(p).unwrap_or([0.0, 0.0]));
                    let id = EntityId(self.next_id);
                    self.next_id += 1;
                    self.entities.push(Entity {
                        id,
                        geometry: Geometry::Point { x: q[0], y: q[1] },
                        construction: false,
                    });
                    v.insert(id);
                }
            }
            let m = |p: &EntityId| points[p];
            let g = match &e.geometry {
                Geometry::Point { .. } => continue,
                Geometry::Line { start, end } => Geometry::Line {
                    start: m(start),
                    end: m(end),
                },
                Geometry::Circle { centre, radius } => Geometry::Circle {
                    centre: m(centre),
                    radius: radius * scale,
                },
                Geometry::Arc { centre, start, end } => {
                    let (s, en) = if mirrored {
                        (m(end), m(start))
                    } else {
                        (m(start), m(end))
                    };
                    Geometry::Arc {
                        centre: m(centre),
                        start: s,
                        end: en,
                    }
                }
                Geometry::Ellipse {
                    centre,
                    major,
                    minor_radius,
                } => Geometry::Ellipse {
                    centre: m(centre),
                    major: m(major),
                    minor_radius: minor_radius * scale,
                },
                Geometry::EllipticalArc {
                    centre,
                    major,
                    minor_radius,
                    start,
                    end,
                } => {
                    let (s, en) = if mirrored {
                        (m(end), m(start))
                    } else {
                        (m(start), m(end))
                    };
                    Geometry::EllipticalArc {
                        centre: m(centre),
                        major: m(major),
                        minor_radius: minor_radius * scale,
                        start: s,
                        end: en,
                    }
                }
                Geometry::Spline {
                    points: fit,
                    closed,
                } => Geometry::Spline {
                    points: fit.iter().map(m).collect(),
                    closed: *closed,
                },
            };
            let id = self.push(g);
            self.copy_flags(c, id);
            made.push(id);
        }
        (made, points)
    }

    /// Mirror: copies of `curves` across the line `axis`, each copied point
    /// held symmetric to its original.
    pub fn mirror(
        &mut self,
        curves: &[EntityId],
        axis: EntityId,
    ) -> Result<Vec<EntityId>, SketchError> {
        let made = self.mirror_raw(curves, axis)?;
        self.record_group(CopyKind::Mirror { axis }, curves, &made);
        Ok(made)
    }

    fn mirror_raw(
        &mut self,
        curves: &[EntityId],
        axis: EntityId,
    ) -> Result<Vec<EntityId>, SketchError> {
        let (a, b) = match self.entity(axis).map(|e| e.geometry.clone()) {
            Some(Geometry::Line { start, end }) => (
                self.point(start).unwrap_or([0.0, 0.0]),
                self.point(end).unwrap_or([0.0, 0.0]),
            ),
            _ => {
                return Err(SketchError::WrongKind(
                    "the mirror line must be a line".into(),
                ))
            }
        };
        let curves: Vec<EntityId> = curves.iter().copied().filter(|&c| c != axis).collect();
        let u = unit(sub(b, a));
        let reflect = move |p: P| {
            let d = sub(p, a);
            add(a, sub(mul(u, 2.0 * dot(d, u)), d))
        };
        let (made, points) = self.copy_curves(&curves, &reflect, true, 1.0);
        for (orig, copy) in points {
            let p = self.point(orig).unwrap_or([0.0, 0.0]);
            let on_axis = cross(u, sub(p, a)).abs() < 1e-9;
            let c = if on_axis {
                SketchConstraint::Coincident(orig, copy)
            } else {
                SketchConstraint::Symmetric {
                    a: orig,
                    b: copy,
                    axis,
                }
            };
            self.push_constraint(c);
        }
        self.link_sizes(&curves, &made);
        self.solve();
        Ok(made)
    }

    /// Rectangular pattern: `nx` × `ny` copies (the original included) along
    /// `dir` and its left perpendicular, `sx` and `sy` apart.
    pub fn pattern_rect(
        &mut self,
        curves: &[EntityId],
        dir: P,
        nx: usize,
        ny: usize,
        sx: f64,
        sy: f64,
    ) -> Result<Vec<EntityId>, SketchError> {
        let made = self.pattern_rect_raw(curves, dir, nx, ny, sx, sy)?;
        self.record_group(
            CopyKind::Rect {
                dir,
                nx,
                ny,
                sx,
                sy,
            },
            curves,
            &made,
        );
        Ok(made)
    }

    fn pattern_rect_raw(
        &mut self,
        curves: &[EntityId],
        dir: P,
        nx: usize,
        ny: usize,
        sx: f64,
        sy: f64,
    ) -> Result<Vec<EntityId>, SketchError> {
        if nx == 0 || ny == 0 || nx * ny > 1000 {
            return Err(SketchError::InvalidValue(
                "pattern counts must be 1 to 1000 copies".into(),
            ));
        }
        let u = unit(dir);
        let v = left(u);
        let mut made = Vec::new();
        for i in 0..nx {
            for j in 0..ny {
                if i == 0 && j == 0 {
                    continue;
                }
                let shift = add(mul(u, sx * i as f64), mul(v, sy * j as f64));
                let (m, pts) = self.copy_curves(curves, &move |p: P| add(p, shift), false, 1.0);
                self.copy_constraints(curves, &m, &pts, true, true);
                self.link_sizes(curves, &m);
                made.extend(m);
            }
        }
        self.solve();
        Ok(made)
    }

    /// Circular pattern: `count` copies (the original included) around
    /// `centre`, spread over `angle` (2π = all the way round).
    pub fn pattern_circ(
        &mut self,
        curves: &[EntityId],
        centre: P,
        count: usize,
        angle: f64,
    ) -> Result<Vec<EntityId>, SketchError> {
        let made = self.pattern_circ_raw(curves, centre, count, angle)?;
        self.record_group(
            CopyKind::Circ {
                centre,
                count,
                angle,
            },
            curves,
            &made,
        );
        Ok(made)
    }

    fn pattern_circ_raw(
        &mut self,
        curves: &[EntityId],
        centre: P,
        count: usize,
        angle: f64,
    ) -> Result<Vec<EntityId>, SketchError> {
        if !(2..=1000).contains(&count) {
            return Err(SketchError::InvalidValue(
                "a circular pattern needs 2 to 1000 copies".into(),
            ));
        }
        let full = (angle.abs() - TAU).abs() < 1e-9;
        let step = if full {
            angle / count as f64
        } else {
            angle / (count - 1) as f64
        };
        let mut made = Vec::new();
        for k in 1..count {
            let t = Transform::Rotate {
                centre,
                angle: step * k as f64,
            };
            let (m, pts) = self.copy_curves(curves, &move |p: P| t.apply(p), false, 1.0);
            self.copy_constraints(curves, &m, &pts, false, true);
            self.link_sizes(curves, &m);
            made.extend(m);
        }
        self.solve();
        Ok(made)
    }

    // ---- Editable patterns and mirrors -------------------------------------------

    fn record_group(&mut self, kind: CopyKind, sources: &[EntityId], made: &[EntityId]) {
        if made.is_empty() {
            return;
        }
        let id = self.take_id();
        let sources = sources
            .iter()
            .copied()
            .filter(|&c| !matches!(kind, CopyKind::Mirror { axis } if axis == c))
            .collect();
        self.groups.push(CopyGroup {
            id,
            kind,
            sources,
            copies: made.to_vec(),
        });
    }

    /// The pattern or mirror a curve belongs to (as a source or a copy).
    pub fn group_of(&self, curve: EntityId) -> Option<&CopyGroup> {
        self.groups.iter().rev().find(|g| {
            (g.sources.contains(&curve) || g.copies.contains(&curve))
                && g.sources.iter().any(|&s| self.entity(s).is_some())
        })
    }

    /// Makes a pattern or mirror again with new values: the old copies go,
    /// new ones take their place. The kind (rectangular, circular, mirror)
    /// stays.
    pub fn edit_group(&mut self, id: u64, kind: CopyKind) -> Result<Vec<EntityId>, SketchError> {
        let i = self
            .groups
            .iter()
            .position(|g| g.id == id)
            .ok_or_else(|| SketchError::InvalidValue("that pattern no longer exists".into()))?;
        let group = self.groups[i].clone();
        if std::mem::discriminant(&group.kind) != std::mem::discriminant(&kind) {
            return Err(SketchError::InvalidValue(
                "a pattern can't change its kind".into(),
            ));
        }
        let sources: Vec<EntityId> = group
            .sources
            .iter()
            .copied()
            .filter(|&s| self.entity(s).is_some())
            .collect();
        if sources.is_empty() {
            return Err(SketchError::InvalidValue(
                "the pattern's original curves are gone".into(),
            ));
        }
        self.delete_copies(&group.copies);
        let made = match kind {
            CopyKind::Rect {
                dir,
                nx,
                ny,
                sx,
                sy,
            } => self.pattern_rect_raw(&sources, dir, nx, ny, sx, sy)?,
            CopyKind::Circ {
                centre,
                count,
                angle,
            } => self.pattern_circ_raw(&sources, centre, count, angle)?,
            CopyKind::Mirror { axis } => self.mirror_raw(&sources, axis)?,
        };
        if let Some(g) = self.groups.iter_mut().find(|g| g.id == id) {
            g.kind = kind;
            g.sources = sources;
            g.copies = made.clone();
        }
        Ok(made)
    }

    /// Forgets a pattern; its copies go too when `delete_copies`.
    pub fn delete_group(&mut self, id: u64, delete_copies: bool) -> Result<(), SketchError> {
        let i = self
            .groups
            .iter()
            .position(|g| g.id == id)
            .ok_or_else(|| SketchError::InvalidValue("that pattern no longer exists".into()))?;
        let group = self.groups.remove(i);
        if delete_copies {
            self.delete_copies(&group.copies);
            self.solve();
        }
        Ok(())
    }

    /// Removes copies with their points and every constraint on them.
    fn delete_copies(&mut self, copies: &[EntityId]) {
        let mut points: BTreeSet<EntityId> = BTreeSet::new();
        for &c in copies {
            if let Some(e) = self.entity(c) {
                points.extend(e.geometry.defining_points());
            }
        }
        for &c in copies {
            if self.entity(c).is_some() {
                let _ = self.remove_entity(c);
            }
        }
        for p in points {
            let shared = self
                .entities
                .iter()
                .any(|e| e.geometry.defining_points().contains(&p));
            if shared || self.entity(p).is_none() {
                continue;
            }
            let driven = &mut self.driven;
            self.constraints.retain(|(cid, c)| {
                let keep = !c.entities().contains(&p);
                if !keep {
                    driven.remove(cid);
                }
                keep
            });
            let _ = self.remove_entity(p);
        }
    }

    // ---- Copy and paste -----------------------------------------------------------

    /// The curves (with their points and the constraints among them) as a
    /// sketch of their own, for the clipboard.
    pub fn extract(&self, curves: &[EntityId]) -> Sketch {
        let mut out = Sketch::new("clipboard", self.plane);
        let mut keep: BTreeSet<EntityId> = BTreeSet::new();
        for &c in curves {
            if let Some(e) = self.entity(c) {
                keep.insert(c);
                keep.extend(e.geometry.defining_points());
            }
        }
        out.entities = self
            .entities
            .iter()
            .filter(|e| keep.contains(&e.id))
            .cloned()
            .collect();
        out.constraints = self
            .constraints
            .iter()
            .filter(|(id, c)| {
                !self.driven.contains(id)
                    && !matches!(c, SketchConstraint::Fix(_))
                    && c.entities().iter().all(|e| keep.contains(e))
            })
            .cloned()
            .collect();
        out.next_id = self.next_id;
        out.solve();
        out
    }

    /// Adds a clipboard sketch's curves, moved by `offset`. Returns the new
    /// curves.
    pub fn paste(&mut self, clip: &Sketch, offset: P) -> Vec<EntityId> {
        let mut map: HashMap<EntityId, EntityId> = HashMap::new();
        // Points first, then curves (which refer to them).
        let mut order: Vec<&Entity> = clip.entities.iter().collect();
        order.sort_by_key(|e| !matches!(e.geometry, Geometry::Point { .. }));
        let mut made = Vec::new();
        for e in order {
            let id = EntityId(self.take_id());
            map.insert(e.id, id);
            let m = |p: &EntityId| map.get(p).copied().unwrap_or(*p);
            let geometry = match &e.geometry {
                Geometry::Point { x, y } => Geometry::Point {
                    x: x + offset[0],
                    y: y + offset[1],
                },
                Geometry::Line { start, end } => Geometry::Line {
                    start: m(start),
                    end: m(end),
                },
                Geometry::Circle { centre, radius } => Geometry::Circle {
                    centre: m(centre),
                    radius: *radius,
                },
                Geometry::Arc { centre, start, end } => Geometry::Arc {
                    centre: m(centre),
                    start: m(start),
                    end: m(end),
                },
                Geometry::Ellipse {
                    centre,
                    major,
                    minor_radius,
                } => Geometry::Ellipse {
                    centre: m(centre),
                    major: m(major),
                    minor_radius: *minor_radius,
                },
                Geometry::EllipticalArc {
                    centre,
                    major,
                    minor_radius,
                    start,
                    end,
                } => Geometry::EllipticalArc {
                    centre: m(centre),
                    major: m(major),
                    minor_radius: *minor_radius,
                    start: m(start),
                    end: m(end),
                },
                Geometry::Spline { points, closed } => Geometry::Spline {
                    points: points.iter().map(m).collect(),
                    closed: *closed,
                },
            };
            if !matches!(geometry, Geometry::Point { .. }) {
                made.push(id);
            }
            self.entities.push(Entity {
                id,
                geometry,
                construction: e.construction,
            });
        }
        let f = |id: EntityId| map.get(&id).copied();
        let copies: Vec<SketchConstraint> = clip
            .constraints
            .iter()
            .filter_map(|(_, c)| remap_constraint(c, &f, true, true))
            .collect();
        for c in copies {
            self.push_constraint(c);
        }
        self.solve();
        made
    }

    /// Move / rotate / scale curves, or copies of them. Constraints that hold
    /// geometry in place (fixes, dimensions) still apply after the move.
    pub fn transform(
        &mut self,
        curves: &[EntityId],
        t: Transform,
        copy: bool,
    ) -> Result<Vec<EntityId>, SketchError> {
        if let Transform::Scale { factor, .. } = t {
            if !positive(factor, 0.0) {
                return Err(SketchError::InvalidValue(
                    "the scale must be positive".into(),
                ));
            }
        }
        if copy {
            let (made, pts) = self.copy_curves(curves, &move |p: P| t.apply(p), false, t.scale());
            // Moved copies keep their directions; rotated ones don't. Scaled
            // copies can't keep the original sizes.
            let axes = matches!(t, Transform::Move { .. });
            let sizes = (t.scale() - 1.0).abs() < 1e-12;
            self.copy_constraints(curves, &made, &pts, axes, sizes);
            self.solve();
            return Ok(made);
        }
        let mut moved = BTreeSet::new();
        for &c in curves {
            let Some(e) = self.entity(c).cloned() else {
                continue;
            };
            let pts = if matches!(e.geometry, Geometry::Point { .. }) {
                vec![c]
            } else {
                e.geometry.defining_points()
            };
            for p in pts {
                if moved.insert(p) {
                    if let Some(xy) = self.point(p) {
                        self.set_point(p, t.apply(xy));
                    }
                }
            }
            let k = t.scale();
            if let Some(e) = self.entities.iter_mut().find(|e| e.id == c) {
                match &mut e.geometry {
                    Geometry::Circle { radius, .. } => *radius *= k,
                    Geometry::Ellipse { minor_radius, .. }
                    | Geometry::EllipticalArc { minor_radius, .. } => *minor_radius *= k,
                    _ => {}
                }
            }
        }
        self.solve();
        Ok(curves.to_vec())
    }

    // ---- Auto Constrain ------------------------------------------------------------

    /// Auto Constrain: adds the relations the geometry already has (joined
    /// ends, horizontal/vertical lines, tangent joins, equal radii), then
    /// dimensions (lengths, sizes, positions from an anchor) until the sketch
    /// is fully constrained or nothing more can be added. Returns how many
    /// constraints were added. Refused ones are skipped.
    pub fn auto_constrain(&mut self) -> usize {
        let before = self.constraints.len();
        let ang_tol = 0.5f64.to_radians();
        let pos_tol = 1e-6;
        let curves = self.curves();

        // Phase 1: relations.
        let ends: Vec<EntityId> = curves
            .iter()
            .filter_map(|&c| self.entity(c)?.geometry.ends())
            .flat_map(|(a, b)| [a, b])
            .collect();
        for (i, &p) in ends.iter().enumerate() {
            for &q in &ends[i + 1..] {
                if p == q {
                    continue;
                }
                if let (Some(a), Some(b)) = (self.point(p), self.point(q)) {
                    if len(sub(a, b)) < pos_tol {
                        let _ = self.add_constraint(SketchConstraint::Coincident(p, q));
                    }
                }
            }
        }
        for &c in &curves {
            if let Some(Geometry::Line { start, end }) = self.entity(c).map(|e| e.geometry.clone())
            {
                let (a, b) = (
                    self.point(start).unwrap_or_default(),
                    self.point(end).unwrap_or_default(),
                );
                let t = (b[1] - a[1])
                    .atan2(b[0] - a[0])
                    .rem_euclid(std::f64::consts::PI);
                if t < ang_tol || std::f64::consts::PI - t < ang_tol {
                    let _ = self.add_constraint(SketchConstraint::Horizontal(c));
                } else if (t - std::f64::consts::FRAC_PI_2).abs() < ang_tol {
                    let _ = self.add_constraint(SketchConstraint::Vertical(c));
                }
            }
        }
        // Tangent joins (line–arc, arc–arc sharing an end).
        for (i, &a) in curves.iter().enumerate() {
            for &b in &curves[i + 1..] {
                let (Some(ga), Some(gb)) = (
                    self.entity(a).map(|e| e.geometry.clone()),
                    self.entity(b).map(|e| e.geometry.clone()),
                ) else {
                    continue;
                };
                let round = |g: &Geometry| matches!(g, Geometry::Arc { .. });
                if !(round(&ga) || round(&gb))
                    || !(matches!(ga, Geometry::Line { .. } | Geometry::Arc { .. })
                        && matches!(gb, Geometry::Line { .. } | Geometry::Arc { .. }))
                {
                    continue;
                }
                let (Some((a0, a1)), Some((b0, b1))) = (ga.ends(), gb.ends()) else {
                    continue;
                };
                let Some(joint) = [a0, a1].into_iter().find(|p| *p == b0 || *p == b1) else {
                    continue;
                };
                let (Some(sa), Some(sb)) = (self.shape(a), self.shape(b)) else {
                    continue;
                };
                let ua = if joint == a0 { 0.0 } else { 1.0 };
                let ub = if joint == b0 { 0.0 } else { 1.0 };
                let (ta, tb) = (tangent(&sa, ua), tangent(&sb, ub));
                if cross(ta, tb).abs() < ang_tol.sin() {
                    let _ = self.add_constraint(SketchConstraint::Tangent(a, b));
                }
            }
        }

        // Phase 2: an anchor, then dimensions.
        let anchored = self
            .constraints
            .iter()
            .any(|(_, c)| matches!(c, SketchConstraint::Fix(_)));
        let all_points: Vec<EntityId> = self
            .entities
            .iter()
            .filter(|e| matches!(e.geometry, Geometry::Point { .. }))
            .map(|e| e.id)
            .collect();
        let anchor = all_points.iter().copied().min_by(|&p, &q| {
            let (a, b) = (
                self.point(p).unwrap_or_default(),
                self.point(q).unwrap_or_default(),
            );
            (a[0] + a[1]).total_cmp(&(b[0] + b[1]))
        });
        let Some(anchor) = anchor else {
            return self.constraints.len() - before;
        };
        if !anchored {
            let _ = self.add_constraint(SketchConstraint::Fix(anchor));
        }
        for &c in &curves {
            if self.solve().dof == 0 {
                break;
            }
            let g = self.entity(c).map(|e| e.geometry.clone());
            let dim = match g {
                Some(Geometry::Line { .. }) => Some(SketchConstraint::Length {
                    line: c,
                    value: 0.0,
                }),
                Some(Geometry::Circle { .. }) => Some(SketchConstraint::Diameter {
                    curve: c,
                    value: 0.0,
                }),
                Some(Geometry::Arc { .. }) => Some(SketchConstraint::Radius {
                    curve: c,
                    value: 0.0,
                }),
                Some(Geometry::Ellipse { .. }) | Some(Geometry::EllipticalArc { .. }) => {
                    for d in [
                        SketchConstraint::MajorRadius {
                            ellipse: c,
                            value: 0.0,
                        },
                        SketchConstraint::MinorRadius {
                            ellipse: c,
                            value: 0.0,
                        },
                    ] {
                        self.add_measured(d);
                    }
                    None
                }
                _ => None,
            };
            if let Some(d) = dim {
                self.add_measured(d);
            }
        }
        // Positions of whatever can still move, from the anchor.
        for &p in &all_points {
            if self.solve().dof == 0 {
                break;
            }
            if p == anchor {
                continue;
            }
            for d in [
                SketchConstraint::HorizontalDistance {
                    a: anchor,
                    b: p,
                    value: 0.0,
                },
                SketchConstraint::VerticalDistance {
                    a: anchor,
                    b: p,
                    value: 0.0,
                },
            ] {
                self.add_measured(d);
            }
        }
        self.solve();
        self.constraints.len() - before
    }

    /// Adds a dimension at its current measured value (skipped when zero or refused).
    fn add_measured(&mut self, mut d: SketchConstraint) {
        if let Some(v) = self.measure(&d) {
            if v > 1e-9 {
                d.set_value(v);
                let _ = self.add_constraint(d);
            }
        }
    }

    // ---- Loose ends --------------------------------------------------------------

    /// End points of (non-construction) open curves that nothing else joins:
    /// where a profile is left open.
    pub fn loose_ends(&self) -> Vec<EntityId> {
        let mut ends: Vec<EntityId> = Vec::new();
        for e in &self.entities {
            if e.construction {
                continue;
            }
            if let Some((a, b)) = e.geometry.ends() {
                ends.push(a);
                ends.push(b);
            }
        }
        let coincident: Vec<(EntityId, EntityId)> = self
            .constraints
            .iter()
            .filter_map(|(_, c)| match *c {
                SketchConstraint::Coincident(a, b) => Some((a, b)),
                _ => None,
            })
            .collect();
        // An end held on another (drawn) curve closes a profile there too.
        let on_curve: Vec<EntityId> = self
            .constraints
            .iter()
            .filter_map(|(_, c)| match *c {
                SketchConstraint::PointOnCurve { point, curve }
                | SketchConstraint::Midpoint { point, line: curve } => self
                    .entities
                    .iter()
                    .any(|e| e.id == curve && !e.construction)
                    .then_some(point),
                _ => None,
            })
            .collect();
        let mut loose = Vec::new();
        for (i, &p) in ends.iter().enumerate() {
            if on_curve.contains(&p) {
                continue;
            }
            let pp = self.point(p).unwrap_or_default();
            let joined = ends.iter().enumerate().any(|(j, &q)| {
                j != i
                    && (q == p
                        || coincident.contains(&(p, q))
                        || coincident.contains(&(q, p))
                        || len(sub(self.point(q).unwrap_or_default(), pp)) < 1e-6)
            });
            if !joined && !loose.contains(&p) {
                loose.push(p);
            }
        }
        loose
    }
}

fn point_in_poly(p: P, poly: &[P]) -> bool {
    let mut inside = false;
    let n = poly.len();
    let mut j = n - 1;
    for i in 0..n {
        let (a, b) = (poly[i], poly[j]);
        if (a[1] > p[1]) != (b[1] > p[1])
            && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]
        {
            inside = !inside;
        }
        j = i;
    }
    inside
}

fn unreachable_shape() -> EntityId {
    EntityId(0)
}

/// The same shape walked the other way.
fn reverse(s: &Shape) -> Shape {
    match s.clone() {
        Shape::Line { a, b } => Shape::Line { a: b, b: a },
        Shape::Arc {
            c,
            r,
            a0,
            sweep,
            closed,
        } => Shape::Arc {
            c,
            r,
            a0: a0 + sweep,
            sweep: -sweep,
            closed,
        },
        Shape::Ellipse {
            c,
            m,
            minor,
            t0,
            sweep,
            closed,
        } => Shape::Ellipse {
            c,
            m,
            minor,
            t0: t0 + sweep,
            sweep: -sweep,
            closed,
        },
        Shape::Poly { mut pts, closed } => {
            pts.reverse();
            Shape::Poly { pts, closed }
        }
    }
}

/// Unit direction of travel at parameter `u`.
fn tangent(s: &Shape, u: f64) -> P {
    let h = 1e-5;
    let (a, b) = if u > 1.0 - h {
        (s.at(u - h), s.at(u))
    } else {
        (s.at(u), s.at(u + h))
    };
    unit(sub(b, a))
}

/// The piece moved `off` to the left of its direction of travel.
fn offset_shape(s: &Shape, off: f64) -> Shape {
    match s.clone() {
        Shape::Line { a, b } => {
            let n = mul(left(unit(sub(b, a))), off);
            Shape::Line {
                a: add(a, n),
                b: add(b, n),
            }
        }
        // Counter-clockwise travel has the centre on the left.
        Shape::Arc {
            c,
            r,
            a0,
            sweep,
            closed,
        } => Shape::Arc {
            c,
            r: if sweep > 0.0 { r - off } else { r + off },
            a0,
            sweep,
            closed,
        },
        other => other,
    }
}

#[cfg(test)]
#[path = "ops_tests.rs"]
mod tests;

/// The same constraint on copied entities, if every entity it names was
/// copied and the copy can keep it (see `copy_constraints`).
fn remap_constraint(
    c: &SketchConstraint,
    f: &dyn Fn(EntityId) -> Option<EntityId>,
    axes: bool,
    sizes: bool,
) -> Option<SketchConstraint> {
    use SketchConstraint::*;
    Some(match *c {
        Coincident(a, b) => Coincident(f(a)?, f(b)?),
        PointOnCurve { point, curve } => PointOnCurve {
            point: f(point)?,
            curve: f(curve)?,
        },
        Parallel(a, b) => Parallel(f(a)?, f(b)?),
        Perpendicular(a, b) => Perpendicular(f(a)?, f(b)?),
        Tangent(a, b) => Tangent(f(a)?, f(b)?),
        Equal(a, b) => Equal(f(a)?, f(b)?),
        Concentric(a, b) => Concentric(f(a)?, f(b)?),
        Collinear(a, b) => Collinear(f(a)?, f(b)?),
        Smooth(a, b) => Smooth(f(a)?, f(b)?),
        Midpoint { point, line } => Midpoint {
            point: f(point)?,
            line: f(line)?,
        },
        Horizontal(a) if axes => Horizontal(f(a)?),
        Vertical(a) if axes => Vertical(f(a)?),
        HorizontalPoints(a, b) if axes => HorizontalPoints(f(a)?, f(b)?),
        VerticalPoints(a, b) if axes => VerticalPoints(f(a)?, f(b)?),
        Length { line, value } if sizes => Length {
            line: f(line)?,
            value,
        },
        Radius { curve, value } if sizes => Radius {
            curve: f(curve)?,
            value,
        },
        Diameter { curve, value } if sizes => Diameter {
            curve: f(curve)?,
            value,
        },
        ArcLength { arc, value } if sizes => ArcLength {
            arc: f(arc)?,
            value,
        },
        MajorRadius { ellipse, value } if sizes => MajorRadius {
            ellipse: f(ellipse)?,
            value,
        },
        MinorRadius { ellipse, value } if sizes => MinorRadius {
            ellipse: f(ellipse)?,
            value,
        },
        Distance { a, b, value } if sizes => Distance {
            a: f(a)?,
            b: f(b)?,
            value,
        },
        LineDistance { a, b, value } if sizes => LineDistance {
            a: f(a)?,
            b: f(b)?,
            value,
        },
        Angle { a, b, value } if sizes => Angle {
            a: f(a)?,
            b: f(b)?,
            value,
        },
        _ => return None,
    })
}
