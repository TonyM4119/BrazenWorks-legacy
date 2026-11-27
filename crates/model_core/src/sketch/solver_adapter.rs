//! The 2D constraint solver, behind BrazenWorks' own types.
//!
//! This is the ONLY file that names the solver library we use (MitCad's
//! `mitcad-solver`, MIT; see THIRD_PARTY_NOTICES.md). Everything else in
//! BrazenWorks talks to [`ConstraintSolver`], [`Rule`] and friends, so swapping
//! or forking the solver later touches this file alone.
//!
//! Units: millimetres and radians, in sketch-plane coordinates.

use mitcad_solver as engine;
use std::fmt;

/// A point in the solver (index into [`ConstraintSolver`]'s point table).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct PointRef(u32);

/// A line, circle or arc in the solver.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CurveRef(u32);

/// A constraint or dimension in the solver.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct RuleRef(u32);

/// A geometric constraint or a dimension.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Rule {
    // ---- Geometric constraints ----
    Coincident(PointRef, PointRef),
    PointOnCurve(PointRef, CurveRef),
    Horizontal(CurveRef),
    Vertical(CurveRef),
    HorizontalPoints(PointRef, PointRef),
    VerticalPoints(PointRef, PointRef),
    Parallel(CurveRef, CurveRef),
    Perpendicular(CurveRef, CurveRef),
    Tangent(CurveRef, CurveRef),
    Equal(CurveRef, CurveRef),
    Fix(PointRef),
    Midpoint(PointRef, CurveRef),
    Concentric(CurveRef, CurveRef),
    Collinear(CurveRef, CurveRef),
    Symmetric {
        a: PointRef,
        b: PointRef,
        axis: CurveRef,
    },
    /// Every point and radius of the curve stays where it is.
    FixCurve(CurveRef),
    /// Curvature continuity (G2) where a spline joins another curve.
    Smooth(CurveRef, CurveRef),
    /// Two curves mirror each other across a line.
    SymmetricCurves {
        a: CurveRef,
        b: CurveRef,
        axis: CurveRef,
    },
    // ---- Dimensions (driving values) ----
    Distance {
        a: PointRef,
        b: PointRef,
        value: f64,
    },
    PointLineDistance {
        point: PointRef,
        line: CurveRef,
        value: f64,
    },
    HorizontalDistance {
        a: PointRef,
        b: PointRef,
        value: f64,
    },
    VerticalDistance {
        a: PointRef,
        b: PointRef,
        value: f64,
    },
    Length {
        line: CurveRef,
        value: f64,
    },
    /// Angle between two lines, radians.
    Angle {
        a: CurveRef,
        b: CurveRef,
        value: f64,
    },
    Radius {
        curve: CurveRef,
        value: f64,
    },
    Diameter {
        curve: CurveRef,
        value: f64,
    },
    /// Distance between two (parallel) lines.
    LineDistance {
        a: CurveRef,
        b: CurveRef,
        value: f64,
    },
    ArcLength {
        arc: CurveRef,
        value: f64,
    },
    /// Gap between a point and a circle's edge (`inside`: point within it).
    PointCircleDistance {
        point: PointRef,
        circle: CurveRef,
        value: f64,
        inside: bool,
    },
    /// Gap between a line and a circle's edge.
    LineCircleDistance {
        line: CurveRef,
        circle: CurveRef,
        value: f64,
    },
    /// Gap between two circles' edges (`inside`: one within the other).
    CircleDistance {
        a: CurveRef,
        b: CurveRef,
        value: f64,
        inside: bool,
    },
    MajorRadius {
        ellipse: CurveRef,
        value: f64,
    },
    MinorRadius {
        ellipse: CurveRef,
        value: f64,
    },
}

impl Rule {
    /// True for dimensions, whose value can be edited.
    pub fn is_dimension(&self) -> bool {
        matches!(
            self,
            Rule::Distance { .. }
                | Rule::PointLineDistance { .. }
                | Rule::HorizontalDistance { .. }
                | Rule::VerticalDistance { .. }
                | Rule::Length { .. }
                | Rule::Angle { .. }
                | Rule::Radius { .. }
                | Rule::Diameter { .. }
                | Rule::LineDistance { .. }
                | Rule::ArcLength { .. }
                | Rule::PointCircleDistance { .. }
                | Rule::LineCircleDistance { .. }
                | Rule::CircleDistance { .. }
                | Rule::MajorRadius { .. }
                | Rule::MinorRadius { .. }
        )
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CurveKind {
    Line,
    Circle,
    Arc,
    Ellipse,
    EllipticalArc,
    Spline,
}

/// A spline as the solver evaluates it (clamped B-spline, mm).
#[derive(Clone, Debug, PartialEq)]
pub struct SplineData {
    pub degree: usize,
    pub knots: Vec<f64>,
    pub weights: Vec<f64>,
    pub control_points: Vec<[f64; 2]>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum SolverError {
    UnknownPoint(PointRef),
    UnknownCurve(CurveRef),
    UnknownRule(RuleRef),
    /// The rule doesn't apply to that kind of curve (e.g. Length of a circle).
    WrongKind(&'static str),
    InvalidValue(&'static str),
    NotADimension(RuleRef),
    Unsupported(&'static str),
    /// The point or curve is still used by something else.
    InUse,
}

impl fmt::Display for SolverError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SolverError::UnknownPoint(p) => write!(f, "unknown point {}", p.0),
            SolverError::UnknownCurve(c) => write!(f, "unknown curve {}", c.0),
            SolverError::UnknownRule(r) => write!(f, "unknown constraint {}", r.0),
            SolverError::WrongKind(expected) => write!(f, "this constraint needs {expected}"),
            SolverError::InvalidValue(what) => write!(f, "invalid value: {what}"),
            SolverError::NotADimension(r) => write!(f, "constraint {} is not a dimension", r.0),
            SolverError::Unsupported(what) => write!(f, "not supported: {what}"),
            SolverError::InUse => write!(f, "still in use"),
        }
    }
}

impl std::error::Error for SolverError {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SolveStatus {
    /// Every constraint holds.
    Solved,
    /// Some constraints contradict each other; see [`SolveReport::conflicting`].
    Conflicting,
    /// The iteration did not converge (e.g. geometry too far from a solution).
    NotConverged,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SolveReport {
    pub status: SolveStatus,
    /// Every rule taking part in a contradiction.
    pub conflicting: Vec<RuleRef>,
    /// Curves that collapsed (zero-length line, zero radius).
    pub degenerate: Vec<CurveRef>,
}

impl SolveReport {
    pub fn is_solved(&self) -> bool {
        self.status == SolveStatus::Solved
    }
}

/// How constrained the sketch is.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Freedom {
    /// Remaining degrees of freedom; 0 = fully constrained.
    pub dof: usize,
    pub fully_constrained_points: Vec<PointRef>,
    pub fully_constrained_curves: Vec<CurveRef>,
    /// Rules that add nothing (implied by others).
    pub redundant: Vec<RuleRef>,
    /// Rules that contradict others.
    pub conflicting: Vec<RuleRef>,
}

/// A planar constraint system: points, curves and rules.
pub struct ConstraintSolver {
    sys: engine::System,
    points: Vec<engine::PointId>,
    curves: Vec<engine::EntityId>,
    rules: Vec<Option<engine::ConstraintId>>,
}

impl Default for ConstraintSolver {
    fn default() -> Self {
        Self::new()
    }
}

impl ConstraintSolver {
    pub fn new() -> Self {
        Self {
            sys: engine::System::new(),
            points: Vec::new(),
            curves: Vec::new(),
            rules: Vec::new(),
        }
    }

    // ---- Geometry ----------------------------------------------------------

    pub fn add_point(&mut self, x: f64, y: f64) -> PointRef {
        self.points.push(self.sys.add_point(x, y));
        PointRef(self.points.len() as u32 - 1)
    }

    pub fn add_line(&mut self, start: PointRef, end: PointRef) -> Result<CurveRef, SolverError> {
        let id = self
            .sys
            .add_line(self.p(start)?, self.p(end)?)
            .map_err(convert_error)?;
        Ok(self.push_curve(id))
    }

    pub fn add_circle(&mut self, centre: PointRef, radius: f64) -> Result<CurveRef, SolverError> {
        let id = self
            .sys
            .add_circle(self.p(centre)?, radius)
            .map_err(convert_error)?;
        Ok(self.push_curve(id))
    }

    /// Counter-clockwise arc from `start` to `end` around `centre`.
    pub fn add_arc(
        &mut self,
        centre: PointRef,
        start: PointRef,
        end: PointRef,
    ) -> Result<CurveRef, SolverError> {
        let id = self
            .sys
            .add_arc(self.p(centre)?, self.p(start)?, self.p(end)?)
            .map_err(convert_error)?;
        Ok(self.push_curve(id))
    }

    /// Ellipse from its centre, the end of its major axis and its minor radius.
    pub fn add_ellipse(
        &mut self,
        centre: PointRef,
        major: PointRef,
        minor_radius: f64,
    ) -> Result<CurveRef, SolverError> {
        let id = self
            .sys
            .add_ellipse(self.p(centre)?, self.p(major)?, minor_radius)
            .map_err(convert_error)?;
        Ok(self.push_curve(id))
    }

    /// Elliptical arc, counter-clockwise (in the ellipse parameter) from `start` to `end`.
    pub fn add_elliptical_arc(
        &mut self,
        centre: PointRef,
        major: PointRef,
        minor_radius: f64,
        start: PointRef,
        end: PointRef,
    ) -> Result<CurveRef, SolverError> {
        let id = self
            .sys
            .add_elliptical_arc(
                self.p(centre)?,
                self.p(major)?,
                minor_radius,
                self.p(start)?,
                self.p(end)?,
            )
            .map_err(convert_error)?;
        Ok(self.push_curve(id))
    }

    /// Cubic spline through fit points; `closed` makes it periodic.
    pub fn add_spline(&mut self, fit: &[PointRef], closed: bool) -> Result<CurveRef, SolverError> {
        let ids = fit
            .iter()
            .map(|&p| self.p(p))
            .collect::<Result<Vec<_>, _>>()?;
        let end = if closed {
            engine::SplineEnd::Periodic
        } else {
            engine::SplineEnd::Natural
        };
        let id = self
            .sys
            .add_fitted_spline(&ids, [end, end])
            .map_err(convert_error)?;
        Ok(self.push_curve(id))
    }

    pub fn minor_radius(&self, c: CurveRef) -> Option<f64> {
        self.sys.minor_radius(*self.curves.get(c.0 as usize)?)
    }

    /// The spline's current shape (control points follow the fit points).
    pub fn spline(&self, c: CurveRef) -> Option<SplineData> {
        let g = self.sys.spline(*self.curves.get(c.0 as usize)?)?;
        Some(SplineData {
            degree: g.degree,
            knots: g.knots,
            weights: g.weights,
            control_points: g.control_points,
        })
    }

    pub fn point(&self, p: PointRef) -> Option<[f64; 2]> {
        self.sys.point(*self.points.get(p.0 as usize)?)
    }

    pub fn set_point(&mut self, p: PointRef, xy: [f64; 2]) -> Result<(), SolverError> {
        self.sys.set_point(self.p(p)?, xy).map_err(convert_error)
    }

    /// Radius of a circle or arc.
    pub fn radius(&self, c: CurveRef) -> Option<f64> {
        self.sys.radius(*self.curves.get(c.0 as usize)?)
    }

    pub fn curve_kind(&self, c: CurveRef) -> Option<CurveKind> {
        match self.sys.entity_kind(*self.curves.get(c.0 as usize)?)? {
            engine::EntityKind::Line => Some(CurveKind::Line),
            engine::EntityKind::Circle => Some(CurveKind::Circle),
            engine::EntityKind::Arc => Some(CurveKind::Arc),
            engine::EntityKind::Ellipse => Some(CurveKind::Ellipse),
            engine::EntityKind::EllipticalArc => Some(CurveKind::EllipticalArc),
            engine::EntityKind::BSpline | engine::EntityKind::FittedSpline => {
                Some(CurveKind::Spline)
            }
        }
    }

    // ---- Rules -------------------------------------------------------------

    /// Adds a constraint or dimension. It is added even if it conflicts; call
    /// [`Self::solve`] or [`Self::analyze`] to find out, or use
    /// [`Self::try_add`] for the interactive "refuse if it conflicts" behaviour.
    pub fn add(&mut self, rule: Rule) -> Result<RuleRef, SolverError> {
        let c = self.convert_rule(&rule)?;
        let id = self.sys.add_constraint(c).map_err(convert_error)?;
        self.rules.push(Some(id));
        Ok(RuleRef(self.rules.len() as u32 - 1))
    }

    /// Adds a rule only if the system still solves with it; otherwise removes
    /// it again and returns the rules it conflicts with.
    pub fn try_add(&mut self, rule: Rule) -> Result<Result<RuleRef, Vec<RuleRef>>, SolverError> {
        let r = self.add(rule)?;
        let report = self.solve();
        if report.status == SolveStatus::Conflicting && report.conflicting.contains(&r) {
            let others = report.conflicting.into_iter().filter(|&o| o != r).collect();
            self.remove(r)?;
            self.solve();
            return Ok(Err(others));
        }
        Ok(Ok(r))
    }

    pub fn remove(&mut self, r: RuleRef) -> Result<(), SolverError> {
        let slot = self
            .rules
            .get_mut(r.0 as usize)
            .ok_or(SolverError::UnknownRule(r))?;
        let id = slot.take().ok_or(SolverError::UnknownRule(r))?;
        self.sys.remove_constraint(id).map_err(convert_error)
    }

    /// Changes a dimension's value (call [`Self::solve`] afterwards).
    pub fn set_value(&mut self, r: RuleRef, value: f64) -> Result<(), SolverError> {
        self.sys
            .set_dimension_value(self.r(r)?, value)
            .map_err(|e| match e {
                engine::Error::NotADimension(_) => SolverError::NotADimension(r),
                other => convert_error(other),
            })
    }

    pub fn value(&self, r: RuleRef) -> Option<f64> {
        self.sys.dimension_value(self.r(r).ok()?)
    }

    // ---- Solving -----------------------------------------------------------

    /// Moves the geometry as little as possible until every rule holds.
    pub fn solve(&mut self) -> SolveReport {
        let result = self.sys.solve(&engine::SolveOptions::default());
        self.report(result)
    }

    /// Pulls a point toward `target` while every rule keeps holding (drag).
    pub fn drag_point(
        &mut self,
        p: PointRef,
        target: [f64; 2],
    ) -> Result<SolveReport, SolverError> {
        let goal = engine::DragGoal::Point {
            point: self.p(p)?,
            target,
        };
        let result = self.sys.drag(&[goal], &engine::SolveOptions::default());
        Ok(self.report(result))
    }

    /// Degrees of freedom, fully constrained geometry, redundant and
    /// conflicting rules.
    pub fn analyze(&mut self) -> Freedom {
        let a = self.sys.analyze();
        Freedom {
            dof: a.dof,
            fully_constrained_points: self.point_refs(&a.fully_constrained_points),
            fully_constrained_curves: self.curve_refs(&a.fully_constrained_entities),
            redundant: self.rule_refs(&a.redundant_constraints()),
            conflicting: self.rule_refs(&a.conflicting_constraints()),
        }
    }

    // ---- Internals ---------------------------------------------------------

    fn push_curve(&mut self, id: engine::EntityId) -> CurveRef {
        self.curves.push(id);
        CurveRef(self.curves.len() as u32 - 1)
    }

    fn p(&self, p: PointRef) -> Result<engine::PointId, SolverError> {
        self.points
            .get(p.0 as usize)
            .copied()
            .ok_or(SolverError::UnknownPoint(p))
    }

    fn c(&self, c: CurveRef) -> Result<engine::EntityId, SolverError> {
        self.curves
            .get(c.0 as usize)
            .copied()
            .ok_or(SolverError::UnknownCurve(c))
    }

    fn r(&self, r: RuleRef) -> Result<engine::ConstraintId, SolverError> {
        self.rules
            .get(r.0 as usize)
            .copied()
            .flatten()
            .ok_or(SolverError::UnknownRule(r))
    }

    fn point_refs(&self, ids: &[engine::PointId]) -> Vec<PointRef> {
        let mut v: Vec<PointRef> = ids
            .iter()
            .filter_map(|id| {
                self.points
                    .iter()
                    .position(|p| p == id)
                    .map(|i| PointRef(i as u32))
            })
            .collect();
        v.sort_unstable();
        v
    }

    fn curve_refs(&self, ids: &[engine::EntityId]) -> Vec<CurveRef> {
        let mut v: Vec<CurveRef> = ids
            .iter()
            .filter_map(|id| {
                self.curves
                    .iter()
                    .position(|c| c == id)
                    .map(|i| CurveRef(i as u32))
            })
            .collect();
        v.sort_unstable();
        v
    }

    fn rule_refs(&self, ids: &[engine::ConstraintId]) -> Vec<RuleRef> {
        let mut v: Vec<RuleRef> = ids
            .iter()
            .filter_map(|id| {
                self.rules
                    .iter()
                    .position(|r| *r == Some(*id))
                    .map(|i| RuleRef(i as u32))
            })
            .collect();
        v.sort_unstable();
        v.dedup();
        v
    }

    fn report(&self, r: engine::SolveResult) -> SolveReport {
        SolveReport {
            status: match r.status {
                engine::SolveStatus::Converged => SolveStatus::Solved,
                engine::SolveStatus::Conflicting => SolveStatus::Conflicting,
                _ => SolveStatus::NotConverged,
            },
            conflicting: self.rule_refs(&r.conflicting()),
            degenerate: self.curve_refs(&r.degenerate),
        }
    }

    fn convert_rule(&self, rule: &Rule) -> Result<engine::Constraint, SolverError> {
        use engine::Constraint as C;
        Ok(match *rule {
            Rule::Coincident(a, b) => C::Coincident(self.p(a)?, self.p(b)?),
            Rule::PointOnCurve(p, c) => C::PointOnCurve(self.p(p)?, self.c(c)?),
            Rule::Horizontal(c) => C::Horizontal(self.c(c)?),
            Rule::Vertical(c) => C::Vertical(self.c(c)?),
            Rule::HorizontalPoints(a, b) => C::HorizontalPoints(self.p(a)?, self.p(b)?),
            Rule::VerticalPoints(a, b) => C::VerticalPoints(self.p(a)?, self.p(b)?),
            Rule::Parallel(a, b) => C::Parallel(self.c(a)?, self.c(b)?),
            Rule::Perpendicular(a, b) => C::Perpendicular(self.c(a)?, self.c(b)?),
            Rule::Tangent(a, b) => C::Tangent(self.c(a)?, self.c(b)?),
            Rule::Equal(a, b) => C::Equal(self.c(a)?, self.c(b)?),
            Rule::Fix(p) => C::FixPoint(self.p(p)?),
            Rule::Midpoint(p, line) => C::Midpoint(self.p(p)?, self.c(line)?),
            Rule::Concentric(a, b) => C::Concentric(self.c(a)?, self.c(b)?),
            Rule::Collinear(a, b) => C::Collinear(self.c(a)?, self.c(b)?),
            Rule::Symmetric { a, b, axis } => C::SymmetricPoints {
                a: self.p(a)?,
                b: self.p(b)?,
                axis: self.c(axis)?,
            },
            Rule::Distance { a, b, value } => C::Distance {
                a: self.p(a)?,
                b: self.p(b)?,
                value,
            },
            Rule::PointLineDistance { point, line, value } => C::PointLineDistance {
                point: self.p(point)?,
                line: self.c(line)?,
                value,
            },
            Rule::HorizontalDistance { a, b, value } => C::HorizontalDistance {
                a: self.p(a)?,
                b: self.p(b)?,
                value,
                signed: false,
            },
            Rule::VerticalDistance { a, b, value } => C::VerticalDistance {
                a: self.p(a)?,
                b: self.p(b)?,
                value,
                signed: false,
            },
            Rule::Length { line, value } => C::Length {
                line: self.c(line)?,
                value,
            },
            Rule::Angle { a, b, value } => C::Angle {
                a: self.c(a)?,
                b: self.c(b)?,
                value,
                supplement: None,
            },
            Rule::Radius { curve, value } => C::Radius {
                entity: self.c(curve)?,
                value,
            },
            Rule::Diameter { curve, value } => C::Diameter {
                entity: self.c(curve)?,
                value,
            },
            Rule::FixCurve(c) => C::FixEntity(self.c(c)?),
            Rule::Smooth(a, b) => C::Smooth(self.c(a)?, self.c(b)?),
            Rule::SymmetricCurves { a, b, axis } => C::SymmetricEntities {
                a: self.c(a)?,
                b: self.c(b)?,
                axis: self.c(axis)?,
            },
            Rule::LineDistance { a, b, value } => C::LineDistance {
                a: self.c(a)?,
                b: self.c(b)?,
                value,
            },
            Rule::ArcLength { arc, value } => C::ArcLength {
                arc: self.c(arc)?,
                value,
            },
            Rule::PointCircleDistance {
                point,
                circle,
                value,
                inside,
            } => C::TangentPointDistance {
                circle: self.c(circle)?,
                point: self.p(point)?,
                value,
                inside,
                far: false,
            },
            Rule::LineCircleDistance {
                line,
                circle,
                value,
            } => C::TangentDistance {
                circle: self.c(circle)?,
                line: self.c(line)?,
                value,
                far: false,
            },
            Rule::CircleDistance {
                a,
                b,
                value,
                inside,
            } => C::TangentCircleDistance {
                a: self.c(a)?,
                b: self.c(b)?,
                value,
                inside,
                far_a: false,
                far_b: false,
            },
            Rule::MajorRadius { ellipse, value } => C::MajorRadius {
                ellipse: self.c(ellipse)?,
                value,
            },
            Rule::MinorRadius { ellipse, value } => C::MinorRadius {
                ellipse: self.c(ellipse)?,
                value,
            },
        })
    }
}

fn convert_error(e: engine::Error) -> SolverError {
    match e {
        engine::Error::WrongEntityKind { expected, .. } => SolverError::WrongKind(expected),
        engine::Error::InvalidValue(what) => SolverError::InvalidValue(what),
        engine::Error::Unsupported(what) => SolverError::Unsupported(what),
        engine::Error::InUse => SolverError::InUse,
        // Ids are validated before calling the engine, so these mean a bug here.
        other => SolverError::Unsupported(match other {
            engine::Error::UnknownPoint(_) => "unknown point in solver",
            engine::Error::UnknownEntity(_) => "unknown curve in solver",
            engine::Error::UnknownConstraint(_) => "unknown constraint in solver",
            engine::Error::NotADimension(_) => "not a dimension",
            _ => "solver error",
        }),
    }
}

// These tests pin the solver's behaviour that BrazenWorks relies on. They run on
// every build, so a solver update that changes behaviour fails CI before merge.
#[cfg(test)]
mod tests {
    use super::*;

    const TOL: f64 = 1e-7;

    fn near(a: [f64; 2], b: [f64; 2]) -> bool {
        (a[0] - b[0]).abs() < TOL && (a[1] - b[1]).abs() < TOL
    }

    /// A rough four-sided shape with shared corners.
    fn rough_rectangle(s: &mut ConstraintSolver) -> ([PointRef; 4], [CurveRef; 4]) {
        let p = [
            s.add_point(0.0, 0.0),
            s.add_point(9.0, 0.5),
            s.add_point(10.0, 6.0),
            s.add_point(-0.5, 5.0),
        ];
        let l = [
            s.add_line(p[0], p[1]).unwrap(),
            s.add_line(p[1], p[2]).unwrap(),
            s.add_line(p[2], p[3]).unwrap(),
            s.add_line(p[3], p[0]).unwrap(),
        ];
        (p, l)
    }

    fn constrain_rectangle(
        s: &mut ConstraintSolver,
        p: &[PointRef; 4],
        l: &[CurveRef; 4],
    ) -> (RuleRef, RuleRef) {
        s.add(Rule::Fix(p[0])).unwrap();
        s.add(Rule::Horizontal(l[0])).unwrap();
        s.add(Rule::Horizontal(l[2])).unwrap();
        s.add(Rule::Vertical(l[1])).unwrap();
        s.add(Rule::Vertical(l[3])).unwrap();
        let w = s
            .add(Rule::Length {
                line: l[0],
                value: 40.0,
            })
            .unwrap();
        let h = s
            .add(Rule::Length {
                line: l[1],
                value: 20.0,
            })
            .unwrap();
        (w, h)
    }

    #[test]
    fn rectangle_solves_exactly_and_is_fully_constrained() {
        let mut s = ConstraintSolver::new();
        let (p, l) = rough_rectangle(&mut s);
        constrain_rectangle(&mut s, &p, &l);

        assert!(s.solve().is_solved());
        assert!(near(s.point(p[2]).unwrap(), [40.0, 20.0]));
        let f = s.analyze();
        assert_eq!(f.dof, 0);
        assert_eq!(f.fully_constrained_points.len(), 4);
        assert!(f.conflicting.is_empty());
    }

    #[test]
    fn editing_a_dimension_resolves() {
        let mut s = ConstraintSolver::new();
        let (p, l) = rough_rectangle(&mut s);
        let (width, _) = constrain_rectangle(&mut s, &p, &l);
        s.solve();

        s.set_value(width, 55.0).unwrap();
        assert_eq!(s.value(width), Some(55.0));
        assert!(s.solve().is_solved());
        assert!(near(s.point(p[2]).unwrap(), [55.0, 20.0]));
    }

    #[test]
    fn under_constrained_reports_freedom() {
        let mut s = ConstraintSolver::new();
        let (p, l) = rough_rectangle(&mut s);
        s.add(Rule::Fix(p[0])).unwrap();
        s.add(Rule::Horizontal(l[0])).unwrap();
        assert!(s.solve().is_solved());
        let f = s.analyze();
        // 4 points × 2 = 8 unknowns, minus fix (2) and horizontal (1).
        assert_eq!(f.dof, 5);
        assert_eq!(f.fully_constrained_points, vec![p[0]]);
    }

    #[test]
    fn conflicting_dimension_is_detected_and_refused() {
        let mut s = ConstraintSolver::new();
        let (p, l) = rough_rectangle(&mut s);
        let (width, _) = constrain_rectangle(&mut s, &p, &l);
        s.solve();

        // The top edge is parallel and must equal the bottom's 40 mm.
        let refused = s
            .try_add(Rule::Length {
                line: l[2],
                value: 30.0,
            })
            .unwrap();
        let others = refused.expect_err("a contradicting dimension must be refused");
        assert!(
            others.contains(&width),
            "the conflict should name the width: {others:?}"
        );
        // The sketch is unchanged and still solved.
        assert!(s.solve().is_solved());
        assert!(near(s.point(p[2]).unwrap(), [40.0, 20.0]));
    }

    #[test]
    fn redundant_constraint_is_reported() {
        let mut s = ConstraintSolver::new();
        let (p, l) = rough_rectangle(&mut s);
        constrain_rectangle(&mut s, &p, &l);
        // Parallel is implied by both edges being horizontal.
        s.add(Rule::Parallel(l[0], l[2])).unwrap();
        s.solve();
        assert!(!s.analyze().redundant.is_empty());
    }

    #[test]
    fn drag_keeps_constraints() {
        let mut s = ConstraintSolver::new();
        let (p, l) = rough_rectangle(&mut s);
        s.add(Rule::Fix(p[0])).unwrap();
        for i in [0, 2] {
            s.add(Rule::Horizontal(l[i])).unwrap();
        }
        for i in [1, 3] {
            s.add(Rule::Vertical(l[i])).unwrap();
        }
        s.solve();

        assert!(s.drag_point(p[2], [30.0, 12.0]).unwrap().is_solved());
        let c = s.point(p[2]).unwrap();
        assert!(
            near(c, [30.0, 12.0]),
            "free corner follows the pointer: {c:?}"
        );
        // Neighbours stay square to it.
        assert!((s.point(p[1]).unwrap()[0] - 30.0).abs() < TOL);
        assert!((s.point(p[3]).unwrap()[1] - 12.0).abs() < TOL);
        assert!(near(s.point(p[0]).unwrap(), [0.0, 0.0]));
    }

    #[test]
    fn slot_with_tangent_arcs() {
        // Two horizontal lines joined by two arcs, tangent at all four joints.
        let mut s = ConstraintSolver::new();
        let a = s.add_point(0.0, 0.0);
        let b = s.add_point(30.0, 0.5);
        let c = s.add_point(30.0, 10.0);
        let d = s.add_point(0.5, 10.0);
        let top = s.add_line(d, c).unwrap();
        let bottom = s.add_line(a, b).unwrap();
        let rc = s.add_point(31.0, 5.0);
        let lc = s.add_point(-1.0, 5.0);
        let right = s.add_arc(rc, b, c).unwrap();
        let left = s.add_arc(lc, d, a).unwrap();
        for (x, y) in [(bottom, right), (right, top), (top, left), (left, bottom)] {
            s.add(Rule::Tangent(x, y)).unwrap();
        }
        s.add(Rule::Fix(a)).unwrap();
        s.add(Rule::Horizontal(bottom)).unwrap();
        s.add(Rule::Length {
            line: bottom,
            value: 40.0,
        })
        .unwrap();
        s.add(Rule::Radius {
            curve: right,
            value: 6.0,
        })
        .unwrap();
        s.add(Rule::Equal(left, right)).unwrap();

        assert!(s.solve().is_solved());
        assert_eq!(s.curve_kind(right), Some(CurveKind::Arc));
        assert!((s.radius(left).unwrap() - 6.0).abs() < TOL);
        assert!(near(s.point(b).unwrap(), [40.0, 0.0]));
        assert!(near(s.point(c).unwrap(), [40.0, 12.0]));
        assert_eq!(s.analyze().dof, 0);
    }

    #[test]
    fn wrong_kind_is_an_error_not_a_panic() {
        let mut s = ConstraintSolver::new();
        let c = s.add_point(0.0, 0.0);
        let circle = s.add_circle(c, 5.0).unwrap();
        assert!(matches!(
            s.add(Rule::Length {
                line: circle,
                value: 3.0
            }),
            Err(SolverError::WrongKind(_))
        ));
        assert!(matches!(
            s.add(Rule::Fix(PointRef(99))),
            Err(SolverError::UnknownPoint(_))
        ));
    }
}
