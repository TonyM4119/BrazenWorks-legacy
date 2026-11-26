//! The sketch: geometry and constraints with stable ids, on a plane.
//!
//! Geometry and constraints are stored by id; every solve feeds them through
//! the constraint solver ([`super::solver_adapter`]) and writes the solved
//! positions back. Ids are never reused, so dimensions, profiles and later
//! features keep pointing at the right thing while the sketch is edited.

use std::collections::{HashMap, HashSet};
use std::fmt;

use super::plane::SketchPlane;
use super::solver_adapter::{
    ConstraintSolver, CurveRef, PointRef, Rule, RuleRef, SolveStatus, SolverError,
};

/// A point, line, circle or arc in a sketch. Stable for the sketch's life.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct EntityId(pub u64);

/// A constraint or dimension in a sketch. Stable for the sketch's life.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ConstraintId(pub u64);

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Geometry {
    Point {
        x: f64,
        y: f64,
    },
    Line {
        start: EntityId,
        end: EntityId,
    },
    Circle {
        centre: EntityId,
        radius: f64,
    },
    /// Counter-clockwise from `start` to `end` around `centre`.
    Arc {
        centre: EntityId,
        start: EntityId,
        end: EntityId,
    },
}

impl Geometry {
    pub fn kind_name(&self) -> &'static str {
        match self {
            Geometry::Point { .. } => "point",
            Geometry::Line { .. } => "line",
            Geometry::Circle { .. } => "circle",
            Geometry::Arc { .. } => "arc",
        }
    }

    /// Points this geometry is defined by.
    pub fn defining_points(&self) -> Vec<EntityId> {
        match *self {
            Geometry::Point { .. } => vec![],
            Geometry::Line { start, end } => vec![start, end],
            Geometry::Circle { centre, .. } => vec![centre],
            Geometry::Arc { centre, start, end } => vec![centre, start, end],
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Entity {
    pub id: EntityId,
    pub geometry: Geometry,
    /// Construction (reference) geometry is solved but never forms profiles.
    pub construction: bool,
}

/// A constraint or dimension between sketch entities.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SketchConstraint {
    Coincident(EntityId, EntityId),
    PointOnCurve {
        point: EntityId,
        curve: EntityId,
    },
    Horizontal(EntityId),
    Vertical(EntityId),
    HorizontalPoints(EntityId, EntityId),
    VerticalPoints(EntityId, EntityId),
    Parallel(EntityId, EntityId),
    Perpendicular(EntityId, EntityId),
    Tangent(EntityId, EntityId),
    Equal(EntityId, EntityId),
    Fix(EntityId),
    Midpoint {
        point: EntityId,
        line: EntityId,
    },
    Concentric(EntityId, EntityId),
    Collinear(EntityId, EntityId),
    Symmetric {
        a: EntityId,
        b: EntityId,
        axis: EntityId,
    },
    Distance {
        a: EntityId,
        b: EntityId,
        value: f64,
    },
    PointLineDistance {
        point: EntityId,
        line: EntityId,
        value: f64,
    },
    HorizontalDistance {
        a: EntityId,
        b: EntityId,
        value: f64,
    },
    VerticalDistance {
        a: EntityId,
        b: EntityId,
        value: f64,
    },
    Length {
        line: EntityId,
        value: f64,
    },
    /// Radians.
    Angle {
        a: EntityId,
        b: EntityId,
        value: f64,
    },
    Radius {
        curve: EntityId,
        value: f64,
    },
    Diameter {
        curve: EntityId,
        value: f64,
    },
}

impl SketchConstraint {
    /// Every entity the constraint refers to.
    pub fn entities(&self) -> Vec<EntityId> {
        use SketchConstraint::*;
        match *self {
            Horizontal(a)
            | Vertical(a)
            | Fix(a)
            | Length { line: a, .. }
            | Radius { curve: a, .. }
            | Diameter { curve: a, .. } => vec![a],
            Coincident(a, b)
            | HorizontalPoints(a, b)
            | VerticalPoints(a, b)
            | Parallel(a, b)
            | Perpendicular(a, b)
            | Tangent(a, b)
            | Equal(a, b)
            | Concentric(a, b)
            | Collinear(a, b)
            | PointOnCurve { point: a, curve: b }
            | Midpoint { point: a, line: b }
            | Distance { a, b, .. }
            | PointLineDistance {
                point: a, line: b, ..
            }
            | HorizontalDistance { a, b, .. }
            | VerticalDistance { a, b, .. }
            | Angle { a, b, .. } => vec![a, b],
            Symmetric { a, b, axis } => vec![a, b, axis],
        }
    }

    pub fn is_dimension(&self) -> bool {
        self.value().is_some()
    }

    /// The driving value of a dimension (mm or radians).
    pub fn value(&self) -> Option<f64> {
        use SketchConstraint::*;
        match *self {
            Distance { value, .. }
            | PointLineDistance { value, .. }
            | HorizontalDistance { value, .. }
            | VerticalDistance { value, .. }
            | Length { value, .. }
            | Angle { value, .. }
            | Radius { value, .. }
            | Diameter { value, .. } => Some(value),
            _ => None,
        }
    }

    fn set_value(&mut self, v: f64) -> bool {
        use SketchConstraint::*;
        match self {
            Distance { value, .. }
            | PointLineDistance { value, .. }
            | HorizontalDistance { value, .. }
            | VerticalDistance { value, .. }
            | Length { value, .. }
            | Angle { value, .. }
            | Radius { value, .. }
            | Diameter { value, .. } => {
                *value = v;
                true
            }
            _ => false,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum SketchError {
    UnknownEntity(EntityId),
    UnknownConstraint(ConstraintId),
    /// A point is still used by a curve or constraint.
    InUse(EntityId),
    /// The constraint doesn't apply to these entities.
    WrongKind(String),
    InvalidValue(String),
    /// Refused: the constraint contradicts these existing ones.
    Conflicts(Vec<ConstraintId>),
    NotADimension(ConstraintId),
}

impl fmt::Display for SketchError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SketchError::UnknownEntity(e) => write!(f, "sketch entity {} does not exist", e.0),
            SketchError::UnknownConstraint(c) => write!(f, "constraint {} does not exist", c.0),
            SketchError::InUse(e) => write!(f, "sketch entity {} is still in use", e.0),
            SketchError::WrongKind(m) => write!(f, "{m}"),
            SketchError::InvalidValue(m) => write!(f, "invalid value: {m}"),
            SketchError::Conflicts(ids) => {
                let list: Vec<String> = ids.iter().map(|c| c.0.to_string()).collect();
                write!(
                    f,
                    "this would over-constrain the sketch (conflicts with constraint {})",
                    list.join(", ")
                )
            }
            SketchError::NotADimension(c) => write!(f, "constraint {} is not a dimension", c.0),
        }
    }
}

impl std::error::Error for SketchError {}

/// Result of solving a sketch.
#[derive(Clone, Debug, PartialEq)]
pub struct SolveSummary {
    pub status: SolveStatus,
    /// Remaining degrees of freedom (0 = fully constrained).
    pub dof: usize,
    /// Entities that can no longer move (drawn black in the UI).
    pub fully_constrained: HashSet<EntityId>,
    pub conflicting: Vec<ConstraintId>,
    pub redundant: Vec<ConstraintId>,
}

impl SolveSummary {
    pub fn is_solved(&self) -> bool {
        self.status == SolveStatus::Solved
    }
    pub fn is_fully_constrained(&self) -> bool {
        self.is_solved() && self.dof == 0
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Sketch {
    pub name: String,
    pub plane: SketchPlane,
    entities: Vec<Entity>,
    constraints: Vec<(ConstraintId, SketchConstraint)>,
    next_id: u64,
}

impl Sketch {
    pub fn new(name: impl Into<String>, plane: SketchPlane) -> Self {
        Self {
            name: name.into(),
            plane,
            entities: Vec::new(),
            constraints: Vec::new(),
            next_id: 1,
        }
    }

    // ---- Queries -----------------------------------------------------------

    pub fn entities(&self) -> &[Entity] {
        &self.entities
    }

    pub fn entity(&self, id: EntityId) -> Option<&Entity> {
        self.entities.iter().find(|e| e.id == id)
    }

    pub fn constraints(&self) -> &[(ConstraintId, SketchConstraint)] {
        &self.constraints
    }

    pub fn constraint(&self, id: ConstraintId) -> Option<&SketchConstraint> {
        self.constraints
            .iter()
            .find(|(c, _)| *c == id)
            .map(|(_, k)| k)
    }

    /// Position of a point entity.
    pub fn point(&self, id: EntityId) -> Option<[f64; 2]> {
        match self.entity(id)?.geometry {
            Geometry::Point { x, y } => Some([x, y]),
            _ => None,
        }
    }

    /// Radius of a circle, or of an arc (distance centre–start).
    pub fn radius(&self, id: EntityId) -> Option<f64> {
        match self.entity(id)?.geometry {
            Geometry::Circle { radius, .. } => Some(radius),
            Geometry::Arc { centre, start, .. } => {
                let (c, s) = (self.point(centre)?, self.point(start)?);
                Some(((s[0] - c[0]).powi(2) + (s[1] - c[1]).powi(2)).sqrt())
            }
            _ => None,
        }
    }

    // ---- Geometry ----------------------------------------------------------

    pub fn add_point(&mut self, x: f64, y: f64) -> EntityId {
        self.push(Geometry::Point { x, y })
    }

    /// A line between two existing points (sharing a point joins lines).
    pub fn add_line(&mut self, start: EntityId, end: EntityId) -> Result<EntityId, SketchError> {
        self.require_point(start)?;
        self.require_point(end)?;
        if start == end {
            return Err(SketchError::InvalidValue(
                "a line needs two different points".into(),
            ));
        }
        Ok(self.push(Geometry::Line { start, end }))
    }

    /// A line with new end points.
    pub fn add_line_xy(&mut self, a: [f64; 2], b: [f64; 2]) -> EntityId {
        let start = self.add_point(a[0], a[1]);
        let end = self.add_point(b[0], b[1]);
        self.push(Geometry::Line { start, end })
    }

    pub fn add_circle(&mut self, centre: EntityId, radius: f64) -> Result<EntityId, SketchError> {
        self.require_point(centre)?;
        if radius <= 0.0 {
            return Err(SketchError::InvalidValue(
                "a circle needs a positive radius".into(),
            ));
        }
        Ok(self.push(Geometry::Circle { centre, radius }))
    }

    pub fn add_circle_xy(
        &mut self,
        centre: [f64; 2],
        radius: f64,
    ) -> Result<EntityId, SketchError> {
        let c = self.add_point(centre[0], centre[1]);
        self.add_circle(c, radius)
    }

    pub fn add_arc(
        &mut self,
        centre: EntityId,
        start: EntityId,
        end: EntityId,
    ) -> Result<EntityId, SketchError> {
        for p in [centre, start, end] {
            self.require_point(p)?;
        }
        if centre == start || centre == end || start == end {
            return Err(SketchError::InvalidValue(
                "an arc needs three different points".into(),
            ));
        }
        Ok(self.push(Geometry::Arc { centre, start, end }))
    }

    /// A rectangle from two opposite corners, as the Rectangle tool draws it:
    /// four lines sharing corner points, with horizontal and vertical
    /// constraints. Returns the lines bottom, right, top, left.
    pub fn add_rectangle(
        &mut self,
        a: [f64; 2],
        b: [f64; 2],
    ) -> Result<[EntityId; 4], SketchError> {
        let (x0, x1) = (a[0].min(b[0]), a[0].max(b[0]));
        let (y0, y1) = (a[1].min(b[1]), a[1].max(b[1]));
        if x1 - x0 <= 0.0 || y1 - y0 <= 0.0 {
            return Err(SketchError::InvalidValue(
                "a rectangle needs width and height".into(),
            ));
        }
        let p = [
            self.add_point(x0, y0),
            self.add_point(x1, y0),
            self.add_point(x1, y1),
            self.add_point(x0, y1),
        ];
        let lines = [
            self.add_line(p[0], p[1])?,
            self.add_line(p[1], p[2])?,
            self.add_line(p[2], p[3])?,
            self.add_line(p[3], p[0])?,
        ];
        self.add_constraint(SketchConstraint::Horizontal(lines[0]))?;
        self.add_constraint(SketchConstraint::Horizontal(lines[2]))?;
        self.add_constraint(SketchConstraint::Vertical(lines[1]))?;
        self.add_constraint(SketchConstraint::Vertical(lines[3]))?;
        Ok(lines)
    }

    pub fn set_construction(
        &mut self,
        id: EntityId,
        construction: bool,
    ) -> Result<(), SketchError> {
        let e = self
            .entities
            .iter_mut()
            .find(|e| e.id == id)
            .ok_or(SketchError::UnknownEntity(id))?;
        e.construction = construction;
        Ok(())
    }

    /// Removes an entity and every constraint that refers to it. Removing a
    /// curve also removes its defining points when nothing else uses them; a
    /// point still used by a curve can't be removed.
    pub fn remove_entity(&mut self, id: EntityId) -> Result<(), SketchError> {
        let entity = *self.entity(id).ok_or(SketchError::UnknownEntity(id))?;
        if matches!(entity.geometry, Geometry::Point { .. })
            && self
                .entities
                .iter()
                .any(|e| e.geometry.defining_points().contains(&id))
        {
            return Err(SketchError::InUse(id));
        }
        self.constraints
            .retain(|(_, c)| !c.entities().contains(&id));
        self.entities.retain(|e| e.id != id);
        for p in entity.geometry.defining_points() {
            let used = self
                .entities
                .iter()
                .any(|e| e.geometry.defining_points().contains(&p))
                || self
                    .constraints
                    .iter()
                    .any(|(_, c)| c.entities().contains(&p));
            if !used {
                self.entities.retain(|e| e.id != p);
            }
        }
        Ok(())
    }

    // ---- Constraints -------------------------------------------------------

    /// Adds a constraint the way the sketch UI does: refused with
    /// [`SketchError::Conflicts`] if it would contradict existing ones.
    /// On success the sketch is re-solved.
    pub fn add_constraint(
        &mut self,
        constraint: SketchConstraint,
    ) -> Result<ConstraintId, SketchError> {
        self.validate(&constraint)?;
        let mut built = self.build_solver_with_constraints()?;
        built.solver.solve();
        let rule = built.rule(&constraint)?;
        match built.solver.try_add(rule).map_err(solver_error)? {
            Ok(_) => {
                // The id is only taken once accepted: a refusal leaves no trace.
                let id = ConstraintId(self.take_id());
                self.constraints.push((id, constraint));
                self.solve();
                Ok(id)
            }
            Err(conflicts) => Err(SketchError::Conflicts(built.constraint_ids(&conflicts))),
        }
    }

    /// Adds a constraint without checking for conflicts (loading files).
    pub fn add_constraint_unchecked(
        &mut self,
        constraint: SketchConstraint,
    ) -> Result<ConstraintId, SketchError> {
        self.validate(&constraint)?;
        let id = ConstraintId(self.take_id());
        self.constraints.push((id, constraint));
        Ok(id)
    }

    pub fn remove_constraint(&mut self, id: ConstraintId) -> Result<(), SketchError> {
        let before = self.constraints.len();
        self.constraints.retain(|(c, _)| *c != id);
        if self.constraints.len() == before {
            return Err(SketchError::UnknownConstraint(id));
        }
        Ok(())
    }

    /// Changes a dimension and re-solves. Restores the old value if the
    /// sketch can't be solved with the new one.
    pub fn set_dimension(
        &mut self,
        id: ConstraintId,
        value: f64,
    ) -> Result<SolveSummary, SketchError> {
        let slot = self
            .constraints
            .iter_mut()
            .find(|(c, _)| *c == id)
            .ok_or(SketchError::UnknownConstraint(id))?;
        let old = slot.1.value().ok_or(SketchError::NotADimension(id))?;
        if !value.is_finite() || (value <= 0.0 && !matches!(slot.1, SketchConstraint::Angle { .. }))
        {
            return Err(SketchError::InvalidValue(
                "dimensions must be positive".into(),
            ));
        }
        slot.1.set_value(value);
        let saved = self.entities.clone();
        let summary = self.solve();
        if !summary.is_solved() {
            let slot = self
                .constraints
                .iter_mut()
                .find(|(c, _)| *c == id)
                .expect("still present");
            slot.1.set_value(old);
            self.entities = saved;
            self.solve();
        }
        Ok(summary)
    }

    // ---- Solving -----------------------------------------------------------

    /// Solves every constraint and stores the resulting positions.
    pub fn solve(&mut self) -> SolveSummary {
        let Ok(mut built) = self.build_solver_with_constraints() else {
            return SolveSummary {
                status: SolveStatus::NotConverged,
                dof: 0,
                fully_constrained: HashSet::new(),
                conflicting: vec![],
                redundant: vec![],
            };
        };
        let report = built.solver.solve();
        self.write_back(&built);
        built.summary(report.status)
    }

    /// Drags a point toward `target` while every constraint keeps holding.
    pub fn drag_point(
        &mut self,
        id: EntityId,
        target: [f64; 2],
    ) -> Result<SolveSummary, SketchError> {
        self.require_point(id)?;
        let mut built = self.build_solver_with_constraints()?;
        built.solver.solve();
        let report = built
            .solver
            .drag_point(built.points[&id], target)
            .map_err(solver_error)?;
        self.write_back(&built);
        Ok(built.summary(report.status))
    }

    // ---- Internals ---------------------------------------------------------

    fn take_id(&mut self) -> u64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn push(&mut self, geometry: Geometry) -> EntityId {
        let id = EntityId(self.take_id());
        self.entities.push(Entity {
            id,
            geometry,
            construction: false,
        });
        id
    }

    fn require_point(&self, id: EntityId) -> Result<(), SketchError> {
        match self.entity(id) {
            Some(Entity {
                geometry: Geometry::Point { .. },
                ..
            }) => Ok(()),
            Some(e) => Err(SketchError::WrongKind(format!(
                "entity {} is a {}, not a point",
                id.0,
                e.geometry.kind_name()
            ))),
            None => Err(SketchError::UnknownEntity(id)),
        }
    }

    fn validate(&self, c: &SketchConstraint) -> Result<(), SketchError> {
        for e in c.entities() {
            if self.entity(e).is_none() {
                return Err(SketchError::UnknownEntity(e));
            }
        }
        if let Some(v) = c.value() {
            let angle = matches!(c, SketchConstraint::Angle { .. });
            if !v.is_finite() || (!angle && v <= 0.0) {
                return Err(SketchError::InvalidValue(
                    "dimensions must be positive".into(),
                ));
            }
        }
        Ok(())
    }

    fn build_solver(&self) -> Result<Built, SketchError> {
        let mut b = Built::default();
        for e in &self.entities {
            if let Geometry::Point { x, y } = e.geometry {
                b.points.insert(e.id, b.solver.add_point(x, y));
            }
        }
        for e in &self.entities {
            let curve = match e.geometry {
                Geometry::Point { .. } => continue,
                Geometry::Line { start, end } => {
                    b.solver.add_line(b.points[&start], b.points[&end])
                }
                Geometry::Circle { centre, radius } => {
                    b.solver.add_circle(b.points[&centre], radius)
                }
                Geometry::Arc { centre, start, end } => {
                    b.solver
                        .add_arc(b.points[&centre], b.points[&start], b.points[&end])
                }
            }
            .map_err(solver_error)?;
            b.curves.insert(e.id, curve);
        }
        Ok(b)
    }

    fn build_solver_with_constraints(&self) -> Result<Built, SketchError> {
        let mut b = self.build_solver()?;
        for (id, c) in &self.constraints {
            let rule = b.rule(c)?;
            let r = b.solver.add(rule).map_err(solver_error)?;
            b.rules.push((r, *id));
        }
        Ok(b)
    }

    fn write_back(&mut self, b: &Built) {
        for e in &mut self.entities {
            match &mut e.geometry {
                Geometry::Point { x, y } => {
                    if let Some(p) = b.points.get(&e.id).and_then(|&r| b.solver.point(r)) {
                        (*x, *y) = (p[0], p[1]);
                    }
                }
                Geometry::Circle { radius, .. } => {
                    if let Some(r) = b.curves.get(&e.id).and_then(|&c| b.solver.radius(c)) {
                        *radius = r;
                    }
                }
                _ => {}
            }
        }
    }
}

/// A solver filled from a sketch, with the id maps both ways.
#[derive(Default)]
struct Built {
    solver: ConstraintSolver,
    points: HashMap<EntityId, PointRef>,
    curves: HashMap<EntityId, CurveRef>,
    rules: Vec<(RuleRef, ConstraintId)>,
}

impl Built {
    fn p(&self, id: EntityId) -> Result<PointRef, SketchError> {
        self.points
            .get(&id)
            .copied()
            .ok_or_else(|| SketchError::WrongKind(format!("entity {} must be a point", id.0)))
    }

    fn c(&self, id: EntityId) -> Result<CurveRef, SketchError> {
        self.curves.get(&id).copied().ok_or_else(|| {
            SketchError::WrongKind(format!("entity {} must be a line, circle or arc", id.0))
        })
    }

    fn rule(&self, c: &SketchConstraint) -> Result<Rule, SketchError> {
        use SketchConstraint as S;
        Ok(match *c {
            S::Coincident(a, b) => Rule::Coincident(self.p(a)?, self.p(b)?),
            S::PointOnCurve { point, curve } => Rule::PointOnCurve(self.p(point)?, self.c(curve)?),
            S::Horizontal(e) => Rule::Horizontal(self.c(e)?),
            S::Vertical(e) => Rule::Vertical(self.c(e)?),
            S::HorizontalPoints(a, b) => Rule::HorizontalPoints(self.p(a)?, self.p(b)?),
            S::VerticalPoints(a, b) => Rule::VerticalPoints(self.p(a)?, self.p(b)?),
            S::Parallel(a, b) => Rule::Parallel(self.c(a)?, self.c(b)?),
            S::Perpendicular(a, b) => Rule::Perpendicular(self.c(a)?, self.c(b)?),
            S::Tangent(a, b) => Rule::Tangent(self.c(a)?, self.c(b)?),
            S::Equal(a, b) => Rule::Equal(self.c(a)?, self.c(b)?),
            S::Fix(p) => Rule::Fix(self.p(p)?),
            S::Midpoint { point, line } => Rule::Midpoint(self.p(point)?, self.c(line)?),
            S::Concentric(a, b) => Rule::Concentric(self.c(a)?, self.c(b)?),
            S::Collinear(a, b) => Rule::Collinear(self.c(a)?, self.c(b)?),
            S::Symmetric { a, b, axis } => Rule::Symmetric {
                a: self.p(a)?,
                b: self.p(b)?,
                axis: self.c(axis)?,
            },
            S::Distance { a, b, value } => Rule::Distance {
                a: self.p(a)?,
                b: self.p(b)?,
                value,
            },
            S::PointLineDistance { point, line, value } => Rule::PointLineDistance {
                point: self.p(point)?,
                line: self.c(line)?,
                value,
            },
            S::HorizontalDistance { a, b, value } => Rule::HorizontalDistance {
                a: self.p(a)?,
                b: self.p(b)?,
                value,
            },
            S::VerticalDistance { a, b, value } => Rule::VerticalDistance {
                a: self.p(a)?,
                b: self.p(b)?,
                value,
            },
            S::Length { line, value } => Rule::Length {
                line: self.c(line)?,
                value,
            },
            S::Angle { a, b, value } => Rule::Angle {
                a: self.c(a)?,
                b: self.c(b)?,
                value,
            },
            S::Radius { curve, value } => Rule::Radius {
                curve: self.c(curve)?,
                value,
            },
            S::Diameter { curve, value } => Rule::Diameter {
                curve: self.c(curve)?,
                value,
            },
        })
    }

    fn constraint_ids(&self, rules: &[RuleRef]) -> Vec<ConstraintId> {
        let mut ids: Vec<ConstraintId> = rules
            .iter()
            .filter_map(|r| self.rules.iter().find(|(x, _)| x == r).map(|(_, id)| *id))
            .collect();
        ids.sort_unstable();
        ids.dedup();
        ids
    }

    fn summary(&mut self, status: SolveStatus) -> SolveSummary {
        let freedom = self.solver.analyze();
        let mut fully = HashSet::new();
        for (id, p) in &self.points {
            if freedom.fully_constrained_points.contains(p) {
                fully.insert(*id);
            }
        }
        for (id, c) in &self.curves {
            if freedom.fully_constrained_curves.contains(c) {
                fully.insert(*id);
            }
        }
        SolveSummary {
            status,
            dof: freedom.dof,
            fully_constrained: fully,
            conflicting: self.constraint_ids(&freedom.conflicting),
            redundant: self.constraint_ids(&freedom.redundant),
        }
    }
}

fn solver_error(e: SolverError) -> SketchError {
    match e {
        SolverError::WrongKind(expected) => {
            SketchError::WrongKind(format!("this constraint needs {expected}"))
        }
        SolverError::InvalidValue(what) => SketchError::InvalidValue(what.to_string()),
        other => SketchError::InvalidValue(other.to_string()),
    }
}

#[cfg(test)]
#[path = "model_tests.rs"]
mod tests;
