use super::*;
use crate::sketch::plane::SketchPlane;
use crate::sketch::profiles::find_regions;

fn sketch() -> Sketch {
    Sketch::new("ops", SketchPlane::default())
}

fn line_pts(s: &Sketch, l: EntityId) -> (P, P) {
    let (a, b) = s.entity(l).unwrap().geometry.ends().unwrap();
    (s.point(a).unwrap(), s.point(b).unwrap())
}

fn near(a: P, b: P) -> bool {
    len(sub(a, b)) < 1e-6
}

#[test]
fn trim_removes_the_picked_piece_between_crossings() {
    let mut s = sketch();
    let h = s.add_line_xy([0.0, 0.0], [30.0, 0.0]);
    s.add_line_xy([10.0, -5.0], [10.0, 5.0]);
    s.add_line_xy([20.0, -5.0], [20.0, 5.0]);
    s.trim(h, [15.0, 0.0]).unwrap();
    // Two pieces left: 0..10 and 20..30.
    let lines: Vec<(P, P)> = s
        .entities()
        .iter()
        .filter(|e| matches!(e.geometry, Geometry::Line { .. }))
        .map(|e| line_pts(&s, e.id))
        .filter(|(a, b)| a[1].abs() < 1e-9 && b[1].abs() < 1e-9)
        .collect();
    assert_eq!(lines.len(), 2);
    assert!(lines
        .iter()
        .any(|(a, b)| near(*a, [0.0, 0.0]) && near(*b, [10.0, 0.0])));
    assert!(lines
        .iter()
        .any(|(a, b)| near(*a, [20.0, 0.0]) && near(*b, [30.0, 0.0])));
}

#[test]
fn trim_without_crossings_deletes_the_curve() {
    let mut s = sketch();
    let l = s.add_line_xy([0.0, 0.0], [10.0, 0.0]);
    s.trim(l, [5.0, 0.0]).unwrap();
    assert!(s.entity(l).is_none());
    assert!(s.entities().is_empty(), "its points went too");
}

#[test]
fn trimming_a_circle_leaves_an_arc() {
    let mut s = sketch();
    let c = s.add_circle_xy([0.0, 0.0], 10.0).unwrap();
    s.add_line_xy([-20.0, 0.0], [20.0, 0.0]);
    s.trim(c, [0.0, 10.0]).unwrap(); // the top half goes
    match s.entity(c).unwrap().geometry {
        Geometry::Arc { start, end, .. } => {
            // Bottom half, counter-clockwise from (-10,0) to (10,0).
            assert!(near(s.point(start).unwrap(), [-10.0, 0.0]));
            assert!(near(s.point(end).unwrap(), [10.0, 0.0]));
        }
        ref g => panic!("expected an arc, got {g:?}"),
    }
}

#[test]
fn break_splits_a_line_at_a_crossing() {
    let mut s = sketch();
    let h = s.add_line_xy([0.0, 0.0], [30.0, 0.0]);
    s.add_line_xy([10.0, -5.0], [10.0, 5.0]);
    s.break_at(h, [20.0, 0.0]).unwrap();
    let horizontal = s
        .entities()
        .iter()
        .filter(|e| matches!(e.geometry, Geometry::Line { .. }))
        .filter(|e| {
            let (a, b) = line_pts(&s, e.id);
            a[1].abs() < 1e-9 && b[1].abs() < 1e-9
        })
        .count();
    assert_eq!(horizontal, 2);
}

#[test]
fn crossing_lines_broken_apart_make_profiles() {
    // A rectangle with a line across it: after Break, two regions.
    let mut s = sketch();
    s.add_rectangle([0.0, 0.0], [20.0, 10.0]).unwrap();
    let across = s.add_line_xy([10.0, -5.0], [10.0, 15.0]);
    assert_eq!(find_regions(&s).len(), 1);
    // Trim the overhanging ends, then break the rectangle sides at the line.
    s.trim(across, [10.0, -3.0]).unwrap();
    s.trim(across, [10.0, 13.0]).unwrap();
    let bottom = s
        .entities()
        .iter()
        .find(|e| {
            matches!(e.geometry, Geometry::Line { .. }) && {
                let (a, b) = line_pts(&s, e.id);
                a[1].abs() < 1e-9 && b[1].abs() < 1e-9 && (a[0] - b[0]).abs() > 15.0
            }
        })
        .unwrap()
        .id;
    let top = s
        .entities()
        .iter()
        .find(|e| {
            matches!(e.geometry, Geometry::Line { .. }) && {
                let (a, b) = line_pts(&s, e.id);
                (a[1] - 10.0).abs() < 1e-9 && (b[1] - 10.0).abs() < 1e-9
            }
        })
        .unwrap()
        .id;
    s.break_at(bottom, [5.0, 0.0]).unwrap();
    s.break_at(top, [5.0, 10.0]).unwrap();
    let regions = find_regions(&s);
    assert_eq!(regions.len(), 2, "split into two halves");
    assert!((regions[0].area() - 100.0).abs() < 1e-6);
}

#[test]
fn extend_reaches_the_next_line() {
    let mut s = sketch();
    let l = s.add_line_xy([0.0, 0.0], [5.0, 0.0]);
    s.add_line_xy([12.0, -5.0], [12.0, 5.0]);
    s.extend(l, [4.0, 0.0]).unwrap();
    let (_, b) = line_pts(&s, l);
    assert!(near(b, [12.0, 0.0]), "{b:?}");
}

#[test]
fn fillet_rounds_a_rectangle_corner() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [20.0, 10.0]).unwrap();
    let (corner, _) = s.entity(lines[0]).unwrap().geometry.ends().unwrap(); // (0,0)
    let arc = s.fillet(corner, 3.0).unwrap();
    assert!((s.radius(arc).unwrap() - 3.0).abs() < 1e-6);
    // The bottom line now starts 3 along.
    let (a, b) = line_pts(&s, lines[0]);
    assert!(near(a, [3.0, 0.0]) || near(b, [3.0, 0.0]));
    // Still one closed profile, area = 200 - (9 - 9π/4).
    let r = find_regions(&s);
    assert_eq!(r.len(), 1);
    let expected = 200.0 - (9.0 - 9.0 * std::f64::consts::PI / 4.0);
    assert!((r[0].area() - expected).abs() < 1e-6, "{}", r[0].area());
}

#[test]
fn chamfer_cuts_a_corner() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [20.0, 10.0]).unwrap();
    let (corner, _) = s.entity(lines[0]).unwrap().geometry.ends().unwrap();
    s.chamfer(corner, 2.0).unwrap();
    let r = find_regions(&s);
    assert!((r[0].area() - (200.0 - 2.0)).abs() < 1e-6);
}

#[test]
fn offset_of_a_rectangle_is_a_bigger_rectangle() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [20.0, 10.0]).unwrap();
    let made = s.offset(&lines, 2.0, [30.0, 5.0]).unwrap(); // outside
    assert_eq!(made.len(), 4);
    let regions = find_regions(&s);
    let biggest = regions.iter().map(|r| r.outer.area).fold(0.0, f64::max);
    assert!((biggest - 24.0 * 14.0).abs() < 1e-6, "{biggest}");
}

#[test]
fn offset_inside_a_circle() {
    let mut s = sketch();
    let c = s.add_circle_xy([0.0, 0.0], 10.0).unwrap();
    let made = s.offset(&[c], 3.0, [1.0, 1.0]).unwrap();
    assert!((s.radius(made[0]).unwrap() - 7.0).abs() < 1e-6);
}

#[test]
fn mirror_copies_across_a_line() {
    let mut s = sketch();
    let axis = s.add_line_xy([0.0, -10.0], [0.0, 10.0]);
    let c = s.add_circle_xy([5.0, 2.0], 1.0).unwrap();
    let made = s.mirror(&[c], axis).unwrap();
    let centre = s.entity(made[0]).unwrap().geometry.defining_points()[0];
    assert!(near(s.point(centre).unwrap(), [-5.0, 2.0]));
    // The copy follows the original's size.
    s.add_constraint(SketchConstraint::Diameter {
        curve: c,
        value: 4.0,
    })
    .unwrap();
    assert!((s.radius(made[0]).unwrap() - 2.0).abs() < 1e-6);
}

#[test]
fn patterns_make_copies() {
    let mut s = sketch();
    let c = s.add_circle_xy([0.0, 0.0], 1.0).unwrap();
    assert_eq!(
        s.pattern_rect(&[c], [1.0, 0.0], 3, 2, 5.0, 4.0)
            .unwrap()
            .len(),
        5
    );
    let mut s = sketch();
    let c = s.add_circle_xy([10.0, 0.0], 1.0).unwrap();
    let made = s.pattern_circ(&[c], [0.0, 0.0], 4, TAU).unwrap();
    assert_eq!(made.len(), 3);
    let centre = s.entity(made[0]).unwrap().geometry.defining_points()[0];
    assert!(near(s.point(centre).unwrap(), [0.0, 10.0]));
}

#[test]
fn move_and_copy() {
    let mut s = sketch();
    let l = s.add_line_xy([0.0, 0.0], [10.0, 0.0]);
    let copies = s
        .transform(&[l], Transform::Move { dx: 0.0, dy: 5.0 }, true)
        .unwrap();
    assert!(near(line_pts(&s, copies[0]).0, [0.0, 5.0]));
    s.transform(
        &[l],
        Transform::Rotate {
            centre: [0.0, 0.0],
            angle: std::f64::consts::FRAC_PI_2,
        },
        false,
    )
    .unwrap();
    assert!(near(line_pts(&s, l).1, [0.0, 10.0]));
}

#[test]
fn auto_constrain_fully_constrains_a_drawn_shape() {
    let mut s = sketch();
    // Four lines drawn end to end, no constraints.
    let p: Vec<EntityId> = [[1.0, 1.0], [31.0, 1.0], [31.0, 16.0], [1.0, 16.0]]
        .iter()
        .map(|q| s.add_point(q[0], q[1]))
        .collect();
    for i in 0..4 {
        s.add_line(p[i], p[(i + 1) % 4]).unwrap();
    }
    s.add_circle_xy([10.0, 8.0], 3.0).unwrap();
    let added = s.auto_constrain();
    assert!(added > 0);
    let summary = s.solve();
    assert_eq!(summary.dof, 0, "fully constrained");
    assert!(summary.conflicting.is_empty());
    assert!(
        near(s.point(p[2]).unwrap(), [31.0, 16.0]),
        "geometry unchanged"
    );
}

#[test]
fn loose_ends_are_the_unjoined_ends() {
    let mut s = sketch();
    let a = s.add_point(0.0, 0.0);
    let b = s.add_point(10.0, 0.0);
    let c = s.add_point(10.0, 10.0);
    s.add_line(a, b).unwrap();
    s.add_line(b, c).unwrap();
    let mut loose = s.loose_ends();
    loose.sort();
    assert_eq!(loose, vec![a, c]);
}

#[test]
fn copies_keep_their_shape() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [10.0, 5.0]).unwrap();
    let before = s.solve().dof;
    s.transform(&lines, Transform::Move { dx: 20.0, dy: 0.0 }, true)
        .unwrap();
    // A rectangle has 4 degrees of freedom; its copy adds just 4 more.
    assert_eq!(s.solve().dof, before + 4);
    let lines = s.add_rectangle([0.0, 20.0], [10.0, 25.0]).unwrap();
    let before = s.solve().dof;
    s.pattern_rect(&lines, [1.0, 0.0], 3, 1, 15.0, 0.0).unwrap();
    assert_eq!(s.solve().dof, before + 8);
}
