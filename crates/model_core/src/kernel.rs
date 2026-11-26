//! The geometry operations the model needs, as a trait. The real
//! implementation lives in C++ on Open CASCADE (see `ffi.rs`); tests use a
//! lightweight mock so model logic can be tested in pure Rust.

/// A closed 2D profile on the XY plane (sketches will produce these).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Profile {
    /// Rectangle with its lower-left corner at (x, y).
    Rect {
        x: f64,
        y: f64,
        width: f64,
        height: f64,
    },
    /// Circle centred on (cx, cy).
    Circle { cx: f64, cy: f64, radius: f64 },
}

/// How a new solid combines with the existing body.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BodyOp {
    Join,
    Cut,
    NewBody,
}

use crate::sketch::{Region, SketchPlane};

/// Every method returns `None` when the operation fails (bad input, kernel
/// error); the timeline reports the failing feature instead of crashing.
pub trait Kernel {
    type Shape;

    fn extrude(&self, profile: &Profile, distance: f64) -> Option<Self::Shape>;
    /// Extrudes a sketch region (outer loop minus holes) along the plane's
    /// normal by `distance` (negative = the other way).
    fn extrude_region(
        &self,
        plane: &SketchPlane,
        region: &Region,
        distance: f64,
    ) -> Option<Self::Shape>;
    fn fuse(&self, a: &Self::Shape, b: &Self::Shape) -> Option<Self::Shape>;
    fn cut(&self, a: &Self::Shape, b: &Self::Shape) -> Option<Self::Shape>;
    fn fillet_all_edges(&self, shape: &Self::Shape, radius: f64) -> Option<Self::Shape>;
}
