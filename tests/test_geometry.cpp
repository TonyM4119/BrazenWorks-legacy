#include "Check.h"
#include "Kernel.h"

#include <cstdio>
#include <filesystem>

int main() {
  constexpr double kTol = 1e-6;
  constexpr double kPi = 3.14159265358979323846;

  // Box volume.
  const TopoDS_Shape box = geom::makeBox(10, 20, 30);
  CHECK(geom::isValid(box));
  CHECK_NEAR(geom::volume(box), 6000.0, kTol);

  // Extruded rectangle == box.
  const TopoDS_Shape plate = geom::extrude(geom::RectProfile{40, 20}, 5);
  CHECK(geom::isValid(plate));
  CHECK_NEAR(geom::volume(plate), 4000.0, kTol);

  // Extruded circle == cylinder.
  const TopoDS_Shape cyl = geom::extrude(geom::CircleProfile{5}, 10);
  CHECK(geom::isValid(cyl));
  CHECK_NEAR(geom::volume(cyl), kPi * 25.0 * 10.0, 1e-3);

  // Cutting a hole removes material.
  const TopoDS_Shape drilled = geom::cut(plate, geom::extrude(geom::CircleProfile{3}, 5));
  CHECK(geom::isValid(drilled));
  CHECK(geom::volume(drilled) < geom::volume(plate));

  // Filleting edges removes a little material and stays valid.
  const TopoDS_Shape rounded = geom::filletAllEdges(box, 1.0);
  CHECK(geom::isValid(rounded));
  CHECK(geom::volume(rounded) < 6000.0);
  CHECK(geom::volume(rounded) > 5900.0);

  // An impossible fillet fails cleanly instead of crashing.
  CHECK(geom::filletAllEdges(box, 50.0).IsNull());

  // STEP export writes a file.
  const auto path = std::filesystem::temp_directory_path() / "geometry_test_box.step";
  CHECK(geom::exportStep(box, path.string()));
  CHECK(std::filesystem::file_size(path) > 0);
  std::filesystem::remove(path);
  return 0;
}
