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

  // App menu behind the logo, like Fusion's File menu button.
  auto* logo = new QToolButton(this);
  logo->setIcon(icons::tinted("logo", theme::current().accent, 22));
  logo->setIconSize(QSize(22, 22));
  logo->setToolTip(tr("Menu"));
  appMenu_ = new QMenu(logo);
  logo->setMenu(appMenu_);
  logo->setPopupMode(QToolButton::InstantPopup);
  logo->setStyleSheet("QToolButton::menu-indicator { image: none; width: 0; }");
  row->addWidget(logo);

  auto* name = new QLabel(branding::kDisplayName, this);
  name->setObjectName("AppName");
  row->addWidget(name);
  row->addSpacing(6);

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

  auto* help = new QToolButton(this);
  help->setIcon(icons::get("help", 18));
  help->setIconSize(QSize(18, 18));
  help->setToolTip(tr("Help and About"));
  connect(help, &QToolButton::clicked, this, &DocumentBar::helpRequested);
  row->addWidget(help);

  auto* avatar = new QLabel(QStringLiteral("·"), this);
  avatar->setObjectName("Avatar");
  avatar->setAlignment(Qt::AlignCenter);
  avatar->setFixedSize(28, 28);
  avatar->setToolTip(tr("Account: not needed. Everything runs locally."));
  row->addWidget(avatar);
}

void DocumentBar::setDocumentName(const QString& name) { docName_->setText(name); }

void DocumentBar::setSaveState(const QString& text) { saveState_->setText(text); }
