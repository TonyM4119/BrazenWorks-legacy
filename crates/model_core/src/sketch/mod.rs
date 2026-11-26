//! 2D sketches: geometry, constraints and dimensions on a plane.

pub mod model;
pub mod plane;
pub mod solver_adapter;

pub use model::{
    ConstraintId, Entity, EntityId, Geometry, Sketch, SketchConstraint, SketchError, SolveSummary,
};
pub use plane::{OriginPlane, SketchPlane};
pub use solver_adapter::SolveStatus;
