#include "Animations.h"

#include "FloatingPanel.h"
#include "Theme.h"

#include <QEnterEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

namespace anim {

namespace {

// Opacity and the panel's drop shadow are both QGraphicsEffects and a widget
// can have only one, so the panel fades without its shadow and gets the shadow
// back when the fade ends.
QGraphicsOpacityEffect* takeOverEffect(QWidget* w) {
  auto* fade = new QGraphicsOpacityEffect(w);
  w->setGraphicsEffect(fade);  // replaces (and deletes) the shadow
  return fade;
}

void restoreShadow(QWidget* w) {
  if (auto* panel = qobject_cast<FloatingPanel*>(w)) {
    panel->applyShadow();
  } else {
    w->setGraphicsEffect(nullptr);
  }
}

}  // namespace

void fadeSlideIn(QWidget* panel, QPoint endPos, QPoint offset) {
  QGraphicsOpacityEffect* fade = takeOverEffect(panel);
  fade->setOpacity(0.0);
  panel->move(endPos + offset);

  auto* group = new QParallelAnimationGroup(panel);
  auto* opacity = new QPropertyAnimation(fade, "opacity", group);
  opacity->setDuration(kNormal);
  opacity->setStartValue(0.0);
  opacity->setEndValue(1.0);
  auto* slide = new QPropertyAnimation(panel, "pos", group);
  slide->setDuration(kNormal);
  slide->setStartValue(endPos + offset);
  slide->setEndValue(endPos);
  slide->setEasingCurve(QEasingCurve::OutCubic);
  QObject::connect(group, &QAbstractAnimation::finished, panel, [panel] { restoreShadow(panel); });
  group->start(QAbstractAnimation::DeleteWhenStopped);
}

void fadeOut(QWidget* panel, const std::function<void()>& done) {
  QGraphicsOpacityEffect* fade = takeOverEffect(panel);
  auto* opacity = new QPropertyAnimation(fade, "opacity", panel);
  opacity->setDuration(kFast);
  opacity->setStartValue(1.0);
  opacity->setEndValue(0.0);
  QObject::connect(opacity, &QAbstractAnimation::finished, panel, done);
  opacity->start(QAbstractAnimation::DeleteWhenStopped);
}

void stopResize(QWidget* widget) {
  if (auto* running = widget->findChild<QPropertyAnimation*>("geometryAnimation", Qt::FindDirectChildrenOnly)) {
    running->stop();
    running->deleteLater();
    running->setObjectName(QString());
  }
}

void resize(QWidget* widget, const QRect& to) {
  stopResize(widget);
  if (widget->geometry() == to) return;
  auto* a = new QPropertyAnimation(widget, "geometry", widget);
  a->setObjectName("geometryAnimation");
  a->setDuration(kNormal);
  a->setEndValue(to);
  a->setEasingCurve(QEasingCurve::OutCubic);
  a->start(QAbstractAnimation::DeleteWhenStopped);
}

}  // namespace anim

AnimatedToolButton::AnimatedToolButton(QWidget* parent) : QToolButton(parent) {
  hover_.setDuration(anim::kFast);
  hover_.setEasingCurve(QEasingCurve::OutQuad);
  connect(&hover_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
    level_ = v.toReal();
    update();
  });
}

void AnimatedToolButton::enterEvent(QEnterEvent* event) {
  if (isEnabled()) {
    hover_.stop();
    hover_.setStartValue(level_);
    hover_.setEndValue(1.0);
    hover_.start();
  }
  QToolButton::enterEvent(event);
}

void AnimatedToolButton::leaveEvent(QEvent* event) {
  hover_.stop();
  hover_.setStartValue(level_);
  hover_.setEndValue(0.0);
  hover_.start();
  QToolButton::leaveEvent(event);
}

void AnimatedToolButton::paintEvent(QPaintEvent* event) {
  if (level_ > 0.0) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = theme::current().hover;
    c.setAlphaF(level_);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
  }
  QToolButton::paintEvent(event);  // icon and border on top
}
