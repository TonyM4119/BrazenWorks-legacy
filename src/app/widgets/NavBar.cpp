#include "NavBar.h"

#include "Animations.h"
#include "Icons.h"
#include "Theme.h"

#include <QActionGroup>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

NavBar::NavBar(QWidget* parent) : QFrame(parent) {
  setObjectName("NavBar");
  setAttribute(Qt::WA_StyledBackground);
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(4, 4, 4, 4);
  row->setSpacing(2);

  connect(addButton("home", tr("Home view")), &QToolButton::clicked, this, &NavBar::homeRequested);
  addSeparator();

  // Left-drag mode: exclusive toggle; clicking the active one returns to Select.
  auto* modes = new QButtonGroup(this);
  modes->setExclusive(false);
  const struct { const char* icon; const char* tip; DragMode mode; } modeButtons[] = {
      {"orbit", "Orbit (left-drag rotates). Shortcut: Shift + middle-drag", DragMode::Orbit},
      {"pan", "Pan (left-drag pans). Shortcut: middle-drag", DragMode::Pan},
      {"zoom", "Zoom (left-drag zooms). Shortcut: mouse wheel", DragMode::Zoom},
  };
  for (const auto& m : modeButtons) {
    QToolButton* b = addButton(m.icon, tr(m.tip), true);
    modes->addButton(b);
    const DragMode mode = m.mode;
    connect(b, &QToolButton::toggled, this, [this, modes, b, mode](bool on) {
      if (on) {
        for (QAbstractButton* other : modes->buttons()) {
          if (other != b) other->setChecked(false);
        }
        emit dragModeChanged(mode);
      } else if (!modes->checkedButton()) {
        emit dragModeChanged(DragMode::Select);
      }
    });
  }

  connect(addButton("fit", tr("Fit (F6)")), &QToolButton::clicked, this, &NavBar::fitRequested);
  addSeparator();

  QToolButton* display = addButton("display", tr("Display settings"));
  auto* menu = new QMenu(display);
  auto* group = new QActionGroup(menu);
  const struct { const char* label; DisplayMode mode; } displayModes[] = {
      {"Shaded with visible edges", DisplayMode::ShadedWithEdges},
      {"Shaded", DisplayMode::Shaded},
      {"Wireframe", DisplayMode::Wireframe},
  };
  for (const auto& d : displayModes) {
    QAction* a = menu->addAction(tr(d.label));
    a->setCheckable(true);
    a->setChecked(d.mode == DisplayMode::ShadedWithEdges);
    group->addAction(a);
    const DisplayMode mode = d.mode;
    connect(a, &QAction::triggered, this, [this, mode] { emit displayModeChanged(mode); });
  }
  display->setMenu(menu);
  display->setPopupMode(QToolButton::InstantPopup);

  QToolButton* grid = addButton("grid", tr("Show grid"), true);
  grid->setChecked(true);
  connect(grid, &QToolButton::toggled, this, &NavBar::gridToggled);
}

QToolButton* NavBar::addButton(const QString& icon, const QString& tip, bool checkable) {
  auto* b = new AnimatedToolButton(this);
  b->setIcon(icons::get(icon, 18));
  b->setIconSize(QSize(18, 18));
  b->setToolTip(tip);
  b->setCheckable(checkable);
  b->setAutoRaise(true);
  layout()->addWidget(b);
  return b;
}

void NavBar::addSeparator() {
  auto* line = new QFrame(this);
  line->setObjectName("NavSeparator");
  line->setAttribute(Qt::WA_StyledBackground);
  line->setFixedSize(1, 20);
  layout()->addWidget(line);
}
