#include "MainWindow.h"

#include "Branding.h"
#include "Icons.h"
#include "Kernel.h"
#include "Theme.h"
#include "Viewport.h"
#include "widgets/Animations.h"
#include "widgets/CommandDialog.h"
#include "widgets/DocumentBar.h"
#include "widgets/FloatingPanel.h"
#include "widgets/NavBar.h"
#include "widgets/Ribbon.h"
#include "widgets/TimelineBar.h"

#include <Standard_Version.hxx>

#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Tools are generic CAD/CAM terms. A non-empty `tip` disables the tool and
// says when it arrives (see docs/ROADMAP.md).
const QString kSketchTip = QStringLiteral("Coming in M1: sketching");
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
            {T{"design.sketch", "Create Sketch", "sketch", kSketchTip},
             T{"design.box", "Box", "box", {}},
             T{"design.extrude", "Extrude", "extrude", kSketchTip},
             T{"design.revolve", "Revolve", "revolve", kSketchTip},
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
             T{"cam.parallel", "Parallel", "parallel", kCamTip}, T{"cam.scallop", "Scallop", "scallop", kCamTip}}},
          G{"DRILLING", {T{"cam.drill", "Drill", "drill", kCamTip}}},
          G{"ACTIONS",
            {T{"cam.simulate", "Simulate", "simulate", kCamTip}, T{"cam.post", "Post Process", "post", kCamTip}}},
          G{"MANAGE", {T{"cam.library", "Tool Library", "library", kCamTip}}},
      }};
  return {design, manufacture};
}

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

enum BrowserRole { kVisibilityRole = Qt::UserRole + 1 };

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  auto* central = new QWidget(this);
  central->setObjectName("Central");
  auto* column = new QVBoxLayout(central);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);

  documentBar_ = new DocumentBar(central);
  connect(documentBar_, &DocumentBar::helpRequested, this, &MainWindow::showAbout);
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
  timelineBar_->setPermanentStatus(QString("Open CASCADE %1  ·  mm").arg(OCC_VERSION_COMPLETE));
  connect(timelineBar_, &TimelineBar::markerRequested, this, [this](int m) { setMarker(m); });
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

  buildAppMenu();
  loadDemoDocument();
  resize(1440, 900);
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
  browser_->setColumnCount(2);
  browser_->setHeaderHidden(true);
  browser_->setIndentation(14);
  browser_->setRootIsDecorated(true);
  browser_->setIconSize(QSize(theme::kSmallIconSize, theme::kSmallIconSize));
  browser_->header()->setStretchLastSection(false);
  browser_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  browser_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  browser_->header()->resizeSection(1, 24);
  browser_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  browser_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  connect(browser_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
    if (column != 1 || !item->data(1, kVisibilityRole).isValid()) return;
    const bool visible = !item->data(1, kVisibilityRole).toBool();
    item->setData(1, kVisibilityRole, visible);
    item->setIcon(1, icons::get(visible ? "eye" : "eye-off", theme::kSmallIconSize));
    viewport_->setBodyVisible(visible);
  });
  browserPanel_->setContent(browser_);
  connect(browserPanel_, &FloatingPanel::collapsedChanged, this, [this] { layoutOverlays(true); });
  connect(browser_, &QTreeWidget::itemExpanded, this, [this] { layoutOverlays(true); });
  connect(browser_, &QTreeWidget::itemCollapsed, this, [this] { layoutOverlays(true); });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
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
  // Nav bar: bottom centre.
  if (navBar_) {
    navBar_->adjustSize();
    navBar_->move((w - navBar_->width()) / 2, h - navBar_->height() - 14);
    navBar_->raise();
  }
}

// ---- Commands -----------------------------------------------------------------

void MainWindow::onTool(const QString& id) {
  if (id == "design.box") {
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
  auto* d = new CommandDialog(tr("Box"), viewport_);
  QDoubleSpinBox* width = d->addNumber(tr("Width"), 20);
  QDoubleSpinBox* depth = d->addNumber(tr("Depth"), 20);
  QDoubleSpinBox* height = d->addNumber(tr("Height"), 20);
  QDoubleSpinBox* x = d->addNumber(tr("Corner X"), 70);
  QDoubleSpinBox* y = d->addNumber(tr("Corner Y"), -10);
  connect(d, &CommandDialog::accepted, this, [=, this] {
    const int n = int(model::timeline_len(*timeline_)) + 1;
    const std::string name = QString("Box %1").arg(n).toStdString();
    model::timeline_add_extrude(*timeline_, name, rectProfile(x->value(), y->value(), width->value(), depth->value()),
                                height->value(), model::BodyOp::Join);
    closeDialog();
    rebuildModel(true);
  });
  showDialog(d);
}

void MainWindow::openFilletDialog() {
  auto* d = new CommandDialog(tr("Fillet"), viewport_);
  QDoubleSpinBox* radius = d->addNumber(tr("Radius"), 1.0, " mm", 0.01, 1000);
  d->addChoice(tr("Edges"), {tr("All edges")});
  connect(d, &CommandDialog::accepted, this, [=, this] {
    model::timeline_add_fillet(*timeline_, "Fillet", radius->value());
    closeDialog();
    rebuildModel();
  });
  showDialog(d);
}

// ---- Model ----------------------------------------------------------------------

void MainWindow::loadDemoDocument() {
  using model::BodyOp;
  documentName_ = "Pillow Block";
  timeline_ = model::new_timeline();
  auto& tl = *timeline_;
  model::timeline_add_extrude(tl, "Base", rectProfile(-60, -25, 120, 50), 12, BodyOp::Join);
  model::timeline_add_extrude(tl, "Boss", circleProfile(0, 0, 24), 45, BodyOp::Join);
  model::timeline_add_extrude(tl, "Bore", circleProfile(0, 0, 13), 45, BodyOp::Cut);
  model::timeline_add_extrude(tl, "Bolt Hole L", circleProfile(-45, 0, 5), 12, BodyOp::Cut);
  model::timeline_add_extrude(tl, "Bolt Hole R", circleProfile(45, 0, 5), 12, BodyOp::Cut);
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
  if (fit) viewport_->fitAll();

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

void MainWindow::refreshBrowser() {
  const bool bodyVisible = [this] {
    for (QTreeWidgetItemIterator it(browser_); *it; ++it) {
      if ((*it)->data(1, kVisibilityRole).isValid()) return (*it)->data(1, kVisibilityRole).toBool();
    }
    return true;
  }();

  browser_->clear();
  auto* root = new QTreeWidgetItem(browser_, {documentName_});
  root->setIcon(0, icons::tinted("body", theme::current().accent, theme::kSmallIconSize));
  auto add = [](QTreeWidgetItem* parent, const QString& text, const QString& icon) {
    auto* item = new QTreeWidgetItem(parent, {text});
    item->setIcon(0, icons::get(icon, theme::kSmallIconSize));
    return item;
  };
  add(root, tr("Document Settings"), "settings");
  add(root, tr("Origin"), "origin");
  QTreeWidgetItem* features = add(root, tr("Features"), "folder");
  for (std::size_t i = 0; i < model::timeline_marker(*timeline_); ++i) {
    const QString type = toQString(model::timeline_feature_type(*timeline_, i));
    add(features, toQString(model::timeline_feature_name(*timeline_, i)), iconForType(type));
  }
  QTreeWidgetItem* bodies = add(root, tr("Bodies"), "folder");
  if (!body_.IsNull()) {
    QTreeWidgetItem* body = add(bodies, tr("Body 1"), "body");
    body->setData(1, kVisibilityRole, bodyVisible);
    body->setIcon(1, icons::get(bodyVisible ? "eye" : "eye-off", theme::kSmallIconSize));
    body->setToolTip(1, tr("Show / hide"));
  }
  browser_->expandAll();
  features->setExpanded(false);
  layoutOverlays();
}

void MainWindow::setMarker(long marker) {
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
