//! 2D sketches: geometry, constraints and dimensions on a plane.

pub mod model;
pub mod plane;
pub mod profiles;
pub mod solver_adapter;

pub use model::{
    ConstraintId, Entity, EntityId, Geometry, Sketch, SketchConstraint, SketchError, SolveSummary,
    Transform,
};
pub use plane::{OriginPlane, SketchPlane};
pub use profiles::{find_region, find_regions, Loop, Region, RegionKey, Segment};
pub use solver_adapter::SolveStatus;
