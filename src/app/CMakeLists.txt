# app — the Qt desktop application. The executable, bundle and desktop entry
# all take their names from branding.json via cmake/Branding.cmake.
set(CMAKE_AUTOMOC ON)

qt_add_executable(app
  main.cpp
  MainWindow.cpp
  MainWindow.h
  Viewport.cpp
  Viewport.h
  Theme.cpp
  Theme.h
  Icons.cpp
  Icons.h
  widgets/Animations.cpp
  widgets/Animations.h
  widgets/CommandDialog.cpp
  widgets/CommandDialog.h
  widgets/DocumentBar.cpp
  widgets/DocumentBar.h
  widgets/FloatingPanel.cpp
  widgets/LengthField.cpp
  widgets/LengthField.h
  widgets/FloatingPanel.h
  widgets/NavBar.cpp
  widgets/NavBar.h
  widgets/Ribbon.cpp
  widgets/Ribbon.h
  widgets/TimelineBar.cpp
  widgets/TimelineBar.h)

target_include_directories(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Icons: every SVG in resources/icons, served as :/icons/<name>.svg
file(GLOB APP_ICONS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(app "icons"
  PREFIX "/"
  BASE "${CMAKE_SOURCE_DIR}/resources"
  FILES ${APP_ICONS})

target_link_libraries(app PRIVATE
  model branding
  Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg
  TKService TKV3d TKOpenGl)

if(WIN32)
  target_sources(app PRIVATE GlWindowHandle_win.cpp)
  target_link_libraries(app PRIVATE opengl32)  # wglGetCurrentDC
endif()

set_target_properties(app PROPERTIES
  OUTPUT_NAME "${BRAND_SLUG}"
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME "${BRAND_DISPLAY_NAME}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${BRAND_APP_ID}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# Windows: copy the Qt DLLs and plugins next to the exe after each build, so it
# starts from Explorer too (msvc.cmd only puts Qt on PATH inside its own window).
if(WIN32)
  set(_qt_bin_hints "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
  if(TARGET Qt6::qmake)
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qmake_dir "${_qmake}" DIRECTORY)
    list(APPEND _qt_bin_hints "${_qmake_dir}")
  endif()
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${_qt_bin_hints} NO_DEFAULT_PATH)
  if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET app POST_BUILD
      COMMAND "${WINDEPLOYQT_EXECUTABLE}" --verbose 0 --no-translations --no-compiler-runtime
              --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:app>"
      COMMENT "Copying Qt next to $<TARGET_FILE_NAME:app>"
      VERBATIM)
  else()
    message(WARNING "windeployqt not found: run the app through tools/dev-env/msvc.cmd")
  endif()
endif()

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
