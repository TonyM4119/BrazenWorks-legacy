//! Profile detection: the closed regions of a sketch that can be extruded.
//!
//! Approach (the standard planar-graph method, as also described in
//! SolidCraft's `crates/sketch/src/profiles.rs`, MIT/Apache-2.0; this is our
//! own implementation):
//!
//! 1. Non-construction lines and arcs form a graph whose nodes are their end
//!    points; points joined by a Coincident constraint or lying within a small
//!    tolerance are merged.
//! 2. Dangling edges (an end with nothing attached) are pruned.
//! 3. Faces are traced with the "first clockwise turn" rule, which walks every
//!    bounded region counter-clockwise (positive area); the outer face of each
//!    connected group comes out clockwise and is dropped. Ties between curves
//!    leaving a point in the same direction are broken by curvature.
//! 4. Full circles are loops of their own.
//! 5. A loop lying inside a loop of another group becomes a hole of the
//!    innermost loop containing it. A circle inside a rectangle therefore
//!    gives two profiles: the rectangle with a hole, and the disc.
//!
//! In M1, curves must meet at their end points; curves crossing mid-way are
//! not split (that arrives with the Trim tool).

use std::collections::{BTreeSet, HashMap};
use std::f64::consts::{PI, TAU};

use super::model::{EntityId, Geometry, Sketch, SketchConstraint};

const MERGE_TOL: f64 = 1e-6;
const MIN_AREA: f64 = 1e-9;
const ARC_SAMPLES: usize = 24;

/// One oriented piece of a loop, in sketch coordinates.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Segment {
    Line {
        a: [f64; 2],
        b: [f64; 2],
    },
    /// From `start` to `end` around `centre`, counter-clockwise if `ccw`.
    Arc {
        centre: [f64; 2],
        radius: f64,
        start: [f64; 2],
        end: [f64; 2],
        ccw: bool,
    },
    /// A whole circle, counter-clockwise.
    Circle {
        centre: [f64; 2],
        radius: f64,
    },
}

impl Segment {
    /// A point halfway along the segment.
    pub fn midpoint(&self) -> [f64; 2] {
        match *self {
            Segment::Line { a, b } => [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0],
            Segment::Arc {
                centre,
                radius,
                start,
                end,
                ccw,
            } => {
                let (a0, sweep) = arc_angles(centre, start, end, ccw);
                let m = a0 + sweep / 2.0;
                [centre[0] + radius * m.cos(), centre[1] + radius * m.sin()]
            }
            Segment::Circle { centre, radius } => [centre[0] + radius, centre[1]],
        }
    }

    /// Points along the segment, excluding the end point.
    fn sample(&self, out: &mut Vec<[f64; 2]>) {
        match *self {
            Segment::Line { a, .. } => out.push(a),
            Segment::Arc {
                centre,
                radius,
                start,
                end,
                ccw,
            } => {
                let (a0, sweep) = arc_angles(centre, start, end, ccw);
                let n = ((sweep.abs() / TAU) * ARC_SAMPLES as f64).ceil().max(4.0) as usize;
                for i in 0..n {
                    let t = a0 + sweep * i as f64 / n as f64;
                    out.push([centre[0] + radius * t.cos(), centre[1] + radius * t.sin()]);
                }
            }
            Segment::Circle { centre, radius } => {
                for i in 0..ARC_SAMPLES * 2 {
                    let t = TAU * i as f64 / (ARC_SAMPLES * 2) as f64;
                    out.push([centre[0] + radius * t.cos(), centre[1] + radius * t.sin()]);
                }
            }
        }
    }
}

/// A closed loop: segments in order, counter-clockwise, with the sketch
/// curves it is made of.
#[derive(Clone, Debug, PartialEq)]
pub struct Loop {
    pub segments: Vec<Segment>,
    pub curves: Vec<EntityId>,
    /// Polygon approximation (counter-clockwise), for containment and drawing.
    pub outline: Vec<[f64; 2]>,
    pub area: f64,
}

/// Stable name of a region: the curves of its outer loop and of each hole.
/// Survives dimension edits, because sketch entity ids never change.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct RegionKey(pub String);

/// An extrudable region: an outer loop minus its holes.
#[derive(Clone, Debug, PartialEq)]
pub struct Region {
    pub key: RegionKey,
    pub outer: Loop,
    pub holes: Vec<Loop>,
}

impl Region {
    pub fn area(&self) -> f64 {
        self.outer.area - self.holes.iter().map(|h| h.area).sum::<f64>()
    }
}

/// Every extrudable region of the sketch, largest first.
pub fn find_regions(sketch: &Sketch) -> Vec<Region> {
    let mut loops = traced_loops(sketch);
    loops.extend(circle_loops(sketch));
    let (loops, groups): (Vec<Loop>, Vec<usize>) = loops.into_iter().unzip();

    // Innermost containing loop of a different group = parent.
    let parent: Vec<Option<usize>> = (0..loops.len())
        .map(|i| {
            let probe = loops[i].outline[0];
            (0..loops.len())
                .filter(|&j| j != i && groups[j] != groups[i] && loops[j].area > loops[i].area)
                .filter(|&j| point_in_polygon(probe, &loops[j].outline))
                .min_by(|&a, &b| loops[a].area.total_cmp(&loops[b].area))
        })
        .collect();

    let mut regions: Vec<Region> = (0..loops.len())
        .map(|i| {
            let holes: Vec<Loop> = (0..loops.len())
                .filter(|&j| parent[j] == Some(i))
                .map(|j| loops[j].clone())
                .collect();
            let key = region_key(&loops[i], &holes);
            Region {
                key,
                outer: loops[i].clone(),
                holes,
            }
        })
        .filter(|r| r.area() > MIN_AREA)
        .collect();
    regions.sort_by(|a, b| {
        b.area()
            .total_cmp(&a.area())
            .then_with(|| a.key.cmp(&b.key))
    });
    regions
}

/// The region with this key, if the sketch still has it.
pub fn find_region(sketch: &Sketch, key: &RegionKey) -> Option<Region> {
    find_regions(sketch).into_iter().find(|r| &r.key == key)
}

fn region_key(outer: &Loop, holes: &[Loop]) -> RegionKey {
    let ids = |l: &Loop| {
        let set: BTreeSet<u64> = l.curves.iter().map(|c| c.0).collect();
        set.iter().map(u64::to_string).collect::<Vec<_>>().join(",")
    };
    let mut hole_keys: Vec<String> = holes.iter().map(ids).collect();
    hole_keys.sort();
    let mut key = ids(outer);
    for h in hole_keys {
        key.push('|');
        key.push_str(&h);
    }
    RegionKey(key)
}

// ---- Graph of lines and arcs ------------------------------------------------

struct Edge {
    curve: EntityId,
    from: usize, // node
    to: usize,   // node
    forward: Segment,
}

struct HalfEdge {
    edge: usize,
    reversed: bool,
    origin: usize,
    target: usize,
    angle: f64,     // leaving direction at origin, [0, 2π)
    curvature: f64, // signed (left turn positive), for ties
}

fn traced_loops(sketch: &Sketch) -> Vec<(Loop, usize)> {
    let pos = |id: EntityId| sketch.point(id).unwrap_or([0.0, 0.0]);

    // Nodes: union-find over point ids; Coincident constraints and nearby
    // positions merge.
    let mut node_of: HashMap<EntityId, usize> = HashMap::new();
    let mut parent: Vec<usize> = Vec::new();
    let mut node_pos: Vec<[f64; 2]> = Vec::new();
    let mut node =
        |id: EntityId, node_of: &mut HashMap<EntityId, usize>, parent: &mut Vec<usize>| -> usize {
            *node_of.entry(id).or_insert_with(|| {
                parent.push(parent.len());
                node_pos.push(pos(id));
                parent.len() - 1
            })
        };

    let mut raw_edges: Vec<(EntityId, EntityId, EntityId, Segment)> = Vec::new(); // curve, start pt, end pt, seg
    for e in sketch.entities() {
        if e.construction {
            continue;
        }
        match e.geometry {
            Geometry::Line { start, end } => {
                let (a, b) = (pos(start), pos(end));
                if dist(a, b) > MERGE_TOL {
                    raw_edges.push((e.id, start, end, Segment::Line { a, b }));
                }
            }
            Geometry::Arc { centre, start, end } => {
                let (c, s, t) = (pos(centre), pos(start), pos(end));
                let radius = dist(c, s);
                if radius > MERGE_TOL && dist(s, t) > MERGE_TOL {
                    raw_edges.push((
                        e.id,
                        start,
                        end,
                        Segment::Arc {
                            centre: c,
                            radius,
                            start: s,
                            end: t,
                            ccw: true,
                        },
                    ));
                }
            }
            _ => {}
        }
    }
    for (_, s, t, _) in &raw_edges {
        node(*s, &mut node_of, &mut parent);
        node(*t, &mut node_of, &mut parent);
    }
    let positions = node_pos.clone();
    let find = |mut i: usize, parent: &mut Vec<usize>| {
        while parent[i] != i {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        i
    };
    for (_, c) in sketch.constraints() {
        if let SketchConstraint::Coincident(a, b) = c {
            if let (Some(&na), Some(&nb)) = (node_of.get(a), node_of.get(b)) {
                let (ra, rb) = (find(na, &mut parent), find(nb, &mut parent));
                parent[ra] = rb;
            }
        }
    }
    for i in 0..positions.len() {
        for j in (i + 1)..positions.len() {
            if dist(positions[i], positions[j]) <= MERGE_TOL {
                let (ri, rj) = (find(i, &mut parent), find(j, &mut parent));
                parent[ri] = rj;
            }
        }
    }

    let mut edges: Vec<Edge> = raw_edges
        .iter()
        .map(|&(curve, s, t, forward)| Edge {
            curve,
            from: find(node_of[&s], &mut parent),
            to: find(node_of[&t], &mut parent),
            forward,
        })
        .filter(|e| e.from != e.to)
        .collect();

    // Prune dangling edges until every node has degree ≥ 2.
    loop {
        let mut degree: HashMap<usize, usize> = HashMap::new();
        for e in &edges {
            *degree.entry(e.from).or_default() += 1;
            *degree.entry(e.to).or_default() += 1;
        }
        let before = edges.len();
        edges.retain(|e| degree[&e.from] >= 2 && degree[&e.to] >= 2);
        if edges.len() == before {
            break;
        }
    }
    if edges.is_empty() {
        return Vec::new();
    }

    // Connected groups of edges (by union of their nodes).
    let mut group_parent: HashMap<usize, usize> = HashMap::new();
    fn root(m: &mut HashMap<usize, usize>, i: usize) -> usize {
        let p = *m.entry(i).or_insert(i);
        if p == i {
            i
        } else {
            let r = root(m, p);
            m.insert(i, r);
            r
        }
    }
    for e in &edges {
        let (a, b) = (
            root(&mut group_parent, e.from),
            root(&mut group_parent, e.to),
        );
        group_parent.insert(a, b);
    }

    // Half-edges and the outgoing order at each node.
    let mut half: Vec<HalfEdge> = Vec::with_capacity(edges.len() * 2);
    for (i, e) in edges.iter().enumerate() {
        for reversed in [false, true] {
            let seg = oriented(&e.forward, reversed);
            let (angle, curvature) = leaving(&seg);
            let (origin, target) = if reversed {
                (e.to, e.from)
            } else {
                (e.from, e.to)
            };
            half.push(HalfEdge {
                edge: i,
                reversed,
                origin,
                target,
                angle,
                curvature,
            });
        }
    }
    let mut outgoing: HashMap<usize, Vec<usize>> = HashMap::new();
    for (h, he) in half.iter().enumerate() {
        outgoing.entry(he.origin).or_default().push(h);
    }
    for list in outgoing.values_mut() {
        list.sort_by(|&a, &b| {
            half[a]
                .angle
                .total_cmp(&half[b].angle)
                .then(half[a].curvature.total_cmp(&half[b].curvature))
        });
    }

    // next(h): at h's target, the outgoing half-edge just clockwise of h's twin.
    let twin = |h: usize| h ^ 1;
    let next = |h: usize| -> usize {
        let list = &outgoing[&half[h].target];
        let t = twin(h);
        let pos = list
            .iter()
            .position(|&x| x == t)
            .expect("twin leaves the target node");
        list[(pos + list.len() - 1) % list.len()]
    };

    let mut used = vec![false; half.len()];
    let mut loops = Vec::new();
    for start in 0..half.len() {
        if used[start] {
            continue;
        }
        let mut cycle = Vec::new();
        let mut h = start;
        while !used[h] {
            used[h] = true;
            cycle.push(h);
            h = next(h);
        }
        if h != start {
            continue; // not a clean cycle (shouldn't happen after pruning)
        }
        let segments: Vec<Segment> = cycle
            .iter()
            .map(|&h| oriented(&edges[half[h].edge].forward, half[h].reversed))
            .collect();
        let mut outline = Vec::new();
        for s in &segments {
            s.sample(&mut outline);
        }
        let area = exact_area(&segments);
        if area <= MIN_AREA {
            continue; // the outer (clockwise) face of a group
        }
        let curves = cycle.iter().map(|&h| edges[half[h].edge].curve).collect();
        let group = root(&mut group_parent, edges[half[start].edge].from);
        loops.push((
            Loop {
                segments,
                curves,
                outline,
                area,
            },
            group,
        ));
    }
    loops
}

fn circle_loops(sketch: &Sketch) -> Vec<(Loop, usize)> {
    sketch
        .entities()
        .iter()
        .filter(|e| !e.construction)
        .filter_map(|e| match e.geometry {
            Geometry::Circle { centre, radius } if radius > MERGE_TOL => {
                let centre = sketch.point(centre)?;
                let seg = Segment::Circle { centre, radius };
                let mut outline = Vec::new();
                seg.sample(&mut outline);
                // Each circle is its own group; offset to avoid clashing with traced groups.
                Some((
                    Loop {
                        segments: vec![seg],
                        curves: vec![e.id],
                        outline,
                        area: PI * radius * radius,
                    },
                    usize::MAX - e.id.0 as usize,
                ))
            }
            _ => None,
        })
        .collect()
}

// ---- Geometry helpers --------------------------------------------------------

fn oriented(seg: &Segment, reversed: bool) -> Segment {
    if !reversed {
        return *seg;
    }
    match *seg {
        Segment::Line { a, b } => Segment::Line { a: b, b: a },
        Segment::Arc {
            centre,
            radius,
            start,
            end,
            ccw,
        } => Segment::Arc {
            centre,
            radius,
            start: end,
            end: start,
            ccw: !ccw,
        },
        circle => circle,
    }
}

/// Direction leaving the segment's start (radians, [0, 2π)) and its signed
/// curvature (left turn positive).
fn leaving(seg: &Segment) -> (f64, f64) {
    let (dx, dy, k) = match *seg {
        Segment::Line { a, b } => (b[0] - a[0], b[1] - a[1], 0.0),
        Segment::Arc {
            centre,
            radius,
            start,
            ccw,
            ..
        } => {
            let (rx, ry) = (start[0] - centre[0], start[1] - centre[1]);
            if ccw {
                (-ry, rx, 1.0 / radius)
            } else {
                (ry, -rx, -1.0 / radius)
            }
        }
        Segment::Circle { .. } => (0.0, 1.0, 0.0),
    };
    (dy.atan2(dx).rem_euclid(TAU), k)
}

/// Start angle and signed sweep of an arc (sweep > 0 counter-clockwise).
fn arc_angles(centre: [f64; 2], start: [f64; 2], end: [f64; 2], ccw: bool) -> (f64, f64) {
    let a0 = (start[1] - centre[1]).atan2(start[0] - centre[0]);
    let a1 = (end[1] - centre[1]).atan2(end[0] - centre[0]);
    let mut sweep = (a1 - a0).rem_euclid(TAU);
    if sweep < 1e-12 {
        sweep = TAU;
    }
    if ccw {
        (a0, sweep)
    } else {
        (a0, sweep - TAU)
    }
}

/// Exact signed area of a closed chain of segments (Green's theorem:
/// A = ½∮(x dy − y dx), with the arc terms integrated analytically).
fn exact_area(segments: &[Segment]) -> f64 {
    segments
        .iter()
        .map(|s| match *s {
            Segment::Line { a, b } => (a[0] * b[1] - b[0] * a[1]) / 2.0,
            Segment::Arc {
                centre,
                radius,
                start,
                end,
                ccw,
            } => {
                let (_, sweep) = arc_angles(centre, start, end, ccw);
                let (cx, cy) = (centre[0], centre[1]);
                (radius * radius * sweep + cx * (end[1] - start[1]) - cy * (end[0] - start[0]))
                    / 2.0
            }
            Segment::Circle { radius, .. } => PI * radius * radius,
        })
        .sum()
}

#[cfg_attr(not(test), allow(dead_code))]
fn signed_area(poly: &[[f64; 2]]) -> f64 {
    let n = poly.len();
    (0..n)
        .map(|i| {
            let (p, q) = (poly[i], poly[(i + 1) % n]);
            p[0] * q[1] - q[0] * p[1]
        })
        .sum::<f64>()
        / 2.0
}

fn point_in_polygon(p: [f64; 2], poly: &[[f64; 2]]) -> bool {
    let mut inside = false;
    let n = poly.len();
    let mut j = n - 1;
    for i in 0..n {
        let (a, b) = (poly[i], poly[j]);
        if (a[1] > p[1]) != (b[1] > p[1])
            && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]
        {
            inside = !inside;
        }
        j = i;
    }
    inside
}

fn dist(a: [f64; 2], b: [f64; 2]) -> f64 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

#[cfg(test)]
#[path = "profiles_tests.rs"]
mod tests;
