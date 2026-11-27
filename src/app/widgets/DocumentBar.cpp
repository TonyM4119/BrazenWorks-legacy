#include "DocumentBar.h"

#include "Branding.h"
#include "Icons.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

DocumentBar::DocumentBar(QWidget* parent) : QFrame(parent) {
  setObjectName("DocumentBar");
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(40);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(8, 0, 10, 0);
  row->setSpacing(8);

  auto* logo = new QLabel(this);
  logo->setPixmap(icons::tinted("logo", theme::current().accent, 22).pixmap(22, 22));
  row->addWidget(logo);

  auto* name = new QLabel(branding::kDisplayName, this);
  name->setObjectName("AppName");
  row->addWidget(name);
  row->addSpacing(4);

  // Quick access toolbar, laid out like Fusion's: data panel, apps | file, save |
  // undo, redo.
  auto quick = [this, row](const QString& icon, const QString& tip, bool enabled = true) {
    auto* b = new QToolButton(this);
    b->setObjectName("QuickButton");
    b->setIcon(icons::get(icon, 18));
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setEnabled(enabled);
    row->addWidget(b);
    return b;
  };
  auto separator = [this, row] {
    auto* line = new QFrame(this);
    line->setObjectName("QuickSeparator");
    line->setFixedSize(1, 20);
    row->addWidget(line);
  };
  separator();
  quick("qa-home", tr("Data panel: coming with projects (M2)"), false);
  quick("qa-apps", tr("Workspaces and apps: coming with projects (M2)"), false);
  separator();
  QToolButton* file = quick("qa-file", tr("File"));
  appMenu_ = new QMenu(file);
  file->setMenu(appMenu_);
  file->setPopupMode(QToolButton::InstantPopup);
  file->setToolButtonStyle(Qt::ToolButtonIconOnly);
  file->setFixedWidth(40);
  quick("qa-save", tr("Save: coming with the document format (M2)"), false);
  separator();
  connect(quick("qa-undo", tr("Undo (Ctrl+Z)")), &QToolButton::clicked, this, &DocumentBar::undoRequested);
  connect(quick("qa-redo", tr("Redo (Ctrl+Y)")), &QToolButton::clicked, this, &DocumentBar::redoRequested);
  separator();
  row->addSpacing(4);

  auto* projects = new QToolButton(this);
  projects->setObjectName("ProjectsButton");
  projects->setText(tr("Projects"));
  projects->setEnabled(false);
  projects->setToolTip(tr("Projects panel: coming with document saving (M2)"));
  row->addWidget(projects);

  // Active document tab (one document for now; tabs arrive with file support).
  auto* tab = new QFrame(this);
  tab->setObjectName("DocTab");
  tab->setAttribute(Qt::WA_StyledBackground);
  auto* tabRow = new QHBoxLayout(tab);
  tabRow->setContentsMargins(12, 4, 6, 4);
  tabRow->setSpacing(6);
  auto* docIcon = new QLabel(tab);
  docIcon->setPixmap(icons::get("document", 14).pixmap(14, 14));
  tabRow->addWidget(docIcon);
  docName_ = new QLabel(tr("Untitled"), tab);
  tabRow->addWidget(docName_);
  row->addWidget(tab, 0, Qt::AlignBottom);

  auto* newTab = new QToolButton(this);
  newTab->setIcon(icons::get("plus", 16));
  newTab->setToolTip(tr("New design: coming with document saving (M2)"));
  newTab->setEnabled(false);
  row->addWidget(newTab);

  row->addStretch(1);

  saveState_ = new QLabel(this);
  saveState_->setObjectName("SaveState");
  row->addWidget(saveState_);

  row->addSpacing(6);
  // Preferences, help menu and version, at the far right (MitCAD's layout).
  auto* prefsButton = quick("qa-settings", tr("Preferences"));
  connect(prefsButton, &QToolButton::clicked, this, &DocumentBar::preferencesRequested);
  QToolButton* help = quick("qa-help", tr("Help"));
  helpMenu_ = new QMenu(help);
  help->setMenu(helpMenu_);
  help->setPopupMode(QToolButton::InstantPopup);
  help->setStyleSheet("QToolButton::menu-indicator { image: none; width: 0; }");
  version_ = new QLabel(QStringLiteral("v%1").arg(branding::kVersion), this);
  version_->setObjectName("VersionLabel");
  version_->setToolTip(tr("%1 version").arg(branding::kDisplayName));
  row->addSpacing(4);
  row->addWidget(version_);
}

void DocumentBar::setDocumentName(const QString& name) { docName_->setText(name); }

void DocumentBar::setSaveState(const QString& text) { saveState_->setText(text); }

void DocumentBar::setVersionVisible(bool visible) { version_->setVisible(visible); }
