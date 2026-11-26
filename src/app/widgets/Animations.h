#pragma once

#include <QPoint>
#include <QRect>
#include <QToolButton>
#include <QVariantAnimation>

#include <functional>

// Small, consistent motion for the UI. Durations are short on purpose: the
// app should feel quick, not floaty.
namespace anim {

inline constexpr int kFast = 120;    // hover, fade out
inline constexpr int kNormal = 180;  // panels in, collapse

// Fades and slides a floating panel into `endPos` from `offset` away.
void fadeSlideIn(QWidget* panel, QPoint endPos, QPoint offset = {16, 0});

// Fades a panel out, then calls `done` (typically deleteLater).
void fadeOut(QWidget* panel, const std::function<void()>& done);

// Animates a widget's geometry (e.g. a panel collapsing).
void resize(QWidget* widget, const QRect& to);

}  // namespace anim

// A tool button whose hover highlight fades in and out instead of snapping.
class AnimatedToolButton : public QToolButton {
  Q_OBJECT
 public:
  explicit AnimatedToolButton(QWidget* parent = nullptr);

 protected:
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  QVariantAnimation hover_;
  qreal level_ = 0.0;
};
