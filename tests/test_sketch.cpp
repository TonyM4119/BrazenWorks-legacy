// Integration test: the sketch API as the Qt UI will use it, through the
// CXX bridge (Rust sketch model + MitCad solver behind the adapter).
#include "Check.h"
#include "GeometryBridge.h"
#include "Kernel.h"
#include "model_core/src/ffi.rs.h"
#include "rust/cxx.h"

#include <cmath>
#include <string>

namespace {

const model::SketchEntityInfo* find(const rust::Vec<model::SketchEntityInfo>& v, std::uint64_t id) {
  for (const auto& e : v)
    if (e.id == id) return &e;
  return nullptr;
}

}  // namespace

int main() {
  constexpr double kTol = 1e-6;
  rust::Box<model::Timeline> tl = model::new_timeline();

  // A sketch on the front plane is a feature with a stable id.
  const std::uint64_t sk = model::timeline_add_sketch(*tl, "Sketch1", model::OriginPlaneKind::XZ);
  CHECK(sk != 0);
  CHECK(model::timeline_index_of(*tl, sk) == 0);
  CHECK(std::string(model::timeline_feature_type(*tl, 0)) == "Sketch");
  const model::PlaneInfo plane = model::sketch_plane(*tl, sk);
  CHECK_NEAR(plane.normal[1], -1.0, kTol);

  // Rectangle tool: 4 lines with horizontal/vertical constraints.
  rust::Vec<std::uint64_t> lines = model::sketch_add_rectangle(*tl, sk, 1, 1, 30, 15);
  CHECK(lines.size() == 4);
  auto entities = model::sketch_entities(*tl, sk);
  CHECK(entities.size() == 8);
  const auto* bottom = find(entities, lines[0]);
  CHECK(bottom && bottom->kind == model::SketchEntityKind::Line);
  CHECK(!bottom->fully_constrained);

  // Fix a corner and dimension it: fully constrained.
  model::ConstraintResult fix = model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Fix, bottom->p0, 0, 0, 0);
  CHECK(fix.ok);
  model::ConstraintResult width =
      model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Length, lines[0], 0, 0, 40);
  model::ConstraintResult height =
      model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Length, lines[1], 0, 0, 20);
  CHECK(width.ok && height.ok);
  model::SketchSolveInfo solved = model::sketch_solve(*tl, sk);
  CHECK(solved.status == model::SketchSolveStatus::Solved);
  CHECK(solved.dof == 0);
  entities = model::sketch_entities(*tl, sk);
  CHECK(find(entities, lines[0])->fully_constrained);
  CHECK_NEAR(find(entities, lines[1])->x1, 41.0, kTol);
  CHECK_NEAR(find(entities, lines[1])->y1, 21.0, kTol);

  // A contradicting dimension is refused, naming the width, with a message.
  model::ConstraintResult bad = model::sketch_add_constraint(*tl, sk, model::ConstraintKind::Length, lines[2], 0, 0, 30);
  CHECK(!bad.ok);
  bool blamesWidth = false;
  for (auto id : bad.conflicts) blamesWidth = blamesWidth || id == width.id;
  CHECK(blamesWidth);
  CHECK(std::string(bad.message).find("over-constrain") != std::string::npos);

  // Editing a dimension moves the geometry.
  solved = model::sketch_set_dimension(*tl, sk, width.id, 55);
  CHECK(solved.status == model::SketchSolveStatus::Solved);
  entities = model::sketch_entities(*tl, sk);
  CHECK_NEAR(find(entities, lines[1])->x1, 56.0, kTol);

  // Constraints are listed back with their kinds and values.
  auto constraints = model::sketch_constraints(*tl, sk);
  CHECK(constraints.size() == 7);  // 4 from the rectangle + fix + 2 lengths
  bool sawWidth = false;
  for (const auto& c : constraints) {
    if (c.id == width.id) {
      sawWidth = true;
      CHECK(c.kind == model::ConstraintKind::Length && c.is_dimension);
      CHECK_NEAR(c.value, 55.0, kTol);
    }
  }
  CHECK(sawWidth);

  // A sketch doesn't change the body; the solid after it still builds.
  model::timeline_add_extrude(*tl, "Plate", model::Profile{model::ProfileKind::Rect, 0, 0, 40, 20, 0}, 5,
                              model::BodyOp::Join);
  rust::Vec<std::size_t> failed;
  auto body = model::timeline_rebuild(*tl, failed);
  CHECK(failed.empty());
  CHECK(body && std::abs(geom::volume(body->value) - 4000.0) < kTol);

  // Inserting before the sketch doesn't change its id.
  model::timeline_set_marker(*tl, 0);
  model::timeline_add_fillet(*tl, "Round", 0.5);
  CHECK(model::timeline_index_of(*tl, sk) == 1);
  CHECK(model::sketch_entities(*tl, sk).size() == 8);

  // Asking for something that isn't a sketch throws, it doesn't crash.
  bool threw = false;
  try {
    model::sketch_solve(*tl, 999);
  } catch (const rust::Error&) {
    threw = true;
  }
  CHECK(threw);
  return 0;
}
