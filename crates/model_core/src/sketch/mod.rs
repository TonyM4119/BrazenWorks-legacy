//! 2D sketches: geometry, constraints and dimensions on a plane.
//!
//! PR 1 adds the constraint solver; the sketch model itself (planes, stable
//! entity ids, feature integration) builds on it next.

pub mod solver_adapter;

pub use solver_adapter::{
    ConstraintSolver, CurveKind, CurveRef, Freedom, PointRef, Rule, RuleRef, SolveReport,
    SolveStatus, SolverError,
};
