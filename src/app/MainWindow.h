#pragma once

#include "GeometryBridge.h"
#include "model_core/src/ffi.rs.h"  // Rust timeline API (generated)

#include <QMainWindow>

class QComboBox;
class QHBoxLayout;
class QListWidget;
class QTabBar;
class QTreeWidget;
class Viewport;

// Fusion-style layout:
//   ┌ workspace switcher │ tabs (SOLID, SURFACE…) / tool groups (CREATE ▾ …) ┐
//   │ Browser (left dock) │            3D viewport + view cube               │
//   └ timeline: ⏮ ◀ ▶ ⏭ [feature][feature]…                                 ┘
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private:
  enum class Workspace { Design, Manufacture };

  QWidget* buildRibbon();
  QWidget* buildTimelineBar();
  void buildMenus();
  void buildBrowser();

  void setWorkspace(Workspace ws);
  void populateToolGroups();
  void loadDemoDocument();

  void rebuildModel(bool fit = false);
  void refreshTimeline();
  void refreshBrowser();
  void stepMarker(int delta);

  void addBox();
  void addFillet();
  void exportStep();
  void showAbout();

  rust::Box<model::Timeline> timeline_ = model::new_timeline();
  TopoDS_Shape body_;
  QString documentName_ = QStringLiteral("Untitled");
  Workspace workspace_ = Workspace::Design;

  Viewport* viewport_ = nullptr;
  QComboBox* workspaceBox_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QHBoxLayout* toolRow_ = nullptr;
  QTreeWidget* browser_ = nullptr;
  QListWidget* timelineList_ = nullptr;
};
