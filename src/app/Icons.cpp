#include "Icons.h"

#include "Theme.h"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace icons {

namespace {

QByteArray svgSource(const QString& name) {
  static QHash<QString, QByteArray> cache;
  if (auto it = cache.constFind(name); it != cache.cend()) return *it;
  QFile file(":/icons/" + name + ".svg");
  QByteArray data;
  if (file.open(QIODevice::ReadOnly)) {
    data = file.readAll();
  } else {
    qWarning("icons: missing resources/icons/%s.svg", qPrintable(name));
  }
  cache.insert(name, data);
  return data;
}

QPixmap render(const QString& name, const QColor& color, int size, qreal dpr) {
  QByteArray svg = svgSource(name);
  if (svg.isEmpty()) return {};
  svg.replace("currentColor", color.name().toUtf8());

  QSvgRenderer renderer(svg);
  const int px = int(size * dpr);
  QPixmap pixmap(px, px);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  renderer.render(&painter);
  painter.end();
  pixmap.setDevicePixelRatio(dpr);
  return pixmap;
}

qreal devicePixelRatio() {
  return qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
}

}  // namespace

QIcon tinted(const QString& name, const QColor& color, int size) {
  QIcon icon;
  icon.addPixmap(render(name, color, size, devicePixelRatio()));
  return icon;
}

QIcon get(const QString& name, int size) {
  const theme::Palette& p = theme::current();
  const qreal dpr = devicePixelRatio();
  QIcon icon;
  icon.addPixmap(render(name, p.text, size, dpr), QIcon::Normal, QIcon::Off);
  icon.addPixmap(render(name, p.textDisabled, size, dpr), QIcon::Disabled, QIcon::Off);
  icon.addPixmap(render(name, p.accent, size, dpr), QIcon::Normal, QIcon::On);
  icon.addPixmap(render(name, p.accent, size, dpr), QIcon::Active, QIcon::On);
  icon.addPixmap(render(name, p.text, size, dpr), QIcon::Active, QIcon::Off);
  icon.addPixmap(render(name, p.text, size, dpr), QIcon::Selected, QIcon::Off);
  return icon;
}

}  // namespace icons
