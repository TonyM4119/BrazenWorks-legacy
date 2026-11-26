use super::*;
use crate::sketch::model::{Sketch, SketchConstraint};
use crate::sketch::plane::SketchPlane;

const TOL: f64 = 1e-6;

fn sketch() -> Sketch {
    Sketch::new("Sketch1", SketchPlane::default())
}

fn areas(regions: &[Region]) -> Vec<f64> {
    regions.iter().map(Region::area).collect()
}

fn close(a: f64, b: f64, rel: f64) -> bool {
    (a - b).abs() <= rel * b.abs().max(1.0)
}

#[test]
fn rectangle_is_one_region() {
    let mut s = sketch();
    s.add_rectangle([0.0, 0.0], [40.0, 20.0]).unwrap();
    let r = find_regions(&s);
    assert_eq!(r.len(), 1);
    assert!((r[0].area() - 800.0).abs() < TOL);
    assert_eq!(r[0].outer.segments.len(), 4);
    assert!(r[0].holes.is_empty());
}

#[test]
fn circle_inside_rectangle_gives_ring_and_disc() {
    let mut s = sketch();
    s.add_rectangle([0.0, 0.0], [40.0, 20.0]).unwrap();
    s.add_circle_xy([20.0, 10.0], 3.0).unwrap();
    let r = find_regions(&s);
    assert_eq!(r.len(), 2);
    assert_eq!(
        r[0].holes.len(),
        1,
        "the rectangle has the circle as a hole"
    );
    assert!(
        close(r[0].area(), 800.0 - PI * 9.0, 1e-12),
        "{:?}",
        areas(&r)
    );
    assert!(close(r[1].area(), PI * 9.0, 1e-12));
}

#[test]
fn line_across_rectangle_splits_it() {
    // Curves meet at end points in M1, so the edges are drawn in halves.
    let mut s = sketch();
    let p = [
        s.add_point(0.0, 0.0),
        s.add_point(20.0, 0.0),
        s.add_point(40.0, 0.0),
        s.add_point(40.0, 20.0),
        s.add_point(20.0, 20.0),
        s.add_point(0.0, 20.0),
    ];
    for (a, b) in [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (5, 0), (1, 4)] {
        s.add_line(p[a], p[b]).unwrap();
    }
    let r = find_regions(&s);
    assert_eq!(r.len(), 2);
    assert!(
        r.iter().all(|x| (x.area() - 400.0).abs() < TOL),
        "{:?}",
        areas(&r)
    );
    assert_ne!(r[0].key, r[1].key);
}

#[test]
fn open_shapes_and_dangling_lines_make_no_region() {
    let mut s = sketch();
    let a = s.add_point(0.0, 0.0);
    let b = s.add_point(10.0, 0.0);
    let c = s.add_point(10.0, 10.0);
    s.add_line(a, b).unwrap();
    s.add_line(b, c).unwrap();
    assert!(find_regions(&s).is_empty());

    // A closed triangle with a stray line hanging off one corner.
    s.add_line(c, a).unwrap();
    let tail = s.add_point(20.0, 20.0);
    s.add_line(c, tail).unwrap();
    let r = find_regions(&s);
    assert_eq!(r.len(), 1);
    assert!((r[0].area() - 50.0).abs() < TOL);
}

#[test]
fn construction_geometry_is_ignored() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [10.0, 10.0]).unwrap();
    s.set_construction(lines[0], true).unwrap();
    assert!(find_regions(&s).is_empty());
    let c = s.add_circle_xy([5.0, 5.0], 2.0).unwrap();
    s.set_construction(c, true).unwrap();
    assert!(find_regions(&s).is_empty());
}

#[test]
fn coincident_points_join_separate_lines() {
    // Two separately drawn lines per side, joined only by Coincident constraints.
    let mut s = sketch();
    let l = [
        s.add_line_xy([0.0, 0.0], [10.0, 0.0]),
        s.add_line_xy([10.0, 0.0], [10.0, 10.0]),
        s.add_line_xy([10.0, 10.0], [0.0, 10.0]),
        s.add_line_xy([0.0, 10.0], [0.0, 0.0]),
    ];
    let ends = |s: &Sketch, id| match s.entity(id).unwrap().geometry {
        Geometry::Line { start, end } => (start, end),
        _ => unreachable!(),
    };
    for i in 0..4 {
        let (_, end) = ends(&s, l[i]);
        let (start, _) = ends(&s, l[(i + 1) % 4]);
        s.add_constraint(SketchConstraint::Coincident(end, start))
            .unwrap();
    }
    let r = find_regions(&s);
    assert_eq!(r.len(), 1);
    assert!((r[0].area() - 100.0).abs() < TOL);
}

#[test]
fn slot_of_lines_and_arcs() {
    let mut s = sketch();
    let a = s.add_point(0.0, 0.0);
    let b = s.add_point(40.0, 0.0);
    let c = s.add_point(40.0, 12.0);
    let d = s.add_point(0.0, 12.0);
    s.add_line(a, b).unwrap();
    s.add_line(c, d).unwrap();
    let rc = s.add_point(40.0, 6.0);
    let lc = s.add_point(0.0, 6.0);
    s.add_arc(rc, b, c).unwrap(); // right end, CCW from bottom to top
    s.add_arc(lc, d, a).unwrap(); // left end, CCW from top to bottom
    let r = find_regions(&s);
    assert_eq!(r.len(), 1);
    assert!(
        close(r[0].area(), 40.0 * 12.0 + PI * 36.0, 1e-12),
        "{:?}",
        areas(&r)
    );
    let arcs = r[0]
        .outer
        .segments
        .iter()
        .filter(|g| matches!(g, Segment::Arc { .. }))
        .count();
    assert_eq!(arcs, 2);
}

#[test]
fn nested_circles_alternate_holes() {
    let mut s = sketch();
    for r in [30.0, 20.0, 10.0] {
        s.add_circle_xy([0.0, 0.0], r).unwrap();
    }
    let r = find_regions(&s);
    assert_eq!(r.len(), 3);
    // Outer ring (30 minus 20), middle ring (20 minus 10), inner disc.
    assert_eq!(
        r.iter().map(|x| x.holes.len()).collect::<Vec<_>>(),
        vec![1, 1, 0]
    );
    assert!(close(r[0].area(), PI * (900.0 - 400.0), 1e-12));
    assert!(close(r[1].area(), PI * (400.0 - 100.0), 1e-12));
}

#[test]
fn region_keys_survive_dimension_edits() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [40.0, 20.0]).unwrap();
    let corner = match s.entity(lines[0]).unwrap().geometry {
        Geometry::Line { start, .. } => start,
        _ => unreachable!(),
    };
    s.add_constraint(SketchConstraint::Fix(corner)).unwrap();
    let w = s
        .add_constraint(SketchConstraint::Length {
            line: lines[0],
            value: 40.0,
        })
        .unwrap();
    s.add_circle_xy([20.0, 10.0], 3.0).unwrap();
    let before: Vec<RegionKey> = find_regions(&s).into_iter().map(|r| r.key).collect();

    s.set_dimension(w, 70.0).unwrap();
    let after = find_regions(&s);
    let keys: Vec<RegionKey> = after.iter().map(|r| r.key.clone()).collect();
    assert_eq!(keys, before);
    let ring = find_region(&s, &before[0]).unwrap();
    assert!(close(ring.area(), 70.0 * 20.0 - PI * 9.0, 1e-12));
}

#[test]
fn segments_form_closed_ccw_chains() {
    let mut s = sketch();
    s.add_rectangle([0.0, 0.0], [10.0, 5.0]).unwrap();
    let r = &find_regions(&s)[0];
    let segs = &r.outer.segments;
    for i in 0..segs.len() {
        let (Segment::Line { b, .. }, Segment::Line { a, .. }) =
            (segs[i], segs[(i + 1) % segs.len()])
        else {
            panic!("expected lines")
        };
        assert!(dist(a, b) < TOL, "segment {i} ends where the next starts");
    }
    assert!(signed_area(&r.outer.outline) > 0.0);
}
