# app — the Qt desktop application. The executable, bundle and desktop entry
# all take their names from branding.json via cmake/Branding.cmake.
set(CMAKE_AUTOMOC ON)

qt_add_executable(app
  main.cpp
  MainWindow.cpp
  MainWindow.h
  Viewport.cpp
  Viewport.h)

qt_add_resources(app "theme"
  PREFIX "/"
  BASE "${CMAKE_SOURCE_DIR}/resources"
  FILES "${CMAKE_SOURCE_DIR}/resources/theme/dark.qss")

target_link_libraries(app PRIVATE
  model branding
  Qt6::Widgets Qt6::OpenGLWidgets
  TKService TKV3d TKOpenGl)

set_target_properties(app PROPERTIES
  OUTPUT_NAME "${BRAND_SLUG}"
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME "${BRAND_DISPLAY_NAME}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${BRAND_APP_ID}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# ---- Install / packaging -----------------------------------------------------
include(GNUInstallDirs)
install(TARGETS app
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .)

if(UNIX AND NOT APPLE)
  configure_file("${CMAKE_SOURCE_DIR}/packaging/linux/app.desktop.in"
                 "${CMAKE_BINARY_DIR}/${BRAND_APP_ID}.desktop" @ONLY)
  install(FILES "${CMAKE_BINARY_DIR}/${BRAND_APP_ID}.desktop"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
