#include "FloatingPanel.h"

#include "Icons.h"
#include "Theme.h"

#include <algorithm>

#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// A strip or corner that resizes its panel by dragging.
class Grip : public QWidget {
 public:
  Grip(FloatingPanel* panel, Qt::Edges edges) : QWidget(panel), panel_(panel), edges_(edges) {
    setCursor(edges == (Qt::LeftEdge | Qt::BottomEdge) ? Qt::SizeBDiagCursor
              : edges == Qt::LeftEdge                  ? Qt::SizeHorCursor
                                                       : Qt::SizeVerCursor);
    setToolTip(edges == (Qt::LeftEdge | Qt::BottomEdge)
                   ? FloatingPanel::tr("Drag to resize; double-click to fit the contents again")
                   : QString());
  }

 protected:
  void mousePressEvent(QMouseEvent* e) override {
    if (e->button() != Qt::LeftButton) return;
    start_ = e->globalPosition().toPoint();
    geom_ = panel_->geometry();
  }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (!(e->buttons() & Qt::LeftButton)) return;
    const QPoint d = e->globalPosition().toPoint() - start_;
    QRect g = geom_;
    const int maxW = panel_->parentWidget() ? panel_->parentWidget()->width() - 16 : 2000;
    const int maxH = panel_->parentWidget() ? panel_->parentWidget()->height() - g.top() - 8 : 2000;
    if (edges_ & Qt::LeftEdge) g.setLeft(geom_.right() + 1 - std::clamp(geom_.width() - d.x(), 240, maxW));
    if (edges_ & Qt::BottomEdge) g.setHeight(std::clamp(geom_.height() + d.y(), 120, std::max(maxH, 120)));
    panel_->setGeometry(g);
    // Only what this handle changes: -1 leaves the other size as it was.
    emit panel_->resizedByUser(QSize(edges_ & Qt::LeftEdge ? g.width() : -1,
                                     edges_ & Qt::BottomEdge ? g.height() : -1));
  }
  void mouseDoubleClickEvent(QMouseEvent*) override { emit panel_->sizeResetByUser(); }
  void paintEvent(QPaintEvent*) override {
    if (edges_ != (Qt::LeftEdge | Qt::BottomEdge)) return;
    // Three dots along the corner, like a window's size grip.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::current().textMuted);
    for (auto [x, y] : {std::pair{3, 4}, std::pair{3, 9}, std::pair{8, 9}}) p.drawEllipse(QPointF(x, y), 1.3, 1.3);
  }

 private:
  FloatingPanel* panel_;
  Qt::Edges edges_;
  QPoint start_;
  QRect geom_;
};

}  // namespace

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

void FloatingPanel::setResizable(bool on) {
  if (on && !grips_[0]) {
    grips_[0] = new Grip(this, Qt::LeftEdge);
    grips_[1] = new Grip(this, Qt::BottomEdge);
    grips_[2] = new Grip(this, Qt::LeftEdge | Qt::BottomEdge);
  }
  for (QWidget* g : grips_)
    if (g) g->setVisible(on);
  QResizeEvent e(size(), size());
  resizeEvent(&e);
}

void FloatingPanel::resizeEvent(QResizeEvent* e) {
  QFrame::resizeEvent(e);
  if (!grips_[0]) return;
  const int w = width(), h = height();
  grips_[0]->setGeometry(0, 0, 5, h);
  grips_[1]->setGeometry(0, h - 5, w, 5);
  grips_[2]->setGeometry(0, h - 13, 13, 13);
  for (QWidget* g : grips_) g->raise();
}
