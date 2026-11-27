#include "Ribbon.h"

#include "Animations.h"
#include "Icons.h"
#include "Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QGridLayout>
#include <QMenu>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

Ribbon::Ribbon(QWidget* parent) : QFrame(parent) {
  setObjectName("Ribbon");
  setAttribute(Qt::WA_StyledBackground);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(10, 6, 10, 4);
  row->setSpacing(12);

  workspaceBox_ = new QComboBox(this);
  workspaceBox_->setObjectName("WorkspaceSwitcher");
  workspaceBox_->setToolTip(tr("Change workspace"));
  connect(workspaceBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
    rebuildTools();
    emit workspaceChanged(i);
  });
  row->addWidget(workspaceBox_, 0, Qt::AlignVCenter);

  auto* right = new QVBoxLayout;
  right->setSpacing(4);
  auto* tabHolder = new QWidget(this);
  tabHolder->setObjectName("RibbonTabs");
  auto* tabRow = new QHBoxLayout(tabHolder);
  tabRow->setContentsMargins(0, 0, 0, 0);
  tabs_ = new QTabBar(tabHolder);
  tabs_->setDrawBase(false);
  tabs_->setExpanding(false);
  tabs_->setUsesScrollButtons(true);
  tabRow->addWidget(tabs_);
  tabRow->addStretch(1);
  right->addWidget(tabHolder);

  toolRow_ = new QHBoxLayout;
  toolRow_->setSpacing(0);
  right->addLayout(toolRow_);
  row->addLayout(right, 1);
}

void Ribbon::setWorkspaces(const QVector<Workspace>& workspaces) {
  workspaces_ = workspaces;
  QSignalBlocker block(workspaceBox_);
  workspaceBox_->clear();
  for (const Workspace& w : workspaces_) workspaceBox_->addItem(w.name.toUpper());
  rebuildTools();
}

void Ribbon::setCurrentWorkspace(int index) { workspaceBox_->setCurrentIndex(index); }

int Ribbon::currentWorkspace() const { return workspaceBox_->currentIndex(); }

void Ribbon::setActiveTool(const QString& id) {
  activeTool_ = id;
  for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
    it.value()->setProperty("active", it.key() == id);
    it.value()->style()->unpolish(it.value());
    it.value()->style()->polish(it.value());
  }
}

void Ribbon::rebuildTools() {
  const int ws = workspaceBox_->currentIndex();
  if (ws < 0 || ws >= workspaces_.size()) return;
  const Workspace& w = workspaces_[ws];

  while (tabs_->count() > 0) tabs_->removeTab(0);
  for (const QString& t : w.tabs) tabs_->addTab(t);

  buttons_.clear();
  while (QLayoutItem* item = toolRow_->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  for (const Group& g : w.groups) {
    auto* box = new QWidget(this);
    box->setObjectName("ToolGroup");
    box->setAttribute(Qt::WA_StyledBackground);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(6, 0, 6, 0);
    col->setSpacing(1);

    auto* buttons = new QGridLayout;
    buttons->setSpacing(2);
    buttons->setContentsMargins(0, 0, 0, 0);
    int visible = 0;
    for (const Tool& t : g.tools) visible += t.menuOnly ? 0 : 1;
    const int perRow = g.compact ? (visible + 1) / 2 : visible;
    int slot = 0;
    auto* menu = new QMenu(box);
    for (const Tool& t : g.tools) {
      const bool enabled = t.tip.isEmpty();
      const QString tip = enabled ? t.label : QString("%1\n%2").arg(t.label, t.tip);
      const QString id = t.id;
      if (t.menuOnly) {
        QAction* a = menu->addAction(icons::get(t.icon, theme::kSmallIconSize), t.label);
        a->setEnabled(enabled);
        connect(a, &QAction::triggered, this, [this, id] { emit toolTriggered(id); });
        continue;
      }

      auto* b = new AnimatedToolButton(box);
      b->setObjectName("ToolButton");
      b->setIcon(icons::get(t.icon, theme::kIconSize));
      b->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
      b->setFixedSize(44, 44);
      if (g.compact) {
        b->setIcon(icons::get(t.icon, 18));
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(26, 21);
      }
      if (t.primary) {
        b->setObjectName("PrimaryToolButton");
        b->setIcon(icons::tinted(t.icon, theme::current().accentText, theme::kIconSize));
        b->setText(t.label);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setFixedSize(QSize(96, 48));
      }
      b->setProperty("active", t.id == activeTool_);
      buttons_.insert(t.id, b);
      b->setToolTip(tip);
      b->setEnabled(enabled);
      connect(b, &QToolButton::clicked, this, [this, id] { emit toolTriggered(id); });
      buttons->addWidget(b, slot / std::max(perRow, 1), slot % std::max(perRow, 1));
      ++slot;

      QAction* a = menu->addAction(icons::get(t.icon, theme::kSmallIconSize), t.label);
      a->setEnabled(enabled);
      if (!enabled) a->setToolTip(t.tip);
      connect(a, &QAction::triggered, this, [this, id] { emit toolTriggered(id); });
    }
    col->addLayout(buttons);

    auto* label = new QToolButton(box);
    label->setObjectName("GroupButton");
    label->setText(g.label + QStringLiteral(" ▾"));
    label->setMenu(menu);
    label->setPopupMode(QToolButton::InstantPopup);
    col->addWidget(label, 0, Qt::AlignHCenter);
    toolRow_->addWidget(box);
  }
  toolRow_->addStretch(1);
}
