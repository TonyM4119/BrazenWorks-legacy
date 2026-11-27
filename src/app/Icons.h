#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

// Icons are SVG files in resources/icons drawn with `currentColor`, so one
// file serves every state and theme: we recolour at load time.
namespace icons {

// A QIcon with normal, disabled and active (checked/selected) colours from the
// current theme.
QIcon get(const QString& name, int size = 24);
// True when resources/icons/color/<name>.svg exists (get() then uses it as is).
bool hasColor(const QString& name);

// A single-colour icon.
QIcon tinted(const QString& name, const QColor& color, int size = 24);

}  // namespace icons
