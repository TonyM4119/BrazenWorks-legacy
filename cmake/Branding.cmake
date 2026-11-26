# Reads branding.json — the ONLY place the product name is defined.
# Every target, generated header, desktop file and bundle name comes from here.
# To rename the product, run: python3 tools/rename.py --name "New Name"

set(BRANDING_FILE "${CMAKE_CURRENT_LIST_DIR}/../branding.json")
file(READ "${BRANDING_FILE}" _branding_json)

# Re-run CMake automatically whenever branding.json changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BRANDING_FILE}")

function(_brand_get out_var key)
  string(JSON _value ERROR_VARIABLE _err GET "${_branding_json}" "${key}")
  if(_err)
    message(FATAL_ERROR "branding.json is missing required key '${key}'")
  endif()
  set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

_brand_get(BRAND_DISPLAY_NAME   displayName)
_brand_get(BRAND_SLUG           slug)
_brand_get(BRAND_TAGLINE        tagline)
_brand_get(BRAND_ORGANIZATION   organization)
_brand_get(BRAND_DOMAIN         domain)
_brand_get(BRAND_REPO_URL       repoUrl)
_brand_get(BRAND_FILE_EXTENSION fileExtension)
_brand_get(BRAND_APP_ID         appId)

if(NOT BRAND_SLUG MATCHES "^[a-z][a-z0-9-]*$")
  message(FATAL_ERROR "branding.json: slug '${BRAND_SLUG}' must be lowercase letters, digits and dashes")
endif()

message(STATUS "Branding: ${BRAND_DISPLAY_NAME} (${BRAND_SLUG})")
