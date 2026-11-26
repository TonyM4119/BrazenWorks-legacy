//! CXX bridge between the Rust model and the C++ side of the app.
//!
//! * `extern "C++"`: geometry functions implemented on Open CASCADE in
//!   `src/bridge/GeometryBridge.cpp`.
//! * `extern "Rust"`: the timeline API the Qt app calls.
//!
//! Everything lives in C++ namespace `model`.

use crate::kernel::{self, Kernel};
use crate::timeline::{Feature, FeatureKind, Timeline};
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
