// The editor's cursor and the prompt beside it, as Fusion shows them: an
// arrow with a crosshair at its tip and a small picture of the running tool,
// and a short "what to click next" label.

#include "SketchEditor.h"

#include "Icons.h"
#include "SketchTools.h"
#include "Theme.h"

#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

using M = SketchEditor::Mode;

// arrow: the pointer shape; cross: crosshair lines through the hot spot.
QCursor makeCursor(const QString& icon, bool cross) {
  const qreal dpr = 2.0;  // sharp on high-DPI screens
  QPixmap pm(QSize(48, 48) * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const QPointF hot(12, 12);
  auto stroke = [&](const QPainterPath& path, qreal w) {
    p.setPen(QPen(QColor(255, 255, 255, 230), w + 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(QPen(QColor(20, 20, 20), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
  };
  if (cross) {
    QPainterPath c;
    c.moveTo(hot.x() - 11, hot.y());
    c.lineTo(hot.x() + 11, hot.y());
    c.moveTo(hot.x(), hot.y() - 11);
    c.lineTo(hot.x(), hot.y() + 11);
    p.setBrush(Qt::NoBrush);
    stroke(c, 1.0);
  }
  // The arrow, its tip on the hot spot.
  QPainterPath a;
  const QPointF t = hot;
  a.moveTo(t);
  a.lineTo(t + QPointF(0, 15));
  a.lineTo(t + QPointF(3.8, 11.5));
  a.lineTo(t + QPointF(6.6, 17.5));
  a.lineTo(t + QPointF(9.0, 16.4));
  a.lineTo(t + QPointF(6.3, 10.6));
  a.lineTo(t + QPointF(11, 10.6));
  a.closeSubpath();
  p.setPen(QPen(QColor(20, 20, 20), 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::white);
  p.drawPath(a);
  if (!icon.isEmpty()) {
    // The tool's picture on a light chip, so it reads on any background.
    const QRectF chip(26, 28, 18, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 215));
    p.drawRoundedRect(chip.adjusted(-1, -1, 1, 1), 3, 3);
    p.drawPixmap(chip.toRect(), icons::get(icon, 18).pixmap(QSize(18, 18), dpr));
  }
  p.end();
  return QCursor(pm, int(hot.x()), int(hot.y()));
}

}  // namespace

void SketchEditor::updateCursor() {
  if (!view_) return;
  if (mode_ == Mode::Select) {
    view_->setCursor(Qt::ArrowCursor);
  } else {
    QString icon;
    if (const SketchToolDef* t = sketchToolFor(mode_)) icon = t->icon;
    if (mode_ == Mode::Constrain) icon = QStringLiteral("coincident");
    // Drawing tools (and anchors such as a base point) aim with a crosshair.
    const bool anchor = (mode_ == Mode::Move || mode_ == Mode::Rotate || mode_ == Mode::Scale ||
                         mode_ == Mode::PatternCirc) && opStage_ >= 1;
    view_->setCursor(makeCursor(icon, isDrawing(mode_) || anchor));
  }
  movePrompt();
}

QString SketchEditor::stepPrompt() const {
  const std::size_t n = clicks_.size();
  auto pick = [n](std::initializer_list<const char*> steps) {
    std::size_t i = std::min(n, steps.size() - 1);
    return tr(*(steps.begin() + i));
  };
  switch (mode_) {
    case Mode::Select: return {};
    case Mode::Line: return pick({"Place first point", "Place next point"});
    case Mode::MidLine: return pick({"Place midpoint", "Place end point"});
    case Mode::Rectangle: return pick({"Place first corner", "Place opposite corner"});
    case Mode::Rectangle3: return pick({"Place first point", "Place second point", "Place third point"});
    case Mode::RectangleCentre: return pick({"Place center point", "Place corner"});
    case Mode::Circle: return pick({"Place center point", "Specify diameter"});
    case Mode::Circle2: return pick({"Place first point", "Place second point"});
    case Mode::Circle3: return pick({"Place first point", "Place second point", "Place third point"});
    case Mode::Arc: return pick({"Place start point", "Place end point", "Place point on arc"});
    case Mode::CentreArc: return pick({"Place center point", "Place start point", "Place end point"});
    case Mode::TangentArc: return pick({"Select end of a line or arc", "Place end point"});
    case Mode::Polygon: return pick({"Place center point", "Place corner"});
    case Mode::PolygonCirc: return pick({"Place center point", "Place middle of a side"});
    case Mode::PolygonEdge: return pick({"Place first point", "Place second point", "Pick the side"});
    case Mode::Ellipse: return pick({"Place center point", "Place major axis point", "Place minor axis point"});
    case Mode::Slot: return pick({"Place first center", "Place second center", "Specify width"});
    case Mode::SlotOverall: return pick({"Place first end", "Place second end", "Specify width"});
    case Mode::SlotCentre: return pick({"Place slot center", "Place arc center", "Specify width"});
    case Mode::Spline: return pick({"Specify first point", "Specify next point (Enter to finish)"});
    case Mode::Point: return tr("Place point");
    case Mode::Dimension:
      return placing_ ? tr("Place the dimension") : tr("Select sketch objects to dimension");
    case Mode::Constrain: return tr("Select geometry");
    case Mode::Trim: return tr("Select curve to trim");
    case Mode::Extend: return tr("Select curve to extend");
    case Mode::Break: return tr("Select curve to break");
    case Mode::Fillet:
    case Mode::Chamfer: return tr("Select corner");
    case Mode::Project: return tr("Select edges to project");
    case Mode::Offset: return opStage_ == 0 ? tr("Select curves to offset") : tr("Pick the side");
    case Mode::Mirror: return opStage_ == 0 ? tr("Select objects (Enter when done)") : tr("Select mirror line");
    case Mode::Move:
      return opStage_ == 0 ? tr("Select objects (Enter when done)")
             : opStage_ == 1 ? tr("Place base point")
                             : tr("Place new position");
    case Mode::Rotate: return opStage_ == 0 ? tr("Select objects (Enter when done)") : tr("Select center");
    case Mode::Scale: return opStage_ == 0 ? tr("Select objects (Enter when done)") : tr("Select point to scale about");
    case Mode::PatternRect: return tr("Select objects (Enter when done)");
    case Mode::PatternCirc: return opStage_ == 0 ? tr("Select objects (Enter when done)") : tr("Select center");
  }
  return {};
}

void SketchEditor::movePrompt() {
  if (!view_) return;
  const QString text = stepPrompt();
  const bool show = !text.isEmpty() && hasCursor_ && !valueEdit_ && !dragging_ && !boxing_;
  if (!show) {
    if (prompt_) prompt_->hide();
    return;
  }
  if (!prompt_) {
    prompt_ = new QLabel(view_);
    prompt_->setObjectName("CursorPrompt");
    prompt_->setAttribute(Qt::WA_TransparentForMouseEvents);
  }
  if (prompt_->text() != text) {
    prompt_->setText(text);
    prompt_->adjustSize();
  }
  QPoint at = cursor_.toPoint() + QPoint(34, 34);
  at.setX(std::min(at.x(), view_->width() - prompt_->width() - 4));
  at.setY(std::min(at.y(), view_->height() - prompt_->height() - 4));
  prompt_->move(at);
  prompt_->show();
  prompt_->raise();
}
