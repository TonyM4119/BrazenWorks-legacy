use super::*;
use crate::sketch::plane::{OriginPlane, SketchPlane};

const TOL: f64 = 1e-7;

fn near(a: [f64; 2], b: [f64; 2]) -> bool {
    (a[0] - b[0]).abs() < TOL && (a[1] - b[1]).abs() < TOL
}

fn sketch() -> Sketch {
    Sketch::new("Sketch1", SketchPlane::origin_plane(OriginPlane::XY))
}

fn line_points(s: &Sketch, line: EntityId) -> (EntityId, EntityId) {
    match s.entity(line).unwrap().geometry {
        Geometry::Line { start, end } => (start, end),
        g => panic!("not a line: {g:?}"),
    }
}

/// Rectangle tool + fix a corner + width and height = fully defined.
fn dimensioned_rectangle() -> (Sketch, [EntityId; 4], ConstraintId, ConstraintId) {
    let mut s = sketch();
    let lines = s.add_rectangle([1.0, 1.0], [30.0, 15.0]).unwrap();
    let (corner, _) = line_points(&s, lines[0]);
    s.add_constraint(SketchConstraint::Fix(corner)).unwrap();
    let w = s
        .add_constraint(SketchConstraint::Length {
            line: lines[0],
            value: 40.0,
        })
        .unwrap();
    let h = s
        .add_constraint(SketchConstraint::Length {
            line: lines[1],
            value: 20.0,
        })
        .unwrap();
    (s, lines, w, h)
}

#[test]
fn rectangle_tool_adds_shared_corners_and_constraints() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [10.0, 5.0]).unwrap();
    // 4 points + 4 lines, 4 horizontal/vertical constraints.
    assert_eq!(s.entities().len(), 8);
    assert_eq!(s.constraints().len(), 4);
    // Corners are shared: bottom's end is right's start.
    assert_eq!(line_points(&s, lines[0]).1, line_points(&s, lines[1]).0);
    let summary = s.solve();
    assert!(summary.is_solved());
    // 8 coordinates − 4 constraints = 4 (position x, y, width, height).
    assert_eq!(summary.dof, 4);
}

#[test]
fn dimensioned_rectangle_is_fully_constrained() {
    let (mut s, lines, _, _) = dimensioned_rectangle();
    let summary = s.solve();
    assert!(summary.is_fully_constrained());
    for line in lines {
        assert!(
            summary.fully_constrained.contains(&line),
            "line {line:?} should be black"
        );
    }
    let (_, top_right) = line_points(&s, lines[1]);
    assert!(near(s.point(top_right).unwrap(), [41.0, 21.0]));
}

#[test]
fn editing_a_dimension_moves_geometry() {
    let (mut s, lines, w, _) = dimensioned_rectangle();
    let summary = s.set_dimension(w, 55.0).unwrap();
    assert!(summary.is_solved());
    let (_, top_right) = line_points(&s, lines[1]);
    assert!(near(s.point(top_right).unwrap(), [56.0, 21.0]));
    assert_eq!(s.constraint(w).unwrap().value(), Some(55.0));
}

#[test]
fn conflicting_constraint_is_refused_and_sketch_unchanged() {
    let (mut s, lines, w, _) = dimensioned_rectangle();
    let before = s.clone();
    let err = s
        .add_constraint(SketchConstraint::Length {
            line: lines[2],
            value: 30.0,
        })
        .unwrap_err();
    match err {
        SketchError::Conflicts(ids) => assert!(ids.contains(&w), "should blame the width: {ids:?}"),
        e => panic!("expected a conflict, got {e}"),
    }
    assert_eq!(s, before);
    assert!(s.solve().is_solved());
}

#[test]
fn invalid_dimension_is_rejected_without_changes() {
    let (mut s, _, w, _) = dimensioned_rectangle();
    assert!(matches!(
        s.set_dimension(w, -5.0),
        Err(SketchError::InvalidValue(_))
    ));
    assert_eq!(s.constraint(w).unwrap().value(), Some(40.0));
}

#[test]
fn ids_are_stable_and_never_reused() {
    let mut s = sketch();
    let a = s.add_line_xy([0.0, 0.0], [10.0, 0.0]);
    let b = s.add_line_xy([0.0, 5.0], [10.0, 5.0]);
    let par = s.add_constraint(SketchConstraint::Parallel(a, b)).unwrap();
    s.remove_entity(a).unwrap();
    // The line, its two unused points and the constraint on it are gone.
    assert!(s.entity(a).is_none());
    assert!(s.constraint(par).is_none());
    assert_eq!(s.entities().len(), 3);
    // b keeps its id; new geometry gets fresh ids.
    assert!(s.entity(b).is_some());
    let c = s.add_line_xy([0.0, 9.0], [10.0, 9.0]);
    assert!(c.0 > b.0 && c != a);
}

#[test]
fn point_used_by_a_curve_cannot_be_removed() {
    let mut s = sketch();
    let line = s.add_line_xy([0.0, 0.0], [10.0, 0.0]);
    let (start, _) = line_points(&s, line);
    assert_eq!(s.remove_entity(start), Err(SketchError::InUse(start)));
}

#[test]
fn wrong_entity_kinds_are_explained() {
    let mut s = sketch();
    let c = s.add_circle_xy([0.0, 0.0], 5.0).unwrap();
    let err = s
        .add_constraint(SketchConstraint::Horizontal(c))
        .unwrap_err();
    assert!(matches!(err, SketchError::WrongKind(_)), "{err:?}");
    let p = s.add_point(1.0, 1.0);
    assert!(matches!(s.add_line(p, c), Err(SketchError::WrongKind(_))));
    assert!(matches!(
        s.add_constraint(SketchConstraint::Fix(EntityId(999))),
        Err(SketchError::UnknownEntity(_))
    ));
}

#[test]
fn circle_radius_and_centre_constraints() {
    let mut s = sketch();
    let origin = s.add_point(0.0, 0.0);
    s.add_constraint(SketchConstraint::Fix(origin)).unwrap();
    let circle = s.add_circle_xy([2.0, 1.0], 7.0).unwrap();
    let Geometry::Circle { centre, .. } = s.entity(circle).unwrap().geometry else {
        unreachable!()
    };
    s.add_constraint(SketchConstraint::Coincident(centre, origin))
        .unwrap();
    s.add_constraint(SketchConstraint::Diameter {
        curve: circle,
        value: 26.0,
    })
    .unwrap();
    let summary = s.solve();
    assert!(summary.is_fully_constrained());
    assert!((s.radius(circle).unwrap() - 13.0).abs() < TOL);
    assert!(near(s.point(centre).unwrap(), [0.0, 0.0]));
}

#[test]
fn drag_moves_free_geometry_and_respects_constraints() {
    let mut s = sketch();
    let lines = s.add_rectangle([0.0, 0.0], [10.0, 5.0]).unwrap();
    let (corner, _) = line_points(&s, lines[0]);
    s.add_constraint(SketchConstraint::Fix(corner)).unwrap();
    let (_, top_right) = line_points(&s, lines[1]);
    let summary = s.drag_point(top_right, [25.0, 12.0]).unwrap();
    assert!(summary.is_solved());
    assert!(near(s.point(top_right).unwrap(), [25.0, 12.0]));
    assert!(near(s.point(corner).unwrap(), [0.0, 0.0]));
    // Still a rectangle: bottom-right shares x with top-right.
    let (_, bottom_right) = line_points(&s, lines[0]);
    assert!((s.point(bottom_right).unwrap()[0] - 25.0).abs() < TOL);
}

#[test]
fn slot_of_lines_and_tangent_arcs() {
    let mut s = sketch();
    let a = s.add_point(0.0, 0.0);
    let b = s.add_point(30.0, 0.5);
    let c = s.add_point(30.0, 10.0);
    let d = s.add_point(0.5, 10.0);
    let bottom = s.add_line(a, b).unwrap();
    let top = s.add_line(c, d).unwrap();
    let rc = s.add_point(31.0, 5.0);
    let lc = s.add_point(-1.0, 5.0);
    let right = s.add_arc(rc, b, c).unwrap();
    let left = s.add_arc(lc, d, a).unwrap();
    s.add_constraint(SketchConstraint::Fix(a)).unwrap();
    s.add_constraint(SketchConstraint::Horizontal(bottom))
        .unwrap();
    for (x, y) in [(bottom, right), (right, top), (top, left), (left, bottom)] {
        s.add_constraint(SketchConstraint::Tangent(x, y)).unwrap();
    }
    s.add_constraint(SketchConstraint::Length {
        line: bottom,
        value: 40.0,
    })
    .unwrap();
    s.add_constraint(SketchConstraint::Radius {
        curve: right,
        value: 6.0,
    })
    .unwrap();
    s.add_constraint(SketchConstraint::Equal(left, right))
        .unwrap();
    let summary = s.solve();
    assert!(summary.is_fully_constrained(), "{summary:?}");
    assert!(near(s.point(c).unwrap(), [40.0, 12.0]));
    assert!((s.radius(left).unwrap() - 6.0).abs() < TOL);
}

#[test]
fn construction_flag_round_trips() {
    let mut s = sketch();
    let l = s.add_line_xy([0.0, 0.0], [5.0, 5.0]);
    assert!(!s.entity(l).unwrap().construction);
    s.set_construction(l, true).unwrap();
    assert!(s.entity(l).unwrap().construction);
}
