//! The plane a sketch lies on, and conversion between sketch (u, v) and world
//! (x, y, z) coordinates.

pub type Vec3 = [f64; 3];

/// A plane with an origin and two perpendicular unit axes. Sketch coordinates
/// (u, v) map to `origin + u * x_axis + v * y_axis`; the normal is
/// `x_axis × y_axis` (the direction extrudes go by default).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SketchPlane {
    pub origin: Vec3,
    pub x_axis: Vec3,
    pub y_axis: Vec3,
}

/// The three origin planes offered by Create Sketch.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OriginPlane {
    /// Ground plane; sketch u = world X, v = world Y, normal +Z.
    XY,
    /// Front plane; u = X, v = Z, normal −Y (faces the viewer in the front view).
    XZ,
    /// Side plane; u = Y, v = Z, normal +X.
    YZ,
}

impl SketchPlane {
    pub fn origin_plane(which: OriginPlane) -> Self {
        let (x_axis, y_axis) = match which {
            OriginPlane::XY => ([1.0, 0.0, 0.0], [0.0, 1.0, 0.0]),
            OriginPlane::XZ => ([1.0, 0.0, 0.0], [0.0, 0.0, 1.0]),
            OriginPlane::YZ => ([0.0, 1.0, 0.0], [0.0, 0.0, 1.0]),
        };
        Self {
            origin: [0.0; 3],
            x_axis,
            y_axis,
        }
    }

    pub fn normal(&self) -> Vec3 {
        cross(self.x_axis, self.y_axis)
    }

    pub fn to_world(&self, u: f64, v: f64) -> Vec3 {
        [
            self.origin[0] + u * self.x_axis[0] + v * self.y_axis[0],
            self.origin[1] + u * self.x_axis[1] + v * self.y_axis[1],
            self.origin[2] + u * self.x_axis[2] + v * self.y_axis[2],
        ]
    }

    /// Projects a world point onto the plane and returns its (u, v).
    pub fn to_sketch(&self, p: Vec3) -> [f64; 2] {
        let d = [
            p[0] - self.origin[0],
            p[1] - self.origin[1],
            p[2] - self.origin[2],
        ];
        [dot(d, self.x_axis), dot(d, self.y_axis)]
    }
}

impl Default for SketchPlane {
    fn default() -> Self {
        Self::origin_plane(OriginPlane::XY)
    }
}

fn cross(a: Vec3, b: Vec3) -> Vec3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn dot(a: Vec3, b: Vec3) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn origin_planes_have_expected_normals() {
        assert_eq!(
            SketchPlane::origin_plane(OriginPlane::XY).normal(),
            [0.0, 0.0, 1.0]
        );
        assert_eq!(
            SketchPlane::origin_plane(OriginPlane::XZ).normal(),
            [0.0, -1.0, 0.0]
        );
        assert_eq!(
            SketchPlane::origin_plane(OriginPlane::YZ).normal(),
            [1.0, 0.0, 0.0]
        );
    }

    #[test]
    fn world_round_trip() {
        let mut plane = SketchPlane::origin_plane(OriginPlane::XZ);
        plane.origin = [5.0, 2.0, -1.0];
        let w = plane.to_world(3.0, 4.0);
        assert_eq!(w, [8.0, 2.0, 3.0]);
        assert_eq!(plane.to_sketch(w), [3.0, 4.0]);
    }
}
