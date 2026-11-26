#include "TimelineBar.h"

#include "Icons.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

TimelineBar::TimelineBar(QWidget* parent) : QFrame(parent) {
  setObjectName("Timeline");
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(48);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(10, 4, 12, 4);
  row->setSpacing(4);

  auto step = [&](const char* icon, const QString& tip, auto handler) {
    auto* b = new QToolButton(this);
    b->setIcon(icons::get(icon, 14));
    b->setIconSize(QSize(14, 14));
    b->setToolTip(tip);
    connect(b, &QToolButton::clicked, this, handler);
    row->addWidget(b);
  };
  step("step-first", tr("Move to beginning"), [this] { emit markerRequested(0); });
  step("step-back", tr("Step back"), [this] { emit stepRequested(-1); });
  step("step-forward", tr("Step forward"), [this] { emit stepRequested(+1); });
  step("step-last", tr("Move to end"), [this] { emit markerRequested(count_); });
  row->addSpacing(8);

  auto* scroll = new QScrollArea(this);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
  auto* strip = new QWidget(scroll);
  chips_ = new QHBoxLayout(strip);
  chips_->setContentsMargins(0, 0, 0, 0);
  chips_->setSpacing(4);
  chips_->addStretch(1);
  scroll->setWidget(strip);
  row->addWidget(scroll, 1);

  status_ = new QLabel(this);
  status_->setObjectName("StatusText");
  row->addWidget(status_);
}

void TimelineBar::setFeatures(const QVector<Chip>& chips, int marker) {
  count_ = int(chips.size());
  while (chips_->count() > 1) {  // keep the trailing stretch
    QLayoutItem* item = chips_->takeAt(0);
    delete item->widget();
    delete item;
  }

  auto addMarker = [this](int at) {
    auto* m = new QFrame;
    m->setObjectName("TimelineMarker");
    m->setAttribute(Qt::WA_StyledBackground);
    m->setFixedSize(4, 32);
    m->setToolTip(tr("Rollback marker: features after it are not applied"));
    chips_->insertWidget(at, m);
  };

  int pos = 0;
  for (int i = 0; i < count_; ++i) {
    if (i == marker) addMarker(pos++);
    const Chip& c = chips[i];
    auto* b = new QToolButton;
    b->setObjectName("FeatureChip");
    b->setIcon(icons::get(c.icon, 18));
    b->setIconSize(QSize(18, 18));
    b->setFixedSize(32, 32);
    b->setToolTip(QString("%1: %2%3").arg(c.type, c.name, c.failed ? tr(" (could not compute)") : QString()));
    b->setProperty("sketch", c.type == "Sketch");
    b->setProperty("rolledBack", i >= marker);
    b->setProperty("failed", c.failed);
    connect(b, &QToolButton::clicked, this, [this, i] { emit markerRequested(i + 1); });
    chips_->insertWidget(pos++, b);
  }
  if (marker >= count_) addMarker(pos);
}

void TimelineBar::setPermanentStatus(const QString& text) {
  permanentStatus_ = text;
  status_->setText(text);
}

void TimelineBar::showStatus(const QString& text, int timeoutMs) {
  status_->setText(text);
  QTimer::singleShot(timeoutMs, this, [this, text] {
    if (status_->text() == text) status_->setText(permanentStatus_);
  });
}
