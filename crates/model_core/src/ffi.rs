//! CXX bridge between the Rust model and the C++ side of the app.
//!
//! * `extern "C++"`: geometry functions implemented on Open CASCADE in
//!   `src/bridge/GeometryBridge.cpp`.
//! * `extern "Rust"`: the timeline API the Qt app calls.
//!
//! Everything lives in C++ namespace `model`.

use crate::kernel::{self, Kernel};
use crate::sketch::{
    find_regions, ConstraintId, CopyGroup, CopyKind, EntityId, Geometry, Loop, OriginPlane, Region,
    RegionKey, Segment, Sketch, SketchConstraint, SketchError, SketchPlane, SolveStatus,
    SolveSummary, Transform,
};
use crate::timeline::{ExtrudeSource, Feature, FeatureId, FeatureKind, Timeline};
use crate::units::{self, LengthUnit};
use cxx::UniquePtr;

#[allow(clippy::too_many_arguments)]
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

    // ---- Units ----

    /// Display/input unit. The model always stores millimetres.
    enum LengthUnitKind {
        Inch,
        Foot,
        Millimeter,
        Centimeter,
        Meter,
    }

    struct LengthParse {
        ok: bool,
        mm: f64,
        error: String,
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
        Ellipse,
        EllipticalArc,
        Spline,
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
        /// Ellipse / elliptical arc: centre p0, major-axis end p1, minor
        /// radius `minor` (radius = major radius); arc start p2, end p3.
        p3: u64,
        x3: f64,
        y3: f64,
        minor: f64,
        /// Spline: fit points in order; closed = periodic.
        fit: Vec<u64>,
        closed: bool,
        /// Splines: the exact curve (clamped B-spline). Ellipses and splines:
        /// a polyline along the curve for picking.
        degree: usize,
        poles: Vec<f64>,
        knots: Vec<f64>,
        weights: Vec<f64>,
        samples: Vec<f64>,
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
        Smooth,
        LineDistance,
        ArcLength,
        PointCircleDistance,
        LineCircleDistance,
        CircleDistance,
        MajorRadius,
        MinorRadius,
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
        /// Reference dimension: follows the geometry.
        driven: bool,
        /// Implied by the other constraints.
        redundant: bool,
        /// Dimensions: where the label was placed, relative to its anchor.
        has_label: bool,
        label_x: f64,
        label_y: f64,
    }

    /// A pattern or mirror that can be edited. kind 0 rectangular (a, b
    /// direction; c, d spacing; n1 × n2), 1 circular (a, b centre; c angle;
    /// n1 count), 2 mirror (axis). id 0: none.
    struct CopyGroupInfo {
        id: u64,
        kind: u8,
        sources: Vec<u64>,
        copies: Vec<u64>,
        a: f64,
        b: f64,
        c: f64,
        d: f64,
        n1: usize,
        n2: usize,
        axis: u64,
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
        redundant: Vec<u64>,
        /// Curves that collapsed to nothing.
        degenerate: Vec<u64>,
    }

    /// Result of adding a constraint: refused ones say why and with what.
    struct ConstraintResult {
        ok: bool,
        id: u64,
        conflicts: Vec<u64>,
        message: String,
        /// Refused because the other constraints already imply it (a
        /// dimension can be added as driven instead).
        redundant: bool,
    }

    // ---- Profiles ----

    enum SegmentKind {
        Line,
        Arc,
        Circle,
        Ellipse,
        EllipseArc,
        Spline,
    }

    /// One oriented piece of a loop in sketch coordinates. Line/arc: start
    /// (x0, y0) to end (x1, y1). Arc/circle: centre and radius.
    struct SegmentData {
        kind: SegmentKind,
        x0: f64,
        y0: f64,
        x1: f64,
        y1: f64,
        cx: f64,
        cy: f64,
        radius: f64,
        ccw: bool,
        /// Ellipse (arc): major semi-axis vector, minor radius, parameters.
        mx: f64,
        my: f64,
        minor: f64,
        t0: f64,
        t1: f64,
        /// Spline: clamped B-spline, walked backwards when `reversed`.
        degree: usize,
        poles: Vec<f64>,
        knots: Vec<f64>,
        weights: Vec<f64>,
        reversed: bool,
    }

    struct LoopData {
        segments: Vec<SegmentData>,
    }

    struct RegionData {
        outer: LoopData,
        holes: Vec<LoopData>,
    }

    /// A profile for the UI: its stable key, area and outline polygons
    /// (x, y pairs; loop_sizes[0] points for the outer loop, then each hole).
    struct ProfileInfo {
        key: String,
        area: f64,
        outlines: Vec<f64>,
        loop_sizes: Vec<usize>,
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
        fn geom_extrude_region(
            plane: &PlaneInfo,
            region: &RegionData,
            distance: f64,
        ) -> UniquePtr<Shape>;
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

        /// A full copy (used for undo/redo and previews).
        fn timeline_clone(t: &Timeline) -> Box<Timeline>;
        fn timeline_units(t: &Timeline) -> LengthUnitKind;
        fn timeline_set_units(t: &mut Timeline, unit: LengthUnitKind);
        /// "1.25 in", "25.40 mm"…
        fn length_format(mm: f64, unit: LengthUnitKind) -> String;
        /// Reads "2", "1 1/4", "1' 6\"", "25mm", "2in + 5mm"…; bare numbers are in `unit`.
        fn length_parse(text: &str, unit: LengthUnitKind) -> LengthParse;
        fn length_unit_symbol(unit: LengthUnitKind) -> &'static str;
        fn length_unit_name(unit: LengthUnitKind) -> &'static str;
        fn length_unit_mm(unit: LengthUnitKind) -> f64;

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
        fn sketch_add_circle(
            t: &mut Timeline,
            sketch: u64,
            centre: u64,
            radius: f64,
        ) -> Result<u64>;
        fn sketch_add_ellipse(
            t: &mut Timeline,
            sketch: u64,
            centre: u64,
            major: u64,
            minor: f64,
        ) -> Result<u64>;
        fn sketch_add_elliptical_arc(
            t: &mut Timeline,
            sketch: u64,
            centre: u64,
            major: u64,
            minor: f64,
            start: u64,
            end: u64,
        ) -> Result<u64>;
        fn sketch_add_spline(
            t: &mut Timeline,
            sketch: u64,
            points: Vec<u64>,
            closed: bool,
        ) -> Result<u64>;
        /// A reference dimension (measures, never refused).
        fn sketch_add_driven(
            t: &mut Timeline,
            sketch: u64,
            kind: ConstraintKind,
            a: u64,
            b: u64,
            c: u64,
        ) -> ConstraintResult;
        fn sketch_set_driven(
            t: &mut Timeline,
            sketch: u64,
            constraint: u64,
            driven: bool,
        ) -> ConstraintResult;
        // ---- Sketch editing tools ----
        fn sketch_trim(t: &mut Timeline, sketch: u64, curve: u64, x: f64, y: f64) -> Result<()>;
        fn sketch_break(t: &mut Timeline, sketch: u64, curve: u64, x: f64, y: f64) -> Result<()>;
        fn sketch_extend(t: &mut Timeline, sketch: u64, curve: u64, x: f64, y: f64) -> Result<()>;
        fn sketch_fillet(t: &mut Timeline, sketch: u64, corner: u64, radius: f64) -> Result<u64>;
        fn sketch_chamfer(t: &mut Timeline, sketch: u64, corner: u64, distance: f64)
            -> Result<u64>;
        fn sketch_offset(
            t: &mut Timeline,
            sketch: u64,
            curves: Vec<u64>,
            distance: f64,
            x: f64,
            y: f64,
        ) -> Result<Vec<u64>>;
        fn sketch_mirror(
            t: &mut Timeline,
            sketch: u64,
            curves: Vec<u64>,
            axis: u64,
        ) -> Result<Vec<u64>>;
        fn sketch_pattern_rect(
            t: &mut Timeline,
            sketch: u64,
            curves: Vec<u64>,
            dx: f64,
            dy: f64,
            nx: usize,
            ny: usize,
            sx: f64,
            sy: f64,
        ) -> Result<Vec<u64>>;
        fn sketch_pattern_circ(
            t: &mut Timeline,
            sketch: u64,
            curves: Vec<u64>,
            cx: f64,
            cy: f64,
            count: usize,
            angle: f64,
        ) -> Result<Vec<u64>>;
        /// kind 0 move (a, b = dx, dy), 1 rotate (a, b centre, c angle),
        /// 2 scale (a, b centre, c factor). `copy` makes copies instead.
        fn sketch_transform(
            t: &mut Timeline,
            sketch: u64,
            curves: Vec<u64>,
            kind: u8,
            a: f64,
            b: f64,
            c: f64,
            copy: bool,
        ) -> Result<Vec<u64>>;
        fn sketch_set_label(
            t: &mut Timeline,
            sketch: u64,
            constraint: u64,
            x: f64,
            y: f64,
        ) -> Result<()>;
        fn sketch_group_of(t: &Timeline, sketch: u64, curve: u64) -> Result<CopyGroupInfo>;
        fn sketch_edit_group(
            t: &mut Timeline,
            sketch: u64,
            group: CopyGroupInfo,
        ) -> Result<Vec<u64>>;
        fn sketch_delete_group(
            t: &mut Timeline,
            sketch: u64,
            group: u64,
            delete_copies: bool,
        ) -> Result<()>;
        /// Clipboard (shared by every sketch in the app): copy curves, paste
        /// them moved by (dx, dy). clipboard_centre: [x, y] or empty.
        fn sketch_copy(t: &Timeline, sketch: u64, curves: Vec<u64>) -> Result<usize>;
        fn sketch_paste(t: &mut Timeline, sketch: u64, dx: f64, dy: f64) -> Result<Vec<u64>>;
        fn sketch_clipboard_centre() -> Vec<f64>;
        fn sketch_auto_constrain(t: &mut Timeline, sketch: u64) -> Result<usize>;
        fn sketch_loose_ends(t: &Timeline, sketch: u64) -> Result<Vec<u64>>;
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

        /// Closed regions of a sketch that can be extruded, largest first.
        fn sketch_profiles(t: &Timeline, sketch: u64) -> Result<Vec<ProfileInfo>>;
        /// True if an active extrude uses this sketch (then it is hidden in the view).
        fn timeline_sketch_consumed(t: &Timeline, sketch: u64) -> bool;
        /// Extrudes profiles (by key) of an earlier sketch; inserted at the marker.
        fn timeline_add_extrude_sketch(
            t: &mut Timeline,
            name: &str,
            sketch: u64,
            profiles: Vec<String>,
            distance: f64,
            op: BodyOp,
        ) -> usize;
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
    fn extrude_region(
        &self,
        plane: &SketchPlane,
        region: &Region,
        distance: f64,
    ) -> Option<Self::Shape> {
        let data = bridge::RegionData {
            outer: loop_data(&region.outer),
            holes: region.holes.iter().map(loop_data).collect(),
        };
        non_null(bridge::geom_extrude_region(
            &plane_info(plane),
            &data,
            distance,
        ))
    }
}

fn plane_info(p: &SketchPlane) -> bridge::PlaneInfo {
    bridge::PlaneInfo {
        origin: p.origin,
        x_axis: p.x_axis,
        y_axis: p.y_axis,
        normal: p.normal(),
    }
}

fn loop_data(l: &Loop) -> bridge::LoopData {
    let seg = |s: &Segment| {
        let mut d = bridge::SegmentData {
            kind: bridge::SegmentKind::Line,
            x0: 0.0,
            y0: 0.0,
            x1: 0.0,
            y1: 0.0,
            cx: 0.0,
            cy: 0.0,
            radius: 0.0,
            ccw: true,
            mx: 0.0,
            my: 0.0,
            minor: 0.0,
            t0: 0.0,
            t1: 0.0,
            degree: 0,
            poles: Vec::new(),
            knots: Vec::new(),
            weights: Vec::new(),
            reversed: false,
        };
        match s.clone() {
            Segment::Line { a, b } => ([d.x0, d.y0], [d.x1, d.y1]) = (a, b),
            Segment::Arc {
                centre,
                radius,
                start,
                end,
                ccw,
            } => {
                d.kind = bridge::SegmentKind::Arc;
                ([d.x0, d.y0], [d.x1, d.y1], [d.cx, d.cy]) = (start, end, centre);
                d.radius = radius;
                d.ccw = ccw;
            }
            Segment::Circle { centre, radius } => {
                d.kind = bridge::SegmentKind::Circle;
                [d.cx, d.cy] = centre;
                d.radius = radius;
            }
            Segment::Ellipse {
                centre,
                major,
                minor,
            } => {
                d.kind = bridge::SegmentKind::Ellipse;
                [d.cx, d.cy] = centre;
                [d.mx, d.my] = major;
                d.minor = minor;
            }
            Segment::EllipseArc {
                centre,
                major,
                minor,
                t0,
                t1,
                ccw,
            } => {
                d.kind = bridge::SegmentKind::EllipseArc;
                [d.cx, d.cy] = centre;
                [d.mx, d.my] = major;
                (d.minor, d.t0, d.t1, d.ccw) = (minor, t0, t1, ccw);
                [d.x0, d.y0] = s.point_at(0.0);
                [d.x1, d.y1] = s.point_at(1.0);
            }
            Segment::Spline { data, reversed } => {
                d.kind = bridge::SegmentKind::Spline;
                d.degree = data.degree;
                d.poles = data
                    .control_points
                    .iter()
                    .flat_map(|p| [p[0], p[1]])
                    .collect();
                d.knots = data.knots.clone();
                d.weights = data.weights.clone();
                d.reversed = reversed;
                [d.x0, d.y0] = s.point_at(0.0);
                [d.x1, d.y1] = s.point_at(1.0);
            }
        }
        d
    };
    bridge::LoopData {
        segments: l.segments.iter().map(seg).collect(),
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
        source: ExtrudeSource::Profile(from_ffi_profile(profile)),
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
        K::Smooth => S::Smooth(a, b),
        K::LineDistance => S::LineDistance { a, b, value },
        K::ArcLength => S::ArcLength { arc: a, value },
        K::PointCircleDistance => S::PointCircleDistance {
            point: a,
            circle: b,
            value,
            inside: false, // set from the geometry when added
        },
        K::LineCircleDistance => S::LineCircleDistance {
            line: a,
            circle: b,
            value,
        },
        K::CircleDistance => S::CircleDistance {
            a,
            b,
            value,
            inside: false,
        },
        K::MajorRadius => S::MajorRadius { ellipse: a, value },
        K::MinorRadius => S::MinorRadius { ellipse: a, value },
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
        S::Smooth(a, b) => (K::Smooth, a.0, b.0, 0, v),
        S::LineDistance { a, b, .. } => (K::LineDistance, a.0, b.0, 0, v),
        S::ArcLength { arc, .. } => (K::ArcLength, arc.0, 0, 0, v),
        S::PointCircleDistance { point, circle, .. } => {
            (K::PointCircleDistance, point.0, circle.0, 0, v)
        }
        S::LineCircleDistance { line, circle, .. } => {
            (K::LineCircleDistance, line.0, circle.0, 0, v)
        }
        S::CircleDistance { a, b, .. } => (K::CircleDistance, a.0, b.0, 0, v),
        S::MajorRadius { ellipse, .. } => (K::MajorRadius, ellipse.0, 0, 0, v),
        S::MinorRadius { ellipse, .. } => (K::MinorRadius, ellipse.0, 0, 0, v),
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
    let s = match sketch_mut(t, sketch) {
        Ok(s) => s,
        Err(m) => return refused(m, vec![]),
    };
    let Some(constraint) = to_constraint(kind, a, b, c, value) else {
        return refused("unknown constraint kind".into(), vec![]);
    };
    constraint_result(s.add_constraint(constraint))
}

fn refused(message: String, conflicts: Vec<u64>) -> bridge::ConstraintResult {
    bridge::ConstraintResult {
        ok: false,
        id: 0,
        conflicts,
        message,
        redundant: false,
    }
}

fn constraint_result(r: Result<ConstraintId, SketchError>) -> bridge::ConstraintResult {
    match r {
        Ok(id) => bridge::ConstraintResult {
            ok: true,
            id: id.0,
            conflicts: vec![],
            message: String::new(),
            redundant: false,
        },
        Err(SketchError::Conflicts(ids)) => {
            let message = SketchError::Conflicts(ids.clone()).to_string();
            refused(message, ids.iter().map(|c| c.0).collect())
        }
        Err(SketchError::Redundant) => bridge::ConstraintResult {
            redundant: true,
            ..refused(SketchError::Redundant.to_string(), vec![])
        },
        Err(e) => refused(e.to_string(), vec![]),
    }
}

fn sketch_add_driven(
    t: &mut Timeline,
    sketch: u64,
    kind: bridge::ConstraintKind,
    a: u64,
    b: u64,
    c: u64,
) -> bridge::ConstraintResult {
    let s = match sketch_mut(t, sketch) {
        Ok(s) => s,
        Err(m) => return refused(m, vec![]),
    };
    let Some(constraint) = to_constraint(kind, a, b, c, 1.0) else {
        return refused("unknown constraint kind".into(), vec![]);
    };
    constraint_result(s.add_driven(constraint))
}

fn sketch_set_driven(
    t: &mut Timeline,
    sketch: u64,
    constraint: u64,
    driven: bool,
) -> bridge::ConstraintResult {
    let s = match sketch_mut(t, sketch) {
        Ok(s) => s,
        Err(m) => return refused(m, vec![]),
    };
    constraint_result(
        s.set_driven(ConstraintId(constraint), driven)
            .map(|()| ConstraintId(constraint)),
    )
}

fn sketch_add_ellipse(
    t: &mut Timeline,
    sketch: u64,
    centre: u64,
    major: u64,
    minor: f64,
) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .add_ellipse(EntityId(centre), EntityId(major), minor)
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_add_elliptical_arc(
    t: &mut Timeline,
    sketch: u64,
    centre: u64,
    major: u64,
    minor: f64,
    start: u64,
    end: u64,
) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .add_elliptical_arc(
            EntityId(centre),
            EntityId(major),
            minor,
            EntityId(start),
            EntityId(end),
        )
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_add_spline(
    t: &mut Timeline,
    sketch: u64,
    points: Vec<u64>,
    closed: bool,
) -> Result<u64, String> {
    let ids: Vec<EntityId> = points.into_iter().map(EntityId).collect();
    sketch_mut(t, sketch)?
        .add_spline(&ids, closed)
        .map(|e| e.0)
        .map_err(err)
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
        redundant: s.redundant.iter().map(|c| c.0).collect(),
        degenerate: s.degenerate.iter().map(|e| e.0).collect(),
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
                p3: 0,
                x3: 0.0,
                y3: 0.0,
                minor: 0.0,
                fit: Vec::new(),
                closed: false,
                degree: 0,
                poles: Vec::new(),
                knots: Vec::new(),
                weights: Vec::new(),
                samples: Vec::new(),
            };
            let sample = |seg: &Segment, closed: bool| -> Vec<f64> {
                let n = 96;
                (0..=n)
                    .filter(|&i| !(closed && i == n))
                    .flat_map(|i| {
                        let p = seg.point_at(i as f64 / n as f64);
                        [p[0], p[1]]
                    })
                    .collect()
            };
            match e.geometry.clone() {
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
                Geometry::Ellipse {
                    centre,
                    major,
                    minor_radius,
                } => {
                    info.kind = bridge::SketchEntityKind::Ellipse;
                    (info.p0, info.p1) = (centre.0, major.0);
                    let (c, m) = (pos(s, centre), pos(s, major));
                    ([info.x0, info.y0], [info.x1, info.y1]) = (c, m);
                    info.radius = (m[0] - c[0]).hypot(m[1] - c[1]);
                    info.minor = minor_radius;
                    if info.radius > 0.0 && minor_radius > 0.0 {
                        let seg = Segment::Ellipse {
                            centre: c,
                            major: [m[0] - c[0], m[1] - c[1]],
                            minor: minor_radius,
                        };
                        info.samples = sample(&seg, true);
                    }
                }
                Geometry::EllipticalArc {
                    centre,
                    major,
                    minor_radius,
                    start,
                    end,
                } => {
                    info.kind = bridge::SketchEntityKind::EllipticalArc;
                    (info.p0, info.p1, info.p2, info.p3) = (centre.0, major.0, start.0, end.0);
                    let (c, m) = (pos(s, centre), pos(s, major));
                    ([info.x0, info.y0], [info.x1, info.y1]) = (c, m);
                    [info.x2, info.y2] = pos(s, start);
                    [info.x3, info.y3] = pos(s, end);
                    info.radius = (m[0] - c[0]).hypot(m[1] - c[1]);
                    info.minor = minor_radius;
                    if info.radius > 0.0 && minor_radius > 0.0 {
                        let mv = [m[0] - c[0], m[1] - c[1]];
                        let seg = Segment::EllipseArc {
                            centre: c,
                            major: mv,
                            minor: minor_radius,
                            t0: ellipse_param_of(c, mv, minor_radius, pos(s, start)),
                            t1: ellipse_param_of(c, mv, minor_radius, pos(s, end)),
                            ccw: true,
                        };
                        info.samples = sample(&seg, false);
                    }
                }
                Geometry::Spline { points, closed } => {
                    info.kind = bridge::SketchEntityKind::Spline;
                    info.fit = points.iter().map(|p| p.0).collect();
                    info.closed = closed;
                    if let Some(&first) = points.first() {
                        info.p0 = first.0;
                        [info.x0, info.y0] = pos(s, first);
                    }
                    if let Some(&last) = points.last() {
                        info.p1 = last.0;
                        [info.x1, info.y1] = pos(s, last);
                    }
                    if let Some(data) = s.spline(e.id) {
                        info.degree = data.degree;
                        info.poles = data
                            .control_points
                            .iter()
                            .flat_map(|p| [p[0], p[1]])
                            .collect();
                        info.knots = data.knots.clone();
                        info.weights = data.weights.clone();
                        let seg = Segment::Spline {
                            data: data.clone(),
                            reversed: false,
                        };
                        info.samples = sample(&seg, closed);
                    }
                }
            }
            info
        })
        .collect())
}

/// Eccentric-angle parameter of a point on an ellipse (as profiles use it).
fn ellipse_param_of(c: [f64; 2], major: [f64; 2], minor: f64, p: [f64; 2]) -> f64 {
    let a = major[0].hypot(major[1]);
    let (ux, uy) = (major[0] / a, major[1] / a);
    let (dx, dy) = (p[0] - c[0], p[1] - c[1]);
    ((-dx * uy + dy * ux) / minor).atan2((dx * ux + dy * uy) / a)
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
                driven: s.is_driven(*id),
                redundant: summary.redundant.contains(id),
                has_label: s.label(*id).is_some(),
                label_x: s.label(*id).map_or(0.0, |l| l[0]),
                label_y: s.label(*id).map_or(0.0, |l| l[1]),
            }
        })
        .collect())
}

fn sketch_profiles(t: &Timeline, sketch: u64) -> Result<Vec<bridge::ProfileInfo>, String> {
    let s = sketch_ref(t, sketch)?;
    Ok(find_regions(s)
        .into_iter()
        .map(|r| {
            let mut outlines = Vec::new();
            let mut loop_sizes = Vec::new();
            for l in std::iter::once(&r.outer).chain(r.holes.iter()) {
                loop_sizes.push(l.outline.len());
                outlines.extend(l.outline.iter().flat_map(|p| [p[0], p[1]]));
            }
            bridge::ProfileInfo {
                key: r.key.0.clone(),
                area: r.area(),
                outlines,
                loop_sizes,
            }
        })
        .collect())
}

fn timeline_add_extrude_sketch(
    t: &mut Timeline,
    name: &str,
    sketch: u64,
    profiles: Vec<String>,
    distance: f64,
    op: bridge::BodyOp,
) -> usize {
    let op = match op {
        bridge::BodyOp::Cut => kernel::BodyOp::Cut,
        bridge::BodyOp::NewBody => kernel::BodyOp::NewBody,
        _ => kernel::BodyOp::Join,
    };
    let source = ExtrudeSource::Sketch {
        sketch: FeatureId(sketch),
        regions: profiles.into_iter().map(RegionKey).collect(),
    };
    t.insert(Feature::new(
        name,
        FeatureKind::Extrude {
            source,
            distance,
            op,
        },
    ))
}

// ---- Units ----------------------------------------------------------------

fn unit_from(k: bridge::LengthUnitKind) -> LengthUnit {
    match k {
        bridge::LengthUnitKind::Foot => LengthUnit::Foot,
        bridge::LengthUnitKind::Millimeter => LengthUnit::Millimeter,
        bridge::LengthUnitKind::Centimeter => LengthUnit::Centimeter,
        bridge::LengthUnitKind::Meter => LengthUnit::Meter,
        _ => LengthUnit::Inch,
    }
}

fn unit_to(u: LengthUnit) -> bridge::LengthUnitKind {
    match u {
        LengthUnit::Inch => bridge::LengthUnitKind::Inch,
        LengthUnit::Foot => bridge::LengthUnitKind::Foot,
        LengthUnit::Millimeter => bridge::LengthUnitKind::Millimeter,
        LengthUnit::Centimeter => bridge::LengthUnitKind::Centimeter,
        LengthUnit::Meter => bridge::LengthUnitKind::Meter,
    }
}

fn timeline_units(t: &Timeline) -> bridge::LengthUnitKind {
    unit_to(t.units())
}

fn timeline_set_units(t: &mut Timeline, unit: bridge::LengthUnitKind) {
    t.set_units(unit_from(unit));
}

fn length_format(mm: f64, unit: bridge::LengthUnitKind) -> String {
    units::format_length(mm, unit_from(unit))
}

fn length_parse(text: &str, unit: bridge::LengthUnitKind) -> bridge::LengthParse {
    match units::parse_length(text, unit_from(unit)) {
        Ok(mm) => bridge::LengthParse {
            ok: true,
            mm,
            error: String::new(),
        },
        Err(e) => bridge::LengthParse {
            ok: false,
            mm: 0.0,
            error: e.0,
        },
    }
}

fn length_unit_symbol(unit: bridge::LengthUnitKind) -> &'static str {
    unit_from(unit).symbol()
}

fn length_unit_name(unit: bridge::LengthUnitKind) -> &'static str {
    unit_from(unit).name()
}

fn length_unit_mm(unit: bridge::LengthUnitKind) -> f64 {
    unit_from(unit).mm_per_unit()
}

fn timeline_clone(t: &Timeline) -> Box<Timeline> {
    Box::new(t.clone())
}

fn sketch_add_circle(
    t: &mut Timeline,
    sketch: u64,
    centre: u64,
    radius: f64,
) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .add_circle(EntityId(centre), radius)
        .map(|e| e.0)
        .map_err(err)
}

fn timeline_sketch_consumed(t: &Timeline, sketch: u64) -> bool {
    (0..t.marker().min(t.len())).any(|i| {
        matches!(
            t.get(i).map(|f| &f.kind),
            Some(FeatureKind::Extrude { source: ExtrudeSource::Sketch { sketch: s, .. }, .. }) if s.0 == sketch
        )
    })
}

// ---- Sketch editing tools ----------------------------------------------------

fn ids(v: Vec<u64>) -> Vec<EntityId> {
    v.into_iter().map(EntityId).collect()
}

fn raw(v: Vec<EntityId>) -> Vec<u64> {
    v.into_iter().map(|e| e.0).collect()
}

fn sketch_trim(t: &mut Timeline, sketch: u64, curve: u64, x: f64, y: f64) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .trim(EntityId(curve), [x, y])
        .map_err(err)
}

fn sketch_break(t: &mut Timeline, sketch: u64, curve: u64, x: f64, y: f64) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .break_at(EntityId(curve), [x, y])
        .map_err(err)
}

fn sketch_extend(t: &mut Timeline, sketch: u64, curve: u64, x: f64, y: f64) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .extend(EntityId(curve), [x, y])
        .map_err(err)
}

fn sketch_fillet(t: &mut Timeline, sketch: u64, corner: u64, radius: f64) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .fillet(EntityId(corner), radius)
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_chamfer(
    t: &mut Timeline,
    sketch: u64,
    corner: u64,
    distance: f64,
) -> Result<u64, String> {
    sketch_mut(t, sketch)?
        .chamfer(EntityId(corner), distance)
        .map(|e| e.0)
        .map_err(err)
}

fn sketch_offset(
    t: &mut Timeline,
    sketch: u64,
    curves: Vec<u64>,
    distance: f64,
    x: f64,
    y: f64,
) -> Result<Vec<u64>, String> {
    sketch_mut(t, sketch)?
        .offset(&ids(curves), distance, [x, y])
        .map(raw)
        .map_err(err)
}

fn sketch_mirror(
    t: &mut Timeline,
    sketch: u64,
    curves: Vec<u64>,
    axis: u64,
) -> Result<Vec<u64>, String> {
    sketch_mut(t, sketch)?
        .mirror(&ids(curves), EntityId(axis))
        .map(raw)
        .map_err(err)
}

#[allow(clippy::too_many_arguments)]
fn sketch_pattern_rect(
    t: &mut Timeline,
    sketch: u64,
    curves: Vec<u64>,
    dx: f64,
    dy: f64,
    nx: usize,
    ny: usize,
    sx: f64,
    sy: f64,
) -> Result<Vec<u64>, String> {
    sketch_mut(t, sketch)?
        .pattern_rect(&ids(curves), [dx, dy], nx, ny, sx, sy)
        .map(raw)
        .map_err(err)
}

fn sketch_pattern_circ(
    t: &mut Timeline,
    sketch: u64,
    curves: Vec<u64>,
    cx: f64,
    cy: f64,
    count: usize,
    angle: f64,
) -> Result<Vec<u64>, String> {
    sketch_mut(t, sketch)?
        .pattern_circ(&ids(curves), [cx, cy], count, angle)
        .map(raw)
        .map_err(err)
}

#[allow(clippy::too_many_arguments)]
fn sketch_transform(
    t: &mut Timeline,
    sketch: u64,
    curves: Vec<u64>,
    kind: u8,
    a: f64,
    b: f64,
    c: f64,
    copy: bool,
) -> Result<Vec<u64>, String> {
    let tr = match kind {
        0 => Transform::Move { dx: a, dy: b },
        1 => Transform::Rotate {
            centre: [a, b],
            angle: c,
        },
        _ => Transform::Scale {
            centre: [a, b],
            factor: c,
        },
    };
    sketch_mut(t, sketch)?
        .transform(&ids(curves), tr, copy)
        .map(raw)
        .map_err(err)
}

fn sketch_auto_constrain(t: &mut Timeline, sketch: u64) -> Result<usize, String> {
    Ok(sketch_mut(t, sketch)?.auto_constrain())
}

fn sketch_loose_ends(t: &Timeline, sketch: u64) -> Result<Vec<u64>, String> {
    Ok(raw(sketch_ref(t, sketch)?.loose_ends()))
}

// ---- Labels, editable patterns, clipboard ---------------------------------------

fn sketch_set_label(
    t: &mut Timeline,
    sketch: u64,
    constraint: u64,
    x: f64,
    y: f64,
) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .set_label(ConstraintId(constraint), [x, y])
        .map_err(err)
}

fn group_info(g: &CopyGroup) -> bridge::CopyGroupInfo {
    let mut info = bridge::CopyGroupInfo {
        id: g.id,
        kind: 0,
        sources: raw(g.sources.clone()),
        copies: raw(g.copies.clone()),
        a: 0.0,
        b: 0.0,
        c: 0.0,
        d: 0.0,
        n1: 0,
        n2: 0,
        axis: 0,
    };
    match g.kind {
        CopyKind::Rect {
            dir,
            nx,
            ny,
            sx,
            sy,
        } => {
            (info.a, info.b, info.c, info.d, info.n1, info.n2) = (dir[0], dir[1], sx, sy, nx, ny);
        }
        CopyKind::Circ {
            centre,
            count,
            angle,
        } => {
            info.kind = 1;
            (info.a, info.b, info.c, info.n1) = (centre[0], centre[1], angle, count);
        }
        CopyKind::Mirror { axis } => {
            info.kind = 2;
            info.axis = axis.0;
        }
    }
    info
}

fn sketch_group_of(t: &Timeline, sketch: u64, curve: u64) -> Result<bridge::CopyGroupInfo, String> {
    let s = sketch_ref(t, sketch)?;
    Ok(match s.group_of(EntityId(curve)) {
        Some(g) => group_info(g),
        None => group_info(&CopyGroup {
            id: 0,
            kind: CopyKind::Mirror { axis: EntityId(0) },
            sources: Vec::new(),
            copies: Vec::new(),
        }),
    })
}

fn sketch_edit_group(
    t: &mut Timeline,
    sketch: u64,
    g: bridge::CopyGroupInfo,
) -> Result<Vec<u64>, String> {
    let kind = match g.kind {
        0 => CopyKind::Rect {
            dir: [g.a, g.b],
            nx: g.n1,
            ny: g.n2,
            sx: g.c,
            sy: g.d,
        },
        1 => CopyKind::Circ {
            centre: [g.a, g.b],
            count: g.n1,
            angle: g.c,
        },
        _ => CopyKind::Mirror {
            axis: EntityId(g.axis),
        },
    };
    sketch_mut(t, sketch)?
        .edit_group(g.id, kind)
        .map(raw)
        .map_err(err)
}

fn sketch_delete_group(
    t: &mut Timeline,
    sketch: u64,
    group: u64,
    delete_copies: bool,
) -> Result<(), String> {
    sketch_mut(t, sketch)?
        .delete_group(group, delete_copies)
        .map_err(err)
}

static CLIPBOARD: std::sync::Mutex<Option<Sketch>> = std::sync::Mutex::new(None);

fn sketch_copy(t: &Timeline, sketch: u64, curves: Vec<u64>) -> Result<usize, String> {
    let clip = sketch_ref(t, sketch)?.extract(&ids(curves));
    let n = clip
        .entities()
        .iter()
        .filter(|e| !matches!(e.geometry, Geometry::Point { .. }))
        .count();
    if n == 0 {
        return Err("select lines, circles or other curves to copy".into());
    }
    *CLIPBOARD
        .lock()
        .map_err(|_| "clipboard unavailable".to_string())? = Some(clip);
    Ok(n)
}

fn sketch_paste(t: &mut Timeline, sketch: u64, dx: f64, dy: f64) -> Result<Vec<u64>, String> {
    let clip = CLIPBOARD
        .lock()
        .map_err(|_| "clipboard unavailable".to_string())?
        .clone()
        .ok_or_else(|| "nothing has been copied".to_string())?;
    Ok(raw(sketch_mut(t, sketch)?.paste(&clip, [dx, dy])))
}

fn sketch_clipboard_centre() -> Vec<f64> {
    let Ok(guard) = CLIPBOARD.lock() else {
        return Vec::new();
    };
    let Some(clip) = guard.as_ref() else {
        return Vec::new();
    };
    let pts: Vec<[f64; 2]> = clip
        .entities()
        .iter()
        .filter_map(|e| match e.geometry {
            Geometry::Point { x, y } => Some([x, y]),
            _ => None,
        })
        .collect();
    if pts.is_empty() {
        return Vec::new();
    }
    let (mut x0, mut y0, mut x1, mut y1) = (f64::MAX, f64::MAX, f64::MIN, f64::MIN);
    for p in pts {
        x0 = x0.min(p[0]);
        y0 = y0.min(p[1]);
        x1 = x1.max(p[0]);
        y1 = y1.max(p[1]);
    }
    vec![(x0 + x1) / 2.0, (y0 + y1) / 2.0]
}
