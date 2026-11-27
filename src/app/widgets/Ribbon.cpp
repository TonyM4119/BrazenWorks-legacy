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

  // The tools never set the window's width: groups fold into menus instead.
  toolHolder_ = new QWidget(this);
  toolHolder_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  toolRow_ = new QHBoxLayout(toolHolder_);
  toolRow_->setContentsMargins(0, 0, 0, 0);
  toolRow_->setSpacing(0);
  right->addWidget(toolHolder_);
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

namespace {

// Hover help, Fusion-style: name and shortcut, what it does, how to use it.
QString richTip(const Ribbon::Tool& t) {
  QString html = QStringLiteral("<b>%1</b>").arg(t.label.toHtmlEscaped());
  if (!t.shortcut.isEmpty())
    html += QStringLiteral("&nbsp;&nbsp;<span style='color:gray'>%1</span>").arg(t.shortcut.toHtmlEscaped());
  if (!t.help.isEmpty())
    html += QStringLiteral("<p style='margin-top:6px'>%1</p>")
                .arg(t.help.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
  if (!t.tip.isEmpty()) html += QStringLiteral("<p style='margin-top:6px; color:gray'>%1</p>").arg(t.tip.toHtmlEscaped());
  return QStringLiteral("<div style='max-width:320px'>%1</div>").arg(html);
}

}  // namespace

void Ribbon::rebuildTools() {
  const int ws = workspaceBox_->currentIndex();
  if (ws < 0 || ws >= workspaces_.size()) return;
  const Workspace& w = workspaces_[ws];

  while (tabs_->count() > 0) tabs_->removeTab(0);
  for (const QString& t : w.tabs) tabs_->addTab(t);
  if (w.activeTab >= 0 && w.activeTab < tabs_->count()) tabs_->setCurrentIndex(w.activeTab);

  buttons_.clear();
  built_.clear();
  while (QLayoutItem* item = toolRow_->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  for (const Group& g : w.groups) {
    auto* box = new QWidget(toolHolder_);
    box->setObjectName("ToolGroup");
    box->setAttribute(Qt::WA_StyledBackground);
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(6, 0, 6, 0);
    col->setSpacing(1);

    auto* holder = new QWidget(box);
    auto* buttons = new QGridLayout(holder);
    buttons->setSpacing(2);
    buttons->setContentsMargins(0, 0, 0, 0);
    int visible = 0;
    for (const Tool& t : g.tools) visible += t.menuOnly ? 0 : 1;
    const int perRow = g.compact ? (visible + 1) / 2 : visible;
    int slot = 0;
    auto* menu = new QMenu(box);
    menu->setToolTipsVisible(true);
    QHash<QString, QMenu*> submenus;
    QString firstIcon;
    for (const Tool& t : g.tools) {
      const bool enabled = t.tip.isEmpty();
      const QString id = t.id;
      if (firstIcon.isEmpty()) firstIcon = t.icon;

      // The menu entry (in a submenu when it has one).
      QMenu* into = menu;
      if (!t.submenu.isEmpty()) {
        if (!submenus.contains(t.submenu)) {
          if (t.separator) menu->addSeparator();
          QMenu* sub = menu->addMenu(t.submenu);
          sub->setToolTipsVisible(true);
          submenus.insert(t.submenu, sub);
        }
        into = submenus.value(t.submenu);
      } else if (t.separator) {
        menu->addSeparator();
      }
      QString text = t.label;
      if (!t.shortcut.isEmpty()) text += QLatin1Char('\t') + t.shortcut;
      QAction* a = into->addAction(icons::get(t.icon, theme::kSmallIconSize), text);
      a->setEnabled(enabled);
      a->setToolTip(richTip(t));
      connect(a, &QAction::triggered, this, [this, id] { emit toolTriggered(id); });
      if (t.menuOnly) continue;

      auto* b = new AnimatedToolButton(holder);
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
      b->setToolTip(richTip(t));
      b->setEnabled(enabled);
      connect(b, &QToolButton::clicked, this, [this, id] { emit toolTriggered(id); });
      buttons->addWidget(b, slot / std::max(perRow, 1), slot % std::max(perRow, 1));
      ++slot;
    }
    col->addWidget(holder, 0, Qt::AlignHCenter);

    // Folded: one icon that opens the group's menu.
    auto* folded = new AnimatedToolButton(box);
    folded->setObjectName("ToolButton");
    folded->setIcon(icons::get(firstIcon, theme::kIconSize));
    folded->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    folded->setFixedSize(44, 44);
    folded->setMenu(menu);
    folded->setPopupMode(QToolButton::InstantPopup);
    folded->setToolTip(g.label);
    folded->hide();
    col->addWidget(folded, 0, Qt::AlignHCenter);

    auto* label = new QToolButton(box);
    label->setObjectName("GroupButton");
    label->setText(g.label + QStringLiteral(" ▾"));
    label->setMenu(menu);
    label->setPopupMode(QToolButton::InstantPopup);
    col->addWidget(label, 0, Qt::AlignHCenter);
    toolRow_->addWidget(box);
    built_.push_back({holder, folded, g.collapse});
  }
  toolRow_->addStretch(1);
  updateCollapse();
}

void Ribbon::resizeEvent(QResizeEvent* e) {
  QFrame::resizeEvent(e);
  updateCollapse();
}

// Folds groups (highest `collapse` first) until the tools fit.
void Ribbon::updateCollapse() {
  if (built_.isEmpty() || !toolHolder_) return;
  for (Built& b : built_) b.buttons->show(), b.folded->hide();
  const int available = toolHolder_->width();
  auto needed = [this] {
    toolRow_->invalidate();
    int w = 0;
    for (int i = 0; i < toolRow_->count(); ++i)
      if (QWidget* wd = toolRow_->itemAt(i)->widget()) w += wd->sizeHint().width();
    return w;
  };
  while (needed() > available) {
    Built* worst = nullptr;
    for (Built& b : built_)
      if (b.collapse > 0 && b.buttons->isVisible() && (!worst || b.collapse > worst->collapse)) worst = &b;
    if (!worst) break;
    worst->buttons->hide();
    worst->folded->show();
  }
}
