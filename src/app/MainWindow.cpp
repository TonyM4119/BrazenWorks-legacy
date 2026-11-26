#include "MainWindow.h"

#include "Branding.h"
#include "Kernel.h"
#include "Viewport.h"

#include <Standard_Version.hxx>

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

namespace {

struct ToolGroup {
  const char* label;
  std::vector<const char*> tools;
};

// Tool layout per workspace. Names are generic CAD/CAM terms; implementations
// land milestone by milestone (see docs/ROADMAP.md).
const std::vector<const char*> kDesignTabs = {"SOLID", "SURFACE", "MESH", "SHEET METAL",
                                              "PLASTIC", "UTILITIES", "MANAGE"};
const std::vector<ToolGroup> kDesignGroups = {
    {"CREATE", {"Sketch", "Box", "Extrude", "Revolve", "Sweep", "Loft", "Hole"}},
    {"MODIFY", {"Press Pull", "Fillet", "Chamfer", "Shell", "Combine"}},
    {"ASSEMBLE", {"Joint"}},
    {"CONSTRUCT", {"Plane", "Axis"}},
    {"INSPECT", {"Measure"}},
    {"INSERT", {"Insert"}},
};
const std::vector<const char*> kCamTabs = {"MILLING", "TURNING", "ADDITIVE",
                                           "PROBING", "FABRICATION", "UTILITIES"};
const std::vector<ToolGroup> kCamGroups = {
    {"SETUP", {"New Setup"}},
    {"2D", {"Face", "2D Adaptive", "2D Contour", "2D Pocket", "Slot", "Engrave"}},
    {"3D", {"Adaptive", "Parallel", "Scallop"}},
    {"DRILLING", {"Drill"}},
    {"ACTIONS", {"Simulate", "Post Process"}},
    {"MANAGE", {"Tool Library"}},
};

QString toQString(const rust::String& s) {
  return QString::fromUtf8(s.data(), int(s.size()));
}

model::Profile rectProfile(double x, double y, double w, double h) {
  return model::Profile{model::ProfileKind::Rect, x, y, w, h, 0.0};
}

model::Profile circleProfile(double cx, double cy, double r) {
  return model::Profile{model::ProfileKind::Circle, cx, cy, 0.0, 0.0, r};
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  viewport_ = new Viewport(this);
  setCentralWidget(viewport_);

  // Ribbon + timeline live in toolbars-as-widgets so the viewport stays central.
  auto* top = new QDockWidget(this);
  top->setObjectName("RibbonDock");
  top->setTitleBarWidget(new QWidget(top));
  top->setFeatures(QDockWidget::NoDockWidgetFeatures);
  top->setWidget(buildRibbon());
  addDockWidget(Qt::TopDockWidgetArea, top);

  auto* bottom = new QDockWidget(this);
  bottom->setObjectName("TimelineDock");
  bottom->setTitleBarWidget(new QWidget(bottom));
  bottom->setFeatures(QDockWidget::NoDockWidgetFeatures);
  bottom->setWidget(buildTimelineBar());
  addDockWidget(Qt::BottomDockWidgetArea, bottom);

  buildBrowser();
  buildMenus();

  statusBar()->addPermanentWidget(new QLabel(
      QString("Open CASCADE %1  ·  mm").arg(OCC_VERSION_COMPLETE)));

  setWorkspace(Workspace::Design);
  loadDemoDocument();
  resize(1440, 900);
}

QWidget* MainWindow::buildRibbon() {
  auto* ribbon = new QWidget;
  ribbon->setObjectName("Ribbon");
  auto* row = new QHBoxLayout(ribbon);
  row->setContentsMargins(8, 4, 8, 4);

  workspaceBox_ = new QComboBox;
  workspaceBox_->setObjectName("WorkspaceSwitcher");
  workspaceBox_->addItems({"DESIGN", "MANUFACTURE"});
  workspaceBox_->setMinimumHeight(48);
  connect(workspaceBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
    setWorkspace(i == 0 ? Workspace::Design : Workspace::Manufacture);
  });
  row->addWidget(workspaceBox_);

  auto* right = new QVBoxLayout;
  right->setSpacing(2);
  tabs_ = new QTabBar;
  tabs_->setDrawBase(false);
  tabs_->setExpanding(false);
  tabs_->setUsesScrollButtons(true);
  tabs_->setMinimumHeight(26);
  right->addWidget(tabs_);

  toolRow_ = new QHBoxLayout;
  toolRow_->setSpacing(0);
  right->addLayout(toolRow_);
  row->addLayout(right, 1);
  return ribbon;
}

void MainWindow::setWorkspace(Workspace ws) {
  workspace_ = ws;
  while (tabs_->count() > 0) tabs_->removeTab(0);
  for (const char* t : ws == Workspace::Design ? kDesignTabs : kCamTabs) tabs_->addTab(t);
  populateToolGroups();
}

void MainWindow::populateToolGroups() {
  while (QLayoutItem* item = toolRow_->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const auto& groups = workspace_ == Workspace::Design ? kDesignGroups : kCamGroups;
  for (const ToolGroup& group : groups) {
    auto* box = new QWidget;
    box->setObjectName("ToolGroup");
    auto* col = new QVBoxLayout(box);
    col->setContentsMargins(6, 0, 6, 0);
    col->setSpacing(0);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    for (const char* name : group.tools) {
      auto* b = new QToolButton;
      b->setText(name);
      b->setToolButtonStyle(Qt::ToolButtonTextOnly);
      const QString tool = name;
      if (tool == "Box") {
        connect(b, &QToolButton::clicked, this, &MainWindow::addBox);
      } else if (tool == "Fillet") {
        connect(b, &QToolButton::clicked, this, &MainWindow::addFillet);
      } else {
        b->setEnabled(false);
        b->setToolTip(tool + " — not implemented yet (see docs/ROADMAP.md)");
      }
      buttons->addWidget(b);
    }
    col->addLayout(buttons);

    auto* label = new QToolButton;
    label->setObjectName("GroupLabel");
    label->setText(QString("%1 ▾").arg(group.label));
    label->setAutoRaise(true);
    col->addWidget(label, 0, Qt::AlignHCenter);
    toolRow_->addWidget(box);
  }
  toolRow_->addStretch(1);
}

void MainWindow::buildBrowser() {
  auto* dock = new QDockWidget("BROWSER", this);
  dock->setObjectName("BrowserDock");
  browser_ = new QTreeWidget(dock);
  browser_->setHeaderHidden(true);
  dock->setWidget(browser_);
  addDockWidget(Qt::LeftDockWidgetArea, dock);
}

QWidget* MainWindow::buildTimelineBar() {
  auto* bar = new QWidget;
  bar->setObjectName("Timeline");
  auto* row = new QHBoxLayout(bar);
  row->setContentsMargins(8, 4, 8, 4);

  auto addStep = [&](const char* text, const char* tip, auto fn) {
    auto* b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    connect(b, &QToolButton::clicked, this, fn);
    row->addWidget(b);
  };
  addStep("|◀", "Go to start", [this] { model::timeline_set_marker(*timeline_, 0); rebuildModel(); });
  addStep("◀", "Step back", [this] { stepMarker(-1); });
  addStep("▶", "Step forward", [this] { stepMarker(+1); });
  addStep("▶|", "Go to end", [this] {
    model::timeline_set_marker(*timeline_, model::timeline_len(*timeline_));
    rebuildModel();
  });

  timelineList_ = new QListWidget;
  timelineList_->setFlow(QListView::LeftToRight);
  timelineList_->setFixedHeight(40);
  timelineList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  timelineList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  timelineList_->setToolTip("Click a feature to roll the model back to just after it");
  connect(timelineList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    model::timeline_set_marker(*timeline_, std::size_t(timelineList_->row(item)) + 1);
    rebuildModel();
  });
  row->addWidget(timelineList_, 1);
  return bar;
}

void MainWindow::buildMenus() {
  QMenu* file = menuBar()->addMenu("&File");
  file->addAction("Export STEP…", this, &MainWindow::exportStep);
  file->addSeparator();
  file->addAction("Quit", QKeySequence::Quit, this, &QWidget::close);

  QMenu* view = menuBar()->addMenu("&View");
  view->addAction("Fit All", QKeySequence(Qt::Key_F6), this, [this] { viewport_->fitAll(); });

  QMenu* help = menuBar()->addMenu("&Help");
  help->addAction(QString("About %1").arg(branding::kDisplayName), this, &MainWindow::showAbout);
}

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
  rebuildModel(/*fit=*/true);
}

void MainWindow::rebuildModel(bool fit) {
  rust::Vec<std::size_t> failed;
  const std::unique_ptr<model::Shape> body = model::timeline_rebuild(*timeline_, failed);
  body_ = body ? body->value : TopoDS_Shape();
  viewport_->showBody(body_);
  if (fit) viewport_->fitAll();

  refreshTimeline();
  refreshBrowser();
  setWindowTitle(QString("%1 — %2").arg(documentName_, branding::kDisplayName));

  if (!failed.empty()) {
    QStringList names;
    for (std::size_t i : failed) names << toQString(model::timeline_feature_name(*timeline_, i));
    statusBar()->showMessage("Could not compute: " + names.join(", "), 8000);
  }
}

void MainWindow::refreshTimeline() {
  timelineList_->clear();
  const std::size_t marker = model::timeline_marker(*timeline_);
  for (std::size_t i = 0; i < model::timeline_len(*timeline_); ++i) {
    auto* item = new QListWidgetItem(QString("%1  %2").arg(
        toQString(model::timeline_feature_type(*timeline_, i)),
        toQString(model::timeline_feature_name(*timeline_, i))));
    if (i >= marker) item->setForeground(QColor("#6B7280"));  // rolled back
    timelineList_->addItem(item);
  }
}

void MainWindow::refreshBrowser() {
  browser_->clear();
  auto* root = new QTreeWidgetItem(browser_, {documentName_});
  new QTreeWidgetItem(root, {"Document Settings"});
  new QTreeWidgetItem(root, {"Origin"});
  auto* features = new QTreeWidgetItem(root, {"Features"});
  for (std::size_t i = 0; i < model::timeline_marker(*timeline_); ++i) {
    new QTreeWidgetItem(features, {toQString(model::timeline_feature_name(*timeline_, i))});
  }
  auto* bodies = new QTreeWidgetItem(root, {"Bodies"});
  if (!body_.IsNull()) new QTreeWidgetItem(bodies, {"Body 1"});
  browser_->expandAll();
}

void MainWindow::stepMarker(int delta) {
  const long next = long(model::timeline_marker(*timeline_)) + delta;
  model::timeline_set_marker(*timeline_, std::size_t(std::max(0L, next)));
  rebuildModel();
}

void MainWindow::addBox() {
  const int n = int(model::timeline_len(*timeline_)) + 1;
  const std::string name = QString("Box %1").arg(n).toStdString();
  model::timeline_add_extrude(*timeline_, name, rectProfile(70, -10, 20, 20), 20, model::BodyOp::Join);
  rebuildModel(true);
}

void MainWindow::addFillet() {
  model::timeline_add_fillet(*timeline_, "Fillet", 1.0);
  rebuildModel();
}

void MainWindow::exportStep() {
  if (body_.IsNull()) return;
  const QString path = QFileDialog::getSaveFileName(this, "Export STEP", documentName_ + ".step",
                                                    "STEP files (*.step *.stp)");
  if (path.isEmpty()) return;
  if (!geom::exportStep(body_, path.toStdString())) {
    QMessageBox::warning(this, branding::kDisplayName, "STEP export failed.");
  }
}

void MainWindow::showAbout() {
  QMessageBox::about(
      this, QString("About %1").arg(branding::kDisplayName),
      QString("<h3>%1 %2</h3><p>%3</p><p><a href='%4'>%4</a></p>"
              "<p>Geometry by Open CASCADE Technology %5 (LGPL-2.1).<br>UI by Qt %6 (LGPL-3.0).</p>")
          .arg(branding::kDisplayName, branding::kVersion, branding::kTagline, branding::kRepoUrl,
               OCC_VERSION_COMPLETE, qVersion()));
}
