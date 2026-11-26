// Integration test: the Rust timeline driving real Open CASCADE geometry
// through the CXX bridge.
#include "Check.h"
#include "GeometryBridge.h"
#include "Kernel.h"
#include "model_core/src/ffi.rs.h"

namespace {

model::Profile rect(double x, double y, double w, double h) {
  return model::Profile{model::ProfileKind::Rect, x, y, w, h, 0.0};
}

model::Profile circle(double cx, double cy, double r) {
  return model::Profile{model::ProfileKind::Circle, cx, cy, 0.0, 0.0, r};
}

double volumeOf(const model::Timeline& tl, rust::Vec<std::size_t>* failed = nullptr) {
  rust::Vec<std::size_t> f;
  const auto body = model::timeline_rebuild(tl, f);
  if (failed) *failed = std::move(f);
  return body ? geom::volume(body->value) : 0.0;
}

}  // namespace

int main() {
  constexpr double kTol = 1e-6;

  rust::Box<model::Timeline> tl = model::new_timeline();
  model::timeline_add_extrude(*tl, "Base", rect(0, 0, 40, 20), 5, model::BodyOp::Join);
  model::timeline_add_extrude(*tl, "Hole", circle(20, 10, 3), 5, model::BodyOp::Cut);
  CHECK(model::timeline_len(*tl) == 2);
  CHECK(model::timeline_marker(*tl) == 2);

  const double full = volumeOf(*tl);
  CHECK(full < 4000.0);
  CHECK(full > 3800.0);

  // Roll back past the hole.
  model::timeline_set_marker(*tl, 1);
  CHECK_NEAR(volumeOf(*tl), 4000.0, kTol);

  // Insert at the marker: lands before "Hole".
  model::timeline_add_fillet(*tl, "Round", 0.5);
  CHECK(std::string(model::timeline_feature_name(*tl, 1)) == "Round");
  CHECK(std::string(model::timeline_feature_type(*tl, 1)) == "Fillet");
  CHECK(std::string(model::timeline_feature_name(*tl, 2)) == "Hole");

  // Parametric edit: thicker base, replay everything.
  model::timeline_set_marker(*tl, model::timeline_len(*tl));
  CHECK(model::timeline_set_extrude_distance(*tl, 0, 10));
  rust::Vec<std::size_t> failed;
  const double thicker = volumeOf(*tl, &failed);
  CHECK(failed.empty());
  CHECK(thicker > full);

  // A failing feature is reported, not fatal.
  model::timeline_add_fillet(*tl, "Too big", 100.0);
  const double afterBad = volumeOf(*tl, &failed);
  CHECK(failed.size() == 1);
  CHECK(failed[0] == 3);
  CHECK_NEAR(afterBad, thicker, 1e-6);

  // Cut with no body fails cleanly.
  rust::Box<model::Timeline> empty = model::new_timeline();
  model::timeline_add_extrude(*empty, "Orphan cut", circle(0, 0, 3), 5, model::BodyOp::Cut);
  CHECK(volumeOf(*empty, &failed) == 0.0);
  CHECK(failed.size() == 1);
  return 0;
}
