#include "SketchPalette.h"

#include "Icons.h"
#include "Settings.h"
#include "SketchTools.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

using K = model::ConstraintKind;

QToolButton* iconButton(QWidget* parent, const QString& icon, const QString& tip, bool checkable = false) {
  auto* b = new QToolButton(parent);
  b->setObjectName("PaletteTool");
  b->setIcon(icons::get(icon, 20));
  b->setIconSize(QSize(20, 20));
  b->setFixedSize(30, 28);
  b->setToolTip(tip);
  b->setCheckable(checkable);
  return b;
}

}  // namespace

SketchPalette::SketchPalette(SketchEditor* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
  setObjectName("SketchPalette");
  setMinimumWidth(220);
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(10, 4, 10, 10);
  col->setSpacing(2);

  // Status.
  QVBoxLayout* status = section(col, "status", &status_);
  auto* unconstrained = new QPushButton(tr("Show"), this);
  unconstrained->setObjectName("PaletteButton");
  unconstrained->setToolTip(tr("Select everything that can still move"));
  connect(unconstrained, &QPushButton::clicked, editor_, &SketchEditor::selectUnconstrained);
  row(status, tr("Unconstrained"), unconstrained, tr("Select everything that can still move"));

  // Feature Options: the running tool.
  QLabel* featureTitle = nullptr;
  QVBoxLayout* feature = section(col, "feature", &featureTitle);
  featureTitle->setText(tr("Feature Options"));
  featureSection_ = feature->parentWidget();
  featureHead_ = featureTitle->parentWidget();
  toolTitle_ = new QLabel(this);
  toolTitle_->setObjectName("PaletteToolTitle");
  toolHint_ = new QLabel(this);
  toolHint_->setObjectName("PaletteHint");
  toolHint_->setWordWrap(true);
  feature->addWidget(toolTitle_);
  feature->addWidget(toolHint_);

  // Options.
  QLabel* optionsTitle = nullptr;
  QVBoxLayout* options = section(col, "options", &optionsTitle);
  optionsTitle->setText(tr("Options"));

  auto* linetype = new QWidget(this);
  auto* lt = new QHBoxLayout(linetype);
  lt->setContentsMargins(0, 0, 0, 0);
  lt->setSpacing(2);
  normalLine_ = iconButton(linetype, "line", tr("Normal: new curves form profiles"), true);
  constructionLine_ = iconButton(linetype, "construction", tr("Construction (X): new curves are reference only"), true);
  normalLine_->setChecked(true);
  connect(normalLine_, &QToolButton::clicked, this, [this] {
    normalLine_->setChecked(true);
    constructionLine_->setChecked(false);
    editor_->setConstructionMode(false);
  });
  connect(constructionLine_, &QToolButton::clicked, this, [this] {
    normalLine_->setChecked(false);
    constructionLine_->setChecked(true);
    editor_->setConstructionMode(true);
  });
  lt->addWidget(normalLine_);
  lt->addWidget(constructionLine_);
  row(options, tr("Linetype"), linetype, tr("Whether new curves are normal or construction geometry"));

  auto* lookAt = iconButton(this, "lookat", tr("Turn the view to face the sketch"));
  connect(lookAt, &QToolButton::clicked, this, &SketchPalette::lookAtRequested);
  row(options, tr("Look At"), lookAt);

  auto check = [&](bool on) {
    auto* c = new QCheckBox(this);
    c->setChecked(on);
    return c;
  };
  QCheckBox* grid = check(prefs::flag(prefs::kShowGrid));
  connect(grid, &QCheckBox::toggled, this, &SketchPalette::gridToggled);
  row(options, tr("Sketch Grid"), grid);
  auto display = [&](const QString& label, SketchEditor::Display what, const QString& tip) {
    QCheckBox* c = check(editor_->display(what));
    connect(c, &QCheckBox::toggled, this, [this, what](bool on) { editor_->setDisplay(what, on); });
    row(options, label, c, tip);
  };
  display(tr("Snap"), SketchEditor::Display::Snap, tr("Snap to points, curves, midpoints and alignment"));
  QCheckBox* slice = check(false);
  slice->setEnabled(false);
  row(options, tr("Slice"), slice, tr("Not implemented yet: cut bodies away at the sketch plane"));
  display(tr("Profile"), SketchEditor::Display::Profiles, tr("Shade closed profiles"));
  display(tr("Points"), SketchEditor::Display::Points, tr("Show sketch points"));
  display(tr("Dimensions"), SketchEditor::Display::Dimensions, tr("Show dimensions"));
  display(tr("Constraints"), SketchEditor::Display::Constraints, tr("Show constraint tags"));
  QCheckBox* projected = check(true);
  projected->setEnabled(false);
  row(options, tr("Projected Geometries"), projected, tr("Projected edges are always shown for now"));
  QCheckBox* construction = check(true);
  construction->setEnabled(false);
  row(options, tr("Construction Geometries"), construction, tr("Construction geometry is always shown for now"));
  QCheckBox* sketch3d = check(false);
  sketch3d->setEnabled(false);
  row(options, tr("3D Sketch"), sketch3d, tr("Not implemented yet"));

  // Finish.
  col->addSpacing(8);
  auto* finish = new QPushButton(icons::get("check", 16), tr("Finish Sketch"), this);
  finish->setObjectName("PrimaryButton");
  connect(finish, &QPushButton::clicked, this, &SketchPalette::finishRequested);
  auto* finishRow = new QHBoxLayout;
  finishRow->addStretch(1);
  finishRow->addWidget(finish);
  col->addLayout(finishRow);

  connect(editor_, &SketchEditor::solveStateChanged, this, &SketchPalette::updateSolve);
  connect(editor_, &SketchEditor::modeChanged, this, &SketchPalette::updateMode);
  updateMode();
  updateSolve(0, false);
}

QVBoxLayout* SketchPalette::section(QVBoxLayout* col, const QString& key, QLabel** title) {
  const QString setting = QStringLiteral("sketchPalette/%1Collapsed").arg(key);
  auto* head = new QWidget(this);
  auto* hl = new QHBoxLayout(head);
  hl->setContentsMargins(0, 6, 0, 2);
  hl->setSpacing(6);
  auto* arrow = new QToolButton(head);
  arrow->setObjectName("PaletteArrow");
  arrow->setIconSize(QSize(12, 12));
  arrow->setAutoRaise(true);
  arrow->setCursor(Qt::PointingHandCursor);
  auto* label = new QLabel(head);
  label->setObjectName("PaletteSection");
  hl->addWidget(arrow);
  hl->addWidget(label, 1);
  if (title) *title = label;

  auto* body = new QWidget(this);
  auto* box = new QVBoxLayout(body);
  box->setContentsMargins(20, 0, 0, 4);
  box->setSpacing(0);
  auto apply = [arrow, body](bool collapsed) {
    body->setVisible(!collapsed);
    arrow->setIcon(icons::get(collapsed ? "chevron-right" : "chevron-down", 12));
  };
  apply(QSettings().value(setting, false).toBool());
  connect(arrow, &QToolButton::clicked, this, [this, body, apply, setting] {
    const bool collapsed = body->isVisible();
    apply(collapsed);
    QSettings().setValue(setting, collapsed);
    emit contentChanged();
  });
  col->addWidget(head);
  col->addWidget(body);
  return box;
}

void SketchPalette::row(QVBoxLayout* box, const QString& label, QWidget* control, const QString& tip) {
  auto* r = new QWidget(this);
  r->setObjectName("PaletteRow");
  r->setAttribute(Qt::WA_StyledBackground);
  auto* l = new QHBoxLayout(r);
  l->setContentsMargins(0, 3, 4, 3);
  auto* text = new QLabel(label, r);
  l->addWidget(text, 1);
  l->addWidget(control, 0, Qt::AlignRight);
  if (!tip.isEmpty()) r->setToolTip(tip), text->setToolTip(tip);
  box->addWidget(r);
}

void SketchPalette::updateSolve(int dof, bool conflicting) {
  QString state;
  if (conflicting) {
    status_->setText(tr("Status: Conflicting"));
    state = "conflict";
  } else if (dof == 0) {
    status_->setText(tr("Status: Fully Constrained"));
    state = "full";
  } else {
    status_->setText(tr("Status: %n degree(s) of freedom", nullptr, std::max(dof, 0)));
    state = "free";
  }
  status_->setProperty("state", state);
  status_->style()->unpolish(status_);
  status_->style()->polish(status_);
}

void SketchPalette::updateMode() {
  using M = SketchEditor::Mode;
  const M m = editor_->mode();
  QString title, hint;
  if (m == M::Constrain) {
    title = tr("Constraint");
    hint = tr("Pick the geometry, or select it first and then click the tool. Esc stops.");
  } else if (const SketchToolDef* t = sketchToolFor(m); t && m != M::Select) {
    title = tr(t->title);
    hint = tr(t->hint) + tr(" Esc ends the tool.");
  }
  // Like Fusion, Feature Options shows only while a tool runs.
  const bool show = !title.isEmpty();
  toolTitle_->setText(title);
  toolHint_->setText(hint);
  featureSection_->setVisible(show);
  featureHead_->setVisible(show);
  emit contentChanged();
}
