#pragma once

#include <QColor>
#include <QString>

// All colours and sizes of the UI, defined once. The stylesheet, icons and the
// 3D viewport read from here, so a light theme is just another Palette.
namespace theme {

struct Palette {
  QColor appBackground;    // behind everything
  QColor barBackground;    // document bar (top)
  QColor toolbar;          // ribbon and timeline
  QColor panel;            // floating panels and dialogs
  QColor panelBorder;
  QColor field;            // inputs
  QColor fieldBorder;
  QColor hover;
  QColor selection;
  QColor text;
  QColor textMuted;
  QColor textDisabled;
  QColor accent;           // brass: active tool, primary button, timeline marker
  QColor accentText;       // text on accent
  QColor highlight;        // selection blue in the viewport
  QColor success;
  QColor danger;
  QColor viewport;         // 3D background
  QColor viewportGrid;
  bool light = false;      // picks the light icon variants
};

const Palette& dark();
const Palette& light();
// The theme chosen in Preferences (read once at startup).
const Palette& current();

// Qt stylesheet generated from the palette.
QString styleSheet(const Palette& p = current());

inline constexpr int kIconSize = 30;        // ribbon tool icons
inline constexpr int kSmallIconSize = 16;   // browser, nav bar, timeline
inline constexpr int kPanelRadius = 8;

}  // namespace theme
