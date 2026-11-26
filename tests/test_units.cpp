// Units through the bridge, as the UI uses them.
#include "Check.h"
#include "GeometryBridge.h"
#include "model_core/src/ffi.rs.h"

#include <string>

int main() {
  using U = model::LengthUnitKind;
  rust::Box<model::Timeline> tl = model::new_timeline();

  // New documents are in inches.
  CHECK(model::timeline_units(*tl) == U::Inch);
  model::timeline_set_units(*tl, U::Millimeter);
  CHECK(model::timeline_units(*tl) == U::Millimeter);

  CHECK(std::string(model::length_format(25.4, U::Inch)) == "1.00 in");
  CHECK(std::string(model::length_format(9.525, U::Inch)) == "0.375 in");
  CHECK(std::string(model::length_format(25.4, U::Millimeter)) == "25.40 mm");

  const auto quarter = model::length_parse("1/4", U::Inch);
  CHECK(quarter.ok);
  CHECK_NEAR(quarter.mm, 6.35, 1e-12);
  const auto mixed = model::length_parse("1' 6\"", U::Millimeter);
  CHECK(mixed.ok);
  CHECK_NEAR(mixed.mm, 457.2, 1e-9);
  const auto metricInInchDoc = model::length_parse("25mm", U::Inch);
  CHECK(metricInInchDoc.ok);
  CHECK_NEAR(metricInInchDoc.mm, 25.0, 1e-12);

  const auto bad = model::length_parse("2 parsecs", U::Inch);
  CHECK(!bad.ok);
  CHECK(std::string(bad.error) == "unknown unit 'parsecs'");

  CHECK(std::string(model::length_unit_symbol(U::Foot)) == "ft");
  CHECK(std::string(model::length_unit_name(U::Centimeter)) == "Centimeters");
  CHECK_NEAR(model::length_unit_mm(U::Inch), 25.4, 1e-12);
  return 0;
}
