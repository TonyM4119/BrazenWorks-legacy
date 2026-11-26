#include "Branding.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // Everything user-visible — window titles, settings paths, the About box —
  // derives from branding.json through these calls.
  QApplication::setApplicationName(branding::kSlug);
  QApplication::setApplicationDisplayName(branding::kDisplayName);
  QApplication::setApplicationVersion(branding::kVersion);
  QApplication::setOrganizationName(branding::kOrganization);
  QApplication::setOrganizationDomain(branding::kDomain);
  QApplication::setDesktopFileName(branding::kAppId);

  app.setStyle("Fusion");  // consistent base on every OS; the theme styles it
  app.setStyleSheet(theme::styleSheet());

  MainWindow window;
  window.show();
  return app.exec();
}
