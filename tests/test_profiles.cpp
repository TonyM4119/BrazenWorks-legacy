// Profiles → real Open CASCADE solids: sketch regions extruded through the
// bridge, volumes checked against exact values.
#include "Check.h"
#include "GeometryBridge.h"
#include "Kernel.h"
#include "model_core/src/ffi.rs.h"
#include "rust/cxx.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;

double rebuildVolume(const model::Timeline& tl, std::size_t expectFailed = 0) {
  rust::Vec<std::size_t> failed;
  auto body = model::timeline_rebuild(tl, failed);
  CHECK(failed.size() == expectFailed);
  return body ? geom::volume(body->value) : 0.0;
}

rust::Vec<rust::String> keys(std::initializer_list<rust::String> list) {
  rust::Vec<rust::String> v;
  for (const auto& k : list) v.push_back(k);
  return v;
}

std::uint64_t firstPoint(model::Timeline& tl, std::uint64_t sk, std::uint64_t line) {
  for (const auto& e : model::sketch_entities(tl, sk))
    if (e.id == line) return e.p0;
  return 0;
}

}  // namespace

int main() {
  // ---- Rectangle with a hole: ring + disc profiles ---------------------------
  rust::Box<model::Timeline> tl = model::new_timeline();
  const std::uint64_t sk = model::timeline_add_sketch(*tl, "Plate", model::OriginPlaneKind::XY);
  auto lines = model::sketch_add_rectangle(*tl, sk, 0, 0, 40, 20);
  model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Fix, firstPoint(*tl, sk, lines[0]), 0, 0, 0);
  auto width = model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Length, lines[0], 0, 0, 40);
  model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Length, lines[1], 0, 0, 20);
  model::sketch_add_circle_xy(*tl, sk, 20, 10, 3);

  auto profiles = model::sketch_profiles(*tl, sk);
  CHECK(profiles.size() == 2);
  const double ring = 800.0 - kPi * 9.0;
  CHECK_NEAR(profiles[0].area, ring, 1e-9);
  CHECK(profiles[0].loop_sizes.size() == 2);  // outer + one hole
  CHECK_NEAR(profiles[1].area, kPi * 9.0, 1e-9);
  const rust::String ringKey = profiles[0].key;
  const rust::String discKey = profiles[1].key;

  model::timeline_add_extrude_sketch(*tl, "Plate", sk, keys({ringKey}), 5, model::BodyOp::Join);
  CHECK_NEAR(rebuildVolume(*tl), ring * 5.0, 1e-6);

  // Editing the sketch updates the solid; the profile keeps its key.
  model::sketch_set_dimension(*tl, sk, width.id, 60);
  CHECK(std::string(model::sketch_profiles(*tl, sk)[0].key) == std::string(ringKey));
  CHECK_NEAR(rebuildVolume(*tl), (1200.0 - kPi * 9.0) * 5.0, 1e-6);

  // Extrude the disc too, joined: a solid block with no hole.
  model::timeline_add_extrude_sketch(*tl, "Plug", sk, keys({discKey}), 5, model::BodyOp::Join);
  CHECK_NEAR(rebuildVolume(*tl), 1200.0 * 5.0, 1e-6);

  // A profile that no longer exists fails cleanly.
  model::timeline_add_extrude_sketch(*tl, "Ghost", sk, keys({"999,1000"}), 5, model::BodyOp::Join);
  CHECK_NEAR(rebuildVolume(*tl, 1), 1200.0 * 5.0, 1e-6);

  // ---- Slot with arcs on the front (XZ) plane -----------------------------------
  rust::Box<model::Timeline> slot = model::new_timeline();
  const std::uint64_t ss = model::timeline_add_sketch(*slot, "Slot", model::OriginPlaneKind::XZ);
  const auto a = model::sketch_add_point(*slot, ss, 0, 0);
  const auto b = model::sketch_add_point(*slot, ss, 40, 0);
  const auto c = model::sketch_add_point(*slot, ss, 40, 12);
  const auto d = model::sketch_add_point(*slot, ss, 0, 12);
  model::sketch_add_line(*slot, ss, a, b);
  model::sketch_add_line(*slot, ss, c, d);
  model::sketch_add_arc(*slot, ss, model::sketch_add_point(*slot, ss, 40, 6), b, c);
  model::sketch_add_arc(*slot, ss, model::sketch_add_point(*slot, ss, 0, 6), d, a);
  auto slotProfiles = model::sketch_profiles(*slot, ss);
  CHECK(slotProfiles.size() == 1);
  const double slotArea = 40.0 * 12.0 + kPi * 36.0;
  CHECK_NEAR(slotProfiles[0].area, slotArea, 1e-9);
  model::timeline_add_extrude_sketch(*slot, "Slot", ss, keys({slotProfiles[0].key}), 8, model::BodyOp::Join);
  rust::Vec<std::size_t> failed;
  auto body = model::timeline_rebuild(*slot, failed);
  CHECK(body && failed.empty());
  CHECK(geom::isValid(body->value));
  CHECK_NEAR(geom::volume(body->value), slotArea * 8.0, 1e-4);

  // The front plane's normal is −Y: the slot spans y ∈ [−8, 0], x ∈ [−6, 46], z ∈ [0, 12].
  Bnd_Box box;
  BRepBndLib::AddOptimal(body->value, box, false, false);
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  CHECK_NEAR(y0, -8.0, 1e-6);
  CHECK_NEAR(y1, 0.0, 1e-6);
  CHECK_NEAR(x0, -6.0, 1e-6);
  CHECK_NEAR(x1, 46.0, 1e-6);
  CHECK_NEAR(z1, 12.0, 1e-6);
  return 0;
}
