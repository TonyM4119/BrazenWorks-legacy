//! The ordered feature history shown in the app's bottom timeline bar.

use crate::kernel::{BodyOp, Kernel, Profile};
use crate::sketch::{find_region, RegionKey, Sketch};

/// A feature's id: stable while the timeline is edited, never reused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct FeatureId(pub u64);

/// Where an extrude gets its shape.
#[derive(Clone, Debug, PartialEq)]
pub enum ExtrudeSource {
    /// A fixed rectangle or circle on the XY plane (the Box tool).
    Profile(Profile),
    /// Profiles of an earlier sketch, by their stable keys; several are
    /// extruded together.
    Sketch {
        sketch: FeatureId,
        regions: Vec<RegionKey>,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub enum FeatureKind {
    /// A 2D sketch. It doesn't change the body; later features use its profiles.
    Sketch(Box<Sketch>),
    Extrude {
        source: ExtrudeSource,
        distance: f64,
        op: BodyOp,
    },
    Fillet {
        radius: f64,
    },
}

#[derive(Clone, Debug, PartialEq)]
pub struct Feature {
    /// Assigned by [`Timeline::insert`].
    pub id: FeatureId,
    pub name: String,
    pub kind: FeatureKind,
    pub suppressed: bool,
}

/// What applying one feature did.
enum Outcome<S> {
    /// The feature produced a new body.
    Body(S),
    /// The feature leaves the body as it is (sketches, construction).
    Unchanged,
    /// The feature could not be computed.
    Failed,
}

impl<S> From<Option<S>> for Outcome<S> {
    fn from(o: Option<S>) -> Self {
        o.map_or(Outcome::Failed, Outcome::Body)
    }
}

impl Feature {
    pub fn new(name: impl Into<String>, kind: FeatureKind) -> Self {
        Self {
            id: FeatureId(0),
            name: name.into(),
            kind,
            suppressed: false,
        }
    }

    pub fn type_name(&self) -> &'static str {
        match self.kind {
            FeatureKind::Sketch(_) => "Sketch",
            FeatureKind::Extrude { .. } => "Extrude",
            FeatureKind::Fillet { .. } => "Fillet",
        }
    }

    pub fn sketch(&self) -> Option<&Sketch> {
        match &self.kind {
            FeatureKind::Sketch(s) => Some(s),
            _ => None,
        }
    }

    pub fn sketch_mut(&mut self) -> Option<&mut Sketch> {
        match &mut self.kind {
            FeatureKind::Sketch(s) => Some(s),
            _ => None,
        }
    }

    /// Features are pure: given the body built so far (and the features
    /// before this one, for references), return the new body. That makes
    /// rollback, reordering and dimension edits a simple replay.
    fn apply<K: Kernel>(
        &self,
        kernel: &K,
        input: Option<&K::Shape>,
        earlier: &[Feature],
    ) -> Outcome<K::Shape> {
        match &self.kind {
            FeatureKind::Sketch(_) => Outcome::Unchanged,
            FeatureKind::Extrude {
                source,
                distance,
                op,
            } => {
                let Some(tool) = extrude_tool(kernel, source, *distance, earlier) else {
                    return Outcome::Failed;
                };
                match (op, input) {
                    (BodyOp::NewBody, _) | (BodyOp::Join, None) => Outcome::Body(tool),
                    (BodyOp::Join, Some(body)) => kernel.fuse(body, &tool).into(),
                    (BodyOp::Cut, Some(body)) => kernel.cut(body, &tool).into(),
                    (BodyOp::Cut, None) => Outcome::Failed,
                }
            }
            FeatureKind::Fillet { radius } => match input {
                Some(body) => kernel.fillet_all_edges(body, *radius).into(),
                None => Outcome::Failed,
            },
        }
    }
}

/// The solid an extrude adds or removes. A sketch must come before the
/// extrude that uses it, and every named profile must still exist.
fn extrude_tool<K: Kernel>(
    kernel: &K,
    source: &ExtrudeSource,
    distance: f64,
    earlier: &[Feature],
) -> Option<K::Shape> {
    match source {
        ExtrudeSource::Profile(profile) => kernel.extrude(profile, distance),
        ExtrudeSource::Sketch { sketch, regions } => {
            let feature = earlier.iter().find(|f| f.id == *sketch)?;
            if feature.suppressed {
                return None;
            }
            let sketch = feature.sketch()?;
            let mut tool: Option<K::Shape> = None;
            for key in regions {
                let region = find_region(sketch, key)?;
                let solid = kernel.extrude_region(&sketch.plane, &region, distance)?;
                tool = Some(match tool {
                    None => solid,
                    Some(t) => kernel.fuse(&t, &solid)?,
                });
            }
            tool
        }
    }
}

pub struct RebuildResult<S> {
    pub body: Option<S>,
    /// Indices of features that could not be applied.
    pub failed: Vec<usize>,
}

/// Features at index >= `marker()` are rolled back: kept, but not applied.
#[derive(Debug, Clone)]
pub struct Timeline {
    features: Vec<Feature>,
    marker: usize,
    next_id: u64,
}

impl Default for Timeline {
    fn default() -> Self {
        Self {
            features: Vec::new(),
            marker: 0,
            next_id: 1,
        }
    }
}

impl Timeline {
    pub fn new() -> Self {
        Self::default()
    }

    /// Inserts at the rollback marker (like dragging the marker back and
    /// adding a feature in Fusion-style CAD) and moves the marker past it.
    pub fn insert(&mut self, mut feature: Feature) -> usize {
        feature.id = FeatureId(self.next_id);
        self.next_id += 1;
        let index = self.marker;
        self.features.insert(index, feature);
        self.marker += 1;
        index
    }

    pub fn remove(&mut self, index: usize) -> Option<Feature> {
        if index >= self.features.len() {
            return None;
        }
        if index < self.marker {
            self.marker -= 1;
        }
        Some(self.features.remove(index))
    }

    pub fn len(&self) -> usize {
        self.features.len()
    }

    pub fn is_empty(&self) -> bool {
        self.features.is_empty()
    }

    pub fn get(&self, index: usize) -> Option<&Feature> {
        self.features.get(index)
    }

    pub fn get_mut(&mut self, index: usize) -> Option<&mut Feature> {
        self.features.get_mut(index)
    }

    pub fn index_of(&self, id: FeatureId) -> Option<usize> {
        self.features.iter().position(|f| f.id == id)
    }

    pub fn find(&self, id: FeatureId) -> Option<&Feature> {
        self.features.iter().find(|f| f.id == id)
    }

    pub fn find_mut(&mut self, id: FeatureId) -> Option<&mut Feature> {
        self.features.iter_mut().find(|f| f.id == id)
    }

    pub fn marker(&self) -> usize {
        self.marker
    }

    pub fn set_marker(&mut self, marker: usize) {
        self.marker = marker.min(self.features.len());
    }

    /// Replays every active feature up to the marker. A failed feature is
    /// skipped so later features still get a chance, and its index reported.
    pub fn rebuild<K: Kernel>(&self, kernel: &K) -> RebuildResult<K::Shape> {
        let mut body: Option<K::Shape> = None;
        let mut failed = Vec::new();
        for (i, feature) in self.features[..self.marker].iter().enumerate() {
            if feature.suppressed {
                continue;
            }
            match feature.apply(kernel, body.as_ref(), &self.features[..i]) {
                Outcome::Body(next) => body = Some(next),
                Outcome::Unchanged => {}
                Outcome::Failed => failed.push(i),
            }
        }
        RebuildResult { body, failed }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sketch::{find_regions, SketchConstraint, SketchPlane};

    /// Mock kernel: a "shape" is just its volume. Enough to test model logic.
    struct VolumeKernel;

    impl Kernel for VolumeKernel {
        type Shape = f64;

        fn extrude(&self, p: &Profile, d: f64) -> Option<f64> {
            let area = match *p {
                Profile::Rect { width, height, .. } if width > 0.0 && height > 0.0 => {
                    width * height
                }
                Profile::Circle { radius, .. } if radius > 0.0 => {
                    std::f64::consts::PI * radius * radius
                }
                _ => return None,
            };
            (d != 0.0).then(|| area * d.abs())
        }
        fn fuse(&self, a: &f64, b: &f64) -> Option<f64> {
            Some(a + b)
        }
        fn cut(&self, a: &f64, b: &f64) -> Option<f64> {
            Some((a - b).max(0.0))
        }
        fn fillet_all_edges(&self, s: &f64, r: f64) -> Option<f64> {
            (r > 0.0 && r < 10.0).then(|| s * 0.99)
        }
        fn extrude_region(
            &self,
            _: &SketchPlane,
            region: &crate::sketch::Region,
            d: f64,
        ) -> Option<f64> {
            (d != 0.0).then(|| region.area() * d.abs())
        }
    }

    fn plate() -> Feature {
        let profile = Profile::Rect {
            x: 0.0,
            y: 0.0,
            width: 40.0,
            height: 20.0,
        };
        Feature::new(
            "Base",
            FeatureKind::Extrude {
                source: ExtrudeSource::Profile(profile),
                distance: 5.0,
                op: BodyOp::Join,
            },
        )
    }

    fn hole() -> Feature {
        let profile = Profile::Circle {
            cx: 20.0,
            cy: 10.0,
            radius: 3.0,
        };
        Feature::new(
            "Hole",
            FeatureKind::Extrude {
                source: ExtrudeSource::Profile(profile),
                distance: 5.0,
                op: BodyOp::Cut,
            },
        )
    }

    #[test]
    fn rollback_and_insert_at_marker() {
        let mut tl = Timeline::new();
        tl.insert(plate());
        tl.insert(hole());
        assert_eq!(tl.marker(), 2);
        let full = tl.rebuild(&VolumeKernel).body.unwrap();
        assert!(full < 4000.0);

        tl.set_marker(1);
        assert_eq!(tl.rebuild(&VolumeKernel).body, Some(4000.0));

        tl.insert(Feature::new("Round", FeatureKind::Fillet { radius: 0.5 }));
        assert_eq!(tl.get(1).unwrap().name, "Round");
        assert_eq!(tl.get(2).unwrap().name, "Hole");
    }

    #[test]
    fn dimension_edit_replays() {
        let mut tl = Timeline::new();
        tl.insert(plate());
        if let FeatureKind::Extrude { distance, .. } = &mut tl.get_mut(0).unwrap().kind {
            *distance = 10.0;
        }
        assert_eq!(tl.rebuild(&VolumeKernel).body, Some(8000.0));
    }

    #[test]
    fn suppressed_and_failing_features() {
        let mut tl = Timeline::new();
        tl.insert(plate());
        tl.insert(hole());
        tl.get_mut(1).unwrap().suppressed = true;
        assert_eq!(tl.rebuild(&VolumeKernel).body, Some(4000.0));

        tl.insert(Feature::new(
            "Too big",
            FeatureKind::Fillet { radius: 100.0 },
        ));
        let r = tl.rebuild(&VolumeKernel);
        assert_eq!(r.failed, vec![2]);
        assert_eq!(r.body, Some(4000.0));
    }

    #[test]
    fn cut_without_body_fails() {
        let mut tl = Timeline::new();
        tl.insert(hole());
        let r = tl.rebuild(&VolumeKernel);
        assert!(r.body.is_none());
        assert_eq!(r.failed, vec![0]);
    }

    #[test]
    fn sketches_pass_the_body_through_and_have_stable_ids() {
        use crate::sketch::{OriginPlane, SketchPlane};
        let mut tl = Timeline::new();
        let sketch = Sketch::new("Sketch1", SketchPlane::origin_plane(OriginPlane::XY));
        tl.insert(Feature::new(
            "Sketch1",
            FeatureKind::Sketch(Box::new(sketch)),
        ));
        tl.insert(plate());
        tl.insert(Feature::new(
            "Sketch2",
            FeatureKind::Sketch(Box::new(Sketch::new("Sketch2", SketchPlane::default()))),
        ));
        let r = tl.rebuild(&VolumeKernel);
        assert!(r.failed.is_empty());
        assert_eq!(r.body, Some(4000.0));

        let ids: Vec<FeatureId> = (0..tl.len()).map(|i| tl.get(i).unwrap().id).collect();
        assert_eq!(ids, vec![FeatureId(1), FeatureId(2), FeatureId(3)]);
        tl.remove(0);
        assert_eq!(tl.index_of(FeatureId(3)), Some(1));
        let added = tl.insert(plate());
        assert_eq!(
            tl.get(added).unwrap().id,
            FeatureId(4),
            "ids are never reused"
        );
        assert_eq!(tl.get(0).unwrap().type_name(), "Extrude");
        assert!(tl.find(FeatureId(3)).unwrap().sketch().is_some());
    }

    /// Sketch with a 40×20 rectangle (width dimensioned) and a hole of radius 3.
    fn plate_sketch() -> (Sketch, crate::sketch::ConstraintId) {
        let mut s = Sketch::new("Sketch1", SketchPlane::default());
        let lines = s.add_rectangle([0.0, 0.0], [40.0, 20.0]).unwrap();
        let corner = match s.entity(lines[0]).unwrap().geometry {
            crate::sketch::Geometry::Line { start, .. } => start,
            _ => unreachable!(),
        };
        s.add_constraint(SketchConstraint::Fix(corner)).unwrap();
        let w = s
            .add_constraint(SketchConstraint::Length {
                line: lines[0],
                value: 40.0,
            })
            .unwrap();
        s.add_constraint(SketchConstraint::Length {
            line: lines[1],
            value: 20.0,
        })
        .unwrap();
        s.add_circle_xy([20.0, 10.0], 3.0).unwrap();
        (s, w)
    }

    fn sketch_extrude(
        tl: &mut Timeline,
        sketch: FeatureId,
        regions: Vec<RegionKey>,
        distance: f64,
    ) -> usize {
        let source = ExtrudeSource::Sketch { sketch, regions };
        tl.insert(Feature::new(
            "Extrude",
            FeatureKind::Extrude {
                source,
                distance,
                op: BodyOp::Join,
            },
        ))
    }

    #[test]
    fn extrude_from_sketch_profiles_follows_dimension_edits() {
        let (sketch, width) = plate_sketch();
        let ring = find_regions(&sketch)[0].key.clone();
        let mut tl = Timeline::new();
        let si = tl.insert(Feature::new(
            "Sketch1",
            FeatureKind::Sketch(Box::new(sketch)),
        ));
        let sid = tl.get(si).unwrap().id;
        sketch_extrude(&mut tl, sid, vec![ring.clone()], 5.0);

        let ring_area = 800.0 - std::f64::consts::PI * 9.0;
        let v = tl.rebuild(&VolumeKernel).body.unwrap();
        assert!((v - ring_area * 5.0).abs() < 1e-9);

        // Edit the sketch's width: the extrude follows.
        tl.find_mut(sid)
            .unwrap()
            .sketch_mut()
            .unwrap()
            .set_dimension(width, 60.0)
            .unwrap();
        let v = tl.rebuild(&VolumeKernel).body.unwrap();
        assert!((v - (1200.0 - std::f64::consts::PI * 9.0) * 5.0).abs() < 1e-9);
    }

    #[test]
    fn extrude_several_profiles_together() {
        let (sketch, _) = plate_sketch();
        let keys: Vec<RegionKey> = find_regions(&sketch).into_iter().map(|r| r.key).collect();
        let mut tl = Timeline::new();
        let si = tl.insert(Feature::new(
            "Sketch1",
            FeatureKind::Sketch(Box::new(sketch)),
        ));
        let sid = tl.get(si).unwrap().id;
        sketch_extrude(&mut tl, sid, keys, 2.0);
        // Ring + disc = the whole rectangle.
        assert!((tl.rebuild(&VolumeKernel).body.unwrap() - 1600.0).abs() < 1e-9);
    }

    #[test]
    fn extrude_with_missing_profile_or_sketch_fails_cleanly() {
        let (sketch, _) = plate_sketch();
        let mut tl = Timeline::new();
        let si = tl.insert(Feature::new(
            "Sketch1",
            FeatureKind::Sketch(Box::new(sketch)),
        ));
        let sid = tl.get(si).unwrap().id;
        let bad = sketch_extrude(&mut tl, sid, vec![RegionKey("no-such-region".into())], 5.0);
        let wrong_sketch = sketch_extrude(&mut tl, FeatureId(999), vec![], 5.0);
        let r = tl.rebuild(&VolumeKernel);
        assert_eq!(r.failed, vec![bad, wrong_sketch]);
        assert!(r.body.is_none());
    }

    #[test]
    fn extrude_cannot_use_a_later_sketch() {
        let (sketch, _) = plate_sketch();
        let key = find_regions(&sketch)[0].key.clone();
        let mut tl = Timeline::new();
        let ext = sketch_extrude(&mut tl, FeatureId(2), vec![key], 5.0); // sketch inserted next gets id 2
        tl.insert(Feature::new(
            "Sketch1",
            FeatureKind::Sketch(Box::new(sketch)),
        ));
        assert_eq!(tl.rebuild(&VolumeKernel).failed, vec![ext]);
    }

    #[test]
    fn remove_before_marker_shifts_marker() {
        let mut tl = Timeline::new();
        tl.insert(plate());
        tl.insert(hole());
        tl.set_marker(2);
        tl.remove(0);
        assert_eq!(tl.marker(), 1);
        assert!(tl.remove(5).is_none());
    }
}
