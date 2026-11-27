#include "SketchPalette.h"

#include "Icons.h"
#include "SketchTools.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace {

using K = model::ConstraintKind;

// Palette shortcuts; titles and hints come from the shared tool table.
const char* const kCreate[] = {"sketch.line", "sketch.rectangle", "sketch.circle", "sketch.arc",
                               "sketch.polygon", "sketch.spline", "sketch.slot", "sketch.ellipse",
                               "sketch.point", "sketch.project", "sketch.mirror", "sketch.dimension"};
const char* const kModify[] = {"sketch.fillet", "sketch.trim", "sketch.extend", "sketch.offset",
                               "sketch.move", "sketch.patternr"};

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
    {"sketch.c.smooth", "smooth", "Smooth (G2)", K::Smooth},
};

// Tool buttons that wrap to the palette's width.
class ToolGrid : public QWidget {
 public:
  explicit ToolGrid(QWidget* parent) : QWidget(parent) {
    QSizePolicy p(QSizePolicy::Preferred, QSizePolicy::Preferred);
    p.setHeightForWidth(true);
    setSizePolicy(p);
  }
  void add(QToolButton* b) {
    b->setParent(this);
    buttons_.push_back(b);
    relayout();
    updateGeometry();
  }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override {
    const int c = columns(w), n = int(buttons_.size());
    return n ? ((n + c - 1) / c) * kCell - kGap : 0;
  }
  QSize sizeHint() const override { return {6 * kCell - kGap, heightForWidth(6 * kCell - kGap)}; }
  QSize minimumSizeHint() const override { return {kCell - kGap, kCell - kGap}; }

 protected:
  void resizeEvent(QResizeEvent*) override { relayout(); }

 private:
  static constexpr int kCell = 42, kGap = 4;  // 38 px buttons
  static int columns(int w) { return std::max(1, (w + kGap) / kCell); }
  void relayout() {
    const int c = columns(width());
    for (int i = 0; i < int(buttons_.size()); ++i) buttons_[i]->move((i % c) * kCell, (i / c) * kCell);
  }
  std::vector<QToolButton*> buttons_;
};

}  // namespace

SketchPalette::SketchPalette(SketchEditor* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
  setObjectName("SketchPalette");
  setMinimumWidth(220);
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
  QVBoxLayout* settingsBox = section(col, "settings", tr("Settings"));
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
  settingsBox->addLayout(settings);
  auto* lookAt = new QPushButton(tr("Look At"), this);
  lookAt->setObjectName("PaletteButton");
  lookAt->setToolTip(tr("Turn the view to face the sketch"));
  connect(lookAt, &QPushButton::clicked, this, &SketchPalette::lookAtRequested);
  auto* lookRow = new QHBoxLayout;
  lookRow->addStretch(1);
  lookRow->addWidget(lookAt);
  settingsBox->addLayout(lookRow);

  // Create.
  auto* create = new ToolGrid(this);
  for (const char* id : kCreate) {
    const SketchToolDef* t = sketchToolById(id);
    create->add(toolButton(t->id, t->icon, tr(t->title)));
  }
  section(col, "create", tr("Create"))->addWidget(create);

  // Modify.
  auto* modify = new ToolGrid(this);
  for (const char* id : kModify) {
    const SketchToolDef* t = sketchToolById(id);
    modify->add(toolButton(t->id, t->icon, tr(t->title)));
  }
  section(col, "modify", tr("Modify"))->addWidget(modify);

  // Constraints.
  auto* cons = new ToolGrid(this);
  for (const ConstraintInfo& c : kConstraints) cons->add(toolButton(c.id, c.icon, tr(c.title)));
  QVBoxLayout* consBox = section(col, "constraints", tr("Constraints"));
  consBox->addWidget(cons);
  auto* autoBtn = new QPushButton(icons::get("autoconstrain", 16), tr("Auto Constrain"), this);
  autoBtn->setObjectName("PaletteButton");
  autoBtn->setToolTip(tr("Add the constraints the drawing already shows: joined ends, horizontal and vertical "
                         "lines, tangent arcs"));
  connect(autoBtn, &QPushButton::clicked, this, [this] { emit toolRequested("sketch.autoconstrain"); });
  consBox->addWidget(autoBtn);

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

QVBoxLayout* SketchPalette::section(QVBoxLayout* col, const QString& key, const QString& title) {
  const QString setting = QStringLiteral("sketchPalette/%1Collapsed").arg(key);
  auto* head = new QToolButton(this);
  head->setObjectName("PaletteHeading");
  head->setText(title);
  head->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  head->setIconSize(QSize(12, 12));
  head->setCursor(Qt::PointingHandCursor);
  head->setToolTip(tr("Click to show or hide"));
  auto* body = new QWidget(this);
  auto* box = new QVBoxLayout(body);
  box->setContentsMargins(0, 0, 0, 0);
  box->setSpacing(6);
  auto apply = [head, body](bool collapsed) {
    body->setVisible(!collapsed);
    head->setIcon(icons::get(collapsed ? "chevron-right" : "chevron-down", 12));
  };
  apply(QSettings().value(setting, false).toBool());
  connect(head, &QToolButton::clicked, this, [this, body, apply, setting] {
    const bool collapsed = body->isVisible();
    apply(collapsed);
    QSettings().setValue(setting, collapsed);
    emit contentChanged();
  });
  col->addWidget(head, 0, Qt::AlignLeft);
  col->addWidget(body);
  return box;
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
  if (editor_->mode() == M::Constrain) {
    for (const ConstraintInfo& c : kConstraints) {
      if (c.kind != editor_->constraintKind()) continue;
      active = c.id;
      title = tr(c.title);
      hint = tr("Pick the geometry, or select it first and then click the tool. Esc stops.");
    }
  } else if (const SketchToolDef* t = sketchToolFor(editor_->mode()); t && editor_->mode() != M::Select) {
    active = t->id;
    title = tr(t->title);
    hint = tr(t->hint);
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
