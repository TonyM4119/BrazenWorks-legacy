#include "SketchPalette.h"

#include "Icons.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

using K = model::ConstraintKind;

struct ToolInfo {
  const char* id;
  const char* icon;
  const char* title;
  const char* hint;
};

const ToolInfo kCreate[] = {
    {"sketch.line", "line", "Line", "Click points; type a length (or Tab) for the next segment. Esc stops."},
    {"sketch.rectangle", "rectangle", "Rectangle", "Click two corners, or type the width, Tab, the height, Enter."},
    {"sketch.circle", "circle", "Center Diameter Circle", "Click the centre, then type the diameter and press Enter."},
    {"sketch.arc", "arc", "3-Point Arc", "Click the start, the end, then a point on the arc."},
    {"sketch.point", "point", "Point", "Click to place points."},
    {"sketch.dimension", "dimension", "Sketch Dimension",
     "Pick a line, circle or arc, or two points/lines; type a value and press Enter."},
};

struct ConstraintInfo {
  const char* id;
  const char* icon;
  const char* title;
  K kind;
};

const ConstraintInfo kConstraints[] = {
    {"sketch.c.coincident", "coincident", "Coincident", K::Coincident},
    {"sketch.c.horizontal", "horizontal", "Horizontal", K::Horizontal},
    {"sketch.c.vertical", "vertical", "Vertical", K::Vertical},
    {"sketch.c.parallel", "parallel", "Parallel", K::Parallel},
    {"sketch.c.perpendicular", "perpendicular", "Perpendicular", K::Perpendicular},
    {"sketch.c.tangent", "tangent", "Tangent", K::Tangent},
    {"sketch.c.equal", "equal", "Equal", K::Equal},
    {"sketch.c.midpoint", "midpoint", "Midpoint", K::Midpoint},
    {"sketch.c.concentric", "concentric", "Concentric", K::Concentric},
    {"sketch.c.collinear", "collinear", "Collinear", K::Collinear},
    {"sketch.c.symmetric", "symmetric", "Symmetric", K::Symmetric},
    {"sketch.c.fix", "fix", "Fix", K::Fix},
};

QLabel* heading(const QString& text, QWidget* parent) {
  auto* l = new QLabel(text, parent);
  l->setObjectName("PaletteHeading");
  return l;
}

}  // namespace

SketchPalette::SketchPalette(SketchEditor* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
  setObjectName("SketchPalette");
  setFixedWidth(316);
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(12, 8, 12, 12);
  col->setSpacing(8);

  // Degrees of freedom.
  auto* dofRow = new QHBoxLayout;
  dof_ = new QLabel(this);
  dof_->setObjectName("PaletteDof");
  auto* unconstrained = new QPushButton(tr("Show Unconstrained"), this);
  unconstrained->setObjectName("PaletteButton");
  connect(unconstrained, &QPushButton::clicked, editor_, &SketchEditor::selectUnconstrained);
  dofRow->addWidget(dof_, 1);
  dofRow->addWidget(unconstrained);
  col->addLayout(dofRow);

  // Settings.
  col->addWidget(heading(tr("Settings"), this));
  construction_ = new QCheckBox(tr("Construction (X)"), this);
  construction_->setToolTip(tr("Draw new lines, circles and arcs as construction geometry"));
  connect(construction_, &QCheckBox::toggled, editor_, &SketchEditor::setConstructionMode);
  auto check = [&](const QString& text, SketchEditor::Display what) {
    auto* c = new QCheckBox(text, this);
    c->setChecked(editor_->display(what));
    connect(c, &QCheckBox::toggled, this, [this, what](bool on) { editor_->setDisplay(what, on); });
    return c;
  };
  // Two columns keep the palette short enough for small screens.
  auto* settings = new QGridLayout;
  settings->setHorizontalSpacing(10);
  settings->setVerticalSpacing(6);
  settings->addWidget(construction_, 0, 0);
  settings->addWidget(check(tr("Snap"), SketchEditor::Display::Snap), 0, 1);
  settings->addWidget(check(tr("Dimensions"), SketchEditor::Display::Dimensions), 1, 0);
  settings->addWidget(check(tr("Constraints"), SketchEditor::Display::Constraints), 1, 1);
  settings->addWidget(check(tr("Points"), SketchEditor::Display::Points), 2, 0);
  settings->addWidget(check(tr("Profiles"), SketchEditor::Display::Profiles), 2, 1);
  col->addLayout(settings);
  auto* lookAt = new QPushButton(tr("Look At"), this);
  lookAt->setObjectName("PaletteButton");
  lookAt->setToolTip(tr("Turn the view to face the sketch"));
  connect(lookAt, &QPushButton::clicked, this, &SketchPalette::lookAtRequested);
  auto* lookRow = new QHBoxLayout;
  lookRow->addStretch(1);
  lookRow->addWidget(lookAt);
  col->addLayout(lookRow);

  // Create.
  col->addWidget(heading(tr("Create"), this));
  auto* create = new QGridLayout;
  create->setSpacing(4);
  int i = 0;
  for (const ToolInfo& t : kCreate) {
    create->addWidget(toolButton(t.id, t.icon, tr(t.title)), i / 6, i % 6);
    ++i;
  }
  col->addLayout(create);

  // Constraints.
  col->addWidget(heading(tr("Constraints"), this));
  auto* cons = new QGridLayout;
  cons->setSpacing(4);
  i = 0;
  for (const ConstraintInfo& c : kConstraints) {
    cons->addWidget(toolButton(c.id, c.icon, tr(c.title)), i / 6, i % 6);
    ++i;
  }
  col->addLayout(cons);

  // The running tool.
  toolTitle_ = new QLabel(this);
  toolTitle_->setObjectName("PaletteToolTitle");
  toolHint_ = new QLabel(this);
  toolHint_->setObjectName("PaletteHint");
  toolHint_->setWordWrap(true);
  col->addSpacing(4);
  col->addWidget(toolTitle_);
  col->addWidget(toolHint_);

  // Finish / Cancel.
  auto* buttons = new QHBoxLayout;
  auto* finish = new QPushButton(tr("Finish Sketch"), this);
  finish->setObjectName("PrimaryButton");
  auto* cancel = new QPushButton(tr("Cancel Sketch"), this);
  cancel->setToolTip(tr("Discard every change made since the sketch was opened"));
  connect(finish, &QPushButton::clicked, this, &SketchPalette::finishRequested);
  connect(cancel, &QPushButton::clicked, this, &SketchPalette::cancelRequested);
  buttons->addWidget(finish, 1);
  buttons->addWidget(cancel, 1);
  col->addSpacing(4);
  col->addLayout(buttons);

  connect(editor_, &SketchEditor::solveStateChanged, this, &SketchPalette::updateSolve);
  connect(editor_, &SketchEditor::modeChanged, this, &SketchPalette::updateMode);
  updateMode();
}

QToolButton* SketchPalette::toolButton(const QString& id, const QString& icon, const QString& tip) {
  auto* b = new QToolButton(this);
  b->setObjectName("PaletteTool");
  b->setIcon(icons::get(icon, 24));
  b->setIconSize(QSize(24, 24));
  b->setFixedSize(38, 38);
  b->setToolTip(tip);
  connect(b, &QToolButton::clicked, this, [this, id] { emit toolRequested(id); });
  tools_.insert(id, b);
  return b;
}

void SketchPalette::updateSolve(int dof, bool conflicting) {
  if (conflicting) {
    dof_->setText(tr("Conflicting constraints"));
    dof_->setProperty("state", "conflict");
  } else if (dof == 0) {
    dof_->setText(tr("Fully constrained"));
    dof_->setProperty("state", "full");
  } else {
    dof_->setText(tr("%n degree(s) of freedom", nullptr, std::max(dof, 0)));
    dof_->setProperty("state", "free");
  }
  dof_->style()->unpolish(dof_);
  dof_->style()->polish(dof_);
}

void SketchPalette::updateMode() {
  using M = SketchEditor::Mode;
  QString active;
  QString title = tr("Select");
  QString hint = tr("Click to select, drag points or lines to move them, Delete removes. Double-click a "
                    "dimension to change it.");
  auto fromCreate = [&](const char* id) {
    for (const ToolInfo& t : kCreate)
      if (QString(t.id) == id) active = t.id, title = tr(t.title), hint = tr(t.hint);
  };
  switch (editor_->mode()) {
    case M::Select: break;
    case M::Line: fromCreate("sketch.line"); break;
    case M::Rectangle: fromCreate("sketch.rectangle"); break;
    case M::Circle: fromCreate("sketch.circle"); break;
    case M::Arc: fromCreate("sketch.arc"); break;
    case M::Point: fromCreate("sketch.point"); break;
    case M::Dimension: fromCreate("sketch.dimension"); break;
    case M::Constrain:
      for (const ConstraintInfo& c : kConstraints) {
        if (c.kind != editor_->constraintKind()) continue;
        active = c.id;
        title = tr(c.title);
        hint = tr("Pick the geometry, or select it first and then click the tool. Esc stops.");
      }
      break;
  }
  if (editor_->mode() != M::Select) hint += tr(" Esc ends the tool.");
  for (auto it = tools_.begin(); it != tools_.end(); ++it) {
    it.value()->setProperty("active", it.key() == active);
    it.value()->style()->unpolish(it.value());
    it.value()->style()->polish(it.value());
  }
  toolTitle_->setText(title);
  toolHint_->setText(hint);
}
