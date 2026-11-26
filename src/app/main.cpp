#include "Branding.h"
#include "MainWindow.h"

#include <QApplication>
#include <QFile>

int main(int argc, char* argv[]) {
  // The OCCT viewport needs a native window handle. Without this flag Qt would
  // make every sibling widget native too, which breaks Qt's compositing.
  QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  QApplication app(argc, argv);

  // Everything user-visible — window titles, settings paths, the About box —
  // derives from branding.json through these calls.
  QApplication::setApplicationName(branding::kSlug);
  QApplication::setApplicationDisplayName(branding::kDisplayName);
  QApplication::setApplicationVersion(branding::kVersion);
  QApplication::setOrganizationName(branding::kOrganization);
  QApplication::setOrganizationDomain(branding::kDomain);
  QApplication::setDesktopFileName(branding::kAppId);

  if (QFile theme(":/theme/dark.qss"); theme.open(QIODevice::ReadOnly)) {
    app.setStyleSheet(QString::fromUtf8(theme.readAll()));
  }

  MainWindow window;
  window.show();
  return app.exec();
}
