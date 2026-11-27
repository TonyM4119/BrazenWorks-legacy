#pragma once

#include <QSettings>
#include <QString>
#include <QVariant>

// User preferences, one key and one default each. Preferences edits them;
// the rest of the app reads them through these helpers.
namespace prefs {

struct Key {
  const char* name;
  QVariant fallback;
};

// General
inline const Key kTheme{"appearance/theme", "dark"};          // "dark" | "light"  (restart)
inline const Key kDefaultUnit{"units/default", "in"};          // unit symbol
inline const Key kShowVersion{"general/showVersion", true};
inline const Key kShowUsedSketches{"general/showUsedSketches", false};
// Navigation
inline const Key kOrbitMiddle{"navigation/orbitWithMiddle", false};  // false: Shift+middle orbits
inline const Key kReverseZoom{"navigation/reverseZoom", false};
inline const Key kAnimateCamera{"navigation/animateCamera", true};
// Display
inline const Key kAntialiasing{"display/antialiasing", true};  // restart
inline const Key kShowGrid{"display/showGrid", true};
inline const Key kHighQuality{"display/highQuality", true};    // restart
// Sketch
inline const Key kSnap{"sketch/snap", true};
inline const Key kInference{"sketch/inference", true};
inline const Key kConstraintTags{"sketch/constraintTags", true};
inline const Key kPickRadius{"sketch/pickRadius", 9};

inline QVariant get(const Key& k) { return QSettings().value(k.name, k.fallback); }
inline bool flag(const Key& k) { return get(k).toBool(); }
inline void set(const Key& k, const QVariant& v) { QSettings().setValue(k.name, v); }

inline const Key* all[] = {&kTheme,     &kDefaultUnit,  &kShowVersion,  &kShowUsedSketches,
                           &kOrbitMiddle, &kReverseZoom, &kAnimateCamera, &kAntialiasing,
                           &kShowGrid,  &kHighQuality,  &kSnap,         &kInference,
                           &kConstraintTags, &kPickRadius};

}  // namespace prefs
