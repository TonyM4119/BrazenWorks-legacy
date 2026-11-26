//! CXX bridge between the Rust model and the C++ side of the app.
//!
//! * `extern "C++"`: geometry functions implemented on Open CASCADE in
//!   `src/bridge/GeometryBridge.cpp`.
//! * `extern "Rust"`: the timeline API the Qt app calls.
//!
//! Everything lives in C++ namespace `model`.

use crate::kernel::{self, Kernel};
use crate::sketch::{
    ConstraintId, EntityId, Geometry, OriginPlane, Sketch, SketchConstraint, SketchError,
    SketchPlane, SolveStatus, SolveSummary,
};
use crate::timeline::{Feature, FeatureId, FeatureKind, Timeline};
use cxx::UniquePtr;

#[cxx::bridge(namespace = "model")]
mod bridge {
    enum ProfileKind {
        Rect,
        Circle,
    }

    /// Rect uses x, y (lower-left corner), width, height.
    /// Circle uses x, y (centre) and radius.
    struct Profile {
        kind: ProfileKind,
        x: f64,
        y: f64,
        width: f64,
        height: f64,
        radius: f64,
    }

    enum BodyOp {
        Join,
        Cut,
        NewBody,
    }

    // ---- Sketches ----

    enum OriginPlaneKind {
        XY,
        XZ,
        YZ,
    }

    /// Plane of a sketch in world coordinates (normal = x_axis × y_axis).
    struct PlaneInfo {
        origin: [f64; 3],
        x_axis: [f64; 3],
        y_axis: [f64; 3],
        normal: [f64; 3],
    }

    enum SketchEntityKind {
        Point,
        Line,
        Circle,
        Arc,
    }

    /// One sketch entity, flattened for drawing. Point: (x0, y0). Line: p0→p1.
    /// Circle: centre p0 and radius. Arc: centre p0, start p1, end p2 (CCW).
    struct SketchEntityInfo {
        id: u64,
        kind: SketchEntityKind,
        construction: bool,
        fully_constrained: bool,
        p0: u64,
        p1: u64,
        p2: u64,
        x0: f64,
        y0: f64,
        x1: f64,
        y1: f64,
        x2: f64,
        y2: f64,
        radius: f64,
    }

    enum ConstraintKind {
        Coincident,
        PointOnCurve,
        Horizontal,
        Vertical,
        HorizontalPoints,
        VerticalPoints,
        Parallel,
        Perpendicular,
        Tangent,
        Equal,
        Fix,
        Midpoint,
        Concentric,
        Collinear,
        Symmetric,
        Distance,
        PointLineDistance,
        HorizontalDistance,
        VerticalDistance,
        Length,
        Angle,
        Radius,
        Diameter,
    }

    /// A constraint: entity ids a, b (and c for Symmetric's axis); value for dimensions.
    struct SketchConstraintInfo {
        id: u64,
        kind: ConstraintKind,
        a: u64,
        b: u64,
        c: u64,
        value: f64,
        is_dimension: bool,
        conflicting: bool,
    }

    enum SketchSolveStatus {
        Solved,
        Conflicting,
        NotConverged,
    }

    struct SketchSolveInfo {
        status: SketchSolveStatus,
        dof: usize,
        conflicting: Vec<u64>,
    }

    /// Result of adding a constraint: refused ones say why and with what.
    struct ConstraintResult {
        ok: bool,
        id: u64,
        conflicts: Vec<u64>,
        message: String,
    }

    unsafe extern "C++" {
        include!("GeometryBridge.h");

        /// Wraps a TopoDS_Shape.
        type Shape;

        fn shape_is_null(shape: &Shape) -> bool;
        fn geom_extrude(profile: &Profile, distance: f64) -> UniquePtr<Shape>;
        fn geom_fuse(a: &Shape, b: &Shape) -> UniquePtr<Shape>;
        fn geom_cut(a: &Shape, b: &Shape) -> UniquePtr<Shape>;
        fn geom_fillet_all_edges(shape: &Shape, radius: f64) -> UniquePtr<Shape>;
    }

    extern "Rust" {
        type Timeline;

        fn new_timeline() -> Box<Timeline>;
        fn timeline_len(t: &Timeline) -> usize;
        fn timeline_marker(t: &Timeline) -> usize;
        fn timeline_set_marker(t: &mut Timeline, marker: usize);
        fn timeline_add_extrude(
            t: &mut Timeline,
            name: &str,
            profile: &Profile,
            distance: f64,
            op: BodyOp,
        ) -> usize;
        fn timeline_add_fillet(t: &mut Timeline, name: &str, radius: f64) -> usize;
        fn timeline_remove(t: &mut Timeline, index: usize) -> bool;
        fn timeline_feature_name(t: &Timeline, index: usize) -> String;
        fn timeline_feature_type(t: &Timeline, index: usize) -> String;
        fn timeline_set_suppressed(t: &mut Timeline, index: usize, suppressed: bool) -> bool;
        fn timeline_set_extrude_distance(t: &mut Timeline, index: usize, distance: f64) -> bool;

        /// Replays the timeline on Open CASCADE. Returns a null pointer if no
        /// body was produced; indices of failed features go into `failed`.
        fn timeline_rebuild(t: &Timeline, failed: &mut Vec<usize>) -> UniquePtr<Shape>;

        /// Stable id of the feature at `index` (0 if out of range).
        fn timeline_feature_id(t: &Timeline, index: usize) -> u64;
        /// Index of the feature with this id, or `usize::MAX`.
        fn timeline_index_of(t: &Timeline, feature: u64) -> usize;

        // Sketches are addressed by feature id, which survives inserts and removals.
        fn timeline_add_sketch(t: &mut Timeline, name: &str, plane: OriginPlaneKind) -> u64;
        fn sketch_plane(t: &Timeline, sketch: u64) -> Result<PlaneInfo>;
        fn sketch_add_point(t: &mut Timeline, sketch: u64, x: f64, y: f64) -> Result<u64>;
        fn sketch_add_line(t: &mut Timeline, sketch: u64, start: u64, end: u64) -> Result<u64>;
        fn sketch_add_line_xy(
            t: &mut Timeline,
            sketch: u64,
            x0: f64,
            y0: f64,
            x1: f64,
            y1: f64,
        ) -> Result<u64>;
        fn sketch_add_circle_xy(
            t: &mut Timeline,
            sketch: u64,
            cx: f64,
            cy: f64,
            radius: f64,
        ) -> Result<u64>;
        fn sketch_add_arc(
            t: &mut Timeline,
            sketch: u64,
            centre: u64,
            start: u64,
            end: u64,
        ) -> Result<u64>;
        /// Lines bottom, right, top, left.
        fn sketch_add_rectangle(
            t: &mut Timeline,
            sketch: u64,
            x0: f64,
            y0: f64,
            x1: f64,
            y1: f64,
        ) -> Result<Vec<u64>>;
        fn sketch_set_construction(
            t: &mut Timeline,
            sketch: u64,
            entity: u64,
            construction: bool,
        ) -> Result<()>;
        fn sketch_remove_entity(t: &mut Timeline, sketch: u64, entity: u64) -> Result<()>;
        /// Unused ids pass 0. Never throws: problems come back in the result.
        fn sketch_add_constraint(
            t: &mut Timeline,
            sketch: u64,
            kind: ConstraintKind,
            a: u64,
            b: u64,
            c: u64,
            value: f64,
        ) -> ConstraintResult;
        fn sketch_remove_constraint(t: &mut Timeline, sketch: u64, constraint: u64) -> Result<()>;
        fn sketch_set_dimension(
            t: &mut Timeline,
            sketch: u64,
            constraint: u64,
            value: f64,
        ) -> Result<SketchSolveInfo>;
        fn sketch_solve(t: &mut Timeline, sketch: u64) -> Result<SketchSolveInfo>;
        fn sketch_drag_point(
            t: &mut Timeline,
            sketch: u64,
            point: u64,
            x: f64,
            y: f64,
        ) -> Result<SketchSolveInfo>;
        fn sketch_entities(t: &mut Timeline, sketch: u64) -> Result<Vec<SketchEntityInfo>>;
        fn sketch_constraints(t: &mut Timeline, sketch: u64) -> Result<Vec<SketchConstraintInfo>>;
    }
}

// ---- Kernel implementation backed by the C++ geometry layer ----------------

struct OcctKernel;

fn non_null(shape: UniquePtr<bridge::Shape>) -> Option<UniquePtr<bridge::Shape>> {
    match shape.as_ref() {
        Some(s) if !bridge::shape_is_null(s) => Some(shape),
        _ => None,
    }
}

fn to_ffi_profile(p: &kernel::Profile) -> bridge::Profile {
    match *p {
        kernel::Profile::Rect {
            x,
            y,
            width,
            height,
        } => bridge::Profile {
            kind: bridge::ProfileKind::Rect,
            x,
            y,
            width,
            height,
            radius: 0.0,
        },
        kernel::Profile::Circle { cx, cy, radius } => bridge::Profile {
            kind: bridge::ProfileKind::Circle,
            x: cx,
            y: cy,
            width: 0.0,
            height: 0.0,
            radius,
        },
    }
}

fn from_ffi_profile(p: &bridge::Profile) -> kernel::Profile {
    match p.kind {
        bridge::ProfileKind::Circle => kernel::Profile::Circle {
            cx: p.x,
            cy: p.y,
            radius: p.radius,
        },
        _ => kernel::Profile::Rect {
            x: p.x,
            y: p.y,
            width: p.width,
            height: p.height,
        },
    }
}

impl Kernel for OcctKernel {
    type Shape = UniquePtr<bridge::Shape>;

    fn extrude(&self, profile: &kernel::Profile, distance: f64) -> Option<Self::Shape> {
        non_null(bridge::geom_extrude(&to_ffi_profile(profile), distance))
    }
    fn fuse(&self, a: &Self::Shape, b: &Self::Shape) -> Option<Self::Shape> {
        non_null(bridge::geom_fuse(a.as_ref()?, b.as_ref()?))
    }
    fn cut(&self, a: &Self::Shape, b: &Self::Shape) -> Option<Self::Shape> {
        non_null(bridge::geom_cut(a.as_ref()?, b.as_ref()?))
    }
    fn fillet_all_edges(&self, shape: &Self::Shape, radius: f64) -> Option<Self::Shape> {
        non_null(bridge::geom_fillet_all_edges(shape.as_ref()?, radius))
    }
}

// ---- Functions exposed to C++ ----------------------------------------------

fn new_timeline() -> Box<Timeline> {
    Box::new(Timeline::new())
}

fn timeline_len(t: &Timeline) -> usize {
    t.len()
}

fn timeline_marker(t: &Timeline) -> usize {
    t.marker()
}

fn timeline_set_marker(t: &mut Timeline, marker: usize) {
    t.set_marker(marker);
}

fn timeline_add_extrude(
    t: &mut Timeline,
    name: &str,
    profile: &bridge::Profile,
    distance: f64,
    op: bridge::BodyOp,
) -> usize {
    let op = match op {
        bridge::BodyOp::Cut => kernel::BodyOp::Cut,
        bridge::BodyOp::NewBody => kernel::BodyOp::NewBody,
        _ => kernel::BodyOp::Join,
    };
    let kind = FeatureKind::Extrude {
        profile: from_ffi_profile(profile),
        distance,
        op,
    };
    t.insert(Feature::new(name, kind))
}

fn timeline_add_fillet(t: &mut Timeline, name: &str, radius: f64) -> usize {
    t.insert(Feature::new(name, FeatureKind::Fillet { radius }))
}

fn timeline_remove(t: &mut Timeline, index: usize) -> bool {
    t.remove(index).is_some()
}

fn timeline_feature_name(t: &Timeline, index: usize) -> String {
    t.get(index).map(|f| f.name.clone()).unwrap_or_default()
}

fn timeline_feature_type(t: &Timeline, index: usize) -> String {
    t.get(index)
        .map(|f| f.type_name().to_string())
        .unwrap_or_default()
}

fn timeline_set_suppressed(t: &mut Timeline, index: usize, suppressed: bool) -> bool {
    match t.get_mut(index) {
        Some(f) => {
            f.suppressed = suppressed;
            true
        }
        None => false,
    }
}

fn timeline_set_extrude_distance(t: &mut Timeline, index: usize, value: f64) -> bool {
    match t.get_mut(index).map(|f| &mut f.kind) {
        Some(FeatureKind::Extrude { distance, .. }) => {
            *distance = value;
            true
        }
        _ => false,
    }
}

fn timeline_rebuild(t: &Timeline, failed: &mut Vec<usize>) -> UniquePtr<bridge::Shape> {
    let result = t.rebuild(&OcctKernel);
    *failed = result.failed;
    result.body.unwrap_or_else(UniquePtr::null)
}

// ---- Sketch functions exposed to C++ ----------------------------------------

fn timeline_feature_id(t: &Timeline, index: usize) -> u64 {
    t.get(index).map_or(0, |f| f.id.0)
}

fn timeline_index_of(t: &Timeline, feature: u64) -> usize {
    t.index_of(FeatureId(feature)).unwrap_or(usize::MAX)
}

fn timeline_add_sketch(t: &mut Timeline, name: &str, plane: bridge::OriginPlaneKind) -> u64 {
    let which = match plane {
        bridge::OriginPlaneKind::XZ => OriginPlane::XZ,
        bridge::OriginPlaneKind::YZ => OriginPlane::YZ,
        _ => OriginPlane::XY,
    };
    let sketch = Sketch::new(name, SketchPlane::origin_plane(which));
    let index = t.insert(Feature::new(name, FeatureKind::Sketch(Box::new(sketch))));
    t.get(index).map_or(0, |f| f.id.0)
}

fn sketch_ref(t: &Timeline, id: u64) -> Result<&Sketch, String> {
    t.find(FeatureId(id))
        .and_then(Feature::sketch)
        .ok_or_else(|| format!("feature {id} is not a sketch"))
}

fn sketch_mut(t: &mut Timeline, id: u64) -> Result<&mut Sketch, String> {
    t.find_mut(FeatureId(id))
        .and_then(Feature::sketch_mut)
        .ok_or_else(|| format!("feature {id} is not a sketch"))
}

fn err(e: SketchError) -> String {
    e.to_string()
}

fn sketch_plane(t: &Timeline, sketch: u64) -> Result<bridge::PlaneInfo, String> {
    let p = sketch_ref(t, sketch)?.plane;
    Ok(bridge::PlaneInfo {
        origin: p.origin,
        x_axis: p.x_axis,
        y_axis: p.y_axis,
        normal: p.normal(),
    })
}

fn sketch_add_point(t: &mut Timeline, sketch: u64, x: f64, y: f64) -> Result<u64, String> {
    Ok(sketch_mut(t, sketch)?.add_point(x, y).0)
}

fn sketch_add_line(t: &mut Timeline, sketch: u64, start: u64, end: u64) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .add_line(EntityId(start), EntityId(end))
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_add_line_xy(
    t: &mut Timeline,
    sketch: u64,
    x0: f64,
    y0: f64,
    x1: f64,
    y1: f64,
) -> Result<u64, String> {
    Ok(sketch_mut(t, sketch)?.add_line_xy([x0, y0], [x1, y1]).0)
}

fn sketch_add_circle_xy(
    t: &mut Timeline,
    sketch: u64,
    cx: f64,
    cy: f64,
    radius: f64,
) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .add_circle_xy([cx, cy], radius)
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_add_arc(
    t: &mut Timeline,
    sketch: u64,
    centre: u64,
    start: u64,
    end: u64,
) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .add_arc(EntityId(centre), EntityId(start), EntityId(end))
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_add_rectangle(
    t: &mut Timeline,
    sketch: u64,
    x0: f64,
    y0: f64,
    x1: f64,
    y1: f64,
) -> Result<Vec<u64>, String> {
    let lines = sketch_mut(t, sketch)?
        .add_rectangle([x0, y0], [x1, y1])
        .map_err(err)?;
    Ok(lines.iter().map(|e| e.0).collect())
}

fn sketch_set_construction(
    t: &mut Timeline,
    sketch: u64,
    entity: u64,
    construction: bool,
) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .set_construction(EntityId(entity), construction)
        .map_err(err)
}

fn sketch_remove_entity(t: &mut Timeline, sketch: u64, entity: u64) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .remove_entity(EntityId(entity))
        .map_err(err)
}

fn to_constraint(
    kind: bridge::ConstraintKind,
    a: u64,
    b: u64,
    c: u64,
    value: f64,
) -> Option<SketchConstraint> {
    use bridge::ConstraintKind as K;
    use SketchConstraint as S;
    let (a, b, c) = (EntityId(a), EntityId(b), EntityId(c));
    Some(match kind {
        K::Coincident => S::Coincident(a, b),
        K::PointOnCurve => S::PointOnCurve { point: a, curve: b },
        K::Horizontal => S::Horizontal(a),
        K::Vertical => S::Vertical(a),
        K::HorizontalPoints => S::HorizontalPoints(a, b),
        K::VerticalPoints => S::VerticalPoints(a, b),
        K::Parallel => S::Parallel(a, b),
        K::Perpendicular => S::Perpendicular(a, b),
        K::Tangent => S::Tangent(a, b),
        K::Equal => S::Equal(a, b),
        K::Fix => S::Fix(a),
        K::Midpoint => S::Midpoint { point: a, line: b },
        K::Concentric => S::Concentric(a, b),
        K::Collinear => S::Collinear(a, b),
        K::Symmetric => S::Symmetric { a, b, axis: c },
        K::Distance => S::Distance { a, b, value },
        K::PointLineDistance => S::PointLineDistance {
            point: a,
            line: b,
            value,
        },
        K::HorizontalDistance => S::HorizontalDistance { a, b, value },
        K::VerticalDistance => S::VerticalDistance { a, b, value },
        K::Length => S::Length { line: a, value },
        K::Angle => S::Angle { a, b, value },
        K::Radius => S::Radius { curve: a, value },
        K::Diameter => S::Diameter { curve: a, value },
        _ => return None,
    })
}

fn from_constraint(c: &SketchConstraint) -> (bridge::ConstraintKind, u64, u64, u64, f64) {
    use bridge::ConstraintKind as K;
    use SketchConstraint as S;
    let v = c.value().unwrap_or(0.0);
    match *c {
        S::Coincident(a, b) => (K::Coincident, a.0, b.0, 0, v),
        S::PointOnCurve { point, curve } => (K::PointOnCurve, point.0, curve.0, 0, v),
        S::Horizontal(a) => (K::Horizontal, a.0, 0, 0, v),
        S::Vertical(a) => (K::Vertical, a.0, 0, 0, v),
        S::HorizontalPoints(a, b) => (K::HorizontalPoints, a.0, b.0, 0, v),
        S::VerticalPoints(a, b) => (K::VerticalPoints, a.0, b.0, 0, v),
        S::Parallel(a, b) => (K::Parallel, a.0, b.0, 0, v),
        S::Perpendicular(a, b) => (K::Perpendicular, a.0, b.0, 0, v),
        S::Tangent(a, b) => (K::Tangent, a.0, b.0, 0, v),
        S::Equal(a, b) => (K::Equal, a.0, b.0, 0, v),
        S::Fix(a) => (K::Fix, a.0, 0, 0, v),
        S::Midpoint { point, line } => (K::Midpoint, point.0, line.0, 0, v),
        S::Concentric(a, b) => (K::Concentric, a.0, b.0, 0, v),
        S::Collinear(a, b) => (K::Collinear, a.0, b.0, 0, v),
        S::Symmetric { a, b, axis } => (K::Symmetric, a.0, b.0, axis.0, v),
        S::Distance { a, b, .. } => (K::Distance, a.0, b.0, 0, v),
        S::PointLineDistance { point, line, .. } => (K::PointLineDistance, point.0, line.0, 0, v),
        S::HorizontalDistance { a, b, .. } => (K::HorizontalDistance, a.0, b.0, 0, v),
        S::VerticalDistance { a, b, .. } => (K::VerticalDistance, a.0, b.0, 0, v),
        S::Length { line, .. } => (K::Length, line.0, 0, 0, v),
        S::Angle { a, b, .. } => (K::Angle, a.0, b.0, 0, v),
        S::Radius { curve, .. } => (K::Radius, curve.0, 0, 0, v),
        S::Diameter { curve, .. } => (K::Diameter, curve.0, 0, 0, v),
    }
}

fn sketch_add_constraint(
    t: &mut Timeline,
    sketch: u64,
    kind: bridge::ConstraintKind,
    a: u64,
    b: u64,
    c: u64,
    value: f64,
) -> bridge::ConstraintResult {
    let refused = |message: String, conflicts: Vec<u64>| bridge::ConstraintResult {
        ok: false,
        id: 0,
        conflicts,
        message,
    };
    let s = match sketch_mut(t, sketch) {
        Ok(s) => s,
        Err(m) => return refused(m, vec![]),
    };
    let Some(constraint) = to_constraint(kind, a, b, c, value) else {
        return refused("unknown constraint kind".into(), vec![]);
    };
    match s.add_constraint(constraint) {
        Ok(id) => bridge::ConstraintResult {
            ok: true,
            id: id.0,
            conflicts: vec![],
            message: String::new(),
        },
        Err(SketchError::Conflicts(ids)) => {
            let message = SketchError::Conflicts(ids.clone()).to_string();
            refused(message, ids.iter().map(|c| c.0).collect())
        }
        Err(e) => refused(e.to_string(), vec![]),
    }
}

fn sketch_remove_constraint(t: &mut Timeline, sketch: u64, constraint: u64) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .remove_constraint(ConstraintId(constraint))
        .map_err(err)
}

fn solve_info(s: &SolveSummary) -> bridge::SketchSolveInfo {
    bridge::SketchSolveInfo {
        status: match s.status {
            SolveStatus::Solved => bridge::SketchSolveStatus::Solved,
            SolveStatus::Conflicting => bridge::SketchSolveStatus::Conflicting,
            SolveStatus::NotConverged => bridge::SketchSolveStatus::NotConverged,
        },
        dof: s.dof,
        conflicting: s.conflicting.iter().map(|c| c.0).collect(),
    }
}

fn sketch_set_dimension(
    t: &mut Timeline,
    sketch: u64,
    constraint: u64,
    value: f64,
) -> Result<bridge::SketchSolveInfo, String> {
    let summary = sketch_mut(t, sketch)?
        .set_dimension(ConstraintId(constraint), value)
        .map_err(err)?;
    Ok(solve_info(&summary))
}

fn sketch_solve(t: &mut Timeline, sketch: u64) -> Result<bridge::SketchSolveInfo, String> {
    Ok(solve_info(&sketch_mut(t, sketch)?.solve()))
}

fn sketch_drag_point(
    t: &mut Timeline,
    sketch: u64,
    point: u64,
    x: f64,
    y: f64,
) -> Result<bridge::SketchSolveInfo, String> {
    let summary = sketch_mut(t, sketch)?
        .drag_point(EntityId(point), [x, y])
        .map_err(err)?;
    Ok(solve_info(&summary))
}

fn sketch_entities(t: &mut Timeline, sketch: u64) -> Result<Vec<bridge::SketchEntityInfo>, String> {
    let s = sketch_mut(t, sketch)?;
    let summary = s.solve();
    let pos = |s: &Sketch, id: EntityId| s.point(id).unwrap_or([0.0, 0.0]);
    Ok(s.entities()
        .iter()
        .map(|e| {
            let mut info = bridge::SketchEntityInfo {
                id: e.id.0,
                kind: bridge::SketchEntityKind::Point,
                construction: e.construction,
                fully_constrained: summary.fully_constrained.contains(&e.id),
                p0: 0,
                p1: 0,
                p2: 0,
                x0: 0.0,
                y0: 0.0,
                x1: 0.0,
                y1: 0.0,
                x2: 0.0,
                y2: 0.0,
                radius: 0.0,
            };
            match e.geometry {
                Geometry::Point { x, y } => (info.x0, info.y0) = (x, y),
                Geometry::Line { start, end } => {
                    info.kind = bridge::SketchEntityKind::Line;
                    (info.p0, info.p1) = (start.0, end.0);
                    [info.x0, info.y0] = pos(s, start);
                    [info.x1, info.y1] = pos(s, end);
                }
                Geometry::Circle { centre, radius } => {
                    info.kind = bridge::SketchEntityKind::Circle;
                    info.p0 = centre.0;
                    [info.x0, info.y0] = pos(s, centre);
                    info.radius = radius;
                }
                Geometry::Arc { centre, start, end } => {
                    info.kind = bridge::SketchEntityKind::Arc;
                    (info.p0, info.p1, info.p2) = (centre.0, start.0, end.0);
                    [info.x0, info.y0] = pos(s, centre);
                    [info.x1, info.y1] = pos(s, start);
                    [info.x2, info.y2] = pos(s, end);
                    info.radius = s.radius(e.id).unwrap_or(0.0);
                }
            }
            info
        })
        .collect())
}

fn sketch_constraints(
    t: &mut Timeline,
    sketch: u64,
) -> Result<Vec<bridge::SketchConstraintInfo>, String> {
    let s = sketch_mut(t, sketch)?;
    let summary = s.solve();
    Ok(s.constraints()
        .iter()
        .map(|(id, c)| {
            let (kind, a, b, cc, value) = from_constraint(c);
            bridge::SketchConstraintInfo {
                id: id.0,
                kind,
                a,
                b,
                c: cc,
                value,
                is_dimension: c.is_dimension(),
                conflicting: summary.conflicting.contains(id),
            }
        })
        .collect())
}
