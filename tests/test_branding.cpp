#include "Branding.h"
#include "Check.h"

#include <cstring>

int main() {
  // The generated header must match branding.json exactly.
  CHECK(std::strcmp(branding::kDisplayName, EXPECTED_DISPLAY_NAME) == 0);
  CHECK(std::strcmp(branding::kSlug, EXPECTED_SLUG) == 0);
  CHECK(std::strlen(branding::kDisplayName) > 0);
  CHECK(std::strlen(branding::kFileExtension) > 0);
  CHECK(std::strlen(branding::kVersion) > 0);
  return 0;
}
