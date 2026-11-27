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
    match &s.entity(line).unwrap().geometry {
        Geometry::Line { start, end } => (*start, *end),
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

#[test]
fn perpendicular_turns_a_line_instead_of_collapsing_one() {
    // MitCAD's residual `unit(a) · (b2 - b1)` is also met by shrinking b to a
    // point; pre-orienting keeps both lines at their length.
    let mut s = Sketch::new("p", SketchPlane::origin_plane(OriginPlane::XY));
    let a0 = s.add_point(0.0, 0.0);
    let a1 = s.add_point(25.0, 12.0);
    let l1 = s.add_line(a0, a1).unwrap();
    let b0 = s.add_point(0.0, -12.0);
    let b1 = s.add_point(37.0, -12.0);
    let l2 = s.add_line(b0, b1).unwrap();
    s.add_constraint(SketchConstraint::Horizontal(l2)).unwrap();
    s.add_constraint(SketchConstraint::Perpendicular(l1, l2))
        .unwrap();
    let len = |p: EntityId, q: EntityId| {
        let (p, q) = (s.point(p).unwrap(), s.point(q).unwrap());
        (q[0] - p[0]).hypot(q[1] - p[1])
    };
    assert!(
        (len(b0, b1) - 37.0).abs() < 1e-6,
        "horizontal line kept its length"
    );
    assert!(
        (len(a0, a1) - 25.0f64.hypot(12.0)).abs() < 1e-6,
        "turned line kept its length"
    );
    let (p, q) = (s.point(a0).unwrap(), s.point(a1).unwrap());
    assert!((q[0] - p[0]).abs() < 1e-7, "the free line turned vertical");
}

#[test]
fn parallel_turns_the_second_line() {
    let mut s = Sketch::new("p", SketchPlane::origin_plane(OriginPlane::XY));
    let l1 = s.add_line_xy([0.0, 0.0], [10.0, 0.0]);
    let l2 = s.add_line_xy([0.0, 5.0], [8.0, 9.0]);
    s.add_constraint(SketchConstraint::Parallel(l1, l2))
        .unwrap();
    let pts = s.entity(l2).unwrap().geometry.defining_points();
    let (p, q) = (s.point(pts[0]).unwrap(), s.point(pts[1]).unwrap());
    assert!((q[1] - p[1]).abs() < 1e-7);
    assert!(((q[0] - p[0]).abs() - 8.0f64.hypot(4.0)).abs() < 1e-6);
}

// ---- Redundant, driven, collapse, first dimension, continuation -----------------

fn rect(s: &mut Sketch) -> [EntityId; 4] {
    s.add_rectangle([0.0, 0.0], [40.0, 20.0]).unwrap()
}

#[test]
fn a_repeated_dimension_is_refused_as_redundant() {
    let mut s = Sketch::new("r", SketchPlane::origin_plane(OriginPlane::XY));
    let l = rect(&mut s);
    s.add_constraint(SketchConstraint::Length {
        line: l[0],
        value: 40.0,
    })
    .unwrap();
    let again = s.add_constraint(SketchConstraint::Length {
        line: l[2],
        value: 40.0,
    });
    assert_eq!(
        again,
        Err(SketchError::Redundant),
        "top = bottom already (horizontal sides)"
    );
    // A driven dimension can still show it.
    let d = s
        .add_driven(SketchConstraint::Length {
            line: l[2],
            value: 1.0,
        })
        .unwrap();
    assert!(s.is_driven(d));
    assert!((s.constraint(d).unwrap().value().unwrap() - 40.0).abs() < 1e-9);
}

#[test]
fn driven_dimensions_follow_the_geometry() {
    let mut s = Sketch::new("d", SketchPlane::origin_plane(OriginPlane::XY));
    let l = rect(&mut s);
    let w = s
        .add_constraint(SketchConstraint::Length {
            line: l[0],
            value: 40.0,
        })
        .unwrap();
    let d = s
        .add_driven(SketchConstraint::Length {
            line: l[2],
            value: 0.0,
        })
        .unwrap();
    s.set_dimension(w, 55.0).unwrap();
    assert!((s.constraint(d).unwrap().value().unwrap() - 55.0).abs() < 1e-6);
    assert!(
        s.set_dimension(d, 10.0).is_err(),
        "driven dimensions can't be set"
    );
    // Making it driving again is checked: still redundant.
    assert_eq!(s.set_driven(d, false), Err(SketchError::Redundant));
}

#[test]
fn a_constraint_that_collapses_a_line_is_refused() {
    let mut s = Sketch::new("c", SketchPlane::origin_plane(OriginPlane::XY));
    let a = s.add_point(0.0, 0.0);
    let b = s.add_point(10.0, 0.0);
    let line = s.add_line(a, b).unwrap();
    s.add_constraint(SketchConstraint::Fix(a)).unwrap();
    s.add_constraint(SketchConstraint::Horizontal(line))
        .unwrap();
    // Making the ends coincide would collapse the line.
    let r = s.add_constraint(SketchConstraint::Coincident(a, b));
    assert!(matches!(r, Err(SketchError::Collapses(_))), "{r:?}");
    assert!(s.point(b).unwrap()[0] > 9.0, "geometry restored");
}

#[test]
fn the_first_dimension_scales_the_sketch() {
    let mut s = Sketch::new("f", SketchPlane::origin_plane(OriginPlane::XY));
    let l = rect(&mut s); // 40 x 20 at the origin
    let c = s.add_circle_xy([10.0, 10.0], 5.0).unwrap();
    s.add_constraint(SketchConstraint::Length {
        line: l[0],
        value: 80.0,
    })
    .unwrap();
    assert!(
        (s.radius(c).unwrap() - 10.0).abs() < 1e-6,
        "everything doubled"
    );
    // The second dimension no longer scales.
    s.add_constraint(SketchConstraint::Radius {
        curve: c,
        value: 3.0,
    })
    .unwrap();
    assert!((s.radius(c).unwrap() - 3.0).abs() < 1e-6);
    let (p, q) = (
        s.point(s.entity(l[1]).unwrap().geometry.defining_points()[0])
            .unwrap(),
        [80.0, 0.0],
    );
    assert!((p[0] - q[0]).abs() < 1e-6);
}

#[test]
fn a_big_dimension_change_keeps_the_shape_unfolded() {
    // A triangle: two sides dimensioned, the third free. Growing one side
    // a lot in one jump can fold the triangle over; steps keep it.
    let mut s = Sketch::new("t", SketchPlane::origin_plane(OriginPlane::XY));
    let a = s.add_point(0.0, 0.0);
    let b = s.add_point(30.0, 0.0);
    let c = s.add_point(10.0, 20.0);
    let ab = s.add_line(a, b).unwrap();
    let bc = s.add_line(b, c).unwrap();
    let ca = s.add_line(c, a).unwrap();
    s.add_constraint(SketchConstraint::Fix(a)).unwrap();
    s.add_constraint(SketchConstraint::Horizontal(ab)).unwrap();
    let side = s
        .add_constraint(SketchConstraint::Length {
            line: ab,
            value: 30.0,
        })
        .unwrap();
    s.add_constraint(SketchConstraint::Length {
        line: bc,
        value: (20f64).hypot(20.0),
    })
    .unwrap();
    s.add_constraint(SketchConstraint::Length {
        line: ca,
        value: (10f64).hypot(20.0),
    })
    .unwrap();
    let up = s.point(c).unwrap()[1] > 0.0;
    s.set_dimension(side, 12.0).unwrap();
    assert_eq!(
        s.point(c).unwrap()[1] > 0.0,
        up,
        "apex stays on the same side"
    );
}

// ---- New shapes and dimensions ----------------------------------------------------

#[test]
fn ellipses_solve_and_take_radius_dimensions() {
    let mut s = Sketch::new("e", SketchPlane::origin_plane(OriginPlane::XY));
    let c = s.add_point(0.0, 0.0);
    let m = s.add_point(20.0, 0.0);
    let e = s.add_ellipse(c, m, 8.0).unwrap();
    s.add_constraint(SketchConstraint::Fix(c)).unwrap();
    s.add_constraint(SketchConstraint::MajorRadius {
        ellipse: e,
        value: 30.0,
    })
    .unwrap();
    s.add_constraint(SketchConstraint::MinorRadius {
        ellipse: e,
        value: 10.0,
    })
    .unwrap();
    let (a, b) = s.ellipse_radii(e).unwrap();
    assert!((a - 30.0).abs() < 1e-6 && (b - 10.0).abs() < 1e-6);
}

#[test]
fn splines_pass_through_their_fit_points() {
    let mut s = Sketch::new("s", SketchPlane::origin_plane(OriginPlane::XY));
    let pts: Vec<EntityId> = [[0.0, 0.0], [10.0, 8.0], [20.0, 0.0], [30.0, 5.0]]
        .iter()
        .map(|p| s.add_point(p[0], p[1]))
        .collect();
    let sp = s.add_spline(&pts, false).unwrap();
    let data = s.spline(sp).expect("shape after solving");
    assert_eq!(data.degree, 3);
    let first = data.control_points[0];
    let last = *data.control_points.last().unwrap();
    assert!(
        (first[0]).abs() < 1e-9 && (last[0] - 30.0).abs() < 1e-9,
        "clamped ends at the fit points"
    );
    // A fit point can be dimensioned and the spline follows.
    s.add_constraint(SketchConstraint::Fix(pts[0])).unwrap();
    s.add_constraint(SketchConstraint::VerticalDistance {
        a: pts[0],
        b: pts[1],
        value: 12.0,
    })
    .unwrap();
    assert!((s.point(pts[1]).unwrap()[1] - 12.0).abs() < 1e-6);
}

#[test]
fn line_and_circle_gap_dimensions() {
    let mut s = Sketch::new("g", SketchPlane::origin_plane(OriginPlane::XY));
    let l1 = s.add_line_xy([0.0, 0.0], [50.0, 0.0]);
    let l2 = s.add_line_xy([0.0, 10.0], [50.0, 10.0]);
    let c = s.add_circle_xy([25.0, 30.0], 5.0).unwrap();
    s.add_constraint(SketchConstraint::Fix(l1)).unwrap(); // fixes a whole curve
    s.add_constraint(SketchConstraint::Parallel(l1, l2))
        .unwrap();
    s.add_constraint(SketchConstraint::LineDistance {
        a: l1,
        b: l2,
        value: 15.0,
    })
    .unwrap();
    s.add_constraint(SketchConstraint::LineCircleDistance {
        line: l2,
        circle: c,
        value: 4.0,
    })
    .unwrap();
    let ys = s.entity(l2).unwrap().geometry.defining_points();
    assert!((s.point(ys[0]).unwrap()[1] - 15.0).abs() < 1e-6);
    let centre = s.entity(c).unwrap().geometry.defining_points()[0];
    assert!((s.point(centre).unwrap()[1] - s.radius(c).unwrap() - 15.0 - 4.0).abs() < 1e-6);
}
