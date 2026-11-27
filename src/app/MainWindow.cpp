#include "MainWindow.h"

#include "Branding.h"
#include "Icons.h"
#include "Kernel.h"
#include "Theme.h"
#include "Settings.h"
#include "Viewport.h"
#include "widgets/PreferencesDialog.h"
#include "sketch/Pickers.h"
#include "sketch/SketchPalette.h"
#include "widgets/Animations.h"
#include "widgets/CommandDialog.h"
#include "widgets/DocumentBar.h"
#include "widgets/FloatingPanel.h"
#include "widgets/LengthField.h"
#include "widgets/NavBar.h"
#include "widgets/Ribbon.h"
#include "widgets/TimelineBar.h"

#include <Standard_Version.hxx>

#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QPushButton>
#include <QScrollArea>
#include <QSysInfo>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QComboBox>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>

namespace {

// Tools are generic CAD/CAM terms. A non-empty `tip` disables the tool and
// says when it arrives (see docs/ROADMAP.md).
const QString kLaterTip = QStringLiteral("Not implemented yet. See docs/ROADMAP.md");
const QString kCamTip = QStringLiteral("Coming in M5: Manufacture workspace");

QVector<Ribbon::Workspace> workspaces() {
  using G = Ribbon::Group;
  using T = Ribbon::Tool;
  Ribbon::Workspace design{
      "Design",
      {"SOLID", "SURFACE", "MESH", "SHEET METAL", "PLASTIC", "UTILITIES", "MANAGE"},
      {
          G{"CREATE",
            {T{"design.sketch", "Create Sketch", "sketch", {}},
             T{"design.box", "Box", "box", {}},
             T{"design.extrude", "Extrude", "extrude", {}},
             T{"design.revolve", "Revolve", "revolve", QStringLiteral("Coming in M3")},
             T{"design.sweep", "Sweep", "sweep", kLaterTip},
             T{"design.loft", "Loft", "loft", kLaterTip},
             T{"design.hole", "Hole", "hole", kLaterTip}}},
          G{"MODIFY",
            {T{"design.presspull", "Press Pull", "press-pull", kLaterTip},
             T{"design.fillet", "Fillet", "fillet", {}},
             T{"design.chamfer", "Chamfer", "chamfer", kLaterTip},
             T{"design.shell", "Shell", "shell", kLaterTip},
             T{"design.combine", "Combine", "combine", kLaterTip}}},
          G{"ASSEMBLE", {T{"design.joint", "Joint", "joint", QStringLiteral("Coming in M4: assemblies")}}},
          G{"CONSTRUCT",
            {T{"design.plane", "Offset Plane", "plane", kLaterTip}, T{"design.axis", "Axis", "axis", kLaterTip}}},
          G{"INSPECT", {T{"design.measure", "Measure", "measure", QStringLiteral("Coming in M3")}}},
          G{"INSERT",
            {T{"design.insert", "Insert", "insert", QStringLiteral("Coming in M3: STEP/STL import")}}},
          G{"SELECT", {T{"design.select", "Select", "select", {}}}},
      }};
  Ribbon::Workspace manufacture{
      "Manufacture",
      {"MILLING", "TURNING", "ADDITIVE", "PROBING", "FABRICATION", "UTILITIES"},
      {
          G{"SETUP", {T{"cam.setup", "New Setup", "setup", kCamTip}}},
          G{"2D",
            {T{"cam.face", "Face", "face", kCamTip}, T{"cam.adaptive2d", "2D Adaptive", "adaptive", kCamTip},
             T{"cam.contour", "2D Contour", "contour", kCamTip}, T{"cam.pocket", "2D Pocket", "pocket", kCamTip},
             T{"cam.slot", "Slot", "slot", kCamTip}, T{"cam.engrave", "Engrave", "engrave", kCamTip}}},
          G{"3D",
            {T{"cam.adaptive", "Adaptive Clearing", "adaptive", kCamTip},
             T{"cam.parallel", "Parallel", "cam-parallel", kCamTip}, T{"cam.scallop", "Scallop", "scallop", kCamTip}}},
          G{"DRILLING", {T{"cam.drill", "Drill", "drill", kCamTip}}},
          G{"ACTIONS",
            {T{"cam.simulate", "Simulate", "simulate", kCamTip}, T{"cam.post", "Post Process", "post", kCamTip}}},
          G{"MANAGE", {T{"cam.library", "Tool Library", "library", kCamTip}}},
      }};
  return {design, manufacture};
}

// The contextual SKETCH tab shown while a sketch is open.
QVector<Ribbon::Workspace> sketchWorkspace() {
  using G = Ribbon::Group;
  using T = Ribbon::Tool;
  Ribbon::Workspace w{
      "Design",
      {"SKETCH"},
      {
          G{"CREATE",
            {T{"sketch.line", "Line (L)", "line", {}}, T{"sketch.rectangle", "Rectangle (R)", "rectangle", {}},
             T{"sketch.circle", "Circle (C)", "circle", {}}, T{"sketch.arc", "3-Point Arc (A)", "arc", {}},
             T{"sketch.point", "Point (P)", "point", {}}}},
          G{"MODIFY",
            {T{"sketch.construction", "Construction (X)", "construction", {}},
             T{"sketch.delete", "Delete (Del)", "close", {}}}},
          G{"CONSTRAINTS",
            {T{"sketch.c.coincident", "Coincident", "coincident", {}},
             T{"sketch.c.horizontal", "Horizontal", "horizontal", {}},
             T{"sketch.c.vertical", "Vertical", "vertical", {}},
             T{"sketch.c.parallel", "Parallel", "parallel", {}},
             T{"sketch.c.perpendicular", "Perpendicular", "perpendicular", {}},
             T{"sketch.c.tangent", "Tangent", "tangent", {}}, T{"sketch.c.equal", "Equal", "equal", {}},
             T{"sketch.c.midpoint", "Midpoint", "midpoint", {}},
             T{"sketch.c.concentric", "Concentric", "concentric", {}},
             T{"sketch.c.collinear", "Collinear", "collinear", {}},
             T{"sketch.c.symmetric", "Symmetric", "symmetric", {}}, T{"sketch.c.fix", "Fix", "fix", {}}}},
          G{"INSPECT", {T{"sketch.dimension", "Sketch Dimension (D)", "dimension", {}}}},
          G{"SELECT", {T{"sketch.select", "Select (S)", "select", {}}}},
          G{"FINISH SKETCH", {T{"sketch.finish", "Finish Sketch", "check", {}, /*primary=*/true}}},
      }};
  return {w};
}

struct ConstraintTool {
  const char* id;
  model::ConstraintKind kind;
};
const ConstraintTool kConstraintTools[] = {
    {"sketch.c.coincident", model::ConstraintKind::Coincident},
    {"sketch.c.horizontal", model::ConstraintKind::Horizontal},
    {"sketch.c.vertical", model::ConstraintKind::Vertical},
    {"sketch.c.parallel", model::ConstraintKind::Parallel},
    {"sketch.c.perpendicular", model::ConstraintKind::Perpendicular},
    {"sketch.c.tangent", model::ConstraintKind::Tangent},
    {"sketch.c.equal", model::ConstraintKind::Equal},
    {"sketch.c.midpoint", model::ConstraintKind::Midpoint},
    {"sketch.c.concentric", model::ConstraintKind::Concentric},
    {"sketch.c.collinear", model::ConstraintKind::Collinear},
    {"sketch.c.symmetric", model::ConstraintKind::Symmetric},
    {"sketch.c.fix", model::ConstraintKind::Fix},
};

QString toQString(const rust::String& s) { return QString::fromUtf8(s.data(), int(s.size())); }

model::Profile rectProfile(double x, double y, double w, double h) {
  return model::Profile{model::ProfileKind::Rect, x, y, w, h, 0.0};
}

model::Profile circleProfile(double cx, double cy, double r) {
  return model::Profile{model::ProfileKind::Circle, cx, cy, 0.0, 0.0, r};
}

QString iconForType(const QString& type) {
  if (type == "Fillet") return "fillet";
  if (type == "Sketch") return "sketch";
  return "extrude";
}

enum BrowserRole { kVisibilityRole = Qt::UserRole + 1, kUnitsRole, kFeatureRole };

constexpr double kInch = 25.4;  // mm

// The unit new documents start in (Preferences → remembered from the last change).
model::LengthUnitKind defaultUnit() {
  const QString saved = QSettings().value("units/default", "in").toString();
  for (auto u : {model::LengthUnitKind::Inch, model::LengthUnitKind::Foot, model::LengthUnitKind::Millimeter,
                 model::LengthUnitKind::Centimeter, model::LengthUnitKind::Meter}) {
    const rust::Str sym = model::length_unit_symbol(u);
    if (saved == QString::fromUtf8(sym.data(), int(sym.size()))) return u;
  }
  return model::LengthUnitKind::Inch;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  auto* central = new QWidget(this);
  central->setObjectName("Central");
  auto* column = new QVBoxLayout(central);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);

  documentBar_ = new DocumentBar(central);
  connect(documentBar_, &DocumentBar::preferencesRequested, this, &MainWindow::openPreferences);
  connect(documentBar_, &DocumentBar::undoRequested, this, [this] { undo(); });
  connect(documentBar_, &DocumentBar::redoRequested, this, [this] { redo(); });
  column->addWidget(documentBar_);

  ribbon_ = new Ribbon(central);
  ribbon_->setWorkspaces(workspaces());
  connect(ribbon_, &Ribbon::toolTriggered, this, &MainWindow::onTool);
  connect(ribbon_, &Ribbon::workspaceChanged, this, [this](int) { closeDialog(); });
  column->addWidget(ribbon_);

  viewport_ = new Viewport(central);
  viewport_->installEventFilter(this);
  column->addWidget(viewport_, 1);

  timelineBar_ = new TimelineBar(central);
  permanentStatus_ = QString("Open CASCADE %1").arg(OCC_VERSION_COMPLETE);
  timelineBar_->setPermanentStatus(permanentStatus_);
  auto* unitsMenu = new QMenu(timelineBar_->unitsButton());
  fillUnitsMenu(unitsMenu);
  timelineBar_->unitsButton()->setMenu(unitsMenu);
  connect(timelineBar_, &TimelineBar::markerRequested, this, [this](int m) { setMarker(m); });
  connect(timelineBar_, &TimelineBar::featureDoubleClicked, this, [this](int index) {
    if (std::size_t(index) >= model::timeline_len(*timeline_)) return;
    if (toQString(model::timeline_feature_type(*timeline_, std::size_t(index))) == "Sketch")
      enterSketch(model::timeline_feature_id(*timeline_, std::size_t(index)));
  });
  connect(timelineBar_, &TimelineBar::stepRequested, this,
          [this](int d) { setMarker(long(model::timeline_marker(*timeline_)) + d); });
  column->addWidget(timelineBar_);
  setCentralWidget(central);

  // Overlays float over the 3D view (children of the viewport).
  buildBrowser();
  navBar_ = new NavBar(viewport_);
  connect(navBar_, &NavBar::homeRequested, viewport_, &Viewport::homeView);
  connect(navBar_, &NavBar::fitRequested, this, [this] { viewport_->fitAll(/*animate=*/true); });
  connect(navBar_, &NavBar::gridToggled, viewport_, &Viewport::setGridVisible);
  connect(navBar_, &NavBar::dragModeChanged, this,
          [this](NavBar::DragMode m) { viewport_->setDragMode(Viewport::DragMode(int(m))); });
  connect(navBar_, &NavBar::displayModeChanged, this,
          [this](NavBar::DisplayMode m) { viewport_->setDisplayMode(Viewport::DisplayMode(int(m))); });

  connect(browser_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    const QVariant id = item->data(0, kFeatureRole);
    if (id.isValid()) enterSketch(id.toULongLong());
  });

  auto* undoAction = new QAction(tr("Undo"), this);
  undoAction->setShortcuts(QKeySequence::Undo);
  connect(undoAction, &QAction::triggered, this, &MainWindow::undo);
  addAction(undoAction);
  auto* redoAction = new QAction(tr("Redo"), this);
  redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
  connect(redoAction, &QAction::triggered, this, &MainWindow::redo);
  addAction(redoAction);

  buildAppMenu();
  buildHelpMenu();
  loadDemoDocument();
  applyPreferences();
  resize(1440, 900);
}

MainWindow::~MainWindow() {
  viewport_->setTool(nullptr);
  sketchEditor_.reset();
  planePicker_.reset();
  profilePicker_.reset();
}

void MainWindow::buildAppMenu() {
  QMenu* menu = documentBar_->appMenu();
  QAction* save = menu->addAction(icons::get("save", 16), tr("Save"));
  save->setEnabled(false);
  save->setToolTip(tr("Coming in M2: native document format"));
  menu->addAction(icons::get("export", 16), tr("Export STEP…"), this, &MainWindow::exportStep);
  menu->addSeparator();
  menu->addAction(icons::get("help", 16), tr("About %1").arg(branding::kDisplayName), this, &MainWindow::showAbout);
  menu->addSeparator();
  QAction* quit = menu->addAction(tr("Quit"), this, &QWidget::close);
  quit->setShortcut(QKeySequence::Quit);
  addAction(quit);

  auto* fit = new QAction(tr("Fit All"), this);
  fit->setShortcut(QKeySequence(Qt::Key_F6));
  connect(fit, &QAction::triggered, this, [this] { viewport_->fitAll(/*animate=*/true); });
  addAction(fit);
}

void MainWindow::buildBrowser() {
  browserPanel_ = new FloatingPanel(tr("BROWSER"), viewport_);
  browserPanel_->setCollapsible(true);
  browserPanel_->setFixedWidth(260);

  browser_ = new QTreeWidget(browserPanel_);
  browser_->setObjectName("Browser");
  browser_->setColumnCount(1);
  browser_->setHeaderHidden(true);
  browser_->setIndentation(14);
  browser_->setRootIsDecorated(true);
  // Each row's icon is [eye][type icon], like Fusion's browser.
  browser_->setIconSize(QSize(2 * theme::kSmallIconSize + 6, theme::kSmallIconSize));
  browser_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  browser_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  browser_->viewport()->installEventFilter(this);
  connect(browser_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
    if (item->data(0, kUnitsRole).toBool()) {
      QMenu menu;
      fillUnitsMenu(&menu);
      menu.exec(QCursor::pos());
    }
  });
  browserPanel_->setContent(browser_);
  connect(browserPanel_, &FloatingPanel::collapsedChanged, this, [this] { layoutOverlays(true); });
  connect(browser_, &QTreeWidget::itemExpanded, this, [this] { layoutOverlays(true); });
  connect(browser_, &QTreeWidget::itemCollapsed, this, [this] { layoutOverlays(true); });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (browser_ && watched == browser_->viewport() && event->type() == QEvent::MouseButtonPress) {
    const QPoint pos = static_cast<QMouseEvent*>(event)->position().toPoint();
    if (QTreeWidgetItem* item = browser_->itemAt(pos)) {
      const QRect r = browser_->visualItemRect(item);
      // The eye is the first ~20 px of the row's icon.
      if (item->data(0, kVisibilityRole).isValid() && pos.x() >= r.left() && pos.x() < r.left() + 22) {
        toggleBrowserVisibility(item);
        return true;
      }
    }
  }
  if (watched == viewport_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
    layoutOverlays();
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::layoutOverlays(bool animate) {
  const int w = viewport_->width();
  const int h = viewport_->height();
  constexpr int kMargin = 12;

  // Browser: top-left, as tall as its content but never past the nav bar.
  if (browserPanel_) {
    int height = browserPanel_->sizeHint().height();
    if (!browserPanel_->isCollapsed()) {
      // Header + one row per visible item (children of collapsed items don't count).
      int rows = 0;
      for (QTreeWidgetItemIterator it(browser_); *it; ++it) {
        bool shown = true;
        for (QTreeWidgetItem* p = (*it)->parent(); p; p = p->parent()) shown = shown && p->isExpanded();
        if (shown) ++rows;
      }
      height = 38 + std::max(rows, 1) * 24 + 14;
    }
    height = std::min(height, h - 2 * kMargin - 60);
    const QRect target(kMargin, kMargin, browserPanel_->width(), std::max(height, 40));
    if (animate) {
      anim::resize(browserPanel_, target);
    } else {
      anim::stopResize(browserPanel_);
      browserPanel_->setGeometry(target);
    }
    browserPanel_->raise();
  }
  // Command dialog: top-right, under the view cube.
  if (dialog_) {
    dialog_->adjustSize();
    if (!dialog_->property("animating").toBool()) dialog_->move(dialogPosition());
    dialog_->raise();
  }
  // Sketch Palette: right side, under the view cube.
  if (sketchPanel_) {
    constexpr int kTop = 196;
    const int wanted = sketchPanel_->isCollapsed() ? sketchPanel_->sizeHint().height()
                                                   : sketchPanel_->property("contentHeight").toInt() + 48;
    const int height = std::min(wanted, h - kTop - 70);
    sketchPanel_->setGeometry(w - sketchPanel_->sizeHint().width() - kMargin, kTop,
                              sketchPanel_->sizeHint().width(), std::max(height, 40));
    sketchPanel_->raise();
  }
  // Nav bar: bottom centre.
  if (navBar_) {
    navBar_->adjustSize();
    navBar_->move((w - navBar_->width()) / 2, h - navBar_->height() - 14);
    navBar_->raise();
  }
}

// ---- Commands -----------------------------------------------------------------

void MainWindow::onTool(const QString& id) {
  if (id.startsWith("sketch.")) {
    onSketchTool(id);
  } else if (id == "design.sketch") {
    openCreateSketch();
  } else if (id == "design.extrude") {
    openExtrudeDialog();
  } else if (id == "design.box") {
    openBoxDialog();
  } else if (id == "design.fillet") {
    openFilletDialog();
  } else if (id == "design.select") {
    closeDialog();
  }
}

void MainWindow::showDialog(CommandDialog* dialog) {
  closeDialog();
  dialog_ = dialog;
  connect(dialog, &CommandDialog::rejected, this, &MainWindow::closeDialog);
  dialog->adjustSize();
  dialog->setProperty("animating", true);
  dialog->show();
  layoutOverlays();
  anim::fadeSlideIn(dialog, dialogPosition());
  QTimer::singleShot(anim::kNormal + 20, dialog, [dialog] { dialog->setProperty("animating", false); });
  dialog->focusFirstField();
}

void MainWindow::closeDialog() {
  cancelPickers();
  if (CommandDialog* d = dialog_) {
    d->setEnabled(false);
    anim::fadeOut(d, [d] { d->deleteLater(); });
  }
  dialog_ = nullptr;
  viewport_->setFocus();
}

QPoint MainWindow::dialogPosition() const {
  // Top-right, under the view cube.
  return {viewport_->width() - (dialog_ ? dialog_->width() : 290) - 16, 140};
}

void MainWindow::openBoxDialog() {
  const model::LengthUnitKind unit = model::timeline_units(*timeline_);
  auto* d = new CommandDialog(tr("Box"), viewport_);
  LengthField* width = d->addLength(tr("Width"), 1.0 * kInch, unit, 0.001);
  LengthField* depth = d->addLength(tr("Depth"), 1.0 * kInch, unit, 0.001);
  LengthField* height = d->addLength(tr("Height"), 1.0 * kInch, unit, 0.001);
  LengthField* x = d->addLength(tr("Corner X"), 3.0 * kInch, unit);
  LengthField* y = d->addLength(tr("Corner Y"), -0.5 * kInch, unit);
  connect(d, &CommandDialog::accepted, this, [=, this] {
    pushUndo();
    const int n = int(model::timeline_len(*timeline_)) + 1;
    const std::string name = QString("Box %1").arg(n).toStdString();
    model::timeline_add_extrude(*timeline_, name,
                                rectProfile(x->valueMm(), y->valueMm(), width->valueMm(), depth->valueMm()),
                                height->valueMm(), model::BodyOp::Join);
    closeDialog();
    rebuildModel(true);
  });
  showDialog(d);
}

void MainWindow::openFilletDialog() {
  auto* d = new CommandDialog(tr("Fillet"), viewport_);
  LengthField* radius = d->addLength(tr("Radius"), 0.03 * kInch, model::timeline_units(*timeline_), 0.001);
  d->addChoice(tr("Edges"), {tr("All edges")});
  connect(d, &CommandDialog::accepted, this, [=, this] {
    pushUndo();
    model::timeline_add_fillet(*timeline_, "Fillet", radius->valueMm());
    closeDialog();
    rebuildModel();
  });
  showDialog(d);
}

// ---- Units ----------------------------------------------------------------------

QString MainWindow::unitName() const {
  return QString::fromUtf8(model::length_unit_name(model::timeline_units(*timeline_)).data(),
                           int(model::length_unit_name(model::timeline_units(*timeline_)).size()));
}

void MainWindow::fillUnitsMenu(QMenu* menu) {
  menu->clear();
  const model::LengthUnitKind current = model::timeline_units(*timeline_);
  auto* group = new QActionGroup(menu);
  const model::LengthUnitKind all[] = {model::LengthUnitKind::Inch, model::LengthUnitKind::Foot,
                                       model::LengthUnitKind::Millimeter, model::LengthUnitKind::Centimeter,
                                       model::LengthUnitKind::Meter};
  for (model::LengthUnitKind u : all) {
    const rust::Str name = model::length_unit_name(u);
    const rust::Str sym = model::length_unit_symbol(u);
    QAction* a = menu->addAction(QString("%1 (%2)").arg(QString::fromUtf8(name.data(), int(name.size())),
                                                       QString::fromUtf8(sym.data(), int(sym.size()))));
    a->setCheckable(true);
    a->setChecked(u == current);
    group->addAction(a);
    connect(a, &QAction::triggered, this, [this, u] { setUnits(u); });
    if (u == model::LengthUnitKind::Foot) menu->addSeparator();
  }
}

void MainWindow::setUnits(model::LengthUnitKind unit, bool remember) {
  model::timeline_set_units(*timeline_, unit);
  const rust::Str sym = model::length_unit_symbol(unit);
  if (remember) QSettings().setValue("units/default", QString::fromUtf8(sym.data(), int(sym.size())));

  timelineBar_->unitsButton()->setText(QString::fromUtf8(sym.data(), int(sym.size())) + QStringLiteral(" ▾"));
  fillUnitsMenu(timelineBar_->unitsButton()->menu());

  // Imperial: 0.1" grid, inch lines stronger. Metric: 10 mm grid, 100 mm lines.
  const bool imperial = unit == model::LengthUnitKind::Inch || unit == model::LengthUnitKind::Foot;
  if (imperial) {
    viewport_->setGridSpacing(0.1 * kInch, 10 * kInch);
  } else {
    viewport_->setGridSpacing(10.0, 250.0);
  }

  if (dialog_) dialog_->setUnit(unit);
  refreshBrowser();
}

// ---- Model ----------------------------------------------------------------------

void MainWindow::loadDemoDocument() {
  // The pillow block, built the way a user would: sketch, extrude, sketch, extrude.
  documentName_ = "Pillow Block";
  timeline_ = model::new_timeline();
  auto& tl = *timeline_;
  constexpr double in = kInch;  // designed in inches; stored in mm
  using K = model::ConstraintKind;

  auto centreOf = [&tl](std::uint64_t sketch, std::uint64_t circle) -> std::uint64_t {
    for (const auto& e : model::sketch_entities(tl, sketch))
      if (e.id == circle) return e.p0;
    return 0;
  };
  auto circle = [&](std::uint64_t sketch, double x, double y, double diameter) {
    const std::uint64_t c = model::sketch_add_circle_xy(tl, sketch, x, y, diameter / 2);
    model::sketch_add_constraint(tl, sketch, K::Fix, centreOf(sketch, c), 0, 0, 0);
    model::sketch_add_constraint(tl, sketch, K::Diameter, c, 0, 0, diameter);
  };
  auto largestProfile = [&tl](std::uint64_t sketch) {
    rust::Vec<rust::String> keys;
    auto profiles = model::sketch_profiles(tl, sketch);
    if (!profiles.empty()) keys.push_back(profiles[0].key);
    return keys;
  };

  // Base: 5 1/2" × 2 1/4" plate with a 1" bore and two 3/8" bolt holes, 1/2" thick.
  const std::uint64_t base = model::timeline_add_sketch(tl, "Base Sketch", model::OriginPlaneKind::XY);
  const auto edges = model::sketch_add_rectangle(tl, base, -2.75 * in, -1.125 * in, 2.75 * in, 1.125 * in);
  std::uint64_t corner = 0;
  for (const auto& e : model::sketch_entities(tl, base))
    if (e.id == edges[0]) corner = e.p0;
  model::sketch_add_constraint(tl, base, K::Fix, corner, 0, 0, 0);
  model::sketch_add_constraint(tl, base, K::Length, edges[0], 0, 0, 5.5 * in);
  model::sketch_add_constraint(tl, base, K::Length, edges[1], 0, 0, 2.25 * in);
  circle(base, 0, 0, 1.0 * in);
  circle(base, -2.125 * in, 0, 0.375 * in);
  circle(base, 2.125 * in, 0, 0.375 * in);
  model::timeline_add_extrude_sketch(tl, "Base", base, largestProfile(base), 0.5 * in, model::BodyOp::Join);

  // Boss: a 1 3/4" ring around the bore, 1 3/4" tall.
  const std::uint64_t boss = model::timeline_add_sketch(tl, "Boss Sketch", model::OriginPlaneKind::XY);
  circle(boss, 0, 0, 1.75 * in);
  circle(boss, 0, 0, 1.0 * in);
  model::timeline_add_extrude_sketch(tl, "Boss", boss, largestProfile(boss), 1.75 * in, model::BodyOp::Join);

  setUnits(defaultUnit(), /*remember=*/false);
  documentBar_->setDocumentName(documentName_);
  documentBar_->setSaveState(tr("Not saved (saving arrives in M2)"));
  rebuildModel(/*fit=*/true);
}

void MainWindow::rebuildModel(bool fit) {
  rust::Vec<std::size_t> failed;
  const std::unique_ptr<model::Shape> body = model::timeline_rebuild(*timeline_, failed);
  body_ = body ? body->value : TopoDS_Shape();
  failed_.clear();
  for (std::size_t i : failed) failed_.push_back(i);

  viewport_->showBody(body_);
  previewing_ = false;
  if (fit) viewport_->fitAll();
  showSketches();

  refreshTimeline();
  refreshBrowser();
  setWindowTitle(QString("%1 — %2").arg(documentName_, branding::kDisplayName));

  if (!failed_.isEmpty()) {
    QStringList names;
    for (std::size_t i : failed_) names << toQString(model::timeline_feature_name(*timeline_, i));
    timelineBar_->showStatus(tr("Could not compute: %1").arg(names.join(", ")));
  }
}

void MainWindow::refreshTimeline() {
  QVector<TimelineBar::Chip> chips;
  for (std::size_t i = 0; i < model::timeline_len(*timeline_); ++i) {
    const QString type = toQString(model::timeline_feature_type(*timeline_, i));
    chips.push_back({toQString(model::timeline_feature_name(*timeline_, i)), type, iconForType(type),
                     failed_.contains(i)});
  }
  timelineBar_->setFeatures(chips, int(model::timeline_marker(*timeline_)));
}

namespace {

enum class Eye { None, Shown, Hidden };

// [eye][gap][type icon] as one pixmap so rows line up like Fusion's browser.
QIcon browserIcon(const QString& type, Eye eye) {
  const int s = theme::kSmallIconSize;
  const qreal dpr = qGuiApp->devicePixelRatio();
  QPixmap pm(QSize(2 * s + 6, s) * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  if (eye != Eye::None) {
    const QColor c = eye == Eye::Shown ? theme::current().text : theme::current().textDisabled;
    p.drawPixmap(0, 0, icons::tinted(eye == Eye::Shown ? "eye" : "eye-off", c, s).pixmap(s, s));
  }
  const QIcon typeIcon = icons::get(type, s);
  p.drawPixmap(s + 6, 0, typeIcon.pixmap(s, s, eye == Eye::Hidden ? QIcon::Disabled : QIcon::Normal));
  p.end();
  return QIcon(pm);
}

}  // namespace

bool MainWindow::sketchVisible(std::uint64_t id) const {
  if (!sketchesShown_) return false;
  if (auto it = sketchVisibility_.constFind(id); it != sketchVisibility_.cend()) return *it;
  // Used sketches hide, as in Fusion, unless Preferences says otherwise.
  return prefs::flag(prefs::kShowUsedSketches) || !model::timeline_sketch_consumed(*timeline_, id);
}

void MainWindow::toggleBrowserVisibility(QTreeWidgetItem* item) {
  const QString what = item->data(0, kVisibilityRole).toString();
  if (what == "body" || what == "bodies") {
    viewport_->setBodyVisible(!viewport_->bodyVisible());
  } else if (what == "sketches") {
    sketchesShown_ = !sketchesShown_;
  } else if (what == "origin") {
    originShown_ = !originShown_;
    showOrigin();
  } else if (what == "sketch") {
    const std::uint64_t id = item->data(0, kFeatureRole).toULongLong();
    const bool visible = sketchVisible(id);
    sketchVisibility_[id] = !visible;
    if (!sketchesShown_ && !visible) sketchesShown_ = true;
  }
  showSketches();
  QTimer::singleShot(0, this, &MainWindow::refreshBrowser);  // not while handling the click
}

void MainWindow::showOrigin() {
  if (!originShown_) {
    viewport_->clearOverlay("origin");
    return;
  }
  const double h = 1.5 * kInch;
  Overlay o;
  const QColor planeColor(240, 160, 75);
  for (int i = 0; i < 3; ++i) {
    geom::Plane p;
    if (i == 1) p.yAxis = {0, 0, 1}, p.normal = {0, -1, 0};
    if (i == 2) p.xAxis = {0, 1, 0}, p.yAxis = {0, 0, 1}, p.normal = {1, 0, 0};
    o.faces.push_back({geom::makePolygonFace(p, {{{0, 0}, {h, 0}, {h, h}, {0, h}}}), planeColor, 0.85});
  }
  auto axis = [&](geom::Plane p, const QColor& c) {
    geom::Seg2 s;
    s.x0 = 0, s.y0 = 0, s.x1 = 2.2 * kInch, s.y1 = 0;
    o.curves.push_back({geom::makeCurves(p, {s}), c, 2.0, false});
  };
  geom::Plane px, py, pz;
  py.xAxis = {0, 1, 0}, py.yAxis = {-1, 0, 0};
  pz.xAxis = {0, 0, 1}, pz.yAxis = {1, 0, 0}, pz.normal = {0, 1, 0};
  axis(px, QColor(229, 72, 77));
  axis(py, QColor(61, 187, 79));
  axis(pz, QColor(62, 124, 240));
  o.points.push_back({geom::makeVertices(px, {{0.0, 0.0}}), QColor(240, 240, 240), 3.0});
  viewport_->setOverlay("origin", o);
}

void MainWindow::refreshBrowser() {
  // Rebuilding the tree expands every item; don't animate each one.
  const QSignalBlocker quiet(browser_);
  browser_->clear();
  const bool bodyShown = viewport_->bodyVisible();
  auto add = [](QTreeWidgetItem* parent, const QString& text, const QString& icon, Eye eye = Eye::None,
                const QString& role = {}) {
    auto* item = new QTreeWidgetItem(parent, {text});
    item->setIcon(0, browserIcon(icon, eye));
    if (!role.isEmpty()) {
      item->setData(0, kVisibilityRole, role);
      item->setToolTip(0, QObject::tr("Click the eye to show or hide"));
    }
    return item;
  };
  auto* root = new QTreeWidgetItem(browser_, {documentName_});
  root->setIcon(0, browserIcon("component", Eye::None));
  QTreeWidgetItem* settings = add(root, tr("Document Settings"), "settings");
  QTreeWidgetItem* unitsItem = add(settings, tr("Units: %1").arg(unitName()), "units");
  unitsItem->setData(0, kUnitsRole, true);
  unitsItem->setToolTip(0, tr("Click to change the document units"));
  add(root, tr("Origin"), "origin", originShown_ ? Eye::Shown : Eye::Hidden, "origin");
  QTreeWidgetItem* sketches =
      add(root, tr("Sketches"), "folder", sketchesShown_ ? Eye::Shown : Eye::Hidden, "sketches");
  for (std::size_t i = 0; i < model::timeline_marker(*timeline_); ++i) {
    if (toQString(model::timeline_feature_type(*timeline_, i)) != "Sketch") continue;
    const std::uint64_t id = model::timeline_feature_id(*timeline_, i);
    QTreeWidgetItem* item = add(sketches, toQString(model::timeline_feature_name(*timeline_, i)), "sketch",
                                sketchVisible(id) ? Eye::Shown : Eye::Hidden, "sketch");
    item->setData(0, kFeatureRole, QVariant::fromValue<qulonglong>(id));
    item->setToolTip(0, tr("Double-click to edit. Click the eye to show or hide."));
  }
  QTreeWidgetItem* bodies = add(root, tr("Bodies"), "folder", bodyShown ? Eye::Shown : Eye::Hidden, "bodies");
  if (!body_.IsNull()) add(bodies, tr("Body 1"), "body", bodyShown ? Eye::Shown : Eye::Hidden, "body");
  browser_->expandAll();
  layoutOverlays();
}

void MainWindow::setMarker(long marker) {
  if (sketchEditor_) finishSketch();
  closeDialog();
  marker = std::clamp(marker, 0L, long(model::timeline_len(*timeline_)));
  model::timeline_set_marker(*timeline_, std::size_t(marker));
  rebuildModel();
}

void MainWindow::exportStep() {
  if (body_.IsNull()) return;
  const QString path = QFileDialog::getSaveFileName(this, tr("Export STEP"), documentName_ + ".step",
                                                    tr("STEP files (*.step *.stp)"));
  if (path.isEmpty()) return;
  if (geom::exportStep(body_, path.toStdString())) {
    timelineBar_->showStatus(tr("Exported %1").arg(QFileInfo(path).fileName()));
  } else {
    QMessageBox::warning(this, branding::kDisplayName, tr("STEP export failed."));
  }
}

void MainWindow::showAbout() {
  QMessageBox::about(
      this, tr("About %1").arg(branding::kDisplayName),
      QString("<h3>%1 %2</h3><p>%3</p><p><a href='%4'>%4</a></p>"
              "<p>Geometry by Open CASCADE Technology %5 (LGPL-2.1).<br>UI by Qt %6 (LGPL-3.0).</p>")
          .arg(branding::kDisplayName, branding::kVersion, branding::kTagline, branding::kRepoUrl,
               OCC_VERSION_COMPLETE, qVersion()));
}

// ---- Undo / redo -------------------------------------------------------------------

void MainWindow::pushUndo() {
  undo_.push_back(model::timeline_clone(*timeline_));
  redo_.clear();
  constexpr std::size_t kMaxUndo = 200;
  if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
}

void MainWindow::undo() {
  if (undo_.empty()) {
    timelineBar_->showStatus(tr("Nothing to undo"));
    return;
  }
  redo_.push_back(model::timeline_clone(*timeline_));
  timeline_ = std::move(undo_.back());
  undo_.pop_back();
  afterHistoryChange();
}

void MainWindow::redo() {
  if (redo_.empty()) {
    timelineBar_->showStatus(tr("Nothing to redo"));
    return;
  }
  undo_.push_back(model::timeline_clone(*timeline_));
  timeline_ = std::move(redo_.back());
  redo_.pop_back();
  afterHistoryChange();
}

void MainWindow::afterHistoryChange() {
  if (dialog_) closeDialog();
  if (sketchEditor_) {
    if (model::timeline_index_of(*timeline_, sketchEditor_->sketch()) == SIZE_MAX) {
      // The open sketch was undone away.
      sketchReturnMarker_ = -1;
      finishSketch();
      return;
    }
    sketchEditor_->reload();
  }
  rebuildModel();
}

// ---- Sketching ---------------------------------------------------------------------

QVector<std::uint64_t> MainWindow::sketchIds(bool unconsumedOnly) const {
  QVector<std::uint64_t> ids;
  const std::size_t end = std::min(model::timeline_marker(*timeline_), model::timeline_len(*timeline_));
  for (std::size_t i = 0; i < end; ++i) {
    if (toQString(model::timeline_feature_type(*timeline_, i)) != "Sketch") continue;
    const std::uint64_t id = model::timeline_feature_id(*timeline_, i);
    if (unconsumedOnly && model::timeline_sketch_consumed(*timeline_, id)) continue;
    ids.push_back(id);
  }
  return ids;
}

void MainWindow::showSketches() {
  // Sketches not yet used by a feature stay visible (thin), like Fusion.
  if (sketchEditor_ || profilePicker_) {
    viewport_->clearOverlay("sketches");
    return;
  }
  const theme::Palette& pal = theme::current();
  Overlay o;
  for (std::uint64_t id : sketchIds(/*unconsumedOnly=*/false)) {
    if (!sketchVisible(id)) continue;
    try {
      const geom::Plane plane = toGeomPlane(model::sketch_plane(*timeline_, id));
      o.curves.push_back({sketchCurves(*timeline_, id, plane), pal.highlight, 1.5, false});
      for (const auto& p : model::sketch_profiles(*timeline_, id))
        o.faces.push_back({geom::makePolygonFace(plane, profileLoops(p)), pal.highlight, 0.88});
    } catch (const std::exception&) {
    }
  }
  viewport_->setOverlay("sketches", o);
}

void MainWindow::cancelPickers() {
  if (planePicker_ || profilePicker_) {
    viewport_->setTool(sketchEditor_ ? static_cast<ViewportTool*>(sketchEditor_.get()) : nullptr);
  }
  planePicker_.reset();
  const bool hadProfiles = bool(profilePicker_);
  profilePicker_.reset();
  if (previewing_ || hadProfiles) {
    previewing_ = false;
    viewport_->showBody(body_);
    showSketches();
  }
  ribbon_->setActiveTool({});
}

void MainWindow::openCreateSketch() {
  if (sketchEditor_) return;
  auto* d = new CommandDialog(tr("Create Sketch"), viewport_);
  QComboBox* plane = d->addChoice(tr("Plane"), {tr("XY (Top)"), tr("XZ (Front)"), tr("YZ (Right)")});
  d->addInfo(tr("Or"), tr("click a plane in the view"));
  connect(d, &CommandDialog::accepted, this, [this, plane] {
    const model::OriginPlaneKind kinds[] = {model::OriginPlaneKind::XY, model::OriginPlaneKind::XZ,
                                            model::OriginPlaneKind::YZ};
    createSketch(kinds[std::clamp(plane->currentIndex(), 0, 2)]);
  });
  showDialog(d);
  planePicker_ = std::make_unique<PlanePicker>(viewport_, 2.0 * kInch, [this](model::OriginPlaneKind k) {
    QTimer::singleShot(0, this, [this, k] { createSketch(k); });
  });
  viewport_->setTool(planePicker_.get());
  ribbon_->setActiveTool("design.sketch");
  timelineBar_->showStatus(tr("Pick a plane for the sketch"));
}

void MainWindow::createSketch(model::OriginPlaneKind plane) {
  closeDialog();
  sketchEntry_ = std::make_unique<rust::Box<model::Timeline>>(model::timeline_clone(*timeline_));
  sketchUndoDepth_ = undo_.size();
  pushUndo();
  const int n = int(sketchIds(false).size()) + 1;
  const std::uint64_t id =
      model::timeline_add_sketch(*timeline_, QString("Sketch %1").arg(n).toStdString(), plane);
  rebuildModel();
  enterSketch(id);
}

void MainWindow::enterSketch(std::uint64_t sketch) {
  if (sketchEditor_) {
    if (sketchEditor_->sketch() == sketch) return;
    finishSketch();
  }
  closeDialog();
  const std::size_t index = model::timeline_index_of(*timeline_, sketch);
  if (index == SIZE_MAX) return;

  if (!sketchEntry_) {
    sketchEntry_ = std::make_unique<rust::Box<model::Timeline>>(model::timeline_clone(*timeline_));
    sketchUndoDepth_ = undo_.size();
  }
  // Editing an older sketch: roll the timeline back to just after it.
  sketchReturnMarker_ = -1;
  if (index + 1 < model::timeline_marker(*timeline_)) {
    sketchReturnMarker_ = long(model::timeline_marker(*timeline_));
    model::timeline_set_marker(*timeline_, index + 1);
  }

  SketchEditor::Host host{
      [this]() -> model::Timeline& { return *timeline_; },
      [this](rust::Box<model::Timeline> before) {
        undo_.push_back(std::move(before));
        redo_.clear();
      },
      [this](rust::Box<model::Timeline> before) { timeline_ = std::move(before); },
  };
  ribbon_->setWorkspaces(sketchWorkspace());
  sketchEditor_ = std::make_unique<SketchEditor>(viewport_, std::move(host), sketch, this);
  SketchEditor* ed = sketchEditor_.get();
  connect(ed, &SketchEditor::message, this, [this](const QString& m) { timelineBar_->showStatus(m, 6000); });
  connect(ed, &SketchEditor::edited, this, [this] {
    refreshTimeline();
    refreshBrowser();
  });
  connect(ed, &SketchEditor::finishRequested, this, [this] { QTimer::singleShot(0, this, &MainWindow::finishSketch); });
  connect(ed, &SketchEditor::solveStateChanged, this, [this](int dof, bool conflicting) {
    QString text;
    if (conflicting) text = tr("Sketch: conflicting constraints");
    else if (dof == 0) text = tr("Sketch: fully constrained");
    else if (dof > 0) text = tr("Sketch: %n degree(s) of freedom", nullptr, dof);
    timelineBar_->setPermanentStatus(text);
  });
  connect(ed, &SketchEditor::modeChanged, this, [this, ed](SketchEditor::Mode m) {
    QString id;
    switch (m) {
      case SketchEditor::Mode::Select: id = "sketch.select"; break;
      case SketchEditor::Mode::Line: id = "sketch.line"; break;
      case SketchEditor::Mode::Rectangle: id = "sketch.rectangle"; break;
      case SketchEditor::Mode::Circle: id = "sketch.circle"; break;
      case SketchEditor::Mode::Arc: id = "sketch.arc"; break;
      case SketchEditor::Mode::Point: id = "sketch.point"; break;
      case SketchEditor::Mode::Dimension: id = "sketch.dimension"; break;
      case SketchEditor::Mode::Constrain:
        for (const auto& t : kConstraintTools)
          if (t.kind == ed->constraintKind()) id = t.id;
        break;
    }
    ribbon_->setActiveTool(id);
  });
  // Sketch Palette on the right, as in MitCAD and Fusion.
  sketchPanel_ = new FloatingPanel(
      tr("SKETCH PALETTE · %1").arg(toQString(model::timeline_feature_name(*timeline_, index))), viewport_);
  auto* palette = new SketchPalette(ed, sketchPanel_);
  // Scrolls when the window is too short for the whole palette.
  auto* scroll = new QScrollArea(sketchPanel_);
  scroll->setObjectName("PaletteScroll");
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidgetResizable(true);
  scroll->setWidget(palette);
  scroll->setFixedWidth(palette->width() + 10);
  scroll->setMinimumHeight(120);
  sketchPanel_->setProperty("contentHeight", palette->sizeHint().height());
  sketchPanel_->setContent(scroll);
  sketchPanel_->setCollapsible(true);
  sketchPanel_->applyShadow();
  connect(palette, &SketchPalette::toolRequested, this, &MainWindow::onSketchTool);
  connect(palette, &SketchPalette::finishRequested, this, [this] { QTimer::singleShot(0, this, &MainWindow::finishSketch); });
  connect(palette, &SketchPalette::cancelRequested, this, [this] { QTimer::singleShot(0, this, &MainWindow::cancelSketch); });
  connect(palette, &SketchPalette::lookAtRequested, this, [this, sketch] {
    const model::PlaneInfo p = model::sketch_plane(*timeline_, sketch);
    viewport_->lookAlong(gp_Dir(p.normal[0], p.normal[1], p.normal[2]), gp_Dir(p.y_axis[0], p.y_axis[1], p.y_axis[2]));
    QTimer::singleShot(450, this, [this] {
      if (sketchEditor_) sketchEditor_->viewChanged();
    });
  });
  connect(sketchPanel_, &FloatingPanel::collapsedChanged, this, [this] { layoutOverlays(); });
  sketchPanel_->show();
  layoutOverlays();

  viewport_->setTool(ed);
  viewport_->setBodyTransparency(0.75);  // see the sketch through the model
  ed->reload();  // emits the solve state with the connections in place
  ed->setMode(SketchEditor::Mode::Select);

  rebuildModel();
  try {
    const model::PlaneInfo p = model::sketch_plane(*timeline_, sketch);
    viewport_->lookAlong(gp_Dir(p.normal[0], p.normal[1], p.normal[2]),
                         gp_Dir(p.y_axis[0], p.y_axis[1], p.y_axis[2]));
    QTimer::singleShot(450, ed, [ed] { ed->viewChanged(); });
  } catch (const std::exception&) {
  }
  timelineBar_->showStatus(tr("Editing %1. Press L for a line, D to dimension, Finish Sketch when done.")
                               .arg(toQString(model::timeline_feature_name(*timeline_, index))),
                           6000);
}

void MainWindow::cancelSketch() {
  if (!sketchEditor_ || !sketchEntry_) return;
  viewport_->setTool(nullptr);
  sketchEditor_.reset();
  timeline_ = std::move(*sketchEntry_);  // also brings back the marker
  sketchEntry_.reset();
  if (undo_.size() > sketchUndoDepth_) undo_.erase(undo_.begin() + long(sketchUndoDepth_), undo_.end());
  redo_.clear();
  sketchReturnMarker_ = -1;
  finishSketch();
  timelineBar_->showStatus(tr("Sketch changes discarded"));
}

void MainWindow::finishSketch() {
  if (!sketchEditor_ && !sketchPanel_) {
    rebuildModel();
    return;
  }
  if (sketchPanel_) sketchPanel_->deleteLater();
  sketchPanel_ = nullptr;
  sketchEntry_.reset();
  viewport_->setTool(nullptr);
  sketchEditor_.reset();
  if (sketchReturnMarker_ >= 0) {
    model::timeline_set_marker(*timeline_,
                               std::min(std::size_t(sketchReturnMarker_), model::timeline_len(*timeline_)));
  }
  sketchReturnMarker_ = -1;
  viewport_->setBodyTransparency(0.0);
  ribbon_->setWorkspaces(workspaces());
  timelineBar_->setPermanentStatus(permanentStatus_);
  rebuildModel();
}

void MainWindow::onSketchTool(const QString& id) {
  SketchEditor* ed = sketchEditor_.get();
  if (!ed) return;
  using M = SketchEditor::Mode;
  if (id == "sketch.finish") {
    finishSketch();
  } else if (id == "sketch.line") {
    ed->setMode(M::Line);
  } else if (id == "sketch.rectangle") {
    ed->setMode(M::Rectangle);
  } else if (id == "sketch.circle") {
    ed->setMode(M::Circle);
  } else if (id == "sketch.arc") {
    ed->setMode(M::Arc);
  } else if (id == "sketch.point") {
    ed->setMode(M::Point);
  } else if (id == "sketch.dimension") {
    ed->setMode(M::Dimension);
  } else if (id == "sketch.select") {
    ed->setMode(M::Select);
  } else if (id == "sketch.construction") {
    ed->toggleConstruction();
  } else if (id == "sketch.delete") {
    ed->deleteSelection();
  } else {
    for (const auto& t : kConstraintTools)
      if (id == t.id) ed->setConstraint(t.kind);
  }
  viewport_->setFocus();
}

// ---- Extrude -----------------------------------------------------------------------

void MainWindow::openExtrudeDialog() {
  if (sketchEditor_) finishSketch();
  QVector<std::uint64_t> sketches = sketchIds(/*unconsumedOnly=*/true);
  if (sketches.isEmpty()) sketches = sketchIds(false);
  if (sketches.isEmpty()) {
    timelineBar_->showStatus(tr("Create a sketch with a closed shape first"));
    return;
  }

  const model::LengthUnitKind unit = model::timeline_units(*timeline_);
  auto* d = new CommandDialog(tr("Extrude"), viewport_);
  QLabel* count = d->addInfo(tr("Profiles"), tr("Click a profile"));
  LengthField* distance = d->addLength(tr("Distance"), 1.0 * kInch, unit, 0.001);
  QComboBox* direction = d->addChoice(tr("Direction"), {tr("One side"), tr("Flipped")});
  QComboBox* operation = d->addChoice(tr("Operation"), {tr("Join"), tr("Cut"), tr("New Body")});

  auto op = [operation] {
    switch (operation->currentIndex()) {
      case 1: return model::BodyOp::Cut;
      case 2: return model::BodyOp::NewBody;
      default: return model::BodyOp::Join;
    }
  };
  auto signedDistance = [distance, direction] {
    return distance->valueMm() * (direction->currentIndex() == 1 ? -1.0 : 1.0);
  };
  auto keysVec = [](const QStringList& keys) {
    rust::Vec<rust::String> v;
    for (const QString& k : keys) v.push_back(rust::String(k.toStdString()));
    return v;
  };

  auto preview = [=, this] {
    if (!profilePicker_ || profilePicker_->keys().isEmpty()) {
      previewing_ = false;
      viewport_->showBody(body_);
      return;
    }
    rust::Box<model::Timeline> copy = model::timeline_clone(*timeline_);
    model::timeline_add_extrude_sketch(*copy, "Preview", profilePicker_->sketch(), keysVec(profilePicker_->keys()),
                                       signedDistance(), op());
    rust::Vec<std::size_t> failed;
    const std::unique_ptr<model::Shape> shape = model::timeline_rebuild(*copy, failed);
    previewing_ = true;
    viewport_->showBody(shape ? shape->value : TopoDS_Shape());
    if (!failed.empty()) timelineBar_->showStatus(tr("That extrude can't be computed"));
  };

  connect(distance, &LengthField::valueChanged, this, [preview](double) { preview(); });
  connect(direction, &QComboBox::currentIndexChanged, this, [preview](int) { preview(); });
  connect(operation, &QComboBox::currentIndexChanged, this, [preview](int) { preview(); });
  connect(d, &CommandDialog::accepted, this, [=, this] {
    if (!profilePicker_ || profilePicker_->keys().isEmpty()) {
      timelineBar_->showStatus(tr("Pick at least one profile"));
      return;
    }
    const std::uint64_t sketch = profilePicker_->sketch();
    const QStringList keys = profilePicker_->keys();
    const double dist = signedDistance();
    const model::BodyOp bodyOp = op();
    closeDialog();
    pushUndo();
    int n = 1;
    for (std::size_t i = 0; i < model::timeline_len(*timeline_); ++i)
      if (toQString(model::timeline_feature_type(*timeline_, i)) == "Extrude") ++n;
    model::timeline_add_extrude_sketch(*timeline_, QString("Extrude %1").arg(n).toStdString(), sketch,
                                       keysVec(keys), dist, bodyOp);
    rebuildModel();
  });

  showDialog(d);  // closes any earlier command (and its pickers)
  viewport_->clearOverlay("sketches");
  std::vector<std::uint64_t> ids(sketches.begin(), sketches.end());
  profilePicker_ = std::make_unique<ProfilePicker>(
      viewport_, *timeline_, ids, [count, preview](std::uint64_t, const QStringList& keys) {
        count->setText(keys.isEmpty() ? QObject::tr("Click a profile")
                                      : QObject::tr("%n selected", nullptr, int(keys.size())));
        preview();
      });
  viewport_->setTool(profilePicker_.get());
  ribbon_->setActiveTool("design.extrude");
  profilePicker_->selectIfSingle();
  if (profilePicker_->profileCount() == 0) timelineBar_->showStatus(tr("No closed profiles in the visible sketches"));
}

// ---- Preferences and help ------------------------------------------------------------

void MainWindow::openPreferences() {
  auto* dialog = new PreferencesDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &PreferencesDialog::applied, this, &MainWindow::applyPreferences);
  dialog->open();
}

void MainWindow::applyPreferences() {
  viewport_->setNavigation(prefs::flag(prefs::kOrbitMiddle), prefs::flag(prefs::kReverseZoom),
                           prefs::flag(prefs::kAnimateCamera));
  navBar_->setGridChecked(prefs::flag(prefs::kShowGrid));
  documentBar_->setVersionVisible(prefs::flag(prefs::kShowVersion));
  if (sketchEditor_) sketchEditor_->reload();
  showSketches();
  refreshBrowser();
}

void MainWindow::buildHelpMenu() {
  QMenu* menu = documentBar_->helpMenu();
  const QString repo = branding::kRepoUrl;
  auto open = [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); };
  menu->addAction(icons::get("document", 16), tr("User Guide"), this, [=] { open(repo + "#readme"); });
  menu->addAction(icons::get("select", 16), tr("Keyboard and Mouse Overview"), this, &MainWindow::showShortcuts);
  menu->addSeparator();
  menu->addAction(icons::get("measure", 16), tr("Diagnostics…"), this, &MainWindow::showDiagnostics);
  menu->addAction(icons::get("folder", 16), tr("Open Settings Folder"), this, [] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(QSettings().fileName()).absolutePath()));
  });
  menu->addAction(icons::get("export", 16), tr("Check for Updates…"), this, [=] { open(repo + "/releases"); });
  menu->addAction(icons::get("comment", 16), tr("Send Feedback…"), this, [=] { open(repo + "/issues/new"); });
  menu->addSeparator();
  menu->addAction(tr("About %1").arg(branding::kDisplayName), this, &MainWindow::showAbout);
  menu->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
}

namespace {

// A read-only two-column table in a themed dialog.
QDialog* infoDialog(QWidget* parent, const QString& title, const QStringList& headers,
                    const QList<QPair<QString, QString>>& rows, bool copyButton) {
  auto* d = new QDialog(parent);
  d->setObjectName("InfoDialog");
  d->setWindowTitle(title);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->resize(620, 460);
  auto* layout = new QVBoxLayout(d);
  auto* table = new QTableWidget(int(rows.size()), 2, d);
  table->setObjectName("InfoTable");
  table->setHorizontalHeaderLabels(headers);
  table->verticalHeader()->hide();
  table->horizontalHeader()->setStretchLastSection(true);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setShowGrid(false);
  for (int i = 0; i < rows.size(); ++i) {
    table->setItem(i, 0, new QTableWidgetItem(rows[i].first));
    table->setItem(i, 1, new QTableWidgetItem(rows[i].second));
  }
  table->resizeColumnToContents(0);
  table->setColumnWidth(0, table->columnWidth(0) + 24);
  layout->addWidget(table);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  if (copyButton) {
    auto* copy = new QPushButton(QObject::tr("Copy to Clipboard"), d);
    QObject::connect(copy, &QPushButton::clicked, d, [rows] {
      QStringList lines;
      for (const auto& r : rows) lines << r.first + ": " + r.second;
      QGuiApplication::clipboard()->setText(lines.join('\n'));
    });
    buttons->addWidget(copy);
  }
  auto* close = new QPushButton(QObject::tr("Close"), d);
  close->setObjectName("PrimaryButton");
  QObject::connect(close, &QPushButton::clicked, d, &QDialog::accept);
  buttons->addWidget(close);
  layout->addLayout(buttons);
  return d;
}

}  // namespace

void MainWindow::showShortcuts() {
  const QList<QPair<QString, QString>> rows = {
      {tr("Middle-drag"), prefs::flag(prefs::kOrbitMiddle) ? tr("Orbit") : tr("Pan")},
      {tr("Shift + middle-drag"), prefs::flag(prefs::kOrbitMiddle) ? tr("Pan") : tr("Orbit")},
      {tr("Mouse wheel"), tr("Zoom at the cursor")},
      {tr("Left-drag"), tr("Window select (or the nav bar's orbit/pan/zoom mode)")},
      {tr("F6"), tr("Fit all")},
      {tr("Ctrl+Z / Ctrl+Y"), tr("Undo / redo (also Ctrl+Shift+Z)")},
      {tr("Sketch: L R C A P"), tr("Line, rectangle, circle, 3-point arc, point")},
      {tr("Sketch: D"), tr("Dimension")},
      {tr("Sketch: X"), tr("Toggle construction")},
      {tr("Sketch: S / Esc"), tr("Select / stop the tool")},
      {tr("Sketch: Tab"), tr("While drawing, jump to the size box and type a value")},
      {tr("Sketch: Delete"), tr("Delete the selection")},
      {tr("Double-click a sketch"), tr("Edit it (timeline or Browser)")},
      {tr("Double-click a dimension"), tr("Change its value")},
  };
  infoDialog(this, tr("Keyboard and Mouse Overview"), {tr("Input"), tr("Action")}, rows, false)->open();
}

void MainWindow::showDiagnostics() {
  const QList<QPair<QString, QString>> rows = {
      {tr("Version"), QString("%1 %2").arg(branding::kDisplayName, branding::kVersion)},
      {tr("Qt"), QString(qVersion())},
      {tr("Open CASCADE"), QString(OCC_VERSION_COMPLETE)},
      {tr("OpenGL"), viewport_->glInfo()},
      {tr("Operating system"), QSysInfo::prettyProductName() + " (" + QSysInfo::currentCpuArchitecture() + ")"},
      {tr("Theme"), theme::current().light ? tr("Light") : tr("Dark")},
      {tr("Settings file"), QSettings().fileName()},
  };
  infoDialog(this, tr("Diagnostics"), {tr("Item"), tr("Value")}, rows, true)->open();
}
