//! The sketch: geometry and constraints with stable ids, on a plane.
//!
//! Geometry and constraints are stored by id; every solve feeds them through
//! the constraint solver ([`super::solver_adapter`]) and writes the solved
//! positions back. Ids are never reused, so dimensions, profiles and later
//! features keep pointing at the right thing while the sketch is edited.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::fmt;

use super::plane::SketchPlane;
pub use super::solver_adapter::SplineData;
use super::solver_adapter::{
    ConstraintSolver, CurveRef, PointRef, Rule, RuleRef, SolveStatus, SolverError,
};

/// A point, line, circle or arc in a sketch. Stable for the sketch's life.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct EntityId(pub u64);

/// A constraint or dimension in a sketch. Stable for the sketch's life.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ConstraintId(pub u64);

#[derive(Clone, Debug, PartialEq)]
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
    /// Ellipse: `major` is the end point of the major axis.
    Ellipse {
        centre: EntityId,
        major: EntityId,
        minor_radius: f64,
    },
    /// Elliptical arc, counter-clockwise from `start` to `end`.
    EllipticalArc {
        centre: EntityId,
        major: EntityId,
        minor_radius: f64,
        start: EntityId,
        end: EntityId,
    },
    /// Cubic spline through its fit points (closed: periodic).
    Spline {
        points: Vec<EntityId>,
        closed: bool,
    },
}

impl Geometry {
    pub fn kind_name(&self) -> &'static str {
        match self {
            Geometry::Point { .. } => "point",
            Geometry::Line { .. } => "line",
            Geometry::Circle { .. } => "circle",
            Geometry::Arc { .. } => "arc",
            Geometry::Ellipse { .. } => "ellipse",
            Geometry::EllipticalArc { .. } => "elliptical arc",
            Geometry::Spline { .. } => "spline",
        }
    }

    /// Points this geometry is defined by.
    pub fn defining_points(&self) -> Vec<EntityId> {
        match self {
            Geometry::Point { .. } => vec![],
            Geometry::Line { start, end } => vec![*start, *end],
            Geometry::Circle { centre, .. } => vec![*centre],
            Geometry::Arc { centre, start, end } => vec![*centre, *start, *end],
            Geometry::Ellipse { centre, major, .. } => vec![*centre, *major],
            Geometry::EllipticalArc {
                centre,
                major,
                start,
                end,
                ..
            } => vec![*centre, *major, *start, *end],
            Geometry::Spline { points, .. } => points.clone(),
        }
    }

    /// End points of an open curve (start, end), if it has them.
    pub fn ends(&self) -> Option<(EntityId, EntityId)> {
        match self {
            Geometry::Line { start, end }
            | Geometry::Arc { start, end, .. }
            | Geometry::EllipticalArc { start, end, .. } => Some((*start, *end)),
            Geometry::Spline {
                points,
                closed: false,
            } if points.len() >= 2 => Some((points[0], points[points.len() - 1])),
            _ => None,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
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
    /// Curvature continuity (G2) where a spline meets another curve.
    Smooth(EntityId, EntityId),
    /// Distance between two parallel lines.
    LineDistance {
        a: EntityId,
        b: EntityId,
        value: f64,
    },
    ArcLength {
        arc: EntityId,
        value: f64,
    },
    /// Gap between a point and the edge of a circle or arc.
    PointCircleDistance {
        point: EntityId,
        circle: EntityId,
        value: f64,
        inside: bool,
    },
    /// Gap between a line and the edge of a circle or arc.
    LineCircleDistance {
        line: EntityId,
        circle: EntityId,
        value: f64,
    },
    /// Gap between the edges of two circles or arcs.
    CircleDistance {
        a: EntityId,
        b: EntityId,
        value: f64,
        inside: bool,
    },
    MajorRadius {
        ellipse: EntityId,
        value: f64,
    },
    MinorRadius {
        ellipse: EntityId,
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
            | Diameter { curve: a, .. }
            | ArcLength { arc: a, .. }
            | MajorRadius { ellipse: a, .. }
            | MinorRadius { ellipse: a, .. } => vec![a],
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
            | Angle { a, b, .. }
            | Smooth(a, b)
            | LineDistance { a, b, .. }
            | PointCircleDistance {
                point: a,
                circle: b,
                ..
            }
            | LineCircleDistance {
                line: a, circle: b, ..
            }
            | CircleDistance { a, b, .. } => vec![a, b],
            Symmetric { a, b, axis } => vec![a, b, axis],
        }
    }

    pub fn is_dimension(&self) -> bool {
        self.value().is_some()
    }

    /// Length-like dimensions (not angles): the ones that may scale a sketch.
    pub fn is_length(&self) -> bool {
        self.is_dimension() && !matches!(self, SketchConstraint::Angle { .. })
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
            | Diameter { value, .. }
            | LineDistance { value, .. }
            | ArcLength { value, .. }
            | PointCircleDistance { value, .. }
            | LineCircleDistance { value, .. }
            | CircleDistance { value, .. }
            | MajorRadius { value, .. }
            | MinorRadius { value, .. } => Some(value),
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
            | Diameter { value, .. }
            | LineDistance { value, .. }
            | ArcLength { value, .. }
            | PointCircleDistance { value, .. }
            | LineCircleDistance { value, .. }
            | CircleDistance { value, .. }
            | MajorRadius { value, .. }
            | MinorRadius { value, .. } => {
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
    /// Refused: the other constraints already imply it (a dimension can be
    /// added as a driven one instead).
    Redundant,
    /// Refused: solving it would shrink these curves to nothing.
    Collapses(Vec<EntityId>),
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
            SketchError::Redundant => {
                write!(
                    f,
                    "the other constraints already set this (it would be redundant)"
                )
            }
            SketchError::Collapses(ids) => {
                let n = ids.len();
                write!(
                    f,
                    "that would shrink {} to nothing",
                    if n == 1 {
                        "a curve".to_string()
                    } else {
                        format!("{n} curves")
                    }
                )
            }
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
    /// Curves that collapsed (zero-length line, zero radius).
    pub degenerate: Vec<EntityId>,
}

impl SolveSummary {
    pub fn is_solved(&self) -> bool {
        self.status == SolveStatus::Solved
    }
    pub fn is_fully_constrained(&self) -> bool {
        self.is_solved() && self.dof == 0
    }
}

/// How a group of copies was made, so it can be made again with new values.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum CopyKind {
    Rect {
        dir: [f64; 2],
        nx: usize,
        ny: usize,
        sx: f64,
        sy: f64,
    },
    Circ {
        centre: [f64; 2],
        count: usize,
        angle: f64,
    },
    Mirror {
        axis: EntityId,
    },
}

/// A pattern or mirror: the curves it copies and the copies it made.
#[derive(Clone, Debug, PartialEq)]
pub struct CopyGroup {
    pub id: u64,
    pub kind: CopyKind,
    pub sources: Vec<EntityId>,
    pub copies: Vec<EntityId>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Sketch {
    pub name: String,
    pub plane: SketchPlane,
    entities: Vec<Entity>,
    constraints: Vec<(ConstraintId, SketchConstraint)>,
    /// Dimensions that only measure (reference dimensions): not solved.
    driven: BTreeSet<ConstraintId>,
    /// Splines as last solved, for drawing and profiles.
    splines: BTreeMap<EntityId, SplineData>,
    /// Where each dimension's label was placed, relative to its anchor.
    labels: BTreeMap<ConstraintId, [f64; 2]>,
    /// Patterns and mirrors that can still be edited.
    groups: Vec<CopyGroup>,
    next_id: u64,
}

impl Sketch {
    pub fn new(name: impl Into<String>, plane: SketchPlane) -> Self {
        Self {
            name: name.into(),
            plane,
            entities: Vec::new(),
            constraints: Vec::new(),
            driven: BTreeSet::new(),
            splines: BTreeMap::new(),
            labels: BTreeMap::new(),
            groups: Vec::new(),
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

    /// Major and minor radius of an ellipse or elliptical arc.
    pub fn ellipse_radii(&self, id: EntityId) -> Option<(f64, f64)> {
        match &self.entity(id)?.geometry {
            Geometry::Ellipse {
                centre,
                major,
                minor_radius,
            }
            | Geometry::EllipticalArc {
                centre,
                major,
                minor_radius,
                ..
            } => {
                let (c, m) = (self.point(*centre)?, self.point(*major)?);
                Some((
                    ((m[0] - c[0]).powi(2) + (m[1] - c[1]).powi(2)).sqrt(),
                    *minor_radius,
                ))
            }
            _ => None,
        }
    }

    /// The spline's shape as last solved.
    pub fn spline(&self, id: EntityId) -> Option<&SplineData> {
        self.splines.get(&id)
    }

    /// True for a driven (reference) dimension, which follows the geometry.
    pub fn is_driven(&self, id: ConstraintId) -> bool {
        self.driven.contains(&id)
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

    /// An ellipse from its centre, the end of its major axis and its minor radius.
    pub fn add_ellipse(
        &mut self,
        centre: EntityId,
        major: EntityId,
        minor_radius: f64,
    ) -> Result<EntityId, SketchError> {
        self.require_point(centre)?;
        self.require_point(major)?;
        if centre == major || !positive(minor_radius, 0.0) {
            return Err(SketchError::InvalidValue(
                "an ellipse needs two positive radii".into(),
            ));
        }
        Ok(self.push(Geometry::Ellipse {
            centre,
            major,
            minor_radius,
        }))
    }

    /// An elliptical arc, counter-clockwise from `start` to `end`.
    pub fn add_elliptical_arc(
        &mut self,
        centre: EntityId,
        major: EntityId,
        minor_radius: f64,
        start: EntityId,
        end: EntityId,
    ) -> Result<EntityId, SketchError> {
        for p in [centre, major, start, end] {
            self.require_point(p)?;
        }
        if centre == major || start == end || !positive(minor_radius, 0.0) {
            return Err(SketchError::InvalidValue(
                "an elliptical arc needs two radii and two different ends".into(),
            ));
        }
        Ok(self.push(Geometry::EllipticalArc {
            centre,
            major,
            minor_radius,
            start,
            end,
        }))
    }

    /// A spline through fit points (at least two; three when closed).
    pub fn add_spline(
        &mut self,
        points: &[EntityId],
        closed: bool,
    ) -> Result<EntityId, SketchError> {
        for &p in points {
            self.require_point(p)?;
        }
        let distinct: BTreeSet<EntityId> = points.iter().copied().collect();
        if points.len() < 2 || (closed && points.len() < 3) || distinct.len() != points.len() {
            return Err(SketchError::InvalidValue(
                "a spline needs at least two different points (three when closed)".into(),
            ));
        }
        let id = self.push(Geometry::Spline {
            points: points.to_vec(),
            closed,
        });
        self.solve(); // fills the spline's shape
        Ok(id)
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
        let entity = self
            .entity(id)
            .ok_or(SketchError::UnknownEntity(id))?
            .clone();
        if matches!(entity.geometry, Geometry::Point { .. })
            && self
                .entities
                .iter()
                .any(|e| e.geometry.defining_points().contains(&id))
        {
            return Err(SketchError::InUse(id));
        }
        let driven = &mut self.driven;
        self.constraints.retain(|(cid, c)| {
            let keep = !c.entities().contains(&id);
            if !keep {
                driven.remove(cid);
            }
            keep
        });
        self.splines.remove(&id);
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
    /// [`SketchError::Conflicts`] if it would contradict existing ones,
    /// [`SketchError::Redundant`] if they already imply it, and
    /// [`SketchError::Collapses`] if it would shrink a curve to nothing. The
    /// first dimension of a sketch scales the whole sketch to it (as Fusion
    /// does). On success the sketch is re-solved.
    pub fn add_constraint(
        &mut self,
        constraint: SketchConstraint,
    ) -> Result<ConstraintId, SketchError> {
        self.validate(&constraint)?;
        let constraint = self.with_sides(constraint);
        let saved = self.entities.clone();
        self.pre_orient(&constraint);
        self.scale_for_first_dimension(&constraint);
        match self.check_add(&constraint) {
            Ok(()) => {
                // The id is only taken once accepted: a refusal leaves no trace.
                let id = ConstraintId(self.take_id());
                self.constraints.push((id, constraint));
                self.solve();
                Ok(id)
            }
            Err(e) => {
                self.entities = saved;
                Err(e)
            }
        }
    }

    /// Adds a dimension that only measures (a reference dimension): it follows
    /// the geometry and is never refused.
    pub fn add_driven(
        &mut self,
        constraint: SketchConstraint,
    ) -> Result<ConstraintId, SketchError> {
        self.validate_entities(&constraint)?;
        if !constraint.is_dimension() {
            return Err(SketchError::NotADimension(ConstraintId(0)));
        }
        let mut constraint = self.with_sides(constraint);
        if let Some(v) = self.measure(&constraint) {
            constraint.set_value(v);
        }
        let id = ConstraintId(self.take_id());
        self.constraints.push((id, constraint));
        self.driven.insert(id);
        Ok(id)
    }

    /// Switches a dimension between driving and driven. Making it driving is
    /// checked like adding it.
    pub fn set_driven(&mut self, id: ConstraintId, driven: bool) -> Result<(), SketchError> {
        let c = *self
            .constraint(id)
            .ok_or(SketchError::UnknownConstraint(id))?;
        if !c.is_dimension() {
            return Err(SketchError::NotADimension(id));
        }
        if driven {
            self.driven.insert(id);
            self.solve();
            return Ok(());
        }
        if !self.driven.contains(&id) {
            return Ok(());
        }
        let saved = self.entities.clone();
        let pos = self
            .constraints
            .iter()
            .position(|(c, _)| *c == id)
            .expect("present");
        let entry = self.constraints.remove(pos);
        self.driven.remove(&id);
        let result = self.check_add(&entry.1);
        self.constraints.insert(pos, entry);
        if let Err(e) = result {
            self.driven.insert(id);
            self.entities = saved;
            return Err(e);
        }
        self.solve();
        Ok(())
    }

    /// Would `constraint` be accepted? (Leaves the sketch as it was.)
    fn check_add(&self, constraint: &SketchConstraint) -> Result<(), SketchError> {
        let mut built = self.build_solver_with_constraints()?;
        let before = built.solver.solve();
        let rule = built.rule(constraint)?;
        match built.solver.try_add(rule).map_err(solver_error)? {
            Ok(r) => {
                let after = built.solver.solve();
                let collapsed: Vec<CurveRef> = after
                    .degenerate
                    .iter()
                    .filter(|c| !before.degenerate.contains(c))
                    .copied()
                    .collect();
                if !collapsed.is_empty() {
                    return Err(SketchError::Collapses(built.entity_ids(&collapsed)));
                }
                if built.solver.analyze().redundant.contains(&r) {
                    return Err(SketchError::Redundant);
                }
                Ok(())
            }
            Err(conflicts) => Err(SketchError::Conflicts(built.constraint_ids(&conflicts))),
        }
    }

    /// Fills in which side a gap dimension measures from the geometry now.
    fn with_sides(&self, c: SketchConstraint) -> SketchConstraint {
        match c {
            SketchConstraint::PointCircleDistance {
                point,
                circle,
                value,
                ..
            } => {
                let inside = match (
                    self.point(point),
                    self.centre_of(circle),
                    self.radius(circle),
                ) {
                    (Some(p), Some(cc), Some(r)) => dist2(p, cc) < r,
                    _ => false,
                };
                SketchConstraint::PointCircleDistance {
                    point,
                    circle,
                    value,
                    inside,
                }
            }
            SketchConstraint::CircleDistance { a, b, value, .. } => {
                let inside = match (
                    self.centre_of(a),
                    self.centre_of(b),
                    self.radius(a),
                    self.radius(b),
                ) {
                    (Some(ca), Some(cb), Some(ra), Some(rb)) => dist2(ca, cb) < (ra - rb).abs(),
                    _ => false,
                };
                SketchConstraint::CircleDistance {
                    a,
                    b,
                    value,
                    inside,
                }
            }
            other => other,
        }
    }

    fn centre_of(&self, id: EntityId) -> Option<[f64; 2]> {
        match &self.entity(id)?.geometry {
            Geometry::Circle { centre, .. }
            | Geometry::Arc { centre, .. }
            | Geometry::Ellipse { centre, .. }
            | Geometry::EllipticalArc { centre, .. } => self.point(*centre),
            _ => None,
        }
    }

    fn line_ends(&self, id: EntityId) -> Option<([f64; 2], [f64; 2])> {
        match &self.entity(id)?.geometry {
            Geometry::Line { start, end } => Some((self.point(*start)?, self.point(*end)?)),
            _ => None,
        }
    }

    /// What a dimension measures in the geometry now (mm or radians).
    pub fn measure(&self, c: &SketchConstraint) -> Option<f64> {
        use SketchConstraint as S;
        let point_line = |p: [f64; 2], (a, b): ([f64; 2], [f64; 2])| {
            let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
            let len = dx.hypot(dy);
            (len > 0.0).then(|| ((p[0] - a[0]) * dy - (p[1] - a[1]) * dx).abs() / len)
        };
        match *c {
            S::Distance { a, b, .. } => Some(dist2(self.point(a)?, self.point(b)?)),
            S::HorizontalDistance { a, b, .. } => {
                Some((self.point(b)?[0] - self.point(a)?[0]).abs())
            }
            S::VerticalDistance { a, b, .. } => Some((self.point(b)?[1] - self.point(a)?[1]).abs()),
            S::PointLineDistance { point, line, .. } => {
                point_line(self.point(point)?, self.line_ends(line)?)
            }
            S::Length { line, .. } => {
                let (a, b) = self.line_ends(line)?;
                Some(dist2(a, b))
            }
            S::Angle { a, b, .. } => {
                let ((a0, a1), (b0, b1)) = (self.line_ends(a)?, self.line_ends(b)?);
                let t1 = (a1[1] - a0[1]).atan2(a1[0] - a0[0]);
                let t2 = (b1[1] - b0[1]).atan2(b1[0] - b0[0]);
                let mut d = (t2 - t1).abs() % std::f64::consts::TAU;
                if d > std::f64::consts::PI {
                    d = std::f64::consts::TAU - d;
                }
                Some(d)
            }
            S::Radius { curve, .. } => self.radius(curve),
            S::Diameter { curve, .. } => Some(2.0 * self.radius(curve)?),
            S::LineDistance { a, b, .. } => {
                let (b0, b1) = self.line_ends(b)?;
                point_line(
                    [(b0[0] + b1[0]) / 2.0, (b0[1] + b1[1]) / 2.0],
                    self.line_ends(a)?,
                )
            }
            S::ArcLength { arc, .. } => match self.entity(arc)?.geometry {
                Geometry::Arc { centre, start, end } => {
                    let (c, s, e) = (self.point(centre)?, self.point(start)?, self.point(end)?);
                    let a0 = (s[1] - c[1]).atan2(s[0] - c[0]);
                    let a1 = (e[1] - c[1]).atan2(e[0] - c[0]);
                    Some(dist2(c, s) * (a1 - a0).rem_euclid(std::f64::consts::TAU))
                }
                _ => None,
            },
            S::PointCircleDistance {
                point,
                circle,
                inside,
                ..
            } => {
                let d = dist2(self.point(point)?, self.centre_of(circle)?);
                let r = self.radius(circle)?;
                Some(if inside { r - d } else { d - r })
            }
            S::LineCircleDistance { line, circle, .. } => Some(
                point_line(self.centre_of(circle)?, self.line_ends(line)?)?
                    - self.radius(circle)?,
            ),
            S::CircleDistance { a, b, inside, .. } => {
                let d = dist2(self.centre_of(a)?, self.centre_of(b)?);
                let (ra, rb) = (self.radius(a)?, self.radius(b)?);
                Some(if inside {
                    ra.max(rb) - ra.min(rb) - d
                } else {
                    d - ra - rb
                })
            }
            S::MajorRadius { ellipse, .. } => Some(self.ellipse_radii(ellipse)?.0),
            S::MinorRadius { ellipse, .. } => Some(self.ellipse_radii(ellipse)?.1),
            _ => None,
        }
    }

    /// The first dimension of a sketch scales everything to it, about the
    /// sketch origin, so a rough sketch keeps its proportions (Fusion's
    /// behaviour). Skipped once any dimension drives the sketch, or when a
    /// point is fixed away from the origin.
    fn scale_for_first_dimension(&mut self, c: &SketchConstraint) {
        if !c.is_length() {
            return;
        }
        let has_dimension = self
            .constraints
            .iter()
            .any(|(id, k)| k.is_dimension() && !self.driven.contains(id));
        let fixed_elsewhere = self.constraints.iter().any(|(_, k)| match *k {
            SketchConstraint::Fix(p) => match self.point(p) {
                Some(xy) => xy[0].abs() > 1e-9 || xy[1].abs() > 1e-9,
                None => true, // a fixed curve
            },
            _ => false,
        });
        if has_dimension || fixed_elsewhere {
            return;
        }
        let (Some(now), Some(target)) = (self.measure(c), c.value()) else {
            return;
        };
        if !positive(now, 1e-9) {
            return;
        }
        let f = target / now;
        if !f.is_finite() || (f - 1.0).abs() < 1e-9 || !(1e-3..=1e3).contains(&f) {
            return;
        }
        for e in &mut self.entities {
            match &mut e.geometry {
                Geometry::Point { x, y } => {
                    *x *= f;
                    *y *= f;
                }
                Geometry::Circle { radius, .. } => *radius *= f,
                Geometry::Ellipse { minor_radius, .. }
                | Geometry::EllipticalArc { minor_radius, .. } => *minor_radius *= f,
                _ => {}
            }
        }
    }

    /// Turns one line of a Parallel/Perpendicular pair into place before
    /// solving, the way interactive CAD does. Without it the solver may meet
    /// the constraint by shrinking a line to nothing (its residual is
    /// `unit(a) · (b2 - b1)`). The second line turns, unless it is held
    /// (horizontal, vertical or a fixed end) and the first is not.
    fn pre_orient(&mut self, constraint: &SketchConstraint) {
        let (a, b, perpendicular) = match *constraint {
            SketchConstraint::Parallel(a, b) => (a, b, false),
            SketchConstraint::Perpendicular(a, b) => (a, b, true),
            _ => return,
        };
        let ends = |s: &Self, id: EntityId| match s.entity(id).map(|e| e.geometry.clone()) {
            Some(Geometry::Line { start, end }) => Some((start, end)),
            _ => None,
        };
        let (Some(la), Some(lb)) = (ends(self, a), ends(self, b)) else {
            return;
        };
        let held = |s: &Self, line: EntityId, (p, q): (EntityId, EntityId)| {
            s.constraints.iter().any(|(_, c)| match *c {
                SketchConstraint::Horizontal(l) | SketchConstraint::Vertical(l) => l == line,
                SketchConstraint::Fix(x) => x == p || x == q,
                _ => false,
            })
        };
        let (reference, moving) = if held(self, b, lb) && !held(self, a, la) {
            (lb, la)
        } else {
            (la, lb)
        };
        let (Some(r0), Some(r1), Some(m0), Some(m1)) = (
            self.point(reference.0),
            self.point(reference.1),
            self.point(moving.0),
            self.point(moving.1),
        ) else {
            return;
        };
        let (rx, ry) = (r1[0] - r0[0], r1[1] - r0[1]);
        let rlen = rx.hypot(ry);
        let (mx, my) = (m1[0] - m0[0], m1[1] - m0[1]);
        let mlen = mx.hypot(my);
        if rlen < 1e-12 || mlen < 1e-12 {
            return;
        }
        let (mut tx, mut ty) = if perpendicular {
            (-ry / rlen, rx / rlen)
        } else {
            (rx / rlen, ry / rlen)
        };
        if tx * mx + ty * my < 0.0 {
            (tx, ty) = (-tx, -ty); // keep the line's own direction
        }
        let mid = [(m0[0] + m1[0]) / 2.0, (m0[1] + m1[1]) / 2.0];
        let half = mlen / 2.0;
        self.set_point(moving.0, [mid[0] - tx * half, mid[1] - ty * half]);
        self.set_point(moving.1, [mid[0] + tx * half, mid[1] + ty * half]);
    }

    fn set_point(&mut self, id: EntityId, p: [f64; 2]) {
        if let Some(e) = self.entities.iter_mut().find(|e| e.id == id) {
            if let Geometry::Point { x, y } = &mut e.geometry {
                (*x, *y) = (p[0], p[1]);
            }
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

    /// Where a dimension's label sits, relative to its anchor (see the UI).
    pub fn label(&self, id: ConstraintId) -> Option<[f64; 2]> {
        self.labels.get(&id).copied()
    }

    pub fn set_label(&mut self, id: ConstraintId, offset: [f64; 2]) -> Result<(), SketchError> {
        if !self.constraints.iter().any(|(c, _)| *c == id) {
            return Err(SketchError::UnknownConstraint(id));
        }
        self.labels.insert(id, offset);
        Ok(())
    }

    pub fn remove_constraint(&mut self, id: ConstraintId) -> Result<(), SketchError> {
        let before = self.constraints.len();
        self.labels.remove(&id);
        self.constraints.retain(|(c, _)| *c != id);
        if self.constraints.len() == before {
            return Err(SketchError::UnknownConstraint(id));
        }
        Ok(())
    }

    /// Changes a dimension and re-solves. Restores the old value if the
    /// sketch can't be solved with the new one, or if it would collapse a
    /// curve. Large changes are made in steps (continuation), so shapes keep
    /// their orientation instead of folding into a mirrored solution.
    pub fn set_dimension(
        &mut self,
        id: ConstraintId,
        value: f64,
    ) -> Result<SolveSummary, SketchError> {
        let constraint = *self
            .constraint(id)
            .ok_or(SketchError::UnknownConstraint(id))?;
        let old = constraint.value().ok_or(SketchError::NotADimension(id))?;
        if self.driven.contains(&id) {
            return Err(SketchError::InvalidValue(
                "a driven dimension follows the geometry; make it driving to change it".into(),
            ));
        }
        let angle = matches!(constraint, SketchConstraint::Angle { .. });
        if !value.is_finite() || (value <= 0.0 && !angle) {
            return Err(SketchError::InvalidValue(
                "dimensions must be positive".into(),
            ));
        }
        let saved_entities = self.entities.clone();
        let before = self.solve();
        let shape = self.orientation();

        let attempt = |sketch: &mut Self, steps: usize| -> Option<SolveSummary> {
            let mut last = None;
            for k in 1..=steps {
                let t = k as f64 / steps as f64;
                // Lengths step geometrically (scale-like), angles linearly.
                let v = if angle || old <= 0.0 {
                    old + (value - old) * t
                } else {
                    old * (value / old).powf(t)
                };
                sketch.set_value_of(id, if k == steps { value } else { v });
                let summary = sketch.solve();
                if !summary.is_solved() {
                    return None;
                }
                last = Some(summary);
            }
            last
        };

        let ratio = if angle {
            1.0
        } else {
            (value / old).max(old / value)
        };
        let swing = if angle { (value - old).abs() } else { 0.0 };
        let mut steps = if ratio > 1.25 || swing > 0.35 { 8 } else { 1 };
        let mut result = attempt(self, steps);
        if result.is_some() && steps > 1 && self.orientation() != shape {
            // Still folded over: retry from the start with finer steps.
            self.entities = saved_entities.clone();
            self.set_value_of(id, old);
            steps = 32;
            result = attempt(self, steps);
        }
        let collapsed = result
            .as_ref()
            .is_some_and(|r| r.degenerate.iter().any(|e| !before.degenerate.contains(e)));
        match result {
            Some(summary) if !collapsed => Ok(summary),
            Some(summary) => {
                self.set_value_of(id, old);
                self.entities = saved_entities;
                self.solve();
                let _ = summary;
                Err(SketchError::Collapses(vec![]))
            }
            None => {
                self.set_value_of(id, old);
                self.entities = saved_entities;
                let mut s = self.solve();
                s.status = SolveStatus::NotConverged;
                Ok(s)
            }
        }
    }

    fn set_value_of(&mut self, id: ConstraintId, v: f64) {
        if let Some(slot) = self.constraints.iter_mut().find(|(c, _)| *c == id) {
            slot.1.set_value(v);
        }
    }

    /// Which way each corner turns (sign of the cross product where two
    /// lines meet): changes when a shape folds into its mirror image.
    fn orientation(&self) -> Vec<i8> {
        let lines: Vec<(EntityId, EntityId)> = self
            .entities
            .iter()
            .filter_map(|e| match e.geometry {
                Geometry::Line { start, end } => Some((start, end)),
                _ => None,
            })
            .collect();
        let mut signs = Vec::new();
        for (i, &(a0, a1)) in lines.iter().enumerate() {
            for &(b0, b1) in &lines[i + 1..] {
                let shared = [
                    (a1, b0, a0, b1),
                    (a0, b0, a1, b1),
                    (a1, b1, a0, b0),
                    (a0, b1, a1, b0),
                ]
                .into_iter()
                .find(|(p, q, _, _)| p == q);
                let Some((corner, _, from, to)) = shared else {
                    continue;
                };
                let (Some(c), Some(f), Some(t)) =
                    (self.point(corner), self.point(from), self.point(to))
                else {
                    continue;
                };
                let cross = (c[0] - f[0]) * (t[1] - c[1]) - (c[1] - f[1]) * (t[0] - c[0]);
                let scale = dist2(c, f) * dist2(t, c);
                signs.push(if cross.abs() <= 1e-6 * scale.max(1e-12) {
                    0
                } else if cross > 0.0 {
                    1
                } else {
                    -1
                });
            }
        }
        signs
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
                degenerate: vec![],
            };
        };
        let report = built.solver.solve();
        self.write_back(&built);
        built.summary(report.status, &report.degenerate)
    }

    /// Drags a point toward `target` while every constraint keeps holding.
    pub fn drag_point(
        &mut self,
        id: EntityId,
        target: [f64; 2],
    ) -> Result<SolveSummary, SketchError> {
        self.require_point(id)?;
        let mut built = self.build_solver_with_constraints()?;
        let before = built.solver.solve();
        let report = built
            .solver
            .drag_point(built.points[&id], target)
            .map_err(solver_error)?;
        // Never drag a curve down to nothing: stop at the last good position.
        let collapsed = report
            .degenerate
            .iter()
            .any(|c| !before.degenerate.contains(c));
        if !collapsed {
            self.write_back(&built);
        }
        Ok(built.summary(report.status, &report.degenerate))
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

    fn validate_entities(&self, c: &SketchConstraint) -> Result<(), SketchError> {
        for e in c.entities() {
            if self.entity(e).is_none() {
                return Err(SketchError::UnknownEntity(e));
            }
        }
        Ok(())
    }

    fn validate(&self, c: &SketchConstraint) -> Result<(), SketchError> {
        self.validate_entities(c)?;
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
                Geometry::Ellipse {
                    centre,
                    major,
                    minor_radius,
                } => b
                    .solver
                    .add_ellipse(b.points[&centre], b.points[&major], minor_radius),
                Geometry::EllipticalArc {
                    centre,
                    major,
                    minor_radius,
                    start,
                    end,
                } => b.solver.add_elliptical_arc(
                    b.points[&centre],
                    b.points[&major],
                    minor_radius,
                    b.points[&start],
                    b.points[&end],
                ),
                Geometry::Spline { ref points, closed } => {
                    let refs: Vec<PointRef> = points.iter().map(|p| b.points[p]).collect();
                    b.solver.add_spline(&refs, closed)
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
            if self.driven.contains(id) {
                continue; // driven dimensions only measure
            }
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
                Geometry::Ellipse { minor_radius, .. }
                | Geometry::EllipticalArc { minor_radius, .. } => {
                    if let Some(r) = b.curves.get(&e.id).and_then(|&c| b.solver.minor_radius(c)) {
                        *minor_radius = r;
                    }
                }
                _ => {}
            }
        }
        self.splines.clear();
        for e in &self.entities {
            if matches!(e.geometry, Geometry::Spline { .. }) {
                if let Some(data) = b.curves.get(&e.id).and_then(|&c| b.solver.spline(c)) {
                    self.splines.insert(e.id, data);
                }
            }
        }
        // Driven dimensions show what the geometry measures now.
        let driven: Vec<ConstraintId> = self.driven.iter().copied().collect();
        for id in driven {
            let Some(c) = self.constraint(id).copied() else {
                continue;
            };
            if let Some(v) = self.measure(&c) {
                self.set_value_of(id, v);
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
        self.curves
            .get(&id)
            .copied()
            .ok_or_else(|| SketchError::WrongKind(format!("entity {} must be a curve", id.0)))
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
            S::Fix(e) => match self.points.get(&e) {
                Some(&p) => Rule::Fix(p),
                None => Rule::FixCurve(self.c(e)?),
            },
            S::Midpoint { point, line } => Rule::Midpoint(self.p(point)?, self.c(line)?),
            S::Concentric(a, b) => Rule::Concentric(self.c(a)?, self.c(b)?),
            S::Collinear(a, b) => Rule::Collinear(self.c(a)?, self.c(b)?),
            S::Symmetric { a, b, axis } => match (self.points.get(&a), self.points.get(&b)) {
                (Some(&pa), Some(&pb)) => Rule::Symmetric {
                    a: pa,
                    b: pb,
                    axis: self.c(axis)?,
                },
                _ => Rule::SymmetricCurves {
                    a: self.c(a)?,
                    b: self.c(b)?,
                    axis: self.c(axis)?,
                },
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
            S::Smooth(a, b) => Rule::Smooth(self.c(a)?, self.c(b)?),
            S::LineDistance { a, b, value } => Rule::LineDistance {
                a: self.c(a)?,
                b: self.c(b)?,
                value,
            },
            S::ArcLength { arc, value } => Rule::ArcLength {
                arc: self.c(arc)?,
                value,
            },
            S::PointCircleDistance {
                point,
                circle,
                value,
                inside,
            } => Rule::PointCircleDistance {
                point: self.p(point)?,
                circle: self.c(circle)?,
                value,
                inside,
            },
            S::LineCircleDistance {
                line,
                circle,
                value,
            } => Rule::LineCircleDistance {
                line: self.c(line)?,
                circle: self.c(circle)?,
                value,
            },
            S::CircleDistance {
                a,
                b,
                value,
                inside,
            } => Rule::CircleDistance {
                a: self.c(a)?,
                b: self.c(b)?,
                value,
                inside,
            },
            S::MajorRadius { ellipse, value } => Rule::MajorRadius {
                ellipse: self.c(ellipse)?,
                value,
            },
            S::MinorRadius { ellipse, value } => Rule::MinorRadius {
                ellipse: self.c(ellipse)?,
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

    fn entity_ids(&self, curves: &[CurveRef]) -> Vec<EntityId> {
        let mut ids: Vec<EntityId> = self
            .curves
            .iter()
            .filter(|(_, c)| curves.contains(c))
            .map(|(id, _)| *id)
            .collect();
        ids.sort_unstable();
        ids
    }

    fn summary(&mut self, status: SolveStatus, degenerate: &[CurveRef]) -> SolveSummary {
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
            degenerate: self.entity_ids(degenerate),
        }
    }
}

/// `x > min`, false for NaN.
pub(crate) fn positive(x: f64, min: f64) -> bool {
    x > min
}

fn dist2(a: [f64; 2], b: [f64; 2]) -> f64 {
    (a[0] - b[0]).hypot(a[1] - b[1])
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

#[path = "ops.rs"]
mod ops;
pub use ops::Transform;

#[cfg(test)]
#[path = "model_tests.rs"]
mod tests;
