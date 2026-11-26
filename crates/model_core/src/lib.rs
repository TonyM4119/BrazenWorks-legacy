//! Parametric model core.
//!
//! This crate owns the design document: the feature timeline and, as they
//! land, the sketch constraint solver and CAM toolpath logic. It never calls
//! Open CASCADE directly; it goes through the [`kernel::Kernel`] trait, which
//! the C++ geometry layer implements over the CXX bridge (`ffi` feature).

pub mod kernel;
pub mod sketch;
pub mod timeline;

#[cfg(feature = "ffi")]
mod ffi;

pub use kernel::{BodyOp, Kernel, Profile};
pub use timeline::{Feature, FeatureKind, RebuildResult, Timeline};
