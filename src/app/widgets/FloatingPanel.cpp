#include "FloatingPanel.h"

#include "Icons.h"
#include "Theme.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

FloatingPanel::FloatingPanel(const QString& title, QWidget* parent) : QFrame(parent) {
  setObjectName("FloatingPanel");
  setAttribute(Qt::WA_StyledBackground);

  applyShadow();

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  header_ = new QWidget(this);
  header_->setObjectName("PanelHeader");
  header_->setAttribute(Qt::WA_StyledBackground);
  auto* h = new QHBoxLayout(header_);
  h->setContentsMargins(10, 6, 6, 6);
  h->setSpacing(8);

  dot_ = new QWidget(header_);
  dot_->setObjectName("AccentDot");
  dot_->setAttribute(Qt::WA_StyledBackground);
  dot_->setFixedSize(8, 8);
  dot_->hide();
  h->addWidget(dot_);

  title_ = new QLabel(title, header_);
  title_->setObjectName("PanelTitle");
  h->addWidget(title_, 1);

  collapse_ = new QToolButton(header_);
  collapse_->setIcon(icons::get("chevron-down", theme::kSmallIconSize));
  collapse_->setIconSize(QSize(theme::kSmallIconSize, theme::kSmallIconSize));
  collapse_->setToolTip(tr("Collapse"));
  collapse_->hide();
  connect(collapse_, &QToolButton::clicked, this, [this] { setCollapsed(!collapsed_); });
  h->addWidget(collapse_);
  outer->addWidget(header_);

  auto* bodyWidget = new QWidget(this);
  body_ = new QVBoxLayout(bodyWidget);
  body_->setContentsMargins(4, 4, 4, 6);
  body_->setSpacing(0);
  outer->addWidget(bodyWidget, 1);
}

void FloatingPanel::applyShadow() {
  auto* shadow = new QGraphicsDropShadowEffect(this);
  shadow->setBlurRadius(18);
  shadow->setOffset(0, 3);
  shadow->setColor(QColor(0, 0, 0, 110));
  setGraphicsEffect(shadow);
}

void FloatingPanel::setContent(QWidget* content) {
  if (content_) {
    body_->removeWidget(content_);
    content_->deleteLater();
  }
  content_ = content;
  if (content_) body_->addWidget(content_);
}

void FloatingPanel::setAccentDot(bool on) { dot_->setVisible(on); }

void FloatingPanel::setCollapsible(bool on) { collapse_->setVisible(on); }

void FloatingPanel::setCollapsed(bool collapsed) {
  if (collapsed_ == collapsed) return;
  collapsed_ = collapsed;
  body_->parentWidget()->setVisible(!collapsed);
  collapse_->setIcon(icons::get(collapsed ? "chevron-right" : "chevron-down", theme::kSmallIconSize));
  collapse_->setToolTip(collapsed ? tr("Expand") : tr("Collapse"));
  adjustSize();
  emit collapsedChanged(collapsed);
}
